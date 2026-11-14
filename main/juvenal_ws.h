#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
typedef void (*juvenal_ws_done_fn)(bool success);
esp_err_t juvenal_ws_init(int16_t *capture_buffer, size_t capacity, juvenal_ws_done_fn done);
bool juvenal_ws_wake(void);
void juvenal_ws_on_pcm(const int16_t *samples, size_t count);
bool juvenal_ws_active(void);
bool juvenal_ws_configured(void);
void juvenal_ws_status(char *output, size_t capacity);
