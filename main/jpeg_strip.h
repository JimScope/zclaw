#ifndef JPEG_STRIP_H
#define JPEG_STRIP_H

#include <stdbool.h>
#include <stdint.h>

// Assembles decoded JPEG blocks (RGB888, left-to-right, top-to-bottom) into
// full-width strips in panel byte order (RGB565 big-endian), one strip per
// row of blocks.
typedef uint16_t *(*jpeg_strip_get_fn)(void *ctx);
typedef void (*jpeg_strip_flush_fn)(void *ctx, int y, int rows, uint16_t *pixels);

typedef struct {
    int width;
    int max_rows;
    uint16_t *buf;
    int row_top;
    jpeg_strip_get_fn get;
    jpeg_strip_flush_fn flush;
    void *ctx;
} jpeg_strip_t;

uint16_t jpeg_rgb888_to_panel(uint8_t r, uint8_t g, uint8_t b);

void jpeg_strip_init(jpeg_strip_t *s, int width, int max_rows, jpeg_strip_get_fn get,
                     jpeg_strip_flush_fn flush, void *ctx);

// Add one block (inclusive rectangle). Returns false when the block does not
// fit a strip (taller than max_rows, or outside the image width).
bool jpeg_strip_block(jpeg_strip_t *s, int left, int top, int right, int bottom,
                      const uint8_t *rgb888);

#endif // JPEG_STRIP_H
