#include "clap_detect.h"

void clap_detect_init(clap_detect_t *d, const clap_config_t *cfg)
{
    d->cfg = *cfg;
    d->noise_q8 = -1;
    d->frame = 0;
    d->in_burst = false;
    d->burst_long = false;
    d->burst_start = 0;
    d->burst_peak = 0;
    d->claps = 0;
    d->first_onset = 0;
    d->second_end = 0;
}

uint16_t clap_detect_noise(const clap_detect_t *d)
{
    return d->noise_q8 < 0 ? 0 : (uint16_t)(d->noise_q8 >> 8);
}

static void update_noise(clap_detect_t *d, uint16_t rms)
{
    int32_t target = (int32_t)rms << 8;
    if (d->noise_q8 < 0) {
        d->noise_q8 = target;
    } else if (target > d->noise_q8) {
        d->noise_q8 += (target - d->noise_q8) / 64;  // rises slowly (speech must not lift it)
    } else {
        d->noise_q8 -= (d->noise_q8 - target) / 8;   // falls quickly
    }
}

static void on_clap(clap_detect_t *d, uint32_t onset, uint32_t end)
{
    if (d->claps == 0) {
        d->claps = 1;
        d->first_onset = onset;
    } else if (d->claps == 1) {
        uint32_t gap = onset - d->first_onset;
        if (gap >= CLAP_MIN_GAP_FRAMES && gap <= CLAP_MAX_GAP_FRAMES) {
            d->claps = 2;
            d->second_end = end;
        } else if (gap > CLAP_MAX_GAP_FRAMES) {
            d->first_onset = onset;  // too late: this one starts a new sequence
        } else {
            d->claps = 0;            // too close to be a double clap
        }
    } else {
        d->claps = 0;                // a third clap: applause, not a command
    }
}

clap_event_t clap_detect_frame(clap_detect_t *d, uint16_t peak, uint16_t rms)
{
    uint32_t t = d->frame++;
    uint32_t noise = clap_detect_noise(d);
    bool loud = peak >= d->cfg.level && (uint32_t)peak >= (uint32_t)d->cfg.ratio * noise;

    if (d->in_burst) {
        if (peak > d->burst_peak) {
            d->burst_peak = peak;
        }
        if ((uint32_t)peak * 2 >= d->burst_peak) {
            if (t - d->burst_start + 1 > CLAP_MAX_BURST_FRAMES) {
                d->burst_long = true;
            }
            return CLAP_NONE;
        }
        d->in_burst = false;
        if (d->burst_long) {
            d->claps = 0;  // sustained sound (speech, music): drop the sequence
        } else {
            on_clap(d, d->burst_start, t);
        }
    }
    if (loud) {
        d->in_burst = true;
        d->burst_long = false;
        d->burst_start = t;
        d->burst_peak = peak;
        return CLAP_NONE;
    }
    update_noise(d, rms);
    if (d->claps == 1 && t - d->first_onset > CLAP_MAX_GAP_FRAMES) {
        d->claps = 0;
    }
    if (d->claps == 2 && t - d->second_end >= CLAP_QUIET_FRAMES) {
        d->claps = 0;
        return CLAP_DOUBLE;
    }
    return CLAP_NONE;
}
