#ifndef GROW_BUF_H
#define GROW_BUF_H

#include <stdbool.h>
#include <stddef.h>

// Growable, NUL-terminated text buffer on the heap. Starts small and doubles
// up to a hard cap so large replies only cost RAM while they are in flight.
typedef struct {
    char *data;      // NUL-terminated
    size_t len;
    size_t cap;
    size_t max;      // hard cap, including the NUL
    bool truncated;
} grow_buf_t;

bool grow_buf_init(grow_buf_t *b, size_t initial, size_t max);
bool grow_buf_append(grow_buf_t *b, const char *src, size_t n);  // false once truncated
char *grow_buf_take(grow_buf_t *b);                              // caller frees; b is emptied
void grow_buf_free(grow_buf_t *b);

#endif // GROW_BUF_H
