#ifndef CLAP_DETECT_H
#define CLAP_DETECT_H

#include <stdbool.h>
#include <stdint.h>

// Double-clap detector on 10 ms frames (peak and RMS of int16 audio).
// A clap is a burst that starts with peak >= level and >= ratio x noise floor
// and decays below half its peak within CLAP_MAX_BURST_FRAMES. Two claps whose
// onsets are CLAP_MIN_GAP..CLAP_MAX_GAP frames apart, followed by
// CLAP_QUIET_FRAMES without a third burst, give CLAP_DOUBLE.
#define CLAP_MAX_BURST_FRAMES 8
#define CLAP_MIN_GAP_FRAMES   15
#define CLAP_MAX_GAP_FRAMES   70
#define CLAP_QUIET_FRAMES     30

typedef enum { CLAP_NONE = 0, CLAP_DOUBLE } clap_event_t;

typedef struct {
    uint16_t level;  // absolute peak threshold (int16 units)
    uint16_t ratio;  // peak must also be >= ratio x noise floor
} clap_config_t;

typedef struct {
    clap_config_t cfg;
    int32_t noise_q8;      // noise floor (RMS) in Q8; < 0 until the first quiet frame
    uint32_t frame;        // frames seen
    bool in_burst;
    bool burst_long;       // the current burst lasted too long: not a clap
    uint32_t burst_start;
    uint16_t burst_peak;
    uint8_t claps;         // claps in the current sequence (0..2)
    uint32_t first_onset;
    uint32_t second_end;
} clap_detect_t;

void clap_detect_init(clap_detect_t *d, const clap_config_t *cfg);
clap_event_t clap_detect_frame(clap_detect_t *d, uint16_t peak, uint16_t rms);
// Current noise floor (RMS), 0 before any quiet frame.
uint16_t clap_detect_noise(const clap_detect_t *d);

#endif // CLAP_DETECT_H
