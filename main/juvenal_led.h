#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t juvenal_led_init(void);
void juvenal_led_wake_detected(void);

#ifdef __cplusplus
}
#endif
