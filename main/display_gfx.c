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

static esp_err_t read_header(FILE *f, size_t file_size, frame_header_t *hdr)
{
    uint8_t raw[FRAME_HEADER_SIZE];

    if (fseek(f, 0, SEEK_SET) != 0 || fread(raw, 1, sizeof(raw), f) != sizeof(raw)) {
        return ESP_FAIL;
    }
    if (frame_header_parse(raw, sizeof(raw), file_size, DISPLAY_W, DISPLAY_H, hdr) != FRAME_OK) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t gfx_draw_frame_file(FILE *f, size_t file_size, frame_header_t *out_hdr)
{
    frame_header_t hdr;
    esp_err_t err = read_header(f, file_size, &hdr);
    if (err != ESP_OK) {
        return err;
    }

    int rows_per_band = DISPLAY_STRIP_PIXELS / hdr.w;
    for (int row = 0; row < hdr.h; row += rows_per_band) {
        int rows = (hdr.h - row < rows_per_band) ? hdr.h - row : rows_per_band;
        size_t bytes = (size_t)rows * hdr.w * sizeof(uint16_t);
        uint16_t *buf = display_hw_acquire_buffer();
        if (!buf) {
            return ESP_FAIL;
        }
        if (fread(buf, 1, bytes, f) != bytes) {
            display_hw_release_buffer(buf);
            return ESP_FAIL;
        }
        err = display_hw_blit(hdr.x, hdr.y + row, hdr.w, rows, buf);
        if (err != ESP_OK) {
            return err;
        }
    }
    if (out_hdr) {
        *out_hdr = hdr;
    }
    return ESP_OK;
}

esp_err_t gfx_draw_frame_region(FILE *full, size_t file_size, int x, int y, int w, int h)
{
    frame_header_t hdr;
    esp_err_t err = read_header(full, file_size, &hdr);
    if (err != ESP_OK) {
        return err;
    }
    if (!frame_is_full_screen(&hdr, DISPLAY_W, DISPLAY_H) || x < 0 || y < 0 || w <= 0 || h <= 0 ||
        x + w > DISPLAY_W || y + h > DISPLAY_H) {
        return ESP_ERR_INVALID_ARG;
    }

    int rows_per_band = DISPLAY_STRIP_PIXELS / w;
    for (int row = 0; row < h; row += rows_per_band) {
        int rows = (h - row < rows_per_band) ? h - row : rows_per_band;
        uint16_t *buf = display_hw_acquire_buffer();
        if (!buf) {
            return ESP_FAIL;
        }
        for (int r = 0; r < rows; r++) {
            size_t offset = frame_pixel_offset(&hdr, (uint16_t)x, (uint16_t)(y + row + r));
            size_t bytes = (size_t)w * sizeof(uint16_t);
            if (fseek(full, (long)offset, SEEK_SET) != 0 ||
                fread(buf + (size_t)r * w, 1, bytes, full) != bytes) {
                display_hw_release_buffer(buf);
                return ESP_FAIL;
            }
        }
        err = display_hw_blit(x, y + row, w, rows, buf);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
