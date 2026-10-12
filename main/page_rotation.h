#ifndef PAGE_ROTATION_H
#define PAGE_ROTATION_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PAGE_FACE = 0,
    PAGE_STATUS,   // device status (/panel), drawn locally
    PAGE_REMOTE,   // dashboard image downloaded from the VPS (/clima, /cripto)
    PAGE_ANSWER    // Fidel's reply as text (question on top)
} page_t;

typedef struct {
    page_t page;
    int64_t since_ms;
    uint32_t face_ms;
    uint32_t status_ms;
    page_t requested;         // page shown on demand
    int64_t requested_until_ms;  // 0 = nothing requested
    bool requested_in_turn;      // requested while an activity was in progress
    bool prev_forced;            // to detect the start of a new activity
} page_rotation_t;

void page_rotation_init(page_rotation_t *r, uint32_t face_ms, uint32_t status_ms, int64_t now_ms);

// face_forced: thinking/talking in progress. Keeps the face page and restarts
// its period, so rotation resumes with a full face period afterwards.
page_t page_rotation_update(page_rotation_t *r, bool face_forced, int64_t now_ms);

// Show `page` for duration_ms (/panel, /clima, /cripto, answers). A new
// request replaces the previous one. Only an activity that starts after the
// request cancels it, so a page requested during Fidel's turn survives that
// turn's reply. An answer never covers a page requested during the same
// turn (a tool showed it): returns false and changes nothing.
bool page_rotation_show(page_rotation_t *r, page_t page, uint32_t duration_ms, int64_t now_ms);

#endif // PAGE_ROTATION_H
