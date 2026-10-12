#ifndef FACE_STATE_H
#define FACE_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FACE_MAX_VARIANTS      8
#define FACE_BLINK_MS          150
#define FACE_BLINK_MIN_GAP_MS  3000
#define FACE_BLINK_MAX_GAP_MS  7000
#define FACE_TALK_TOGGLE_MS    180
#define FACE_TALK_MS_PER_CHAR  60
#define FACE_TALK_MIN_MS       2000
#define FACE_TALK_MAX_MS       6000
#define FACE_CYCLE_MS          800

typedef enum {
    FACE_EXPR_NEUTRAL = 0,
    FACE_EXPR_THINKING,
    FACE_EXPR_SLEEPING,
    FACE_EXPR_HAPPY,
    FACE_EXPR_ANGRY,
    FACE_EXPR_SPEECH,
    FACE_EXPR_LISTENING,
    FACE_EXPR_COUNT
} face_expr_t;

typedef enum {
    FACE_PATCH_NONE = 0,
    FACE_PATCH_EYES_CLOSED,
    FACE_PATCH_SPEAKING,
    FACE_PATCH_COUNT
} face_patch_t;

typedef enum {
    FACE_MODE_IDLE = 0,
    FACE_MODE_TALKING,
    FACE_MODE_THINKING,
    FACE_MODE_OFFLINE,
    FACE_MODE_LISTENING
} face_mode_t;

typedef struct {
    bool text_fallback;      // no usable frame: draw the mode as text
    face_expr_t expr;        // full frame to show
    uint8_t expr_variant;    // 0-based index into the available variants
    face_patch_t patch;      // patch over neutral, or FACE_PATCH_NONE
    uint8_t patch_variant;
} face_frame_t;

typedef struct {
    bool wifi_up;
    bool thinking;
    bool listening;
    int64_t talk_until_ms;
    face_mode_t last_mode;
    int64_t mode_since_ms;
    int64_t next_blink_ms;
    int64_t blink_until_ms;
    uint32_t rng;
    uint8_t expr_variants[FACE_EXPR_COUNT];
    uint8_t patch_variants[FACE_PATCH_COUNT];
} face_state_t;

void face_state_init(face_state_t *s, uint32_t seed, int64_t now_ms);
void face_state_set_catalog(face_state_t *s, const uint8_t expr_variants[FACE_EXPR_COUNT],
                            const uint8_t patch_variants[FACE_PATCH_COUNT]);
void face_state_set_wifi(face_state_t *s, bool up);
void face_state_set_thinking(face_state_t *s, bool active);
void face_state_set_listening(face_state_t *s, bool active);
void face_state_start_talking(face_state_t *s, size_t reply_chars, int64_t now_ms);
int64_t face_state_talk_duration_ms(size_t reply_chars);
face_mode_t face_state_mode(const face_state_t *s, int64_t now_ms);
bool face_state_is_active(const face_state_t *s, int64_t now_ms);

// Frame to show at now_ms. Advances the blink schedule and animation phase.
face_frame_t face_state_frame(face_state_t *s, int64_t now_ms);

#endif // FACE_STATE_H
