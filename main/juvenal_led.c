#include <stdint.h>
#include <stdbool.h>

#include "juvenal_led.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define LED_GPIO 48
#define LED_COUNT 1
#define WAKE_COOLDOWN_MS 4000

static const char *TAG = "JUVENAL_LED";
static led_strip_handle_t led = NULL;
static TaskHandle_t led_task_handle = NULL;

static TickType_t last_detection = 0;
static bool has_detection = false;

static esp_err_t set_color(uint8_t r, uint8_t g, uint8_t b)
{
    esp_err_t err = led_strip_set_pixel(led, 0, r, g, b);

    if (err != ESP_OK) {
        return err;
    }

    return led_strip_refresh(led);
}

static void led_task(void *arg)
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGI(TAG, "Wake word: 3 piscadas verdes");

        for (int i = 0; i < 3; i++) {
            ESP_ERROR_CHECK(set_color(0, 28, 0));
            vTaskDelay(pdMS_TO_TICKS(180));

            ESP_ERROR_CHECK(led_strip_clear(led));
            vTaskDelay(pdMS_TO_TICKS(150));
        }

        ESP_ERROR_CHECK(set_color(20, 14, 0));

        ESP_LOGI(TAG, "Aguardando: LED amarelo");
    }
}

esp_err_t juvenal_led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10000000,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false
        }
    };

    esp_err_t err = led_strip_new_rmt_device(
        &strip_cfg, &rmt_cfg, &led
    );

    if (err != ESP_OK) {
        return err;
    }

    err = set_color(20, 14, 0);

    if (err != ESP_OK) {
        led_strip_del(led);
        led = NULL;
        return err;
    }

    if (xTaskCreate(
        led_task,
        "juvenal_led",
        3072,
        NULL,
        3,
        &led_task_handle
    ) != pdPASS) {
        led_strip_del(led);
        led = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LED pronto: amarelo aguardando");

    return ESP_OK;
}

void juvenal_led_wake_detected(void)
{
    if (led_task_handle == NULL) {
        return;
    }

    TickType_t now = xTaskGetTickCount();

    if (has_detection &&
        (TickType_t)(now - last_detection) <
            pdMS_TO_TICKS(WAKE_COOLDOWN_MS)) {
        return;
    }

    last_detection = now;
    has_detection = true;

    xTaskNotifyGive(led_task_handle);
}
