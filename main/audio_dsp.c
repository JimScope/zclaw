#include "audio_dsp.h"

void audio_dsp_init(audio_dsp_t *d)
{
    d->prev_in = 0;
    d->prev_out = 0;
    d->primed = false;
}

size_t audio_dsp_convert(audio_dsp_t *d, const uint16_t *raw, size_t n_raw, int16_t *out)
{
    size_t w = 0;

    for (size_t i = 0; i + 1 < n_raw; i += 2) {
        int32_t avg = ((int32_t)raw[i] + (int32_t)raw[i + 1]) / 2;
        int32_t x = (avg - AUDIO_RAW_MID) * 16;
        if (!d->primed) {
            d->prev_in = x;  // the first sample sets the DC level: no start-up thump
            d->primed = true;
        }
        // y[n] = x[n] - x[n-1] + 0.996 * y[n-1]  (corner ~10 Hz at 16 kHz)
        int32_t y = x - d->prev_in + (d->prev_out * 255) / 256;
        d->prev_in = x;
        if (y > 32767) {
            y = 32767;
        } else if (y < -32768) {
            y = -32768;
        }
        d->prev_out = y;
        out[w++] = (int16_t)y;
    }
    return w;
}
