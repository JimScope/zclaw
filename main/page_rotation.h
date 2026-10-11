#ifndef PAGE_ROTATION_H
#define PAGE_ROTATION_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PAGE_FACE = 0,
    PAGE_STATUS
} page_t;

typedef struct {
    page_t page;
    int64_t since_ms;
    uint32_t face_ms;
    uint32_t status_ms;
} page_rotation_t;

void page_rotation_init(page_rotation_t *r, uint32_t face_ms, uint32_t status_ms, int64_t now_ms);

// face_forced: thinking/talking in progress. Keeps the face page and restarts
// its period, so rotation resumes with a full face period afterwards.
page_t page_rotation_update(page_rotation_t *r, bool face_forced, int64_t now_ms);

#endif // PAGE_ROTATION_H
