#include "page_rotation.h"

void page_rotation_init(page_rotation_t *r, uint32_t face_ms, uint32_t status_ms, int64_t now_ms)
{
    r->page = PAGE_FACE;
    r->since_ms = now_ms;
    r->face_ms = face_ms;
    r->status_ms = status_ms;
    r->requested = PAGE_STATUS;
    r->requested_until_ms = 0;
    r->requested_in_turn = false;
    r->prev_forced = false;
}

bool page_rotation_show(page_rotation_t *r, page_t page, uint32_t duration_ms, int64_t now_ms)
{
    bool tool_page_up = r->requested_until_ms != 0 && now_ms < r->requested_until_ms &&
                        r->requested != PAGE_ANSWER && r->requested_in_turn;
    if (page == PAGE_ANSWER && tool_page_up && r->prev_forced) {
        return false;
    }
    r->requested = page;
    r->requested_until_ms = now_ms + duration_ms;
    r->requested_in_turn = r->prev_forced;
    return true;
}

page_t page_rotation_update(page_rotation_t *r, bool face_forced, int64_t now_ms)
{
    bool new_activity = face_forced && !r->prev_forced;
    r->prev_forced = face_forced;
    if (new_activity) {
        r->requested_until_ms = 0;
    }
    if (r->requested_until_ms != 0) {
        if (now_ms < r->requested_until_ms) {
            if (r->page != r->requested) {
                r->page = r->requested;
                r->since_ms = now_ms;
            }
            return r->page;
        }
        // Requested page expired: back to a full face period.
        r->requested_until_ms = 0;
        r->page = PAGE_FACE;
        r->since_ms = now_ms;
        return r->page;
    }

    if (face_forced || r->status_ms == 0) {
        if (face_forced || r->page != PAGE_FACE) {
            r->since_ms = now_ms;
        }
        r->page = PAGE_FACE;
        return r->page;
    }

    uint32_t period = (r->page == PAGE_FACE) ? r->face_ms : r->status_ms;
    if (now_ms - r->since_ms >= (int64_t)period) {
        r->page = (r->page == PAGE_FACE) ? PAGE_STATUS : PAGE_FACE;
        r->since_ms = now_ms;
    }
    return r->page;
}
