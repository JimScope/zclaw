/*
 * Host tests for assembling decoded JPEG blocks into display strips.
 */

#include <stdio.h>
#include <string.h>

#include "jpeg_strip.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

static uint16_t s_strip[16 * 8];
static int s_flushes;
static int s_last_y;
static int s_last_rows;
static uint16_t s_snapshot[16 * 8];

static uint16_t *fake_get(void *ctx)
{
    (void)ctx;
    memset(s_strip, 0, sizeof(s_strip));
    return s_strip;
}

static void fake_flush(void *ctx, int y, int rows, uint16_t *pixels)
{
    (void)ctx;
    s_flushes++;
    s_last_y = y;
    s_last_rows = rows;
    memcpy(s_snapshot, pixels, sizeof(s_snapshot));
}

static void fill_block(uint8_t *rgb, int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < w * h; i++) {
        rgb[i * 3] = r;
        rgb[i * 3 + 1] = g;
        rgb[i * 3 + 2] = b;
    }
}

TEST(converts_to_panel_byte_order)
{
    ASSERT(jpeg_rgb888_to_panel(255, 0, 0) == 0x00F8);
    ASSERT(jpeg_rgb888_to_panel(0, 0, 255) == 0x1F00);
    ASSERT(jpeg_rgb888_to_panel(255, 255, 255) == 0xFFFF);
    ASSERT(jpeg_rgb888_to_panel(0, 0, 0) == 0x0000);
    return 0;
}

TEST(flushes_one_strip_per_block_row)
{
    jpeg_strip_t st;
    uint8_t red[8 * 8 * 3];
    uint8_t blue[8 * 8 * 3];

    s_flushes = 0;
    fill_block(red, 8, 8, 255, 0, 0);
    fill_block(blue, 8, 8, 0, 0, 255);
    jpeg_strip_init(&st, 16, 8, fake_get, fake_flush, NULL);

    ASSERT(jpeg_strip_block(&st, 0, 0, 7, 7, red));
    ASSERT(s_flushes == 0);
    ASSERT(jpeg_strip_block(&st, 8, 0, 15, 7, blue));
    ASSERT(s_flushes == 1);
    ASSERT(s_last_y == 0 && s_last_rows == 8);
    ASSERT(s_snapshot[3 * 16 + 2] == jpeg_rgb888_to_panel(255, 0, 0));
    ASSERT(s_snapshot[3 * 16 + 9] == jpeg_rgb888_to_panel(0, 0, 255));

    ASSERT(jpeg_strip_block(&st, 0, 8, 7, 15, blue));
    ASSERT(jpeg_strip_block(&st, 8, 8, 15, 15, red));
    ASSERT(s_flushes == 2 && s_last_y == 8);
    return 0;
}

TEST(rejects_blocks_taller_than_strip)
{
    jpeg_strip_t st;
    uint8_t big[8 * 16 * 3];

    fill_block(big, 8, 16, 1, 2, 3);
    jpeg_strip_init(&st, 16, 8, fake_get, fake_flush, NULL);
    ASSERT(!jpeg_strip_block(&st, 0, 0, 7, 15, big));
    return 0;
}

int test_jpeg_strip_all(void)
{
    int failures = 0;

    printf("\nJPEG Strip Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(converts_to_panel_byte_order);
    RUN(flushes_one_strip_per_block_row);
    RUN(rejects_blocks_taller_than_strip);

#undef RUN
    return failures;
}
