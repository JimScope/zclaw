#include "display_gfx.h"

#include "display_hw.h"
#include "font8x16.h"
#include "text_latin1.h"

static uint16_t to_panel(gfx_color_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}

void gfx_fill_rect(int x, int y, int w, int h, gfx_color_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > DISPLAY_W) { w = DISPLAY_W - x; }
    if (y + h > DISPLAY_H) { h = DISPLAY_H - y; }
    if (w <= 0 || h <= 0) {
        return;
    }

    int rows_per_band = DISPLAY_STRIP_PIXELS / w;
    uint16_t value = to_panel(color);
    for (int row = 0; row < h; row += rows_per_band) {
        int rows = (h - row < rows_per_band) ? h - row : rows_per_band;
        uint16_t *buf = display_hw_acquire_buffer();
        if (!buf) {
            return;
        }
        for (int i = 0; i < rows * w; i++) {
            buf[i] = value;
        }
        display_hw_blit(x, y + row, w, rows, buf);
    }
}

int gfx_text_width(const char *utf8, int scale)
{
    return (int)text_latin1_length(utf8) * FONT_GLYPH_W * scale;
}

int gfx_text(int x, int y, const char *utf8, gfx_color_t fg, gfx_color_t bg, int scale)
{
    uint8_t chars[DISPLAY_W / FONT_GLYPH_W];
    size_t count = 0;
    const char *p = utf8;
    uint8_t c;

    if (!utf8 || scale < 1 || scale > 3 || x < 0 || y < 0 || x >= DISPLAY_W || y >= DISPLAY_H) {
        return 0;
    }
    int max_chars = (DISPLAY_W - x) / (FONT_GLYPH_W * scale);
    while ((int)count < max_chars && (c = text_latin1_next(&p)) != 0) {
        chars[count++] = c;
    }
    if (count == 0) {
        return 0;
    }

    int w = (int)count * FONT_GLYPH_W * scale;
    int h = FONT_GLYPH_H * scale;
    if (y + h > DISPLAY_H) {
        h = DISPLAY_H - y;
    }
    int rows_per_band = DISPLAY_STRIP_PIXELS / w;
    uint16_t fg_px = to_panel(fg);
    uint16_t bg_px = to_panel(bg);

    for (int row = 0; row < h; row += rows_per_band) {
        int rows = (h - row < rows_per_band) ? h - row : rows_per_band;
        uint16_t *buf = display_hw_acquire_buffer();
        if (!buf) {
            return 0;
        }
        for (int r = 0; r < rows; r++) {
            int glyph_row = (row + r) / scale;
            uint16_t *out = buf + r * w;
            for (size_t i = 0; i < count; i++) {
                uint8_t bits = font8x16_latin1[chars[i]][glyph_row];
                for (int b = 0; b < FONT_GLYPH_W; b++) {
                    uint16_t px = (bits & (0x80 >> b)) ? fg_px : bg_px;
                    for (int s = 0; s < scale; s++) {
                        *out++ = px;
                    }
                }
            }
        }
        display_hw_blit(x, y + row, w, rows, buf);
    }
    return w;
}
