#include "display_face.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "display_gfx.h"
#include "display_hw.h"
#include "esp_log.h"
#include "frame_format.h"
#include "sd_card.h"

#define FACE_DIR      SD_MOUNT_POINT "/face"
#define FACE_PATH_MAX 64

static const char *TAG = "display_face";

static const char *const s_expr_names[FACE_EXPR_COUNT] = {
    "neutral", "thinking", "sleeping", "happy", "angry", "speech",
};
static const char *const s_patch_names[FACE_PATCH_COUNT] = {
    NULL, "eyes_closed", "speaking",
};

typedef struct {
    uint8_t count;
    uint8_t suffix[FACE_MAX_VARIANTS];  // file suffix: 1 = "<name>.r565", n = "<name>_n.r565"
} variant_list_t;

typedef struct {
    bool valid;
    bool text;
    face_mode_t text_mode;
    face_expr_t expr;
    uint8_t expr_variant;
    face_patch_t patch;
    uint8_t patch_variant;
} drawn_t;

static variant_list_t s_expr[FACE_EXPR_COUNT];
static variant_list_t s_patch[FACE_PATCH_COUNT];
static frame_header_t s_patch_hdr[FACE_PATCH_COUNT][FACE_MAX_VARIANTS];
static drawn_t s_drawn;

static void frame_path(char *buf, size_t len, const char *name, uint8_t suffix)
{
    if (suffix <= 1) {
        snprintf(buf, len, FACE_DIR "/%s.r565", name);
    } else {
        snprintf(buf, len, FACE_DIR "/%s_%u.r565", name, (unsigned)suffix);
    }
}

static bool probe(const char *path, frame_header_t *hdr)
{
    struct stat st;
    uint8_t raw[FRAME_HEADER_SIZE];

    if (stat(path, &st) != 0) {
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    size_t got = fread(raw, 1, sizeof(raw), f);
    fclose(f);

    frame_err_t err = frame_header_parse(raw, got, (size_t)st.st_size, DISPLAY_W, DISPLAY_H, hdr);
    if (err != FRAME_OK) {
        ESP_LOGW(TAG, "Skipping %s (frame error %d)", path, (int)err);
        return false;
    }
    return true;
}

static void scan_list(const char *name, bool want_patch, variant_list_t *list,
                      frame_header_t *hdrs)
{
    char path[FACE_PATH_MAX];

    list->count = 0;
    for (uint8_t suffix = 1; suffix <= FACE_MAX_VARIANTS; suffix++) {
        frame_header_t hdr;
        frame_path(path, sizeof(path), name, suffix);
        if (!probe(path, &hdr)) {
            continue;
        }
        bool is_patch = (hdr.flags & FRAME_FLAG_PATCH) != 0;
        bool type_ok = want_patch ? is_patch
                                  : (!is_patch && frame_is_full_screen(&hdr, DISPLAY_W, DISPLAY_H));
        if (!type_ok) {
            ESP_LOGW(TAG, "Skipping %s (expected %s)", path, want_patch ? "a patch" : "a full frame");
            continue;
        }
        if (hdrs) {
            hdrs[list->count] = hdr;
        }
        list->suffix[list->count++] = suffix;
    }
}

void display_face_rescan(face_state_t *st)
{
    uint8_t expr_counts[FACE_EXPR_COUNT] = {0};
    uint8_t patch_counts[FACE_PATCH_COUNT] = {0};

    for (int i = 0; i < FACE_EXPR_COUNT; i++) {
        scan_list(s_expr_names[i], false, &s_expr[i], NULL);
        expr_counts[i] = s_expr[i].count;
    }
    for (int i = 1; i < FACE_PATCH_COUNT; i++) {
        scan_list(s_patch_names[i], true, &s_patch[i], s_patch_hdr[i]);
        patch_counts[i] = s_patch[i].count;
    }
    face_state_set_catalog(st, expr_counts, patch_counts);
    s_drawn.valid = false;
    ESP_LOGI(TAG, "Face catalogue: neutral=%u thinking=%u sleeping=%u eyes_closed=%u speaking=%u",
             expr_counts[FACE_EXPR_NEUTRAL], expr_counts[FACE_EXPR_THINKING],
             expr_counts[FACE_EXPR_SLEEPING], patch_counts[FACE_PATCH_EYES_CLOSED],
             patch_counts[FACE_PATCH_SPEAKING]);
}

void display_face_clear(face_state_t *st)
{
    uint8_t expr_counts[FACE_EXPR_COUNT] = {0};
    uint8_t patch_counts[FACE_PATCH_COUNT] = {0};

    memset(s_expr, 0, sizeof(s_expr));
    memset(s_patch, 0, sizeof(s_patch));
    face_state_set_catalog(st, expr_counts, patch_counts);
    s_drawn.valid = false;
}

void display_face_invalidate(void)
{
    s_drawn.valid = false;
}

static const char *mode_label(face_mode_t mode)
{
    switch (mode) {
    case FACE_MODE_THINKING:
        return "pensando...";
    case FACE_MODE_TALKING:
        return "hablando...";
    case FACE_MODE_OFFLINE:
        return "sin conexi\xC3\xB3n";
    case FACE_MODE_IDLE:
    default:
        return "Fidel";
    }
}

static void draw_text_face(face_mode_t mode)
{
    const char *label = mode_label(mode);
    const char *hint = sd_card_is_mounted() ? "faltan las caras en /face" : "sin tarjeta SD";
    int w;

    gfx_fill_rect(0, 0, DISPLAY_W, DISPLAY_H, GFX_BLACK);
    w = gfx_text_width(label, 2);
    gfx_text(w < DISPLAY_W ? (DISPLAY_W - w) / 2 : 0, 104, label, GFX_WHITE, GFX_BLACK, 2);
    w = gfx_text_width(hint, 1);
    gfx_text(w < DISPLAY_W ? (DISPLAY_W - w) / 2 : 0, 210, hint, GFX_GRAY, GFX_BLACK, 1);
}

static bool open_frame(const char *name, uint8_t suffix, FILE **f, size_t *size)
{
    char path[FACE_PATH_MAX];
    struct stat st;

    frame_path(path, sizeof(path), name, suffix);
    if (stat(path, &st) != 0) {
        return false;
    }
    *f = fopen(path, "rb");
    if (!*f) {
        return false;
    }
    *size = (size_t)st.st_size;
    return true;
}

static bool draw_file(const char *name, uint8_t suffix)
{
    FILE *f;
    size_t size;

    if (!open_frame(name, suffix, &f, &size)) {
        return false;
    }
    esp_err_t err = gfx_draw_frame_file(f, size, NULL);
    fclose(f);
    return err == ESP_OK;
}

static bool restore_region(const frame_header_t *rect)
{
    FILE *f;
    size_t size;

    if (!open_frame(s_expr_names[FACE_EXPR_NEUTRAL], s_expr[FACE_EXPR_NEUTRAL].suffix[0], &f, &size)) {
        return false;
    }
    esp_err_t err = gfx_draw_frame_region(f, size, rect->x, rect->y, rect->w, rect->h);
    fclose(f);
    return err == ESP_OK;
}

bool display_face_draw(face_state_t *st, int64_t now_ms)
{
    face_frame_t fr = face_state_frame(st, now_ms);
    face_mode_t mode = face_state_mode(st, now_ms);

    if (fr.text_fallback) {
        if (!s_drawn.valid || !s_drawn.text || s_drawn.text_mode != mode) {
            draw_text_face(mode);
            memset(&s_drawn, 0, sizeof(s_drawn));
            s_drawn.valid = true;
            s_drawn.text = true;
            s_drawn.text_mode = mode;
        }
        return true;
    }

    if (!s_drawn.valid || s_drawn.text || fr.expr != s_drawn.expr ||
        fr.expr_variant != s_drawn.expr_variant) {
        if (!draw_file(s_expr_names[fr.expr], s_expr[fr.expr].suffix[fr.expr_variant])) {
            return false;
        }
        memset(&s_drawn, 0, sizeof(s_drawn));
        s_drawn.valid = true;
        s_drawn.expr = fr.expr;
        s_drawn.expr_variant = fr.expr_variant;
        s_drawn.patch = FACE_PATCH_NONE;
    }

    if (fr.patch != s_drawn.patch || fr.patch_variant != s_drawn.patch_variant) {
        if (s_drawn.patch != FACE_PATCH_NONE &&
            !restore_region(&s_patch_hdr[s_drawn.patch][s_drawn.patch_variant])) {
            return false;
        }
        if (fr.patch != FACE_PATCH_NONE &&
            !draw_file(s_patch_names[fr.patch], s_patch[fr.patch].suffix[fr.patch_variant])) {
            return false;
        }
        s_drawn.patch = fr.patch;
        s_drawn.patch_variant = fr.patch_variant;
    }
    return true;
}
