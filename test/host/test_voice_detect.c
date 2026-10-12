/*
 * Host tests for voice detection: double clap and end of speech.
 */

#include <stdio.h>

#include "clap_detect.h"
#include "endpoint.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

/* ---------- clap_detect ---------- */
static clap_detect_t s_clap;
static const clap_config_t CLAP_CFG = { .level = 13107, .ratio = 6 };  /* 40 % of full scale */

/* Feeds n frames; returns the index of the first CLAP_DOUBLE, or -1. */
static int feed(int n, uint16_t peak, uint16_t rms)
{
    int ev = -1;
    for (int i = 0; i < n; i++) {
        if (clap_detect_frame(&s_clap, peak, rms) == CLAP_DOUBLE && ev < 0) ev = i;
    }
    return ev;
}
static int quiet(int n) { return feed(n, 300, 100); }
static int clap(void)
{
    int ev = feed(1, 20000, 8000);
    int ev2 = feed(1, 12000, 4000);
    return ev >= 0 ? ev : ev2;
}

TEST(clap_single_is_ignored)
{
    clap_detect_init(&s_clap, &CLAP_CFG);
    ASSERT(quiet(100) < 0);
    ASSERT(clap() < 0);
    ASSERT(quiet(200) < 0);
    return 0;
}

TEST(clap_double_fires_after_quiet_window)
{
    clap_detect_init(&s_clap, &CLAP_CFG);
    ASSERT(quiet(100) < 0);
    ASSERT(clap() < 0);
    ASSERT(quiet(28) < 0);          /* onsets 30 frames (300 ms) apart */
    ASSERT(clap() < 0);
    ASSERT(quiet(100) == 30);       /* 300 ms of quiet after the second clap */
    ASSERT(quiet(200) < 0);         /* only once */
    return 0;
}

TEST(clap_gap_out_of_range_is_ignored)
{
    clap_detect_init(&s_clap, &CLAP_CFG);
    quiet(100);
    clap();
    quiet(8);                       /* 100 ms: too close */
    clap();
    ASSERT(quiet(200) < 0);
    clap_detect_init(&s_clap, &CLAP_CFG);
    quiet(100);
    clap();
    quiet(88);                      /* 900 ms: too far */
    clap();
    ASSERT(quiet(200) < 0);
    return 0;
}

TEST(clap_three_is_applause)
{
    clap_detect_init(&s_clap, &CLAP_CFG);
    quiet(100);
    clap();
    quiet(28);
    clap();
    quiet(28);
    clap();
    ASSERT(quiet(200) < 0);
    return 0;
}

TEST(clap_sustained_sound_is_not_a_clap)
{
    clap_detect_init(&s_clap, &CLAP_CFG);
    quiet(100);
    ASSERT(feed(50, 20000, 8000) < 0);  /* half a second of loud sound */
    ASSERT(quiet(28) < 0);
    clap();
    ASSERT(quiet(200) < 0);
    /* a proper double clap afterwards still works */
    clap();
    quiet(28);
    clap();
    ASSERT(quiet(100) == 30);
    return 0;
}

TEST(clap_needs_level_and_ratio)
{
    clap_detect_init(&s_clap, &CLAP_CFG);
    quiet(100);
    feed(2, 9000, 3000);            /* below the 40 % level */
    quiet(28);
    feed(2, 9000, 3000);
    ASSERT(quiet(200) < 0);
    clap_detect_init(&s_clap, &CLAP_CFG);
    ASSERT(feed(400, 3000, 2000) < 0);  /* noisy room: floor ~2000 */
    ASSERT(clap_detect_noise(&s_clap) >= 1990 && clap_detect_noise(&s_clap) <= 2000);
    feed(2, 11000, 4000);           /* 11000 < 6 x 2000 */
    feed(28, 3000, 2000);
    feed(2, 11000, 4000);
    ASSERT(feed(200, 3000, 2000) < 0);
    return 0;
}

/* ---------- endpoint ---------- */
static int run_until(endpoint_t *e, int n, uint16_t rms, endpoint_result_t *res)
{
    for (int i = 0; i < n; i++) {
        endpoint_result_t r = endpoint_frame(e, rms);
        if (r != ENDPOINT_CONTINUE) {
            *res = r;
            return i;
        }
    }
    *res = ENDPOINT_CONTINUE;
    return -1;
}

TEST(endpoint_speech_then_silence)
{
    endpoint_t e;
    endpoint_result_t r;
    endpoint_init(&e, 100, 3, 300);
    ASSERT(e.threshold == 300);
    ASSERT(run_until(&e, 50, 100, &r) < 0);
    ASSERT(run_until(&e, 100, 2000, &r) < 0);
    ASSERT(endpoint_heard_speech(&e));
    ASSERT(run_until(&e, 500, 100, &r) == 119);  /* the 120th quiet frame ends it */
    ASSERT(r == ENDPOINT_DONE);
    return 0;
}

TEST(endpoint_no_speech_gives_up)
{
    endpoint_t e;
    endpoint_result_t r;
    endpoint_init(&e, 200, 3, 300);
    ASSERT(e.threshold == 600);
    ASSERT(run_until(&e, 1000, 100, &r) == 399);
    ASSERT(r == ENDPOINT_NO_SPEECH);
    return 0;
}

TEST(endpoint_short_blips_do_not_start_speech)
{
    endpoint_t e;
    endpoint_result_t r = ENDPOINT_CONTINUE;
    int frames = 0;
    endpoint_init(&e, 100, 3, 300);
    while (r == ENDPOINT_CONTINUE && frames < 1000) {
        int n = run_until(&e, 5, 2000, &r);    /* 50 ms loud */
        frames += 5;
        if (n >= 0) break;
        run_until(&e, 10, 100, &r);            /* 100 ms quiet */
        frames += 10;
    }
    ASSERT(r == ENDPOINT_NO_SPEECH);
    ASSERT(!endpoint_heard_speech(&e));
    return 0;
}

TEST(endpoint_stops_at_ten_seconds)
{
    endpoint_t e;
    endpoint_result_t r;
    endpoint_init(&e, 100, 3, 300);
    ASSERT(run_until(&e, 2000, 3000, &r) == 999);
    ASSERT(r == ENDPOINT_DONE);
    return 0;
}

int test_voice_detect_all(void)
{
    int failures = 0;

    printf("\nVoice Detection Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(clap_single_is_ignored);
    RUN(clap_double_fires_after_quiet_window);
    RUN(clap_gap_out_of_range_is_ignored);
    RUN(clap_three_is_applause);
    RUN(clap_sustained_sound_is_not_a_clap);
    RUN(clap_needs_level_and_ratio);
    RUN(endpoint_speech_then_silence);
    RUN(endpoint_no_speech_gives_up);
    RUN(endpoint_short_blips_do_not_start_speech);
    RUN(endpoint_stops_at_ten_seconds);

#undef RUN
    return failures;
}
