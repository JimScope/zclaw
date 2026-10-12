#include "grow_buf.h"

#include <stdlib.h>
#include <string.h>

bool grow_buf_init(grow_buf_t *b, size_t initial, size_t max)
{
    memset(b, 0, sizeof(*b));
    if (initial == 0 || max < initial) {
        return false;
    }
    b->data = malloc(initial);
    if (!b->data) {
        return false;
    }
    b->data[0] = '\0';
    b->cap = initial;
    b->max = max;
    return true;
}

bool grow_buf_append(grow_buf_t *b, const char *src, size_t n)
{
    if (!b->data || b->truncated) {
        return false;
    }
    size_t want = b->len + n + 1;
    if (want > b->cap) {
        size_t cap = b->cap;
        while (cap < want && cap < b->max) {
            cap = (cap * 2 > b->max) ? b->max : cap * 2;
        }
        if (cap != b->cap) {
            char *grown = realloc(b->data, cap);
            if (grown) {
                b->data = grown;
                b->cap = cap;
            }
        }
    }
    size_t room = b->cap - 1 - b->len;
    size_t copy = n < room ? n : room;
    memcpy(b->data + b->len, src, copy);
    b->len += copy;
    b->data[b->len] = '\0';
    if (copy < n) {
        b->truncated = true;
        return false;
    }
    return true;
}

char *grow_buf_take(grow_buf_t *b)
{
    char *s = b->data;
    memset(b, 0, sizeof(*b));
    return s;
}

void grow_buf_free(grow_buf_t *b)
{
    free(b->data);
    memset(b, 0, sizeof(*b));
}
