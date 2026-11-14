#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t juvenal_speaker_init(void);
bool juvenal_speaker_available(void);
bool juvenal_speaker_begin(void);
bool juvenal_speaker_push(const uint8_t *pcm, size_t bytes);
bool juvenal_speaker_end(uint32_t total_bytes);
esp_err_t juvenal_speaker_poll(void);
uint32_t juvenal_speaker_consumed(void);
bool juvenal_speaker_finished(void);
void juvenal_speaker_cancel(void);
bool juvenal_speaker_set_volume(unsigned percent);
unsigned juvenal_speaker_get_volume(void);
/* Media absoluta do PCM apos ganho, publicada apos EOF do DMA. */
unsigned juvenal_speaker_played_level(bool *playing);
#ifdef __cplusplus
}
#endif
