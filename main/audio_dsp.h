#ifndef AUDIO_DSP_H
#define AUDIO_DSP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_RAW_MID 2048  // 12-bit ADC mid-scale (MAX4466 output idles at VCC/2)

// 32 kHz 12-bit ADC values -> 16 kHz int16: averages each pair (a crude
// low-pass), centres, scales x16 and removes DC with a one-pole high-pass.
typedef struct {
    int32_t prev_in;
    int32_t prev_out;
    bool primed;
} audio_dsp_t;

void audio_dsp_init(audio_dsp_t *d);
// n_raw even; writes n_raw/2 samples to out and returns that count.
size_t audio_dsp_convert(audio_dsp_t *d, const uint16_t *raw, size_t n_raw, int16_t *out);

#endif // AUDIO_DSP_H
