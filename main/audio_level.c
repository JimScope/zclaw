#include "audio_level.h"

#include <math.h>

static uint32_t isqrt64(uint64_t v)
{
    uint64_t r = 0;
    uint64_t bit = (uint64_t)1 << 62;

    while (bit > v) {
        bit >>= 2;
    }
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)r;
}

void audio_level_frame(const int16_t *x, size_t n, audio_frame_stats_t *out)
{
    uint64_t sum = 0;
    uint32_t peak = 0;
    uint16_t clipped = 0;

    for (size_t i = 0; i < n; i++) {
        int32_t v = x[i];
        uint32_t a = (uint32_t)(v < 0 ? -v : v);
        if (a > peak) {
            peak = a;
        }
        if (a >= AUDIO_CLIP_LEVEL) {
            clipped++;
        }
        sum += (uint64_t)((int64_t)v * v);
    }
    out->peak = (uint16_t)(peak > 65535 ? 65535 : peak);
    out->rms = (uint16_t)(n ? isqrt64(sum / n) : 0);
    out->clipped = clipped;
}

int audio_level_dbfs_tenths(uint32_t value)
{
    if (value == 0) {
        return AUDIO_DBFS_FLOOR;
    }
    double db10 = 200.0 * log10((double)value / 32768.0);
    int t = (int)(db10 < 0 ? db10 - 0.5 : db10 + 0.5);
    return t < AUDIO_DBFS_FLOOR ? AUDIO_DBFS_FLOOR : t;
}

int audio_level_bar(int dbfs_tenths, int width)
{
    if (dbfs_tenths <= -600) {
        return 0;
    }
    if (dbfs_tenths >= 0) {
        return width;
    }
    return (int)((long)(dbfs_tenths + 600) * width / 600);
}
