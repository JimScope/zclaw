#ifndef DISPLAY_API_H
#define DISPLAY_API_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

// Start the display (and SD card) subsystem. Never fatal: returns an error
// and leaves the rest of the firmware untouched if the hardware is missing.
// ESP_ERR_NOT_SUPPORTED when CONFIG_ZCLAW_DISPLAY_ENABLE is off.
esp_err_t display_start(void);

// Non-blocking event hooks; no-ops when the display is disabled or not started.
void display_notify_thinking(bool active);
void display_notify_talking(size_t reply_chars);
void display_notify_wifi(bool connected);

#endif // DISPLAY_API_H
