#include "dash_state.h"

#include <string.h>

void dash_state_init(dash_state_t *s, void (*free_fn)(void *))
{
    memset(s, 0, sizeof(*s));
    s->index = -1;
    s->free_fn = free_fn;
}

void dash_state_clear_result(dash_state_t *s)
{
    if (s->jpeg) {
        s->free_fn(s->jpeg);
    }
    s->jpeg = NULL;
    s->len = 0;
    s->failed = false;
}

uint32_t dash_state_request(dash_state_t *s, int index)
{
    dash_state_clear_result(s);
    s->index = index;
    s->need_fetch = true;
    return ++s->request;
}

uint32_t dash_state_picture(dash_state_t *s, void *jpeg, size_t len)
{
    dash_state_clear_result(s);
    s->index = -1;
    s->need_fetch = false;
    s->jpeg = jpeg;
    s->len = len;
    return ++s->request;
}

bool dash_state_done(dash_state_t *s, uint32_t request, void *jpeg, size_t len)
{
    if (request != s->request) {
        return false;
    }
    dash_state_clear_result(s);
    s->jpeg = jpeg;
    s->len = len;
    s->failed = (jpeg == NULL);
    return true;
}

void dash_state_page(dash_state_t *s, bool remote_page_active)
{
    if (remote_page_active || (!s->need_fetch && !s->jpeg && !s->failed)) {
        return;
    }
    s->request++;  // a download still in flight is now stale
    s->need_fetch = false;
    dash_state_clear_result(s);
}
