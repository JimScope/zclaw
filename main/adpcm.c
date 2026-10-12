#include "adpcm.h"

static const int16_t STEP_TABLE[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
};
static const int8_t INDEX_TABLE[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

void adpcm_init(adpcm_state_t *s)
{
    s->predictor = 0;
    s->index = 0;
}

void adpcm_header(const adpcm_state_t *s, uint8_t out[ADPCM_HEADER_BYTES])
{
    uint16_t p = (uint16_t)s->predictor;
    out[0] = (uint8_t)(p & 0xFF);
    out[1] = (uint8_t)(p >> 8);
    out[2] = s->index;
    out[3] = 0;
}

uint8_t adpcm_encode_sample(adpcm_state_t *s, int16_t sample)
{
    int32_t diff = (int32_t)sample - s->predictor;
    int32_t step = STEP_TABLE[s->index];
    int32_t vpdiff = step >> 3;
    uint8_t code = 0;

    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    if (diff >= step) {
        code |= 4;
        diff -= step;
        vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
        code |= 2;
        diff -= step;
        vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
        code |= 1;
        vpdiff += step;
    }
    int32_t pred = (code & 8) ? s->predictor - vpdiff : s->predictor + vpdiff;
    if (pred > 32767) {
        pred = 32767;
    } else if (pred < -32768) {
        pred = -32768;
    }
    s->predictor = (int16_t)pred;
    int idx = (int)s->index + INDEX_TABLE[code];
    s->index = (uint8_t)(idx < 0 ? 0 : idx > 88 ? 88 : idx);
    return code;
}

size_t adpcm_encode(adpcm_state_t *s, const int16_t *pcm, size_t n, uint8_t *out)
{
    size_t w = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint8_t lo = adpcm_encode_sample(s, pcm[i]);
        uint8_t hi = adpcm_encode_sample(s, pcm[i + 1]);
        out[w++] = (uint8_t)(lo | (hi << 4));
    }
    return w;
}
