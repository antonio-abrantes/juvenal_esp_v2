#include <cstdint>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_err.h"

extern "C" {
#include "juvenal_led.h"
}

#include "esphome/components/i2s_audio/i2s_audio.h"
#include "esphome/components/micro_wake_word/micro_wake_word.h"

extern const uint8_t juvenal_model_start[]
    asm("_binary_juvehnal_tflite_start");

extern const uint8_t juvenal_model_end[]
    asm("_binary_juvehnal_tflite_end");

static const char *TAG = "JUVENAL";

static esphome::i2s_audio::I2SAudioMicrophone microphone;
static esphome::micro_wake_word::MicroWakeWord wake_word;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando Juvenal com MicroWakeWord");

    ESP_ERROR_CHECK(juvenal_led_init());

    ESP_LOGI(TAG, "Configurando INMP441");

    microphone.set_bclk_pin(GPIO_NUM_12);
    microphone.set_lrclk_pin(GPIO_NUM_11);
    microphone.set_din_pin(GPIO_NUM_10);

    microphone.set_sample_rate(16000);
    microphone.set_bits_per_sample(I2S_DATA_BIT_WIDTH_32BIT);

    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256;

    microphone.set_channel(chan_cfg);
    microphone.setup();

    if (microphone.has_error()) {
        ESP_LOGE(TAG, "Erro microfone: %s",
                 microphone.get_error_message().c_str());
        return;
    }

    ESP_LOGI(TAG, "Configurando modelo juvehnal");

    wake_word.set_microphone(&microphone);
    wake_word.set_features_step_size(10);

    wake_word.add_wake_word_model(
        juvenal_model_start,
        0.97f,
        5,
        "juvehnal",
        30000
    );

    wake_word.add_detection_callback([](std::string word) {
        ESP_LOGW(TAG, "WAKE WORD DETECTADA: %s", word.c_str());
        juvenal_led_wake_detected();
    });

    wake_word.setup();

    if (wake_word.has_error()) {
        ESP_LOGE(TAG, "Erro MicroWakeWord: %s",
                 wake_word.get_error_message().c_str());
        return;
    }

    wake_word.start();

    ESP_LOGI(TAG, "Juvenal aguardando palavra-chave");

    while (true) {
        if (microphone.has_error()) {
            ESP_LOGE(TAG, "Erro I2S: %s",
                     microphone.get_error_message().c_str());
        }

        if (wake_word.has_error()) {
            ESP_LOGE(TAG, "Erro detector: %s",
                     wake_word.get_error_message().c_str());
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

