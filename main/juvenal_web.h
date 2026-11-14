#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ponte entre o servidor HTTP e o controlador I2S principal. */
bool juvenal_capture_web_start(void);
bool juvenal_capture_web_stop(void);
bool juvenal_capture_web_clear(void);
bool juvenal_capture_web_audio(const unsigned char **data, size_t *size);
bool juvenal_capture_web_send(void);
void juvenal_capture_web_status(char *out, size_t size);

/* Painel web local (HTTP Basic). URL persistida em NVS. */
esp_err_t juvenal_web_init(void);
esp_err_t juvenal_web_start(void);
bool juvenal_web_settings_pending(void);
void juvenal_web_flush_settings(void);
void juvenal_web_get_url(char *out, size_t size);
bool juvenal_web_set_url(const char *url);

#ifdef __cplusplus
}
#endif
