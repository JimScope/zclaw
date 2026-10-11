#include "face_state.h"

#include <string.h>

static uint32_t next_random(face_state_t *s)
{
    uint32_t x = s->rng ? s->rng : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x;
    return x;
}

static int64_t blink_gap_ms(face_state_t *s)
{
    uint32_t span = (uint32_t)(FACE_BLINK_MAX_GAP_MS - FACE_BLINK_MIN_GAP_MS + 1);
    return FACE_BLINK_MIN_GAP_MS + (int64_t)(next_random(s) % span);
}

static uint8_t cycle_variant(int64_t elapsed_ms, uint8_t count)
{
    if (count <= 1 || elapsed_ms < 0) {
        return 0;
    }
    return (uint8_t)((elapsed_ms / FACE_CYCLE_MS) % count);
}

void face_state_init(face_state_t *s, uint32_t seed, int64_t now_ms)
{
    memset(s, 0, sizeof(*s));
    s->rng = seed;
    s->last_mode = FACE_MODE_OFFLINE;
    s->mode_since_ms = now_ms;
    s->next_blink_ms = now_ms + blink_gap_ms(s);
}

void face_state_set_catalog(face_state_t *s, const uint8_t expr_variants[FACE_EXPR_COUNT],
                            const uint8_t patch_variants[FACE_PATCH_COUNT])
{
    for (int i = 0; i < FACE_EXPR_COUNT; i++) {
        s->expr_variants[i] = expr_variants[i] > FACE_MAX_VARIANTS ? FACE_MAX_VARIANTS : expr_variants[i];
    }
    for (int i = 0; i < FACE_PATCH_COUNT; i++) {
        s->patch_variants[i] = patch_variants[i] > FACE_MAX_VARIANTS ? FACE_MAX_VARIANTS : patch_variants[i];
    }
    s->patch_variants[FACE_PATCH_NONE] = 0;
}

void face_state_set_wifi(face_state_t *s, bool up)
{
    s->wifi_up = up;
}

void face_state_set_thinking(face_state_t *s, bool active)
{
    s->thinking = active;
}

int64_t face_state_talk_duration_ms(size_t reply_chars)
{
    int64_t ms = (int64_t)reply_chars * FACE_TALK_MS_PER_CHAR;
    if (ms < FACE_TALK_MIN_MS) {
        return FACE_TALK_MIN_MS;
    }
    if (ms > FACE_TALK_MAX_MS) {
        return FACE_TALK_MAX_MS;
    }
    return ms;
}

void face_state_start_talking(face_state_t *s, size_t reply_chars, int64_t now_ms)
{
    int64_t until = now_ms + face_state_talk_duration_ms(reply_chars);
    if (until > s->talk_until_ms) {
        s->talk_until_ms = until;
    }
}

face_mode_t face_state_mode(const face_state_t *s, int64_t now_ms)
{
    if (!s->wifi_up) {
        return FACE_MODE_OFFLINE;
    }
    if (s->thinking) {
        return FACE_MODE_THINKING;
    }
    if (now_ms < s->talk_until_ms) {
        return FACE_MODE_TALKING;
    }
    return FACE_MODE_IDLE;
}

bool face_state_is_active(const face_state_t *s, int64_t now_ms)
{
    face_mode_t mode = face_state_mode(s, now_ms);
    return mode == FACE_MODE_THINKING || mode == FACE_MODE_TALKING;
}

face_frame_t face_state_frame(face_state_t *s, int64_t now_ms)
{
    face_frame_t f = {
        .text_fallback = false,
        .expr = FACE_EXPR_NEUTRAL,
        .expr_variant = 0,
        .patch = FACE_PATCH_NONE,
        .patch_variant = 0,
    };
    face_mode_t mode = face_state_mode(s, now_ms);

    if (mode != s->last_mode) {
        s->last_mode = mode;
        s->mode_since_ms = now_ms;
    }
    int64_t elapsed = now_ms - s->mode_since_ms;

    switch (mode) {
    case FACE_MODE_OFFLINE:
        f.expr = FACE_EXPR_SLEEPING;
        f.expr_variant = cycle_variant(elapsed, s->expr_variants[FACE_EXPR_SLEEPING]);
        break;
    case FACE_MODE_THINKING:
        f.expr = FACE_EXPR_THINKING;
        f.expr_variant = cycle_variant(elapsed, s->expr_variants[FACE_EXPR_THINKING]);
        break;
    case FACE_MODE_TALKING: {
        int64_t step = elapsed / FACE_TALK_TOGGLE_MS;
        uint8_t count = s->patch_variants[FACE_PATCH_SPEAKING];
        if (step % 2 == 0 && count > 0) {
            f.patch = FACE_PATCH_SPEAKING;
            f.patch_variant = (uint8_t)((step / 2) % count);
        }
        break;
    }
    case FACE_MODE_IDLE:
    default:
        if (now_ms >= s->blink_until_ms && now_ms >= s->next_blink_ms) {
            s->blink_until_ms = now_ms + FACE_BLINK_MS;
            s->next_blink_ms = s->blink_until_ms + blink_gap_ms(s);
        }
        if (now_ms < s->blink_until_ms && s->patch_variants[FACE_PATCH_EYES_CLOSED] > 0) {
            f.patch = FACE_PATCH_EYES_CLOSED;
        }
        break;
    }

    if (s->expr_variants[f.expr] == 0) {
        f.expr = FACE_EXPR_NEUTRAL;
        f.expr_variant = 0;
    }
    if (f.expr != FACE_EXPR_NEUTRAL) {
        f.patch = FACE_PATCH_NONE;
        f.patch_variant = 0;
    }
    if (f.expr == FACE_EXPR_NEUTRAL && s->expr_variants[FACE_EXPR_NEUTRAL] == 0) {
        f.text_fallback = true;
        f.patch = FACE_PATCH_NONE;
        f.patch_variant = 0;
    }
    return f;
}
