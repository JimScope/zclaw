#include "page_rotation.h"

void page_rotation_init(page_rotation_t *r, uint32_t face_ms, uint32_t status_ms, int64_t now_ms)
{
    r->page = PAGE_FACE;
    r->since_ms = now_ms;
    r->face_ms = face_ms;
    r->status_ms = status_ms;
    r->status_until_ms = 0;
}

void page_rotation_show_status(page_rotation_t *r, uint32_t duration_ms, int64_t now_ms)
{
    r->status_until_ms = now_ms + duration_ms;
}

page_t page_rotation_update(page_rotation_t *r, bool face_forced, int64_t now_ms)
{
    if (face_forced) {
        r->status_until_ms = 0;
    } else if (r->status_until_ms != 0) {
        if (now_ms < r->status_until_ms) {
            if (r->page != PAGE_STATUS) {
                r->page = PAGE_STATUS;
                r->since_ms = now_ms;
            }
            return r->page;
        }
        // Requested page expired: back to a full face period.
        r->status_until_ms = 0;
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
