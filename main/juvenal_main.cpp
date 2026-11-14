/*
  Juvenal: unico INMP441, wake word e PCM incremental por WS de sessao.
  Diagnostico WAV/POST preservado separadamente, com seu limite de gravacao.
  Integra o componente MicroWakeWord-IDF JA VALIDADO no projeto original.
  ESP-IDF 5.4.4 / ESP32-S3 N16R8.
*/
#include <algorithm>
#include <cstdio>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/i2s_std.h"
#include "esphome/components/i2s_audio/microphone/i2s_audio_microphone.h"
#include "esphome/components/micro_wake_word/micro_wake_word.h"
#include "juvenal_face_neutral.h"
#include "juvenal_web.h"
#include "juvenal_config.h"
#include "juvenal_ws.h"
#include "juvenal_led.h"
#include "juvenal_speaker.h"
#include <ctime>
#include "esp_idf_version.h"
#include "esp_netif_sntp.h"

static_assert(ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(5, 4, 4), "Juvenal requer ESP-IDF 5.4.4");

extern const uint8_t juvenal_model_start[] asm("_binary_juvehnal_tflite_start");

namespace {
static constexpr uint32_t SAMPLE_RATE = 16000;
// Limite do WAV de diagnostico/legado. No WS, esta area e um ring reutilizavel.
static constexpr uint32_t MAX_SECONDS = 6;
static constexpr size_t MAX_SAMPLES = SAMPLE_RATE * MAX_SECONDS;
static constexpr size_t WAV_HEADER_BYTES = 44;
static constexpr size_t WAV_CAPACITY = WAV_HEADER_BYTES + MAX_SAMPLES * sizeof(int16_t);
static constexpr uint32_t SILENCE_MS = 2000;
static constexpr uint32_t SPEECH_RMS = 300;  // calibrar com logs
static constexpr uint32_t SILENCE_RMS = 100; // pedido inicial; calibrar no ambiente
static constexpr uint32_t VOICE_FRAMES_TO_START = 3;
static constexpr uint32_t HTTP_TIMEOUT_MS = 12000;
static const char *TAG = "JUVENAL_HTTP";

enum class Phase : uint8_t { IDLE, AUTO_PREPARING, RECORDING, SENDING, WEB_PREPARING, WEB_READY, WEB_CLOSING, WS_ACTIVE, SETTINGS_SAVING };

static esphome::i2s_audio::I2SAudioMicrophone s_mic;
static esphome::micro_wake_word::MicroWakeWord s_wake;
static TaskHandle_t s_controller_task = nullptr;
static uint8_t *s_wav = nullptr;
static std::atomic<Phase> s_phase{Phase::IDLE};
static std::atomic<bool> s_runtime_ready{false}, s_pcm_seen{false}, s_wifi_failed{false};
static std::atomic<size_t> s_samples{0};
static std::atomic<uint32_t> s_writers{0};
static std::atomic<bool> s_stop{false};
static std::atomic<bool> s_web_mode{false};
static std::atomic<int64_t> s_web_last_access_us{0};
static std::atomic<bool> s_speech_found{false};
static std::atomic<int> s_last_rms{0};
static uint32_t s_consecutive_voice_frames = 0; // so a task do microfone escreve
static size_t s_silence_samples = 0;             // so a task do microfone escreve
static size_t s_mic_sub_id = 0;
static EventGroupHandle_t s_wifi_events = nullptr;
static constexpr EventBits_t WIFI_READY_BIT = BIT0;

// Diagnosticos atomicos: somente metadados, jamais URL, API Key ou conteudo do audio.
enum class PostFailure : uint8_t { NONE, WIFI_OFFLINE, KEY_MISSING, CLIENT_INIT, TRANSPORT, HTTP_STATUS };
static std::atomic<bool> s_post_in_progress{false};
static std::atomic<bool> s_last_post_ok{false};
static std::atomic<unsigned> s_post_sequence{0};
static std::atomic<int> s_last_http_status{0};
static std::atomic<int64_t> s_last_post_ms{0};
static std::atomic<size_t> s_last_post_bytes{0};
static std::atomic<PostFailure> s_post_failure{PostFailure::NONE};

static const char *post_failure_name(PostFailure f)
{
    switch (f) {
        case PostFailure::NONE: return "none";
        case PostFailure::WIFI_OFFLINE: return "wifi_offline";
        case PostFailure::KEY_MISSING: return "api_key_missing";
        case PostFailure::CLIENT_INIT: return "client_init";
        case PostFailure::TRANSPORT: return "transport";
        case PostFailure::HTTP_STATUS: return "http_status";
    }
    return "unknown";
}

// Executado uma vez por POST (automatico ou manual). Publica resultado mesmo
// quando retorna antes de criar o cliente HTTP; nao retem audio em memoria extra.
struct PostTelemetry {
    const int64_t started_us = esp_timer_get_time();
    PostFailure failure = PostFailure::NONE;
    int http_status = 0;
    size_t bytes = 0;
    bool success = false;

    PostTelemetry() { s_post_in_progress.store(true, std::memory_order_release); }
    ~PostTelemetry()
    {
        s_last_http_status.store(http_status, std::memory_order_relaxed);
        s_last_post_ms.store((esp_timer_get_time() - started_us) / 1000, std::memory_order_relaxed);
        s_last_post_bytes.store(bytes, std::memory_order_relaxed);
        s_last_post_ok.store(success, std::memory_order_relaxed);
        s_post_failure.store(failure, std::memory_order_relaxed);
        s_post_in_progress.store(false, std::memory_order_release);
        // Sequencia e publicada por ultimo: o painel nao le resultado incompleto.
        s_post_sequence.fetch_add(1, std::memory_order_release);
    }
};

static void on_wifi_event(void *, esp_event_base_t base, int32_t id, void *)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (esp_wifi_connect() != ESP_OK) s_wifi_failed.store(true);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_READY_BIT);
        s_wifi_failed.store(true);
        s_runtime_ready.store(false);
        juvenal_led_set_state(JUVENAL_LED_BOOT_ERROR);
        ESP_LOGW(TAG, "Wi-Fi desconectado: reconectando");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifi_failed.store(false);
        xEventGroupSetBits(s_wifi_events, WIFI_READY_BIT);
        ESP_LOGI(TAG, "Wi-Fi conectado: IP recebido");
    }
}

#define WIFI_CHECK(call) do { const esp_err_t result = (call); if (result != ESP_OK) return result; } while (0)
static esp_err_t wifi_start()
{
    if (!JUVENAL_WIFI_SSID[0]) {
        ESP_LOGW(TAG, "Configure o Wi-Fi no arquivo local; inicializacao incompleta");
        return ESP_ERR_INVALID_ARG;
    }
    WIFI_CHECK(esp_netif_init());
    WIFI_CHECK(esp_event_loop_create_default());
    WIFI_CHECK(esp_netif_create_default_wifi_sta() ? ESP_OK : ESP_FAIL);
    s_wifi_events = xEventGroupCreate();
    WIFI_CHECK(s_wifi_events ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    WIFI_CHECK(esp_wifi_init(&cfg));
    WIFI_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    WIFI_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, nullptr));
    WIFI_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, nullptr));

    wifi_config_t sta = {};
    std::strncpy(reinterpret_cast<char *>(sta.sta.ssid), JUVENAL_WIFI_SSID,
                 sizeof(sta.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char *>(sta.sta.password), JUVENAL_WIFI_PASSWORD,
                 sizeof(sta.sta.password) - 1);
    WIFI_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    WIFI_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    WIFI_CHECK(esp_wifi_start());
    return ESP_OK;
}

#undef WIFI_CHECK

static void put16(uint8_t *&p, uint16_t x)
{
    *p++ = static_cast<uint8_t>(x);
    *p++ = static_cast<uint8_t>(x >> 8);
}
static void put32(uint8_t *&p, uint32_t x)
{
    for (int i = 0; i < 4; ++i) *p++ = static_cast<uint8_t>(x >> (i * 8));
}
static void write_wav_header(size_t num_samples)
{
    const uint32_t data_bytes = static_cast<uint32_t>(num_samples * sizeof(int16_t));
    uint8_t *p = s_wav;
    std::memcpy(p, "RIFF", 4); p += 4;
    put32(p, 36 + data_bytes);
    std::memcpy(p, "WAVE", 4); p += 4;
    std::memcpy(p, "fmt ", 4); p += 4;
    put32(p, 16); put16(p, 1); put16(p, 1);
    put32(p, SAMPLE_RATE); put32(p, SAMPLE_RATE * 2);
    put16(p, 2); put16(p, 16);
    std::memcpy(p, "data", 4); p += 4;
    put32(p, data_bytes);
}


// Apenas animacao: nao interfere com o MicroWakeWord nem dispara captura.
// O INMP441 permanece ativo no estado de repouso. Deteccao por RMS sustentado,
// com baseline adaptativo simples. Nao classifica fala vs. outros sons.
static void ambient_motion_detector(const int16_t *input, size_t n)
{
    static int floor_rms = 12;
    static unsigned consecutive = 0;
    if (s_phase.load(std::memory_order_relaxed) != Phase::IDLE ||
        !juvenal_face_is_sleeping() || n < 16) {
        consecutive = 0;
        return;
    }
    uint64_t energy = 0;
    size_t sampled = 0;
    for (size_t i = 0; i < n; i += 4) {
        const int32_t v = input[i];
        energy += static_cast<uint64_t>(v * v);
        ++sampled;
    }
    const int rms = static_cast<int>(std::sqrt(static_cast<double>(energy) / sampled));
    const int threshold = std::max(95, floor_rms * 4);
    if (rms >= threshold) {
        if (++consecutive >= 10) {
            consecutive = 0;
            juvenal_face_event(JUVENAL_FACE_SOUND_WAKE);
        }
    } else {
        consecutive = 0;
        floor_rms = (floor_rms * 31 + rms) / 32;
        floor_rms = std::max(6, floor_rms);
    }
}

// Processamento somente APOS terminar a captura. A wake word recebe PCM original.
// Filtro passa-altas suave (~80 Hz) remove DC e parte de vibracoes graves.
// Ganho moderado +4 dB (1.6x), com saturacao int16 para evitar wrap.
static void finish_audio_pcm(size_t count)
{
    auto *pcm = reinterpret_cast<int16_t *>(s_wav + WAV_HEADER_BYTES);
    float last_x = 0.0f, last_y = 0.0f;
    unsigned clipped = 0;
    for (size_t i = 0; i < count; ++i) {
        const float x = static_cast<float>(pcm[i]);
        const float y = 0.9695f * (last_y + x - last_x);
        last_x = x;
        last_y = y;
        int32_t value = static_cast<int32_t>(std::lrintf(y * 1.6f));
        if (value > 32767) { value = 32767; ++clipped; }
        if (value < -32768) { value = -32768; ++clipped; }
        pcm[i] = static_cast<int16_t>(value);
    }
    ESP_LOGI(TAG, "PCM finalizado: ganho 1.6x, HPF 80 Hz, clipping=%u de %u",
             clipped, static_cast<unsigned>(count));
}

// Callback leve: executado na task de leitura I2S da biblioteca.
// Nunca envia HTTP, nunca usa LVGL, nunca faz alocacao de memoria.
static void on_pcm(const int16_t *input, size_t n)
{
    if (!input || !n) return;
    s_pcm_seen.store(true, std::memory_order_release);
    // Continuar drenando uma sessao ja ativa mesmo se a rede cair.
    if (!s_runtime_ready.load(std::memory_order_acquire) &&
        s_phase.load(std::memory_order_acquire) != Phase::WS_ACTIVE &&
        s_phase.load(std::memory_order_acquire) != Phase::RECORDING) return;
    if (s_phase.load(std::memory_order_acquire) == Phase::WS_ACTIVE) {
        juvenal_ws_on_pcm(input, n);
        return;
    }
    if (s_phase.load(std::memory_order_acquire) != Phase::RECORDING) {
        ambient_motion_detector(input, n);
        return;
    }
    s_writers.fetch_add(1, std::memory_order_acq_rel);
    if (s_phase.load(std::memory_order_acquire) != Phase::RECORDING) {
        s_writers.fetch_sub(1, std::memory_order_acq_rel);
        return;
    }
    const size_t used = s_samples.load(std::memory_order_relaxed);
    const size_t count = std::min(n, MAX_SAMPLES - used);
    if (count > 0) {
        std::memcpy(s_wav + WAV_HEADER_BYTES + used * sizeof(int16_t),
                    input, count * sizeof(int16_t));
        uint64_t energy = 0;
        for (size_t i = 0; i < count; ++i) {
            const int32_t v = input[i];
            energy += static_cast<uint64_t>(v * v);
        }
        const int rms = static_cast<int>(std::sqrt(static_cast<double>(energy) / count));
        s_last_rms.store(rms, std::memory_order_relaxed);
        if (!s_web_mode.load(std::memory_order_relaxed)) {
        if (!s_speech_found.load(std::memory_order_relaxed)) {
            if (rms >= static_cast<int>(SPEECH_RMS)) {
                if (++s_consecutive_voice_frames >= VOICE_FRAMES_TO_START) {
                    s_speech_found.store(true, std::memory_order_release);
                    s_silence_samples = 0;
                }
            } else {
                s_consecutive_voice_frames = 0;
            }
        } else {
            if (rms < static_cast<int>(SILENCE_RMS)) {
                s_silence_samples += count;
                if (s_silence_samples >= SAMPLE_RATE * SILENCE_MS / 1000) {
                    s_stop.store(true, std::memory_order_release);
                }
            } else {
                s_silence_samples = 0;
            }
        }
        } // teste manual nao usa VAD
        s_samples.store(used + count, std::memory_order_release);
    }
    if (used + count >= MAX_SAMPLES) s_stop.store(true, std::memory_order_release);
    s_writers.fetch_sub(1, std::memory_order_acq_rel);
}

static esp_err_t juvenal_http_event(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_DATA &&
        event->data != nullptr && event->data_len > 0) {

        int len = event->data_len > 300 ? 300 : event->data_len;

        (void)len; // Corpo do receptor nao e registrado no firmware.
    }
    return ESP_OK;
}

static bool post_wav(size_t sample_count)
{
    PostTelemetry post;
    char webhook_url[256];
    juvenal_web_get_url(webhook_url, sizeof(webhook_url));
    if ((xEventGroupGetBits(s_wifi_events) & WIFI_READY_BIT) == 0) {
        ESP_LOGE(TAG, "Sem Wi-Fi: POST nao realizado");
        post.failure = PostFailure::WIFI_OFFLINE;
        return false;
    }
    if (!JUVENAL_API_KEY[0] || !std::strcmp(JUVENAL_API_KEY, "COLOQUE_SUA_API_KEY_AQUI")) {
        ESP_LOGE(TAG, "Configure JUVENAL_API_KEY no arquivo main/juvenal_secrets.h");
        post.failure = PostFailure::KEY_MISSING;
        return false;
    }
    write_wav_header(sample_count);
    const int wav_bytes = static_cast<int>(WAV_HEADER_BYTES + sample_count * sizeof(int16_t));
    post.bytes = static_cast<size_t>(wav_bytes);
    const int16_t *pcm_debug = reinterpret_cast<const int16_t *>(s_wav + WAV_HEADER_BYTES);
    size_t nonzero = 0;
    int peak = 0;

    for (size_t i = 0; i < sample_count; ++i) {
        int value = static_cast<int>(pcm_debug[i]);
        if (value != 0) ++nonzero;
        int amplitude = value < 0 ? -value : value;
        if (amplitude > peak) peak = amplitude;
    }

    ESP_LOGI(TAG, "POST manual/legado: receptor configurado, URL omitida");
    ESP_LOGI(TAG, "WAV: %d bytes, %u amostras, nao-zero=%u, pico=%d",
             wav_bytes, (unsigned)sample_count, (unsigned)nonzero, peak);
    ESP_LOGI(TAG, "WAV header: %.4s / %.4s / %.4s",
             (const char*)s_wav, (const char*)(s_wav + 8),
             (const char*)(s_wav + 36));
    esp_http_client_config_t cfg = {};
    cfg.url = webhook_url;
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = HTTP_TIMEOUT_MS;
    cfg.event_handler = juvenal_http_event;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "Falhou ao criar o cliente HTTPS");
        post.failure = PostFailure::CLIENT_INIT;
        return false;
    }
    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    esp_http_client_set_header(client, "X-API-Key", JUVENAL_API_KEY);
    esp_http_client_set_header(client, "X-Device-ID", "juvenal-01");
    esp_http_client_set_post_field(client, reinterpret_cast<const char *>(s_wav), wav_bytes);
    const int64_t t0 = esp_timer_get_time();
    const esp_err_t err = esp_http_client_perform(client);
    const int64_t elapsed_ms = (esp_timer_get_time() - t0) / 1000;
    const int status = (err == ESP_OK) ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);
    const bool ok = (err == ESP_OK && status >= 200 && status < 300);
    post.http_status = status;
    post.success = ok;
    post.failure = err != ESP_OK ? PostFailure::TRANSPORT :
                   (ok ? PostFailure::NONE : PostFailure::HTTP_STATUS);
    ESP_LOGI(TAG, "POST: %d bytes, %lld ms, HTTP=%d, resultado=%s",
             wav_bytes, static_cast<long long>(elapsed_ms), status, ok ? "OK" : "FALHOU");
    if (err != ESP_OK) ESP_LOGE(TAG, "Erro HTTPS: %s", esp_err_to_name(err));
    return ok;
}

static void recorder_controller(void *)
{
    while (true) {
        const uint32_t wake = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        if (wake == 0) {
            if (s_phase.load(std::memory_order_acquire) == Phase::WEB_READY &&
                esp_timer_get_time() - s_web_last_access_us.load() > 120000000LL) {
                ESP_LOGW(TAG, "Painel inativo: liberando WAV e reativando wake word");
                juvenal_capture_web_clear();
            }
            continue;
        }
        if (s_phase.load(std::memory_order_acquire) != Phase::RECORDING) continue;
        s_wake.pause();
        const int64_t t0 = esp_timer_get_time();
        ESP_LOGI(TAG, "Gravando... maximo %u s; RMS fala >= %u, silencio < %u por %u ms",
                 static_cast<unsigned>(MAX_SECONDS), static_cast<unsigned>(SPEECH_RMS), static_cast<unsigned>(SILENCE_RMS), static_cast<unsigned>(SILENCE_MS));
        int64_t last_log = t0;
        while (!s_stop.load(std::memory_order_acquire)) {
            const int64_t now = esp_timer_get_time();
            if (now - t0 > static_cast<int64_t>(MAX_SECONDS + 1) * 1000000) {
                ESP_LOGW(TAG, "Timeout de segurança da captura");
                break;
            }
            if (now - last_log >= 1000000) {
                ESP_LOGI(TAG, "Captura %u amostras / RMS atual=%d / voz=%s",
                         static_cast<unsigned>(s_samples.load()), s_last_rms.load(),
                         s_speech_found.load() ? "sim" : "nao");
                last_log = now;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        s_phase.store(Phase::SENDING, std::memory_order_release);
        juvenal_face_event(JUVENAL_FACE_SENDING);
        while (s_writers.load(std::memory_order_acquire) != 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        const size_t count = s_samples.load(std::memory_order_acquire);
        ESP_LOGI(TAG, "Gravacao finalizada: %.2f s (%u amostras)",
                 static_cast<double>(count) / SAMPLE_RATE, static_cast<unsigned>(count));

        finish_audio_pcm(count);
        if (s_web_mode.load(std::memory_order_acquire)) {
            write_wav_header(count);
            s_phase.store(Phase::WEB_READY, std::memory_order_release);
            juvenal_face_event(JUVENAL_FACE_IDLE);
            ESP_LOGI(TAG, "Gravacao web pronta: %u amostras, %u bytes. Aguardando ouvir/enviar/liberar",
                     static_cast<unsigned>(count),
                     static_cast<unsigned>(WAV_HEADER_BYTES + count * 2));
            continue; // Mantem s_wake pausada e WAV disponivel no painel.
        }

        if (count >= SAMPLE_RATE / 4) {
            const bool enviado = post_wav(count);
            juvenal_face_event(enviado ? JUVENAL_FACE_SUCCESS : JUVENAL_FACE_ERROR);
        } else {
            ESP_LOGW(TAG, "Nenhuma fala valida detectada; POST nao realizado");
            juvenal_face_event(JUVENAL_FACE_ERROR);
        }
        // Apaga todo o audio que foi gravado; buffer fica reservado para proxima ativacao.
        std::memset(s_wav, 0, WAV_HEADER_BYTES + count * sizeof(int16_t));
        s_samples.store(0, std::memory_order_relaxed);
        s_stop.store(false, std::memory_order_relaxed);
        s_speech_found.store(false, std::memory_order_relaxed);
        s_last_rms.store(0, std::memory_order_relaxed);
        s_consecutive_voice_frames = 0;
        s_silence_samples = 0;
        s_phase.store(Phase::IDLE, std::memory_order_release);
        s_wake.resume();
        // Nao antecipar IDLE visual: preserve animacoes de sucesso/erro da v0.3.
        ESP_LOGI(TAG, "Pronto. Aguardando 'Juvenal' novamente");
    }
}

static void on_wake(const std::string &name)
{
    if (name != "juvehnal" || !s_runtime_ready.load(std::memory_order_acquire)) return;
    // Reservar o estado ANTES de resetar campos compartilhados com o teste web.
    Phase expected = Phase::IDLE;
    if (!s_phase.compare_exchange_strong(expected, Phase::AUTO_PREPARING,
                                         std::memory_order_acq_rel)) return;
    if (JUVENAL_AUTO_TRANSPORT_WS) {
        ESP_LOGW(TAG, "Wake word DETECTADA: %s; transporte WS v1", name.c_str());
        s_wake.pause();
        s_phase.store(Phase::WS_ACTIVE, std::memory_order_release);
        juvenal_face_event(JUVENAL_FACE_WAKE);
        if (!juvenal_ws_wake()) {
            s_phase.store(Phase::IDLE, std::memory_order_release);
            s_wake.resume();
            juvenal_face_event(JUVENAL_FACE_ERROR);
        }
        return;
    }
    s_samples.store(0, std::memory_order_relaxed);
    s_stop.store(false, std::memory_order_relaxed);
    s_speech_found.store(false, std::memory_order_relaxed);
    s_last_rms.store(0, std::memory_order_relaxed);
    s_consecutive_voice_frames = 0;
    s_silence_samples = 0;
    s_phase.store(Phase::RECORDING, std::memory_order_release);
    juvenal_face_event(JUVENAL_FACE_WAKE);
    ESP_LOGW(TAG, "Wake word DETECTADA: %s", name.c_str());
    xTaskNotifyGive(s_controller_task);
}
} // namespace

// Ponte com o painel. Um unico WAV em PSRAM, sem segunda captura I2S.
extern "C" bool juvenal_capture_web_start(void)
{
    if (!s_runtime_ready.load(std::memory_order_acquire)) return false;
    Phase expected = Phase::IDLE;
    if (!s_phase.compare_exchange_strong(expected, Phase::WEB_PREPARING,
                                         std::memory_order_acq_rel)) return false;
    s_web_mode.store(true, std::memory_order_release);
    s_web_last_access_us.store(esp_timer_get_time(), std::memory_order_release);
    s_samples.store(0, std::memory_order_relaxed);
    s_stop.store(false, std::memory_order_relaxed);
    s_speech_found.store(false, std::memory_order_relaxed);
    s_last_rms.store(0, std::memory_order_relaxed);
    s_consecutive_voice_frames = 0;
    s_silence_samples = 0;
    s_phase.store(Phase::RECORDING, std::memory_order_release);
    juvenal_face_event(JUVENAL_FACE_TEST_RECORDING);
    xTaskNotifyGive(s_controller_task);
    return true;
}
extern "C" bool juvenal_capture_web_stop(void)
{
    if (s_phase.load(std::memory_order_acquire) != Phase::RECORDING ||
        !s_web_mode.load(std::memory_order_acquire)) return false;
    s_stop.store(true, std::memory_order_release);
    return true;
}
extern "C" bool juvenal_capture_web_audio(const unsigned char **data, size_t *size)
{
    if (!data || !size || s_phase.load(std::memory_order_acquire) != Phase::WEB_READY) return false;
    const size_t count = s_samples.load(std::memory_order_acquire);
    s_web_last_access_us.store(esp_timer_get_time(), std::memory_order_release);
    *data = s_wav;
    *size = WAV_HEADER_BYTES + count * sizeof(int16_t);
    return true;
}

// A tarefa HTTP apenas enfileira UM envio. TLS e POST rodam numa task propria.
static void juvenal_http_send_worker(void *)
{
    const size_t count = s_samples.load(std::memory_order_acquire);
    const bool ok = post_wav(count);
    juvenal_face_event(ok ? JUVENAL_FACE_SUCCESS : JUVENAL_FACE_ERROR);
    s_web_last_access_us.store(esp_timer_get_time(), std::memory_order_release);
    s_phase.store(Phase::WEB_READY, std::memory_order_release);
    ESP_LOGI(TAG, "Envio manual encerrado; WAV preservado para reproducao ou descarte");
    vTaskDelete(nullptr);
}
extern "C" bool juvenal_capture_web_send(void)
{
    Phase expected = Phase::WEB_READY;
    if (!s_phase.compare_exchange_strong(expected, Phase::SENDING,
                                         std::memory_order_acq_rel)) return false;
    s_web_last_access_us.store(esp_timer_get_time(), std::memory_order_release);
    // Publicar estado visual antes de liberar a task: POST pode falhar imediatamente.
    juvenal_face_event(JUVENAL_FACE_SENDING);
    const BaseType_t created = xTaskCreate(juvenal_http_send_worker, "juvenal_send",
                                            10240, nullptr, 4, nullptr);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Falhou ao criar tarefa POST; WAV mantido no painel");
        juvenal_face_event(JUVENAL_FACE_ERROR);
        s_phase.store(Phase::WEB_READY, std::memory_order_release);
        return false;
    }
    return true;
}
extern "C" bool juvenal_capture_web_clear(void)
{
    Phase expected = Phase::WEB_READY;
    if (!s_phase.compare_exchange_strong(expected, Phase::WEB_CLOSING,
                                         std::memory_order_acq_rel)) return false;
    const size_t count = s_samples.load(std::memory_order_relaxed);
    std::memset(s_wav, 0, WAV_HEADER_BYTES + count * sizeof(int16_t));
    s_samples.store(0, std::memory_order_relaxed);
    s_stop.store(false, std::memory_order_relaxed);
    s_speech_found.store(false, std::memory_order_relaxed);
    s_last_rms.store(0, std::memory_order_relaxed);
    s_consecutive_voice_frames = 0;
    s_silence_samples = 0;
    s_web_mode.store(false, std::memory_order_release);
    s_phase.store(Phase::IDLE, std::memory_order_release);
    s_wake.resume();
    juvenal_face_event(JUVENAL_FACE_IDLE);
    return true;
}
extern "C" void juvenal_capture_web_status(char *out, size_t size)
{
    if (!out || !size) return;
    if (s_web_mode.load(std::memory_order_acquire))
        s_web_last_access_us.store(esp_timer_get_time(), std::memory_order_release);
    const Phase p = s_phase.load(std::memory_order_acquire);
    const bool is_manual = s_web_mode.load(std::memory_order_acquire);
    const char *name = "idle";
    if (p == Phase::RECORDING && is_manual) name = "recording";
    else if (p == Phase::WEB_READY) name = "ready";
    else if (p == Phase::SENDING) name = "sending";
    else if (p == Phase::WEB_PREPARING || p == Phase::WEB_CLOSING ||
             p == Phase::AUTO_PREPARING) name = "closing";
    else if (p == Phase::RECORDING || p == Phase::WS_ACTIVE) name = "busy";
    const size_t count = s_samples.load(std::memory_order_relaxed);
    const bool wifi = s_wifi_events &&
                      ((xEventGroupGetBits(s_wifi_events) & WIFI_READY_BIT) != 0);
    const unsigned sequence = s_post_sequence.load(std::memory_order_acquire);
    const bool posting = s_post_in_progress.load(std::memory_order_acquire);
    const char *result = posting ? "sending" : (sequence == 0 ? "none" :
                         (s_last_post_ok.load() ? "success" : "failed"));
    std::snprintf(out, size,
        "{\"phase\":\"%s\",\"seconds\":%.2f,\"capture_mode\":\"%s\","
        "\"samples\":%u,\"wav_bytes\":%u,\"rms\":%d,\"wifi_connected\":%s,"
        "\"post_in_progress\":%s,\"last_post_sequence\":%u,"
        "\"last_post_result\":\"%s\",\"last_http_status\":%d,"
        "\"last_post_ms\":%lld,\"last_post_bytes\":%u,"
        "\"last_post_error\":\"%s\"}",
        name, static_cast<double>(count) / SAMPLE_RATE,
        is_manual ? "manual" : (p == Phase::IDLE ? "none" : "wake"),
        static_cast<unsigned>(count),
        static_cast<unsigned>(count ? WAV_HEADER_BYTES + count * 2 : 0),
        s_last_rms.load(std::memory_order_relaxed), wifi ? "true" : "false",
        posting ? "true" : "false", sequence, result,
        s_last_http_status.load(),
        static_cast<long long>(s_last_post_ms.load()),
        static_cast<unsigned>(s_last_post_bytes.load()),
        post_failure_name(s_post_failure.load()));
    char ws_status[384];
    juvenal_ws_status(ws_status, sizeof(ws_status));
    const size_t used = std::strlen(out);
    if (used && used + std::strlen(ws_status) + 1 < size && out[used - 1] == '}') {
        out[used - 1] = ',';
        std::snprintf(out + used, size - used, "%s", ws_status + 1);
    }
}

static bool boot_check(esp_err_t result, const char *stage)
{
    if (result == ESP_OK) return true;
    s_runtime_ready.store(false, std::memory_order_release);
    juvenal_led_set_state(JUVENAL_LED_BOOT_ERROR);
    ESP_LOGE(TAG, "Inicializacao falhou em %s: %s; LED vermelho", stage, esp_err_to_name(result));
    return false;
}

extern "C" void app_main(void)
{
    // O LED existe antes da tela, rede, NVS e captura.
    if (!boot_check(juvenal_led_init(), "LED/RMT")) return;
    ESP_LOGI(TAG, "Juvenal integrado: wake -> WS v1; WAV/POST manual preservado");
    // Nunca apagar NVS automaticamente.
    if (!boot_check(nvs_flash_init(), "NVS") ||
        !boot_check(juvenal_web_init(), "configuracao do painel")) return;
    s_wav = static_cast<uint8_t *>(heap_caps_calloc(1, WAV_CAPACITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!boot_check(s_wav ? ESP_OK : ESP_ERR_NO_MEM, "buffer PSRAM")) return;
    ESP_LOGI(TAG, "Area PSRAM: %u bytes; WAV diagnostico max %u s; WS usa ring",
             static_cast<unsigned>(WAV_CAPACITY), static_cast<unsigned>(MAX_SECONDS));
    if (JUVENAL_AUTO_TRANSPORT_WS &&
        !boot_check(juvenal_ws_configured() ? ESP_OK : ESP_ERR_INVALID_ARG, "configuracao WS")) return;
    if (!boot_check(juvenal_ws_init(reinterpret_cast<int16_t *>(s_wav + WAV_HEADER_BYTES),
        MAX_SAMPLES, [](bool success) {
            s_phase.store(Phase::IDLE, std::memory_order_release);
            s_wake.resume();
            juvenal_face_event(success ? JUVENAL_FACE_SUCCESS : JUVENAL_FACE_ERROR);
        }), "worker WS")) return;
    if (!boot_check(juvenal_face_neutral_init(), "GC9A01/LVGL")) return;
    if (JUVENAL_SPEAKER_ENABLED && !boot_check(juvenal_speaker_init(), "I2S TX")) return;
    if (!boot_check(wifi_start(), "Wi-Fi")) return;
    const bool tls = JUVENAL_AUTO_TRANSPORT_WS && !std::strncmp(JUVENAL_WS_URL, "wss://", 6);
    if (tls) {
        esp_sntp_config_t clock = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        if (!boot_check(esp_netif_sntp_init(&clock), "relogio TLS")) return;
    }
    if (!boot_check(xTaskCreate(recorder_controller, "juvenal_rec", 8192, nullptr,
                                 5, &s_controller_task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM,
                    "worker WAV")) return;
    i2s_chan_config_t mic_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    mic_cfg.dma_desc_num = 8;
    mic_cfg.dma_frame_num = 256;
    s_mic.set_channel(mic_cfg);
    s_mic.set_bclk_pin(GPIO_NUM_12);
    s_mic.set_lrclk_pin(GPIO_NUM_11);
    s_mic.set_din_pin(GPIO_NUM_10);
    s_mic.set_sample_rate(SAMPLE_RATE);
    s_mic.set_bits_per_sample(I2S_DATA_BIT_WIDTH_32BIT);
    s_mic.setup();
    if (!boot_check(s_mic.has_error() ? ESP_FAIL : ESP_OK, "microfone")) return;
    s_wake.set_microphone(&s_mic);
    s_wake.set_features_step_size(10);
    s_wake.add_wake_word_model(juvenal_model_start, 0.97f, 5, "juvehnal", 30000);
    s_wake.add_detection_callback([](std::string word) { on_wake(word); });
    s_wake.setup();
    if (!boot_check(s_wake.has_error() ? ESP_FAIL : ESP_OK, "modelo wake")) return;
    s_mic_sub_id = s_mic.subscribe([](const int16_t *pcm, size_t count) { on_pcm(pcm, count); });
    if (!boot_check(s_mic_sub_id ? ESP_OK : ESP_FAIL, "assinatura PCM")) return;
    s_wake.start();
    if (!boot_check(juvenal_web_start(), "servidor HTTP autenticado")) return;

    const int64_t started = esp_timer_get_time();
    bool fatal = false, announced = false, audio_started = false;
    // Monitor leve; task de LED independente mantem o pisca mesmo em falha de LCD/rede.
    while (true) {
        const bool wifi = (xEventGroupGetBits(s_wifi_events) & WIFI_READY_BIT) != 0;
        const bool audio_error = s_mic.has_error() || s_wake.has_error();
        // pause() deixa wake em IDLE durante sessao/manual: isto nao e falha.
        const bool audio_ready = s_mic.is_running() && (s_wake.is_running() || audio_started) && s_pcm_seen.load();
        if (audio_ready) audio_started = true;
        const bool clock_ready = !tls || std::time(nullptr) >= 1704067200;
        if (audio_error && !fatal) {
            fatal = true;
            ESP_LOGE(TAG, "Falha microfone/wake: mic=%d wake=%d",
                     static_cast<int>(s_mic.get_last_error()), static_cast<int>(s_wake.get_last_error()));
        }
        const bool ready = !fatal && wifi && audio_ready && clock_ready;
        const bool timeout = esp_timer_get_time() - started >= 30000000LL;
        if (!ready) {
            s_runtime_ready.store(false, std::memory_order_release);
            if (fatal || s_wifi_failed.load() || timeout) {
                if (juvenal_led_get_state() != JUVENAL_LED_BOOT_ERROR)
                    ESP_LOGE(TAG, "Inicializacao/conexao pendente: wifi=%d audio=%d relogio=%d", wifi, audio_ready, clock_ready);
                juvenal_led_set_state(JUVENAL_LED_BOOT_ERROR);
            } else juvenal_led_set_state(JUVENAL_LED_BOOTING);
        } else {
            const juvenal_led_state_t state = juvenal_led_get_state();
            if (state == JUVENAL_LED_BOOTING || state == JUVENAL_LED_BOOT_ERROR) {
                ESP_LOGI(TAG, "Inicializacao OK; LED verde por 2 s");
                juvenal_led_set_state(JUVENAL_LED_BOOT_OK);
            } else if (state == JUVENAL_LED_NORMAL) {
                s_runtime_ready.store(true, std::memory_order_release);
                if (!announced) { ESP_LOGI(TAG, "Aguardando a palavra-chave Juvenal"); announced = true; }
            }
        }
        // Reservar o estado impede uma nova wake/captura durante escrita na flash.
        Phase idle = Phase::IDLE;
        if (juvenal_web_settings_pending() &&
            s_phase.compare_exchange_strong(idle, Phase::SETTINGS_SAVING, std::memory_order_acq_rel)) {
            juvenal_web_flush_settings();
            s_phase.store(Phase::IDLE, std::memory_order_release);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
