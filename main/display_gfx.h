#ifndef DISPLAY_GFX_H
#define DISPLAY_GFX_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "frame_format.h"

typedef uint16_t gfx_color_t;  // RGB565 in host byte order

#define GFX_RGB(r, g, b) ((gfx_color_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define GFX_BLACK GFX_RGB(0, 0, 0)
#define GFX_WHITE GFX_RGB(255, 255, 255)
#define GFX_GRAY  GFX_RGB(150, 150, 150)
#define GFX_AMBER GFX_RGB(255, 191, 0)

void gfx_fill_rect(int x, int y, int w, int h, gfx_color_t color);

// One line of UTF-8 text with the 8x16 font scaled by `scale` (1-3), clipped
// at the right edge. Paints the text box background. Returns the drawn width.
int gfx_text(int x, int y, const char *utf8, gfx_color_t fg, gfx_color_t bg, int scale);
int gfx_text_width(const char *utf8, int scale);

// Draw a whole .r565 file (full frame or patch at its own rectangle).
// ESP_ERR_INVALID_SIZE for a bad header, ESP_FAIL for read errors.
esp_err_t gfx_draw_frame_file(FILE *f, size_t file_size, frame_header_t *out_hdr);

// Redraw rectangle (x, y, w, h) from a full-screen .r565 file.
esp_err_t gfx_draw_frame_region(FILE *full, size_t file_size, int x, int y, int w, int h);

#endif // DISPLAY_GFX_H
