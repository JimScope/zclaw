#ifndef VOICE_BUF_H
#define VOICE_BUF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Recording buffer made of fixed-size chunks, so a long recording never needs
// one large contiguous block from a fragmented heap. Appends are written whole
// into one chunk.
#define VOICE_BUF_CHUNK_BYTES 8192
#define VOICE_BUF_MAX_CHUNKS  16

typedef struct {
    uint8_t *chunk[VOICE_BUF_MAX_CHUNKS];
    size_t used[VOICE_BUF_MAX_CHUNKS];  // bytes written in each chunk
    size_t count;                       // allocated chunks
    size_t cur;                         // chunk being filled
    size_t total;                       // bytes written
} voice_buf_t;

// Allocates enough chunks to hold `capacity` bytes written in appends of at
// most `max_append` bytes. All or nothing: false leaves the buffer empty.
bool voice_buf_alloc(voice_buf_t *b, size_t capacity, size_t max_append);

// Room for `n` more bytes in one piece, or NULL when the buffer is full.
// The caller writes the bytes.
uint8_t *voice_buf_append(voice_buf_t *b, size_t n);

// Frees the chunks that hold no data.
void voice_buf_trim(voice_buf_t *b);

// Frees every chunk; the buffer is empty and can be allocated again.
void voice_buf_free(voice_buf_t *b);

#endif // VOICE_BUF_H
