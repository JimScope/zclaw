#ifndef DISPLAY_HW_H
#define DISPLAY_HW_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define DISPLAY_W 240
#define DISPLAY_H 240
#define DISPLAY_STRIP_PIXELS (DISPLAY_W * 16)

esp_err_t display_hw_init(void);
bool display_hw_ready(void);

// One of two DMA buffers (DISPLAY_STRIP_PIXELS pixels). Blocks until the
// buffer's previous transfer has finished. Hand it to display_hw_blit(), or
// back with display_hw_release_buffer() if it ends up unused.
uint16_t *display_hw_acquire_buffer(void);
void display_hw_release_buffer(uint16_t *buf);

// Queue w*h big-endian RGB565 pixels at (x, y). The buffer is released
// automatically when the transfer completes.
esp_err_t display_hw_blit(int x, int y, int w, int h, uint16_t *buf);

// Wait until no transfer is in flight.
void display_hw_wait_idle(void);

void display_hw_set_brightness(uint8_t percent);

#endif // DISPLAY_HW_H
