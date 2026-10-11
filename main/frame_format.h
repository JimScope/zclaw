#ifndef FRAME_FORMAT_H
#define FRAME_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// .r565 frame file: 16-byte header (little-endian fields) + w*h RGB565
// big-endian pixels, row-major. See docs/superpowers/specs/2026-10-11-display-sd-design.md.
#define FRAME_HEADER_SIZE 16
#define FRAME_FLAG_PATCH  0x0001u

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint16_t flags;
} frame_header_t;

typedef enum {
    FRAME_OK = 0,
    FRAME_ERR_SHORT,
    FRAME_ERR_MAGIC,
    FRAME_ERR_BOUNDS,
    FRAME_ERR_SIZE,
} frame_err_t;

frame_err_t frame_header_parse(const uint8_t *buf, size_t buf_len, size_t file_size,
                               uint16_t screen_w, uint16_t screen_h, frame_header_t *out);

// File offset of pixel (col, row) relative to the frame rectangle.
size_t frame_pixel_offset(const frame_header_t *hdr, uint16_t col, uint16_t row);

bool frame_is_full_screen(const frame_header_t *hdr, uint16_t screen_w, uint16_t screen_h);

#endif // FRAME_FORMAT_H
