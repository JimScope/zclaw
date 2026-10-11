#ifndef DISPLAY_REMOTE_H
#define DISPLAY_REMOTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define DISPLAY_REMOTE_MAX_BYTES (48 * 1024)

// Called from the fetch task when the download ends. jpeg is NULL on failure;
// otherwise the receiver owns it and must free() it.
typedef void (*display_remote_done_fn)(uint32_t request_id, uint8_t *jpeg, size_t len);

// Download url (a JPEG dashboard) in a short-lived task. False if a download
// is already running or the task cannot start.
bool display_remote_fetch(const char *url, uint32_t request_id, display_remote_done_fn done);

// Decode a 240x240 baseline JPEG onto the panel (display task only).
esp_err_t display_remote_draw(const uint8_t *jpeg, size_t len);

#endif // DISPLAY_REMOTE_H
