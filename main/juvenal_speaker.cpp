#include "juvenal_speaker.h"
#include "juvenal_config.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <climits>
#include <initializer_list>
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"

namespace {
constexpr uint32_t CAPACITY = 16384;
constexpr uint32_t MAX_BYTES = 16000 * 2 * 60;
static i2s_chan_handle_t tx = nullptr;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t ring[CAPACITY];
static uint32_t read_at, write_at, used, received, consumed, drained, staged, expected;
static uint32_t played_gap = UINT32_MAX;
static bool active, running, ended, short_block, failed;
static unsigned volume = 100, played_level;
static TickType_t played_at;
struct Slot { void *address; uint32_t bytes; uint32_t gap_at; bool gap; unsigned level; };
static Slot slots[4];

static bool valid_pin(int pin)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) return false;
    if (pin >= 26 && pin <= 37) return false; // Flash/PSRAM da N16R8.
    for (int reserved : {7, 8, 9, 10, 11, 12, 13, 14, 43, 44, 48})
        if (pin == reserved) return false;
    return true;
}

static bool configured()
{
    return JUVENAL_SPEAKER_ENABLED &&
        valid_pin(JUVENAL_TX_BCLK_GPIO) && valid_pin(JUVENAL_TX_WS_GPIO) &&
        valid_pin(JUVENAL_TX_DOUT_GPIO) &&
        JUVENAL_TX_BCLK_GPIO != JUVENAL_TX_WS_GPIO &&
        JUVENAL_TX_BCLK_GPIO != JUVENAL_TX_DOUT_GPIO &&
        JUVENAL_TX_WS_GPIO != JUVENAL_TX_DOUT_GPIO;
}

// O callback confirma o buffer que SAIU pelo DMA e preenche seu proximo ciclo.
// Nao usa rede, heap, LVGL ou operacoes de bloqueio.
static bool IRAM_ATTR on_sent(i2s_chan_handle_t, i2s_event_data_t *event, void *)
{
    portENTER_CRITICAL_ISR(&mux);
    if (!active || !event->dma_buf || event->size != 1280) {
        failed = active;
        portEXIT_CRITICAL_ISR(&mux);
        return false;
    }
    Slot *slot = nullptr;
    for (auto &item : slots) {
        if (item.address == event->dma_buf) { slot = &item; break; }
    }
    if (!slot) {
        for (auto &item : slots) {
            if (!item.address) { item.address = event->dma_buf; slot = &item; break; }
        }
    }
    if (!slot) {
        failed = true;
        portEXIT_CRITICAL_ISR(&mux);
        return false;
    }
    drained += slot->bytes;
    // Metadado do descritor que SAIU, nao do audio recebido/na fila.
    played_level = slot->level;
    played_at = xTaskGetTickCountFromISR();
    if (slot->gap) {
        if (slot->gap_at < played_gap) played_gap = slot->gap_at;
        if (received > played_gap) failed = true;
    }
    slot->bytes = 0;
    slot->gap = false;
    const uint32_t bytes = used >= 640 ? 640 : ((ended || short_block) ? used : 0);
    auto *destination = static_cast<int16_t *>(event->dma_buf);
    uint32_t amplitude = 0;
    const unsigned gain = volume;
    for (uint32_t offset = 0; offset < bytes; offset += 2) {
        const uint8_t low = ring[read_at];
        read_at = (read_at + 1) % CAPACITY;
        const uint8_t high = ring[read_at];
        read_at = (read_at + 1) % CAPACITY;
        const int16_t raw = static_cast<int16_t>(low | (uint16_t(high) << 8));
        const int16_t value = static_cast<int16_t>(int32_t(raw) * int32_t(gain) / 100);
        amplitude += value < 0 ? uint32_t(-int32_t(value)) : uint32_t(value);
        const uint32_t index = offset; // Duplicacao mono -> dois slots I2S.
        destination[index] = value;
        destination[index + 1] = value;
    }
    used -= bytes;
    consumed += bytes;
    staged += bytes;
    slot->bytes = bytes;
    slot->level = bytes ? amplitude / (bytes / 2) : 0;
    if (!bytes && staged && !ended && !short_block) {
        slot->gap = true;
        slot->gap_at = staged;
    }
    portEXIT_CRITICAL_ISR(&mux);
    return false;
}
}

esp_err_t juvenal_speaker_init(void)
{
    if (!configured()) return ESP_ERR_INVALID_STATE;
    if (tx) return ESP_OK;
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    channel.dma_desc_num = 4;
    channel.dma_frame_num = 320;
    channel.auto_clear_before_cb = true;
    channel.auto_clear_after_cb = false;
    esp_err_t result = i2s_new_channel(&channel, &tx, nullptr);
    if (result != ESP_OK) return result;
    i2s_std_config_t config = {};
    config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000);
    config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    config.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    config.gpio_cfg.bclk = static_cast<gpio_num_t>(JUVENAL_TX_BCLK_GPIO);
    config.gpio_cfg.ws = static_cast<gpio_num_t>(JUVENAL_TX_WS_GPIO);
    config.gpio_cfg.dout = static_cast<gpio_num_t>(JUVENAL_TX_DOUT_GPIO);
    config.gpio_cfg.din = I2S_GPIO_UNUSED;
    result = i2s_channel_init_std_mode(tx, &config);
    if (result == ESP_OK) {
        i2s_event_callbacks_t callbacks = {};
        callbacks.on_sent = on_sent;
        result = i2s_channel_register_event_callback(tx, &callbacks, nullptr);
    }
    if (result != ESP_OK) { i2s_del_channel(tx); tx = nullptr; }
    return result;
}

bool juvenal_speaker_available(void) { return configured() && tx != nullptr; }

bool juvenal_speaker_begin(void)
{
    if (!juvenal_speaker_available() || active || running) return false;
    portENTER_CRITICAL(&mux);
    read_at = write_at = used = received = consumed = drained = staged = expected = 0;
    played_gap = UINT32_MAX;
    ended = short_block = failed = false;
    played_level = 0;
    std::memset(slots, 0, sizeof(slots));
    active = true;
    portEXIT_CRITICAL(&mux);
    return true;
}

bool juvenal_speaker_push(const uint8_t *pcm, size_t bytes)
{
    portENTER_CRITICAL(&mux);
    if (!active || ended || short_block || !bytes || bytes > 640 || bytes % 2 ||
        used + bytes > CAPACITY || received + bytes > MAX_BYTES || failed) {
        failed = true;
        portEXIT_CRITICAL(&mux);
        return false;
    }
    for (size_t i = 0; i < bytes; ++i) {
        ring[write_at] = pcm[i];
        write_at = (write_at + 1) % CAPACITY;
    }
    used += bytes;
    received += bytes;
    short_block = bytes < 640;
    if (played_gap != UINT32_MAX && received > played_gap) failed = true;
    const bool ok = !failed;
    portEXIT_CRITICAL(&mux);
    return ok;
}

bool juvenal_speaker_end(uint32_t total_bytes)
{
    portENTER_CRITICAL(&mux);
    const bool ok = active && !ended && total_bytes > 0 && total_bytes == received && !failed;
    if (ok) { expected = total_bytes; ended = true; }
    else failed = true;
    portEXIT_CRITICAL(&mux);
    return ok;
}

esp_err_t juvenal_speaker_poll(void)
{
    portENTER_CRITICAL(&mux);
    const bool error = failed;
    const bool start = active && !running && (used >= 6 * 640 || ended);
    portEXIT_CRITICAL(&mux);
    if (error) return ESP_FAIL;
    if (start) {
        const esp_err_t result = i2s_channel_enable(tx);
        if (result != ESP_OK) return result;
        portENTER_CRITICAL(&mux);
        running = true;
        portEXIT_CRITICAL(&mux);
    }
    return ESP_OK;
}

uint32_t juvenal_speaker_consumed(void)
{
    portENTER_CRITICAL(&mux);
    const uint32_t value = consumed;
    portEXIT_CRITICAL(&mux);
    return value;
}

bool juvenal_speaker_finished(void)
{
    portENTER_CRITICAL(&mux);
    const bool value = active && ended && !failed && !used && drained == expected && staged == drained;
    portEXIT_CRITICAL(&mux);
    return value;
}

void juvenal_speaker_cancel(void)
{
    portENTER_CRITICAL(&mux);
    const bool was_running = running;
    portEXIT_CRITICAL(&mux);
    if (tx && was_running) i2s_channel_disable(tx);
    portENTER_CRITICAL(&mux);
    running = false;
    active = false;
    played_level = 0;
    portEXIT_CRITICAL(&mux);
    if (tx) { i2s_del_channel(tx); tx = nullptr; }
    // Sem DMA ativo nem novas escritas aceitas: descartar o PCM residual.
    std::memset(ring, 0, sizeof(ring));
}

bool juvenal_speaker_set_volume(unsigned percent)
{
    if (percent > 100) return false;
    portENTER_CRITICAL(&mux);
    volume = percent;
    portEXIT_CRITICAL(&mux);
    return true;
}
unsigned juvenal_speaker_get_volume(void)
{
    portENTER_CRITICAL(&mux);
    const unsigned value = volume;
    portEXIT_CRITICAL(&mux);
    return value;
}
unsigned juvenal_speaker_played_level(bool *playing)
{
    portENTER_CRITICAL(&mux);
    const bool live = active && running && !failed;
    const unsigned level = live && xTaskGetTickCount() - played_at <= pdMS_TO_TICKS(120) ? played_level : 0;
    portEXIT_CRITICAL(&mux);
    if (playing) *playing = live;
    return level;
}
