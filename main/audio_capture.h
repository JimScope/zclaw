#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define AUDIO_CAPTURE_RATE_HZ       16000
#define AUDIO_CAPTURE_FRAME_SAMPLES 160   // 10 ms

typedef void (*audio_frame_cb_t)(const int16_t *pcm, size_t n, void *ctx);

// Samples `gpio` (ADC1) at 32 kHz and calls cb from the audio task with
// 10 ms frames of 16 kHz int16 audio. ESP_ERR_INVALID_ARG for a non-ADC1 pin.
esp_err_t audio_capture_start(int gpio, audio_frame_cb_t cb, void *ctx);
uint32_t audio_capture_overruns(void);   // conversion frames lost (pool full)
uint16_t audio_capture_raw_mean(void);   // mean raw 12-bit value (~2048 when centred)

#endif // AUDIO_CAPTURE_H
