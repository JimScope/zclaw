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

#endif // DISPLAY_API_H
