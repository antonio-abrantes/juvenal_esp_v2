#include "juvenal_ws.h"
#include "juvenal_config.h"
#include "juvenal_face_neutral.h"
#include "juvenal_speaker.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cctype>
#include <ctime>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_crt_bundle.h"
#include "esp_websocket_client.h"
#include "cJSON.h"

namespace {
enum class Stage { IDLE, CONNECTING, LISTENING, CAPTURING, PROCESSING, PLAYBACK };
enum class StopReason { SERVER_VAD, IDLE_TIMEOUT, NO_SPEECH };
static std::atomic<StopReason> stop_reason{StopReason::SERVER_VAD};
static std::atomic<unsigned> speech_threshold{300};
static std::atomic<Stage> stage{Stage::IDLE};
static std::atomic<bool> active{false}, capture{false}, overflow{false}, fault{false};
static std::atomic<bool> connected{false}, ready{false}, stop_input{false}, terminal{false};
static std::atomic<bool> client_finished{false};
static std::atomic<bool> terminal_ok{false}, closing{false}, input_ended{false}, output_live{false};
// SPSC: totais monotônicos; o espaço liberado pelo worker volta ao produtor.
// A capacidade limita atraso de rede, nunca a duração da fala.
static std::atomic<size_t> samples{0}, released{0}, transmitted{0}, writers{0};
static std::atomic<unsigned> last_rms{0};
static int16_t *buffer;
static size_t capacity;
static juvenal_ws_done_fn finished;
static TaskHandle_t task;
static esp_websocket_client_handle_t client;
static portMUX_TYPE ids_mux = portMUX_INITIALIZER_UNLOCKED;
static char session_id[33], turn_id[33], last_error[64];
static char incoming[8193];
static size_t incoming_used, frame_used, frame_length;
static int message_opcode;
static bool message_open;
static std::atomic<uint32_t> output_bytes{0};

static void fail(const char *code)
{
    portENTER_CRITICAL(&ids_mux);
    const bool first = !last_error[0];
    // O erro de encerramento nao deve apagar a causa inicial.
    if (first) std::snprintf(last_error, sizeof(last_error), "%s", code);
    portEXIT_CRITICAL(&ids_mux);
    capture.store(false, std::memory_order_release);
    fault.store(true, std::memory_order_release);
    // fail() roda nas tarefas WS/worker; UART sempre fora da critical section.
    if (first) ESP_LOGE("JUVENAL_WS", "Primeira falha: %s", code);
}

static bool identifier(const char *value, size_t required)
{
    if (!value || std::strlen(value) != required) return false;
    for (const char *p = value; *p; ++p)
        if (!std::isxdigit(static_cast<unsigned char>(*p))) return false;
    return true;
}

static bool unique(cJSON *object)
{
    if (!cJSON_IsObject(object)) return false;
    for (cJSON *a = object->child; a; a = a->next)
        for (cJSON *b = a->next; b; b = b->next)
            if (!a->string || !b->string || !std::strcmp(a->string, b->string)) return false;
    return true;
}

static const char *string(cJSON *object, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) ? item->valuestring : nullptr;
}

static bool number(cJSON *object, const char *name, uint32_t expected)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) && std::isfinite(item->valuedouble) &&
        item->valuedouble == static_cast<double>(expected);
}

static bool pcm_format(cJSON *payload)
{
    const char *format = string(payload, "sample_format");
    return number(payload, "sample_rate", 16000) && number(payload, "channels", 1) &&
        format && !std::strcmp(format, "s16le");
}

static bool matching(cJSON *root)
{
    const char *sid = string(root, "session_id"), *tid = string(root, "turn_id");
    portENTER_CRITICAL(&ids_mux);
    const bool ok = sid && tid && !std::strcmp(sid, session_id) && !std::strcmp(tid, turn_id);
    portEXIT_CRITICAL(&ids_mux);
    return ok;
}

static bool shallow_json(const char *data, size_t size)
{
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < size; ++i) {
        const char c = data[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 8) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
    }
    return !depth && !quoted;
}

static void control(const char *text, size_t size)
{
    if (!shallow_json(text, size)) { fail("invalid_json"); return; }
    const char *parsed_end = nullptr;
    cJSON *root = cJSON_ParseWithLengthOpts(text, size + 1, &parsed_end, true);
    if (!root) { fail("invalid_json"); return; }
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    const char *event = string(root, "event");
    if (!unique(root) || !unique(payload) || !number(root, "protocol_version", 1) || !event) {
        cJSON_Delete(root); fail("invalid_envelope"); return;
    }
    if (!std::strcmp(event, "session.error")) {
        fail("server_session_error");
    } else if (!std::strcmp(event, "session.ready")) {
        const char *sid = string(root, "session_id"), *tid = string(root, "turn_id");
        const char *mode = string(payload, "output_mode");
        cJSON *threshold = cJSON_GetObjectItemCaseSensitive(payload, "vad_start_rms");
        const Stage current = stage.load(std::memory_order_acquire);
        bool same_session;
        portENTER_CRITICAL(&ids_mux);
        same_session = !session_id[0] || (sid && !std::strcmp(session_id, sid));
        portEXIT_CRITICAL(&ids_mux);
        if (!identifier(sid, 32) || !identifier(tid, 32) || !same_session || !pcm_format(payload) ||
            !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(payload, "conversation_events")) ||
            !cJSON_IsNumber(threshold) || !std::isfinite(threshold->valuedouble) ||
            threshold->valuedouble < 1 || threshold->valuedouble > 32767 ||
            std::floor(threshold->valuedouble) != threshold->valuedouble || ready.load() ||
            !mode || (std::strcmp(mode, "local") && std::strcmp(mode, "endpoint") && std::strcmp(mode, "esp32")) ||
            (current != Stage::CONNECTING && current != Stage::PROCESSING) ||
            (!std::strcmp(mode, "esp32") && !juvenal_speaker_available())) {
            fail("invalid_session_ready");
        } else {
            portENTER_CRITICAL(&ids_mux);
            std::snprintf(session_id, sizeof(session_id), "%s", sid);
            std::snprintf(turn_id, sizeof(turn_id), "%s", tid);
            portEXIT_CRITICAL(&ids_mux);
            ESP_LOGI("JUVENAL_WS", "Sessao %.32s pronta; PCM16 mono 16000 Hz; saida=%s", sid, mode);
            speech_threshold.store(static_cast<unsigned>(threshold->valuedouble));
            ready.store(true, std::memory_order_release);
        }
    } else if (!matching(root)) {
        fail("session_or_turn_mismatch");
    } else if (!std::strcmp(event, "input.stop")) {
        const Stage current = stage.load();
        const char *reason = string(payload, "reason");
        if ((current != Stage::CAPTURING && current != Stage::LISTENING) || input_ended.load() ||
            !reason || (std::strcmp(reason, "vad_silence") && std::strcmp(reason, "conversation_idle_timeout") &&
                        std::strcmp(reason, "no_speech"))) fail("unexpected_input_stop");
        else {
            stop_reason.store(!std::strcmp(reason, "conversation_idle_timeout") ? StopReason::IDLE_TIMEOUT :
                              !std::strcmp(reason, "no_speech") ? StopReason::NO_SPEECH : StopReason::SERVER_VAD);
            capture.store(false, std::memory_order_release);
            stop_input.store(true, std::memory_order_release);
        }
    } else if (!std::strcmp(event, "output.start")) {
        if (!input_ended.load() || output_live.load() || !pcm_format(payload) ||
            !number(payload, "block_samples", 320) || !number(payload, "block_bytes", 640) ||
            !juvenal_speaker_begin()) fail("invalid_output_start");
        else {
            output_live.store(true, std::memory_order_release);
            stage.store(Stage::PLAYBACK, std::memory_order_release);
        }
    } else if (!std::strcmp(event, "output.end")) {
        cJSON *bytes = cJSON_GetObjectItemCaseSensitive(payload, "total_bytes");
        if (!output_live.load() || !cJSON_IsNumber(bytes) || !std::isfinite(bytes->valuedouble) ||
            bytes->valuedouble < 2 || bytes->valuedouble > 1920000 ||
            std::floor(bytes->valuedouble) != bytes->valuedouble ||
            !number(payload, "total_samples", static_cast<uint32_t>(bytes->valuedouble) / 2)) {
            fail("invalid_output_end");
        } else {
            const uint32_t total = static_cast<uint32_t>(bytes->valuedouble);
            output_bytes.store(total, std::memory_order_release);
            if (!juvenal_speaker_end(total)) fail("invalid_output_end");
        }
    } else if (!std::strcmp(event, "session.end")) {
        const char *result = string(payload, "result");
        const bool successful = result && (!std::strcmp(result, "completed") || !std::strcmp(result, "inactivity"));
        if (output_live.load() && successful) {
            cJSON_Delete(root); fail("playback_not_drained"); return;
        }
        terminal_ok.store(successful);
        capture.store(false, std::memory_order_release);
        terminal.store(true, std::memory_order_release);
    } else if (std::strcmp(event, "assistant.state") && std::strcmp(event, "assistant.response") &&
               std::strcmp(event, "transcript")) {
        fail("unexpected_server_event");
    }
    cJSON_Delete(root);
}

static void ws_event(void *, esp_event_base_t, int32_t id, void *argument)
{
    if (id == WEBSOCKET_EVENT_CONNECTED) { connected.store(true); return; }
    if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_CLOSED || id == WEBSOCKET_EVENT_FINISH) {
        if (id == WEBSOCKET_EVENT_FINISH) client_finished.store(true);
        connected.store(false);
        if (!closing.load() && !terminal.load()) fail("connection_closed");
        return;
    }
    if (id == WEBSOCKET_EVENT_ERROR) { if (!closing.load()) fail("websocket_transport"); return; }
    if (id != WEBSOCKET_EVENT_DATA || !argument) return;
    auto *data = static_cast<esp_websocket_event_data_t *>(argument);
    if (data->op_code >= 8) return; // Ping, pong e close pertencem ao cliente WS.
    if (data->data_len < 0 || data->payload_len < 0 || data->payload_offset < 0) {
        fail("invalid_frame"); return;
    }
    if (data->payload_offset == 0) {
        frame_used = 0;
        frame_length = data->payload_len;
        if (data->op_code == 1 || data->op_code == 2) {
            if (message_open) { fail("overlapping_frame"); return; }
            incoming_used = 0;
            message_opcode = data->op_code;
            message_open = true;
        } else if (data->op_code != 0 || !message_open) {
            fail("invalid_continuation"); return;
        }
    }
    const size_t limit = message_opcode == 2 ? 640 : 8192;
    if (!message_open || static_cast<size_t>(data->payload_offset) != frame_used ||
        frame_used + data->data_len > frame_length || incoming_used + data->data_len > limit) {
        fail("frame_limit"); return;
    }
    if (data->data_len) std::memcpy(incoming + incoming_used, data->data_ptr, data->data_len);
    incoming_used += data->data_len;
    frame_used += data->data_len;
    if (frame_used == frame_length && data->fin) {
        message_open = false;
        if (message_opcode == 2) {
            if (!output_live.load() || !juvenal_speaker_push(
                    reinterpret_cast<const uint8_t *>(incoming), incoming_used)) fail("playback_buffer_or_format");
        } else {
            incoming[incoming_used] = 0;
            control(incoming, incoming_used);
        }
    }
}

static bool send(const char *event, cJSON *payload)
{
    if (fault.load() && std::strcmp(event, "session.cancel")) {
        cJSON_Delete(payload);
        return false;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root || !payload) { cJSON_Delete(root); cJSON_Delete(payload); fail("control_memory"); return false; }
    char sid[33], tid[33];
    portENTER_CRITICAL(&ids_mux);
    std::memcpy(sid, session_id, sizeof(sid));
    std::memcpy(tid, turn_id, sizeof(tid));
    portEXIT_CRITICAL(&ids_mux);
    cJSON_AddNumberToObject(root, "protocol_version", 1);
    cJSON_AddStringToObject(root, "event", event);
    if (sid[0]) cJSON_AddStringToObject(root, "session_id", sid);
    if (tid[0]) cJSON_AddStringToObject(root, "turn_id", tid);
    cJSON_AddItemToObject(root, "payload", payload);
    char *json = cJSON_PrintUnformatted(root);
    bool ok = false;
    if (json) {
        const int length = static_cast<int>(std::strlen(json));
        ok = length <= 8192 && esp_websocket_client_send_text(client, json, length, pdMS_TO_TICKS(1000)) == length;
        cJSON_free(json);
    }
    cJSON_Delete(root);
    if (!ok) fail("control_send_failed");
    return ok;
}

static bool configured()
{
    const char *url = JUVENAL_WS_URL, *token = JUVENAL_WS_DEVICE_TOKEN, *device = JUVENAL_WS_DEVICE_ID;
    const size_t url_length = std::strlen(url);
    if ((std::strncmp(url, "ws://", 5) && std::strncmp(url, "wss://", 6)) ||
        url_length < 15 || url_length > 255 || std::strcmp(url + url_length - 7, "/ws/esp") ||
        std::strchr(url, '?') || std::strchr(url, '#') || std::strchr(url, '@') ||
        std::strlen(token) < 16 || std::strlen(token) > 128 ||
        !device[0] || std::strlen(device) > 64) return false;
    for (const char *p = url; *p; ++p) if (static_cast<unsigned char>(*p) <= 32) return false;
    for (const char *p = token; *p; ++p)
        if (static_cast<unsigned char>(*p) <= 32 || static_cast<unsigned char>(*p) >= 127) return false;
    for (const char *p = device; *p; ++p)
        if (!std::isalnum(static_cast<unsigned char>(*p)) && *p != '-' && *p != '_') return false;
    return true;
}

static void worker(void *)
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        fault.store(false); terminal.store(false); terminal_ok.store(false); connected.store(false);
        ready.store(false); stop_input.store(false); closing.store(false);
        client_finished.store(false);
        input_ended.store(false); output_live.store(false);
        message_open = false; incoming_used = frame_used = frame_length = 0;
        portENTER_CRITICAL(&ids_mux);
        session_id[0] = turn_id[0] = last_error[0] = 0;
        portEXIT_CRITICAL(&ids_mux);
        const esp_err_t speaker_result = juvenal_speaker_init();
        if (JUVENAL_SPEAKER_ENABLED && speaker_result != ESP_OK)
            ESP_LOGW("JUVENAL_WS", "TX indisponivel: %s", esp_err_to_name(speaker_result));
        char authorization[180];
        std::snprintf(authorization, sizeof(authorization), "Authorization: Bearer %s\r\n", JUVENAL_WS_DEVICE_TOKEN);
        esp_websocket_client_config_t config = {};
        config.uri = JUVENAL_WS_URL;
        config.headers = authorization;
        config.disable_auto_reconnect = true;
        config.enable_close_reconnect = false;
        config.network_timeout_ms = 1000;
        config.buffer_size = 1024;
        config.task_stack = 6144;
        config.crt_bundle_attach = esp_crt_bundle_attach;
        client = esp_websocket_client_init(&config);
        if (!client) fail("client_init");
        if (client && esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, ws_event, nullptr) != ESP_OK)
            fail("event_registration");
        if (client && !fault.load() && esp_websocket_client_start(client) != ESP_OK) fail("connect_start");
        const int64_t started = esp_timer_get_time();
        bool hello = false, input_started = false, speech_reported = false;
        size_t sent_samples = 0;
        int64_t next_input_log = started + 2000000;
        uint32_t last_credit = 0;
        float last_x = 0, last_y = 0;
        while (!fault.load() && !terminal.load()) {
            const int64_t now = esp_timer_get_time();
            // Somente protecao da abertura; inatividade de conversa pertence ao Python.
            if (stage.load() == Stage::CONNECTING && now - started > 5000000LL) { fail("connect_timeout"); break; }
            if (connected.load() && !hello) {
                cJSON *payload = cJSON_CreateObject();
                cJSON_AddStringToObject(payload, "device_id", JUVENAL_WS_DEVICE_ID);
                cJSON_AddBoolToObject(payload, "playback", juvenal_speaker_available());
                cJSON_AddBoolToObject(payload, "conversation_events", true);
                cJSON_AddNumberToObject(payload, "playback_buffer_bytes", 16384);
                hello = send("session.start", payload);
            }
            if (ready.exchange(false)) {
                const bool continuation = input_started;
                if (continuation) {
                    capture.store(false, std::memory_order_release);
                    while (writers.load()) vTaskDelay(1);
                    std::memset(buffer, 0, capacity * sizeof(int16_t));
                    samples.store(0); released.store(0); transmitted.store(0); overflow.store(false);
                    sent_samples = 0; last_x = last_y = 0; last_credit = 0;
                }
                input_ended.store(false);
                stop_input.store(false);
                output_bytes.store(0);
                speech_reported = false;
                stage.store(Stage::LISTENING, std::memory_order_release);
                capture.store(true, std::memory_order_release);
                cJSON *payload = cJSON_CreateObject();
                cJSON_AddNumberToObject(payload, "sample_rate", 16000);
                cJSON_AddNumberToObject(payload, "channels", 1);
                cJSON_AddStringToObject(payload, "sample_format", "s16le");
                if (!send("input.start", payload) || !send("input.ready", cJSON_CreateObject())) break;
                input_started = true;
                next_input_log = now + 2000000;
                // A primeira wake preserva suas tres piscadas; depois, escuta local
                // na mesma sessao, sem inferencia de outra wake ou espera bloqueante.
                if (continuation) juvenal_face_event(JUVENAL_FACE_LISTENING);
                ESP_LOGI("JUVENAL_WS", "Escuta pronta; aguardando fala e encerramento pelo servidor");
            }
            if (overflow.load()) { fail("input_buffer_overflow"); break; }
            if (stop_input.exchange(false) && input_started && !input_ended.load()) {
                capture.store(false, std::memory_order_release);
                while (writers.load()) vTaskDelay(1);
                if (overflow.load()) { fail("input_buffer_overflow"); break; }
                input_ended.store(true);
                stage.store(Stage::PROCESSING);
                juvenal_face_event(JUVENAL_FACE_SENDING);
                cJSON *payload = cJSON_CreateObject();
                const StopReason reason = stop_reason.load();
                cJSON_AddStringToObject(payload, "reason", reason == StopReason::IDLE_TIMEOUT ?
                    "conversation_idle_timeout" : reason == StopReason::NO_SPEECH ? "no_speech" : "server_vad");
                cJSON_AddNumberToObject(payload, "total_samples", sent_samples);
                if (!send("input.end", payload)) break;
                ESP_LOGI("JUVENAL_WS", "Captura encerrada pelo servidor; enviadas %u amostras", static_cast<unsigned>(sent_samples));
            }
            if ((stage.load() == Stage::LISTENING || stage.load() == Stage::CAPTURING) && !input_ended.load()) {
                for (unsigned batch = 0; batch < 4 && !stop_input.load(); ++batch) {
                    const size_t read_at = released.load(std::memory_order_relaxed);
                    const size_t available = samples.load(std::memory_order_acquire);
                    if (available - read_at < 320) break;
                    int16_t pcm[320];
                    for (size_t i = 0; i < 320; ++i) {
                        const float x = buffer[(read_at + i) % capacity];
                        const float y = 0.9695f * (last_y + x - last_x);
                        last_x = x; last_y = y;
                        const int value = static_cast<int>(std::lrintf(y * 1.6f));
                        pcm[i] = static_cast<int16_t>(std::max(-32768, std::min(32767, value)));
                    }
                    if (!speech_reported) {
                        uint64_t energy = 0;
                        for (int16_t value : pcm) energy += static_cast<int64_t>(value) * value;
                        const unsigned rms = static_cast<unsigned>(std::sqrt(static_cast<double>(energy) / 320));
                        if (rms >= speech_threshold.load()) {
                            // Aviso no primeiro bloco elegivel; VAD do servidor ainda
                            // confirma a fala antes de STT. Nao ha timer ocioso aqui.
                            if (!send("input.speech_started", cJSON_CreateObject())) break;
                            speech_reported = true;
                            stage.store(Stage::CAPTURING, std::memory_order_release);
                            ESP_LOGI("JUVENAL_WS", "Inicio de fala comunicado ao servidor; RMS=%u", rms);
                        }
                    }
                    // PCM já está no bloco local: o produtor pode reutilizar o ring.
                    released.store(read_at + 320, std::memory_order_release);
                    if (esp_websocket_client_send_bin(client, reinterpret_cast<const char *>(pcm),
                                                      sizeof(pcm), pdMS_TO_TICKS(1000)) != sizeof(pcm)) {
                        fail("audio_send_failed"); break;
                    }
                    sent_samples += 320;
                    transmitted.store(sent_samples, std::memory_order_release);
                }
                if (now >= next_input_log) {
                    const size_t read_at = released.load(std::memory_order_acquire);
                    const size_t produced = samples.load(std::memory_order_acquire);
                    ESP_LOGI("JUVENAL_WS", "PCM streaming: capturadas=%u enviadas=%u pendentes=%u RMS=%u",
                             static_cast<unsigned>(produced), static_cast<unsigned>(sent_samples),
                             static_cast<unsigned>(produced - read_at), last_rms.load());
                    next_input_log = now + 2000000;
                }
            }
            if (output_live.load() && !fault.load()) {
                if (juvenal_speaker_poll() != ESP_OK) { fail("playback_underrun"); break; }
                const uint32_t consumed = juvenal_speaker_consumed();
                if (consumed != last_credit) {
                    cJSON *payload = cJSON_CreateObject();
                    cJSON_AddNumberToObject(payload, "consumed_bytes", consumed);
                    if (!send("playback.credit", payload)) break;
                    last_credit = consumed;
                }
                if (juvenal_speaker_finished()) {
                    if (fault.load()) break;
                    const uint32_t total = output_bytes.load(std::memory_order_acquire);
                    juvenal_speaker_cancel();
                    juvenal_speaker_init();
                    output_live.store(false);
                    stage.store(Stage::PROCESSING);
                    cJSON *payload = cJSON_CreateObject();
                    cJSON_AddNumberToObject(payload, "total_bytes", total);
                    cJSON_AddNumberToObject(payload, "total_samples", total / 2);
                    if (!send("playback.finished", payload)) break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        capture.store(false, std::memory_order_release);
        while (writers.load()) vTaskDelay(1);
        // Captura compartilhada liberada antes de retomar a wake/painel.
        std::memset(buffer, 0, capacity * sizeof(int16_t));
        if (client) {
            if (fault.load() && connected.load() && session_id[0] && !terminal.load()) {
                cJSON *payload = cJSON_CreateObject();
                cJSON_AddStringToObject(payload, "reason", "client_failure");
                send("session.cancel", payload);
            }
            closing.store(true);
            if (terminal.load()) {
                // O servidor fecha depois de session.end; deixar o cliente responder.
                const TickType_t close_started = xTaskGetTickCount();
                while (!client_finished.load() &&
                       xTaskGetTickCount() - close_started < pdMS_TO_TICKS(1500))
                    vTaskDelay(pdMS_TO_TICKS(10));
            } else if (esp_websocket_client_is_connected(client)) {
                esp_websocket_client_close(client, pdMS_TO_TICKS(1500));
            }
            // destroy() aguarda a task somente se ela ainda estiver ativa.
            esp_websocket_client_destroy(client);
            client = nullptr;
        }
        juvenal_speaker_cancel();
        const bool success = terminal.load() && terminal_ok.load() && !fault.load();
        char error[64];
        portENTER_CRITICAL(&ids_mux);
        std::memcpy(error, last_error, sizeof(error));
        portEXIT_CRITICAL(&ids_mux);
        if (!success) ESP_LOGW("JUVENAL_WS", "Interacao encerrada: %s; sem replay", error[0] ? error : "session_failed");
        else ESP_LOGI("JUVENAL_WS", "Interacao concluida; retornando a espera");
        stage.store(Stage::IDLE, std::memory_order_release);
        active.store(false, std::memory_order_release);
        finished(success);
    }
}
}

esp_err_t juvenal_ws_init(int16_t *capture_buffer, size_t count, juvenal_ws_done_fn done)
{
    if (!capture_buffer || count < 320 || !done || task) return ESP_ERR_INVALID_ARG;
    buffer = capture_buffer; capacity = count; finished = done;
    return xTaskCreate(worker, "juvenal_ws", 9216, nullptr, 4, &task) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

bool juvenal_ws_wake(void)
{
    if (!task || !configured()) {
        ESP_LOGW("JUVENAL_WS", "Configure URL WS, identidade e token no arquivo local");
        return false;
    }
    if (!std::strncmp(JUVENAL_WS_URL, "wss://", 6) && std::time(nullptr) < 1704067200) {
        ESP_LOGW("JUVENAL_WS", "Aguardando sincronizacao de horario para validar TLS");
        return false;
    }
    bool idle = false;
    if (!active.compare_exchange_strong(idle, true)) return false;
    samples.store(0); released.store(0); transmitted.store(0); overflow.store(false); last_rms.store(0);
    stage.store(Stage::CONNECTING, std::memory_order_release);
    capture.store(true, std::memory_order_release);
    xTaskNotifyGive(task);
    return true;
}

void juvenal_ws_on_pcm(const int16_t *input, size_t count)
{
    if (!capture.load(std::memory_order_acquire)) return;
    writers.fetch_add(1, std::memory_order_acq_rel);
    if (capture.load(std::memory_order_acquire)) {
        const size_t produced = samples.load(std::memory_order_relaxed);
        const size_t read_at = released.load(std::memory_order_acquire);
        const size_t queued = produced - read_at;
        if (queued > capacity || count > capacity - queued) {
            // Não sobrescrever PCM ainda não enviado nem tratar perda como fala válida.
            capture.store(false, std::memory_order_release);
            overflow.store(true, std::memory_order_release);
        } else {
            const size_t offset = produced % capacity;
            const size_t first = std::min(count, capacity - offset);
            if (first) std::memcpy(buffer + offset, input, first * sizeof(int16_t));
            if (count > first) std::memcpy(buffer, input + first, (count - first) * sizeof(int16_t));
            uint64_t energy = 0;
            for (size_t i = 0; i < count; ++i) {
                const int32_t value = input[i];
                energy += static_cast<uint64_t>(value * value);
            }
            if (count) last_rms.store(static_cast<unsigned>(std::sqrt(double(energy) / count)));
            samples.store(produced + count, std::memory_order_release);
        }
    }
    writers.fetch_sub(1, std::memory_order_acq_rel);
}

bool juvenal_ws_active(void) { return active.load(std::memory_order_acquire); }
bool juvenal_ws_configured(void) { return configured(); }

void juvenal_ws_status(char *output, size_t size)
{
    const char *state = "idle";
    switch (stage.load()) {
        case Stage::CONNECTING: state = "connecting"; break;
        case Stage::LISTENING: state = "listening"; break;
        case Stage::CAPTURING: state = "capturing"; break;
        case Stage::PROCESSING: state = "processing"; break;
        case Stage::PLAYBACK: state = "playback"; break;
        default: break;
    }
    char error[64];
    portENTER_CRITICAL(&ids_mux);
    std::memcpy(error, last_error, sizeof(error));
    portEXIT_CRITICAL(&ids_mux);
    const size_t read_at = released.load(std::memory_order_acquire);
    const size_t produced = samples.load(std::memory_order_acquire);
    std::snprintf(output, size,
        "{\"auto_transport\":\"%s\",\"ws_active\":%s,\"ws_state\":\"%s\",\"ws_samples\":%u,\"ws_rms\":%u,\"ws_error\":\"%s\",\"ws_sent_samples\":%u,\"ws_pending_samples\":%u}",
        JUVENAL_AUTO_TRANSPORT_WS ? "ws" : "http",
        active.load() ? "true" : "false", state, static_cast<unsigned>(produced),
        last_rms.load(), error, static_cast<unsigned>(transmitted.load()),
        static_cast<unsigned>(produced >= read_at ? produced - read_at : 0));
}
