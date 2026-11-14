/* Unico dono do WS2812. Boot independente de LCD/LVGL/Wi-Fi. */
#include "juvenal_led.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stddef.h>
static led_strip_handle_t s_strip;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static juvenal_led_state_t s_state = JUVENAL_LED_BOOTING;
static TickType_t s_since;
static uint32_t s_color = 0x2b1600;
static void led_task(void *unused)
{
    (void)unused;
    uint32_t previous = UINT32_MAX;
    while (1) {
        TickType_t now = xTaskGetTickCount();
        portENTER_CRITICAL(&s_mux);
        juvenal_led_state_t state = s_state;
        TickType_t elapsed = now - s_since;
        if (state == JUVENAL_LED_BOOT_OK && elapsed >= pdMS_TO_TICKS(2000))
            state = s_state = JUVENAL_LED_NORMAL;
        uint32_t color = s_color;
        portEXIT_CRITICAL(&s_mux);
        if (state == JUVENAL_LED_BOOTING)
            color = ((elapsed / pdMS_TO_TICKS(350)) % 2) ? 0 : 0x2b1600;
        else if (state == JUVENAL_LED_BOOT_ERROR)
            color = ((elapsed / pdMS_TO_TICKS(250)) % 2) ? 0 : 0x340000;
        else if (state == JUVENAL_LED_BOOT_OK) color = 0x002e08;
        if (color != previous) {
            esp_err_t err = led_strip_set_pixel(s_strip, 0, color >> 16,
                                                (color >> 8) & 255, color & 255);
            if (err == ESP_OK) err = led_strip_refresh(s_strip);
            if (err == ESP_OK) previous = color;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
esp_err_t juvenal_led_init(void)
{
    if (s_strip) return ESP_ERR_INVALID_STATE;
    const led_strip_config_t strip = {
        .strip_gpio_num = 48, .max_leds = 1, .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    const led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10000000,
        .mem_block_symbols = 64, .flags = {.with_dma = false},
    };
    esp_err_t err = led_strip_new_rmt_device(&strip, &rmt, &s_strip);
    if (err != ESP_OK) return err;
    s_since = xTaskGetTickCount();
    if (xTaskCreate(led_task, "juvenal_led", 3072, NULL, 3, NULL) != pdPASS) {
        led_strip_del(s_strip); s_strip = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void juvenal_led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    portENTER_CRITICAL(&s_mux);
    s_color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    portEXIT_CRITICAL(&s_mux);
}
void juvenal_led_set_state(juvenal_led_state_t state)
{
    portENTER_CRITICAL(&s_mux);
    if (s_state != state) { s_state = state; s_since = xTaskGetTickCount(); }
    portEXIT_CRITICAL(&s_mux);
}
juvenal_led_state_t juvenal_led_get_state(void)
{
    portENTER_CRITICAL(&s_mux);
    juvenal_led_state_t state = s_state;
    portEXIT_CRITICAL(&s_mux);
    return state;
}
const char *juvenal_led_state_name(void)
{
    switch (juvenal_led_get_state()) {
        case JUVENAL_LED_BOOTING: return "starting";
        case JUVENAL_LED_BOOT_ERROR: return "error";
        case JUVENAL_LED_BOOT_OK: return "ready";
        default: return "normal";
    }
}
