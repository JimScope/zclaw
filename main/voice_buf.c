#include "voice_buf.h"

#include <stdlib.h>
#include <string.h>

bool voice_buf_alloc(voice_buf_t *b, size_t capacity, size_t max_append)
{
    if (max_append == 0 || max_append > VOICE_BUF_CHUNK_BYTES) {
        return false;
    }
    // An append that does not fit moves to the next chunk, so each chunk is
    // only sure to hold this much.
    size_t sure = VOICE_BUF_CHUNK_BYTES - (max_append - 1);
    size_t n = (capacity + sure - 1) / sure;
    if (n == 0) {
        n = 1;
    }
    if (n > VOICE_BUF_MAX_CHUNKS) {
        return false;
    }
    voice_buf_free(b);
    for (size_t i = 0; i < n; i++) {
        b->chunk[i] = malloc(VOICE_BUF_CHUNK_BYTES);
        if (!b->chunk[i]) {
            b->count = i;
            voice_buf_free(b);
            return false;
        }
    }
    b->count = n;
    return true;
}

uint8_t *voice_buf_append(voice_buf_t *b, size_t n)
{
    if (n == 0 || n > VOICE_BUF_CHUNK_BYTES) {
        return NULL;
    }
    while (b->cur < b->count) {
        if (b->used[b->cur] + n <= VOICE_BUF_CHUNK_BYTES) {
            uint8_t *p = b->chunk[b->cur] + b->used[b->cur];
            b->used[b->cur] += n;
            b->total += n;
            return p;
        }
        b->cur++;
    }
    return NULL;
}

void voice_buf_trim(voice_buf_t *b)
{
    while (b->count > 0 && b->used[b->count - 1] == 0) {
        b->count--;
        free(b->chunk[b->count]);
        b->chunk[b->count] = NULL;
    }
    if (b->cur > b->count) {
        b->cur = b->count;
    }
}

void voice_buf_free(voice_buf_t *b)
{
    for (size_t i = 0; i < b->count; i++) {
        free(b->chunk[i]);
    }
    memset(b, 0, sizeof(*b));
}
