#ifndef AUDIO_LEVEL_H
#define AUDIO_LEVEL_H

#include <stddef.h>
#include <stdint.h>

#define AUDIO_CLIP_LEVEL  32000  // |sample| at or above this counts as clipped
#define AUDIO_DBFS_FLOOR  (-900) // tenths of dB reported for silence

typedef struct {
    uint16_t peak;     // max |sample|
    uint16_t rms;
    uint16_t clipped;  // samples with |x| >= AUDIO_CLIP_LEVEL
} audio_frame_stats_t;

void audio_level_frame(const int16_t *x, size_t n, audio_frame_stats_t *out);

// 20*log10(value/32768) in tenths of dB, floored at AUDIO_DBFS_FLOOR
// (integers only: newlib nano printf cannot print floats).
int audio_level_dbfs_tenths(uint32_t value);

// Bar width for a level: -60 dBFS (or less) -> 0, 0 dBFS -> width.
int audio_level_bar(int dbfs_tenths, int width);

#endif // AUDIO_LEVEL_H
