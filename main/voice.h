#ifndef VOICE_H
#define VOICE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Microphone capture, double-clap detection and the speech-to-text pipeline.
// Transcripts go to agent_queue as MSG_SOURCE_VOICE messages. Never fatal:
// ESP_ERR_NOT_SUPPORTED when CONFIG_ZCLAW_VOICE_ENABLE is off.
esp_err_t voice_start(QueueHandle_t agent_queue);

// Open listening now, without claps (serial /escucha). False when voice is
// off or a voice turn is already running.
bool voice_request_listen(void);

// Microphone calibration (/mic): level page plus serial stats for
// duration_ms. False when voice is off.
bool voice_request_probe(uint32_t duration_ms);

#endif // VOICE_H
