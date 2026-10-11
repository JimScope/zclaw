/*
 * Host tests for the .r565 frame header format.
 */

#include <stdio.h>
#include <string.h>

#include "frame_format.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void make_header(uint8_t *buf, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t flags)
{
    memcpy(buf, "R565", 4);
    put16(buf + 4, x);
    put16(buf + 6, y);
    put16(buf + 8, w);
    put16(buf + 10, h);
    put16(buf + 12, flags);
    put16(buf + 14, 0);
}

static size_t file_size_for(uint16_t w, uint16_t h)
{
    return FRAME_HEADER_SIZE + (size_t)w * h * 2u;
}

TEST(full_frame_parses)
{
    uint8_t buf[FRAME_HEADER_SIZE];
    frame_header_t hdr;
    make_header(buf, 0, 0, 240, 240, 0);
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(240, 240), 240, 240, &hdr) == FRAME_OK);
    ASSERT(hdr.w == 240 && hdr.h == 240 && hdr.flags == 0);
    ASSERT(frame_is_full_screen(&hdr, 240, 240));
    return 0;
}

TEST(patch_parses)
{
    uint8_t buf[FRAME_HEADER_SIZE];
    frame_header_t hdr;
    make_header(buf, 82, 74, 110, 40, FRAME_FLAG_PATCH);
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(110, 40), 240, 240, &hdr) == FRAME_OK);
    ASSERT(hdr.x == 82 && hdr.y == 74 && hdr.w == 110 && hdr.h == 40);
    ASSERT((hdr.flags & FRAME_FLAG_PATCH) != 0);
    ASSERT(!frame_is_full_screen(&hdr, 240, 240));
    return 0;
}

TEST(short_buffer_is_rejected)
{
    uint8_t buf[FRAME_HEADER_SIZE];
    frame_header_t hdr;
    make_header(buf, 0, 0, 240, 240, 0);
    ASSERT(frame_header_parse(buf, 10, file_size_for(240, 240), 240, 240, &hdr) == FRAME_ERR_SHORT);
    ASSERT(frame_header_parse(NULL, 16, file_size_for(240, 240), 240, 240, &hdr) == FRAME_ERR_SHORT);
    return 0;
}

TEST(bad_magic_is_rejected)
{
    uint8_t buf[FRAME_HEADER_SIZE];
    frame_header_t hdr;
    make_header(buf, 0, 0, 240, 240, 0);
    buf[0] = 'X';
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(240, 240), 240, 240, &hdr) == FRAME_ERR_MAGIC);
    return 0;
}

TEST(out_of_bounds_rectangle_is_rejected)
{
    uint8_t buf[FRAME_HEADER_SIZE];
    frame_header_t hdr;
    make_header(buf, 200, 0, 50, 10, FRAME_FLAG_PATCH);
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(50, 10), 240, 240, &hdr) == FRAME_ERR_BOUNDS);
    make_header(buf, 0, 0, 0, 10, 0);
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(0, 10), 240, 240, &hdr) == FRAME_ERR_BOUNDS);
    make_header(buf, 0, 0xFFFF, 10, 10, 0);
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(10, 10), 240, 240, &hdr) == FRAME_ERR_BOUNDS);
    return 0;
}

TEST(truncated_or_padded_file_is_rejected)
{
    uint8_t buf[FRAME_HEADER_SIZE];
    frame_header_t hdr;
    make_header(buf, 0, 0, 240, 240, 0);
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(240, 240) - 2, 240, 240, &hdr) == FRAME_ERR_SIZE);
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(240, 240) + 1, 240, 240, &hdr) == FRAME_ERR_SIZE);
    return 0;
}

TEST(pixel_offset_is_row_major)
{
    frame_header_t hdr = { .x = 10, .y = 20, .w = 100, .h = 50, .flags = 0 };
    ASSERT(frame_pixel_offset(&hdr, 0, 0) == FRAME_HEADER_SIZE);
    ASSERT(frame_pixel_offset(&hdr, 3, 0) == FRAME_HEADER_SIZE + 6);
    ASSERT(frame_pixel_offset(&hdr, 0, 2) == FRAME_HEADER_SIZE + 400);
    return 0;
}

int test_frame_format_all(void)
{
    int failures = 0;

    printf("\nFrame Format Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(full_frame_parses);
    RUN(patch_parses);
    RUN(short_buffer_is_rejected);
    RUN(bad_magic_is_rejected);
    RUN(out_of_bounds_rectangle_is_rejected);
    RUN(truncated_or_padded_file_is_rejected);
    RUN(pixel_offset_is_row_major);

#undef RUN
    return failures;
}
