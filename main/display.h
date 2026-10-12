#ifndef DISPLAY_API_H
#define DISPLAY_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Start the display (and SD card) subsystem. Never fatal: returns an error
// and leaves the rest of the firmware untouched if the hardware is missing.
// ESP_ERR_NOT_SUPPORTED when CONFIG_ZCLAW_DISPLAY_ENABLE is off.
esp_err_t display_start(void);

// Non-blocking event hooks; no-ops when the display is disabled or not started.
void display_notify_thinking(bool active);
void display_notify_talking(size_t reply_chars);
void display_notify_wifi(bool connected);

// Show the status page for duration_ms (e.g. /panel). False when the display
// is disabled or not running.
bool display_request_status(uint32_t duration_ms);

// Show the VPS dashboard <name> ("clima", "cripto") for duration_ms.
// False when the display is disabled or not running.
bool display_request_dashboard(const char *name, uint32_t duration_ms);

// Show an already downloaded 240x240 JPEG for duration_ms. On success the
// display owns jpeg (it frees it); on false the caller still owns it.
bool display_show_jpeg(uint8_t *jpeg, size_t len, uint32_t duration_ms);

// Show `reply` as text under a grey header "<label> <prompt>" (prompt
// clipped to 160 bytes, reply to 1024, both copied). The page lasts 70 ms
// per character (8-45 s) and pages itself. False when the display is
// disabled, not running, out of memory or its queue is full.
bool display_show_answer(const char *label, const char *prompt, const char *reply);

// True once display_start() succeeded.
bool display_is_running(void);

typedef struct {
    int16_t peak_dbfs10;    // tenths of dBFS
    int16_t rms_dbfs10;
    int16_t noise_dbfs10;
    uint16_t clip_permille;
    uint16_t adc_offset;    // mean raw 12-bit ADC value (ideal ~2048)
    uint32_t overruns;      // ADC frames lost since boot
} display_mic_stats_t;

// Voice: listening face + level bar while active. Non-blocking.
void display_notify_listening(bool active);

// Voice level for the bar, 0-100 (a plain store, no queue event).
void display_set_level(uint8_t percent);

// Microphone calibration page for duration_ms; numbers come from
// display_set_mic_stats (copied under a lock). False when not running.
bool display_show_mic(uint32_t duration_ms);
void display_set_mic_stats(const display_mic_stats_t *stats);

// Short message page (title + text, both copied) for duration_ms.
bool display_show_notice(const char *title, const char *text, uint32_t duration_ms);

#endif // DISPLAY_API_H
