#include "frame_format.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

frame_err_t frame_header_parse(const uint8_t *buf, size_t buf_len, size_t file_size,
                               uint16_t screen_w, uint16_t screen_h, frame_header_t *out)
{
    frame_header_t hdr;

    if (!buf || !out || buf_len < FRAME_HEADER_SIZE || file_size < FRAME_HEADER_SIZE) {
        return FRAME_ERR_SHORT;
    }
    if (memcmp(buf, "R565", 4) != 0) {
        return FRAME_ERR_MAGIC;
    }

    hdr.x = read_le16(buf + 4);
    hdr.y = read_le16(buf + 6);
    hdr.w = read_le16(buf + 8);
    hdr.h = read_le16(buf + 10);
    hdr.flags = read_le16(buf + 12);

    if (hdr.w == 0 || hdr.h == 0 ||
        (uint32_t)hdr.x + hdr.w > screen_w ||
        (uint32_t)hdr.y + hdr.h > screen_h) {
        return FRAME_ERR_BOUNDS;
    }
    if (file_size != FRAME_HEADER_SIZE + (size_t)hdr.w * hdr.h * 2u) {
        return FRAME_ERR_SIZE;
    }

    *out = hdr;
    return FRAME_OK;
}

size_t frame_pixel_offset(const frame_header_t *hdr, uint16_t col, uint16_t row)
{
    return FRAME_HEADER_SIZE + ((size_t)row * hdr->w + col) * 2u;
}

bool frame_is_full_screen(const frame_header_t *hdr, uint16_t screen_w, uint16_t screen_h)
{
    return hdr->x == 0 && hdr->y == 0 && hdr->w == screen_w && hdr->h == screen_h;
}
