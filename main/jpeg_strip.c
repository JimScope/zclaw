#include "jpeg_strip.h"

#include <stddef.h>

uint16_t jpeg_rgb888_to_panel(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

void jpeg_strip_init(jpeg_strip_t *s, int width, int max_rows, jpeg_strip_get_fn get,
                     jpeg_strip_flush_fn flush, void *ctx)
{
    s->width = width;
    s->max_rows = max_rows;
    s->buf = NULL;
    s->row_top = 0;
    s->get = get;
    s->flush = flush;
    s->ctx = ctx;
}

bool jpeg_strip_block(jpeg_strip_t *s, int left, int top, int right, int bottom,
                      const uint8_t *rgb888)
{
    int w = right - left + 1;
    int h = bottom - top + 1;

    if (w <= 0 || h <= 0 || h > s->max_rows || left < 0 || right >= s->width) {
        return false;
    }
    if (!s->buf) {
        s->buf = s->get(s->ctx);
        if (!s->buf) {
            return false;
        }
        s->row_top = top;
    }

    for (int y = 0; y < h; y++) {
        uint16_t *out = s->buf + (size_t)(top - s->row_top + y) * s->width + left;
        const uint8_t *in = rgb888 + (size_t)y * w * 3;
        for (int x = 0; x < w; x++, in += 3) {
            out[x] = jpeg_rgb888_to_panel(in[0], in[1], in[2]);
        }
    }

    if (right == s->width - 1) {
        s->flush(s->ctx, s->row_top, h, s->buf);
        s->buf = NULL;
    }
    return true;
}
