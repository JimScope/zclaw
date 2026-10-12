#ifndef DASH_STATE_H
#define DASH_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Bookkeeping for the remote page (dashboards and pictures): which request is
// current, whether it still needs a download, and the JPEG waiting to be
// drawn. Owns that JPEG and releases it with free_fn as soon as it can no
// longer be shown, so a cancelled request never parks a buffer in RAM.
typedef struct {
    uint32_t request;
    int index;          // dashboard index, or -1 for a picture
    bool need_fetch;
    void *jpeg;
    size_t len;
    bool failed;
    void (*free_fn)(void *);
} dash_state_t;

void dash_state_init(dash_state_t *s, void (*free_fn)(void *));

// New dashboard request: drops any held result and returns the request id.
uint32_t dash_state_request(dash_state_t *s, int index);

// A picture that is already downloaded; dash_state owns jpeg from now on.
uint32_t dash_state_picture(dash_state_t *s, void *jpeg, size_t len);

// A download finished (jpeg NULL = failed). True when accepted (dash_state
// owns jpeg); false for a stale request (the caller still owns jpeg).
bool dash_state_done(dash_state_t *s, uint32_t request, void *jpeg, size_t len);

// Call after every page update. When the remote page is not active, pending
// downloads become stale and any held JPEG is released.
void dash_state_page(dash_state_t *s, bool remote_page_active);

// Release the held result after it was drawn.
void dash_state_clear_result(dash_state_t *s);

#endif // DASH_STATE_H
