/*
 * Host tests for the face animation state machine.
 */

#include <stdio.h>
#include <string.h>

#include "face_state.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

static void full_catalog(face_state_t *s, uint8_t speaking_variants, uint8_t thinking_variants)
{
    uint8_t expr[FACE_EXPR_COUNT] = {1, 1, 1, 1, 1, 1};
    uint8_t patch[FACE_PATCH_COUNT] = {0, 1, 1};
    expr[FACE_EXPR_THINKING] = thinking_variants;
    patch[FACE_PATCH_SPEAKING] = speaking_variants;
    face_state_set_catalog(s, expr, patch);
}

static void online_state(face_state_t *s)
{
    face_state_init(s, 12345u, 0);
    full_catalog(s, 1, 1);
    face_state_set_wifi(s, true);
}

TEST(talk_duration_is_clamped)
{
    ASSERT(face_state_talk_duration_ms(0) == FACE_TALK_MIN_MS);
    ASSERT(face_state_talk_duration_ms(50) == 50 * FACE_TALK_MS_PER_CHAR);
    ASSERT(face_state_talk_duration_ms(100000) == FACE_TALK_MAX_MS);
    return 0;
}

TEST(priority_offline_thinking_talking_idle)
{
    face_state_t s;
    face_state_init(&s, 1u, 0);
    full_catalog(&s, 1, 1);
    ASSERT(face_state_mode(&s, 0) == FACE_MODE_OFFLINE);

    face_state_set_thinking(&s, true);
    face_state_start_talking(&s, 50, 0);
    ASSERT(face_state_mode(&s, 10) == FACE_MODE_OFFLINE);

    face_state_set_wifi(&s, true);
    ASSERT(face_state_mode(&s, 10) == FACE_MODE_THINKING);

    face_state_set_thinking(&s, false);
    ASSERT(face_state_mode(&s, 10) == FACE_MODE_TALKING);
    ASSERT(face_state_is_active(&s, 10));

    ASSERT(face_state_mode(&s, 3000) == FACE_MODE_IDLE);
    ASSERT(!face_state_is_active(&s, 3000));
    return 0;
}

TEST(offline_shows_sleeping_and_thinking_shows_thinking)
{
    face_state_t s;
    face_state_init(&s, 1u, 0);
    full_catalog(&s, 1, 1);
    face_frame_t f = face_state_frame(&s, 0);
    ASSERT(!f.text_fallback && f.expr == FACE_EXPR_SLEEPING && f.patch == FACE_PATCH_NONE);

    face_state_set_wifi(&s, true);
    face_state_set_thinking(&s, true);
    f = face_state_frame(&s, 100);
    ASSERT(f.expr == FACE_EXPR_THINKING && f.patch == FACE_PATCH_NONE);
    return 0;
}

TEST(talking_alternates_mouth_patch_and_cycles_variants)
{
    face_state_t s;
    face_state_init(&s, 1u, 0);
    full_catalog(&s, 2, 1);
    face_state_set_wifi(&s, true);
    face_state_start_talking(&s, 50, 1000);  /* 3000 ms */

    face_frame_t f = face_state_frame(&s, 1000);
    ASSERT(f.expr == FACE_EXPR_NEUTRAL && f.patch == FACE_PATCH_SPEAKING && f.patch_variant == 0);
    f = face_state_frame(&s, 1000 + FACE_TALK_TOGGLE_MS);
    ASSERT(f.patch == FACE_PATCH_NONE);
    f = face_state_frame(&s, 1000 + 2 * FACE_TALK_TOGGLE_MS);
    ASSERT(f.patch == FACE_PATCH_SPEAKING && f.patch_variant == 1);
    f = face_state_frame(&s, 1000 + 4 * FACE_TALK_TOGGLE_MS);
    ASSERT(f.patch == FACE_PATCH_SPEAKING && f.patch_variant == 0);

    ASSERT(face_state_mode(&s, 3999) == FACE_MODE_TALKING);
    ASSERT(face_state_mode(&s, 4000) == FACE_MODE_IDLE);
    return 0;
}

TEST(longer_reply_extends_talking)
{
    face_state_t s;
    online_state(&s);
    face_state_start_talking(&s, 100, 0);   /* 6000 ms */
    face_state_start_talking(&s, 0, 1000);  /* 2000 ms would end earlier: keep 6000 */
    ASSERT(face_state_mode(&s, 5999) == FACE_MODE_TALKING);
    return 0;
}

TEST(idle_blinks_with_bounded_gaps)
{
    face_state_t s;
    online_state(&s);

    int64_t last_start = -1;
    int64_t first_start = -1;
    int blink_frames = 0;
    int blinks = 0;
    bool in_blink = false;

    for (int64_t t = 0; t <= 60000; t += 10) {
        face_frame_t f = face_state_frame(&s, t);
        bool closed = f.patch == FACE_PATCH_EYES_CLOSED;
        if (closed && !in_blink) {
            if (first_start < 0) {
                first_start = t;
            }
            if (last_start >= 0) {
                int64_t gap = t - last_start;
                ASSERT(gap >= FACE_BLINK_MIN_GAP_MS + FACE_BLINK_MS);
                ASSERT(gap <= FACE_BLINK_MAX_GAP_MS + FACE_BLINK_MS + 10);
            }
            last_start = t;
            blinks++;
            blink_frames = 0;
        }
        if (closed) {
            blink_frames++;
        } else if (in_blink) {
            ASSERT(blink_frames == FACE_BLINK_MS / 10);
        }
        in_blink = closed;
    }

    ASSERT(first_start >= 0 && first_start <= FACE_BLINK_MAX_GAP_MS + 10);
    ASSERT(blinks >= 60000 / (FACE_BLINK_MAX_GAP_MS + FACE_BLINK_MS));
    return 0;
}

TEST(thinking_cycles_variants)
{
    face_state_t s;
    face_state_init(&s, 1u, 0);
    full_catalog(&s, 1, 3);
    face_state_set_wifi(&s, true);
    face_state_set_thinking(&s, true);
    ASSERT(face_state_frame(&s, 500).expr_variant == 0);
    ASSERT(face_state_frame(&s, 500 + FACE_CYCLE_MS).expr_variant == 1);
    ASSERT(face_state_frame(&s, 500 + 2 * FACE_CYCLE_MS).expr_variant == 2);
    ASSERT(face_state_frame(&s, 500 + 3 * FACE_CYCLE_MS).expr_variant == 0);
    return 0;
}

TEST(missing_frames_fall_back)
{
    face_state_t s;
    uint8_t expr[FACE_EXPR_COUNT] = {1, 0, 0, 0, 0, 0};
    uint8_t patch[FACE_PATCH_COUNT] = {0, 0, 0};

    face_state_init(&s, 1u, 0);
    face_state_set_catalog(&s, expr, patch);
    face_state_set_wifi(&s, true);
    face_state_set_thinking(&s, true);
    face_frame_t f = face_state_frame(&s, 0);
    ASSERT(!f.text_fallback && f.expr == FACE_EXPR_NEUTRAL);

    face_state_set_thinking(&s, false);
    for (int64_t t = 0; t <= 20000; t += 10) {
        ASSERT(face_state_frame(&s, t).patch == FACE_PATCH_NONE);
    }

    expr[FACE_EXPR_NEUTRAL] = 0;
    face_state_set_catalog(&s, expr, patch);
    f = face_state_frame(&s, 20010);
    ASSERT(f.text_fallback);

    expr[FACE_EXPR_THINKING] = 1;
    face_state_set_catalog(&s, expr, patch);
    face_state_set_thinking(&s, true);
    f = face_state_frame(&s, 20020);
    ASSERT(!f.text_fallback && f.expr == FACE_EXPR_THINKING);
    return 0;
}

int test_face_state_all(void)
{
    int failures = 0;

    printf("\nFace State Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(talk_duration_is_clamped);
    RUN(priority_offline_thinking_talking_idle);
    RUN(offline_shows_sleeping_and_thinking_shows_thinking);
    RUN(talking_alternates_mouth_patch_and_cycles_variants);
    RUN(longer_reply_extends_talking);
    RUN(idle_blinks_with_bounded_gaps);
    RUN(thinking_cycles_variants);
    RUN(missing_frames_fall_back);

#undef RUN
    return failures;
}
