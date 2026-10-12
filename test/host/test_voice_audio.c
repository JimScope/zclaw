/*
 * Host tests for voice audio: IMA-ADPCM, ADC conversion, levels.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "adpcm.h"
#include "audio_dsp.h"
#include "audio_level.h"

#define TEST_PI 3.14159265358979

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

/* ---------- adpcm ---------- */
static const int16_t GOLDEN_PCM[64] = {
    -1000, 3508, 7653, 8908, 11413, 12800, 10813, 9908, 8053, 3308, 400, -4708, -7253, -9108,
    -12213, -12000, -10613, -10308, -6853, -2708, -400, 4108, 6053, 9508, 12013, 11200, 11413,
    10508, 6453, 3908, 1000, -4108, -6653, -10708, -11613, -11400, -12213, -9708, -6253, -4308,
    200, 4708, 6653, 10108, 10413, 11800, 12013, 8908, 7053, 4508, -600, -3508, -6053, -10108,
    -11013, -13000, -11613, -9108, -7853, -3708, 800, 3108, 7253, 10708,
};
static const uint8_t GOLDEN_ADPCM[32] = {
    0x7f, 0x77, 0x77, 0x77, 0xe3, 0xba, 0xaa, 0x8a, 0x82, 0x36, 0x42, 0x31, 0x93, 0x90, 0xae, 0xda,
    0xaa, 0x09, 0x28, 0x24, 0x35, 0x31, 0x10, 0xc0, 0xaa, 0xaf, 0xb9, 0xa8, 0x31, 0x61, 0x23, 0x33,
};

TEST(adpcm_matches_golden_vector)
{
    adpcm_state_t s;
    uint8_t out[32];
    uint8_t hdr[ADPCM_HEADER_BYTES];
    adpcm_init(&s);
    adpcm_header(&s, hdr);
    ASSERT(hdr[0] == 0 && hdr[1] == 0 && hdr[2] == 0 && hdr[3] == 0);
    ASSERT(adpcm_encode(&s, GOLDEN_PCM, 64, out) == 32);
    ASSERT(memcmp(out, GOLDEN_ADPCM, sizeof(out)) == 0);
    return 0;
}

TEST(adpcm_header_layout)
{
    adpcm_state_t s = { .predictor = -2, .index = 5 };
    uint8_t hdr[ADPCM_HEADER_BYTES];
    adpcm_header(&s, hdr);
    ASSERT(hdr[0] == 0xFE && hdr[1] == 0xFF && hdr[2] == 5 && hdr[3] == 0);
    return 0;
}

/* Test-only decoder (the board never decodes). */
static const int16_t STEPS[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
};
static const int8_t IDX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static void decode(const uint8_t *in, size_t bytes, int16_t *out)
{
    int32_t pred = 0;
    int idx = 0;
    for (size_t i = 0; i < bytes * 2; i++) {
        uint8_t code = (i & 1) ? (in[i / 2] >> 4) : (in[i / 2] & 0x0F);
        int32_t step = STEPS[idx];
        int32_t vp = step >> 3;
        if (code & 4) vp += step;
        if (code & 2) vp += step >> 1;
        if (code & 1) vp += step >> 2;
        pred = (code & 8) ? pred - vp : pred + vp;
        pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
        idx += IDX[code];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        out[i] = (int16_t)pred;
    }
}

TEST(adpcm_round_trip_keeps_speech_like_signal)
{
    static int16_t pcm[16000];
    static int16_t back[16000];
    static uint8_t enc[8000];
    adpcm_state_t s;
    double num = 0, den = 0;
    for (int i = 0; i < 16000; i++) {
        double t = (double)i / 16000.0;
        pcm[i] = (int16_t)(4800 * sin(2 * TEST_PI * 220 * t) + 2000 * sin(2 * TEST_PI * 730 * t) +
                           2000 * sin(2 * TEST_PI * 2100 * t));
    }
    adpcm_init(&s);
    ASSERT(adpcm_encode(&s, pcm, 16000, enc) == 8000);
    decode(enc, 8000, back);
    for (int i = 0; i < 16000; i++) {
        num += (double)pcm[i] * pcm[i];
        den += (double)(pcm[i] - back[i]) * (pcm[i] - back[i]);
    }
    ASSERT(10 * log10(num / den) > 20.0);
    return 0;
}

/* ---------- audio_dsp ---------- */
TEST(dsp_constant_input_is_silence)
{
    static uint16_t raw[400];
    static int16_t out[200];
    audio_dsp_t d;
    for (int i = 0; i < 400; i++) raw[i] = 2548;
    audio_dsp_init(&d);
    ASSERT(audio_dsp_convert(&d, raw, 400, out) == 200);
    for (int i = 0; i < 200; i++) ASSERT(out[i] == 0);
    return 0;
}

TEST(dsp_removes_a_dc_step)
{
    static uint16_t raw[4002];
    static int16_t out[2001];
    audio_dsp_t d;
    raw[0] = raw[1] = 2048;
    for (int i = 2; i < 4002; i++) raw[i] = 2548;
    audio_dsp_init(&d);
    audio_dsp_convert(&d, raw, 4002, out);
    ASSERT(out[0] == 0);
    ASSERT(out[1] == 8000);          /* the step passes once ... */
    ASSERT(out[2000] < 50 && out[2000] > -50);  /* ... and decays */
    return 0;
}

static int16_t peak_after(const int16_t *x, size_t from, size_t n)
{
    int16_t p = 0;
    for (size_t i = from; i < n; i++) {
        int16_t a = (int16_t)(x[i] < 0 ? -x[i] : x[i]);
        if (a > p) p = a;
    }
    return p;
}

TEST(dsp_keeps_speech_band_and_cuts_high_tones)
{
    static uint16_t raw[6400];
    static int16_t out[3200];
    audio_dsp_t d;
    for (int i = 0; i < 6400; i++) raw[i] = (uint16_t)(2048 + 1000 * sin(2 * TEST_PI * 1000 * i / 32000.0));
    audio_dsp_init(&d);
    audio_dsp_convert(&d, raw, 6400, out);
    int16_t p = peak_after(out, 1600, 3200);
    ASSERT(p > 15000 && p < 16600);  /* 1 kHz: ~16000 */
    for (int i = 0; i < 6400; i++) raw[i] = (uint16_t)(2048 + 1000 * sin(2 * TEST_PI * 15000 * i / 32000.0));
    audio_dsp_init(&d);
    audio_dsp_convert(&d, raw, 6400, out);
    ASSERT(peak_after(out, 1600, 3200) < 3000);  /* 15 kHz: mostly gone */
    return 0;
}

/* ---------- audio_level ---------- */
TEST(level_of_silence_and_full_scale)
{
    static int16_t x[160];
    audio_frame_stats_t s;
    memset(x, 0, sizeof(x));
    audio_level_frame(x, 160, &s);
    ASSERT(s.peak == 0 && s.rms == 0 && s.clipped == 0);
    ASSERT(audio_level_dbfs_tenths(s.rms) == AUDIO_DBFS_FLOOR);
    for (int i = 0; i < 160; i++) x[i] = (int16_t)((i & 1) ? 32767 : -32767);
    audio_level_frame(x, 160, &s);
    ASSERT(s.peak == 32767 && s.rms == 32767 && s.clipped == 160);
    ASSERT(audio_level_dbfs_tenths(s.rms) == 0);
    return 0;
}

TEST(level_of_a_sine)
{
    static int16_t x[160];
    audio_frame_stats_t s;
    for (int i = 0; i < 160; i++) x[i] = (int16_t)(16384 * sin(2 * TEST_PI * i / 16.0));
    audio_level_frame(x, 160, &s);
    ASSERT(s.peak >= 16383 && s.peak <= 16384);
    ASSERT(s.rms >= 11580 && s.rms <= 11590);       /* 16384 / sqrt(2) */
    ASSERT(audio_level_dbfs_tenths(s.rms) == -90);  /* -9.0 dBFS */
    ASSERT(s.clipped == 0);
    return 0;
}

TEST(level_bar_width)
{
    ASSERT(audio_level_bar(AUDIO_DBFS_FLOOR, 200) == 0);
    ASSERT(audio_level_bar(-600, 200) == 0);
    ASSERT(audio_level_bar(-300, 200) == 100);
    ASSERT(audio_level_bar(0, 200) == 200);
    ASSERT(audio_level_bar(15, 200) == 200);
    return 0;
}

int test_voice_audio_all(void)
{
    int failures = 0;

    printf("\nVoice Audio Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(adpcm_matches_golden_vector);
    RUN(adpcm_header_layout);
    RUN(adpcm_round_trip_keeps_speech_like_signal);
    RUN(dsp_constant_input_is_silence);
    RUN(dsp_removes_a_dc_step);
    RUN(dsp_keeps_speech_band_and_cuts_high_tones);
    RUN(level_of_silence_and_full_scale);
    RUN(level_of_a_sine);
    RUN(level_bar_width);

#undef RUN
    return failures;
}
