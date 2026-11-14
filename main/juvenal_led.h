#pragma once
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { JUVENAL_LED_BOOTING, JUVENAL_LED_BOOT_ERROR,
               JUVENAL_LED_BOOT_OK, JUVENAL_LED_NORMAL } juvenal_led_state_t;
esp_err_t juvenal_led_init(void);
void juvenal_led_set_rgb(uint8_t r, uint8_t g, uint8_t b);
void juvenal_led_set_state(juvenal_led_state_t state);
juvenal_led_state_t juvenal_led_get_state(void);
const char *juvenal_led_state_name(void);
#ifdef __cplusplus
}
#endif
