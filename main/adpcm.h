#ifndef ADPCM_H
#define ADPCM_H

#include <stddef.h>
#include <stdint.h>

// IMA-ADPCM, 4 bits per sample. Stream for the STT service: a 4-byte header
// (predictor int16 LE, step index, 0) followed by the codes, first sample in
// the low nibble.
#define ADPCM_HEADER_BYTES 4

typedef struct {
    int16_t predictor;
    uint8_t index;  // 0..88
} adpcm_state_t;

void adpcm_init(adpcm_state_t *s);
void adpcm_header(const adpcm_state_t *s, uint8_t out[ADPCM_HEADER_BYTES]);
uint8_t adpcm_encode_sample(adpcm_state_t *s, int16_t sample);
// Encodes n samples (n even) into n/2 bytes; returns the bytes written.
size_t adpcm_encode(adpcm_state_t *s, const int16_t *pcm, size_t n, uint8_t *out);

#endif // ADPCM_H
