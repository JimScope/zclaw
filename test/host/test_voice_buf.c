/*
 * Host tests for the chunked recording buffer.
 */

#include <stdio.h>
#include <string.h>

#include "voice_buf.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

#define FRAME 80
#define CAPACITY (4 + 1000 * FRAME)  // ADPCM header + 10 s at 16 kHz

static size_t chunk_of(const voice_buf_t *b, const uint8_t *p)
{
    for (size_t i = 0; i < b->count; i++) {
        if (p >= b->chunk[i] && p < b->chunk[i] + VOICE_BUF_CHUNK_BYTES) {
            return i;
        }
    }
    return (size_t)-1;
}

TEST(holds_header_and_ten_seconds)
{
    voice_buf_t b;
    memset(&b, 0, sizeof(b));
    ASSERT(voice_buf_alloc(&b, CAPACITY, FRAME));
    ASSERT(b.count * VOICE_BUF_CHUNK_BYTES < CAPACITY + VOICE_BUF_CHUNK_BYTES);  // no spare chunk
    ASSERT(voice_buf_append(&b, 4) != NULL);
    for (int i = 0; i < 1000; i++) {
        ASSERT(voice_buf_append(&b, FRAME) != NULL);
    }
    ASSERT(b.total == CAPACITY);
    voice_buf_free(&b);
    return 0;
}

TEST(append_never_straddles_chunks)
{
    voice_buf_t b;
    memset(&b, 0, sizeof(b));
    ASSERT(voice_buf_alloc(&b, CAPACITY, FRAME));
    uint8_t *p = voice_buf_append(&b, 4);
    ASSERT(p == b.chunk[0]);
    for (int i = 0; i < 1000; i++) {
        p = voice_buf_append(&b, FRAME);
        ASSERT(p != NULL);
        size_t c = chunk_of(&b, p);
        ASSERT(c != (size_t)-1);
        ASSERT(p + FRAME <= b.chunk[c] + VOICE_BUF_CHUNK_BYTES);
        memset(p, (int)(i & 0xff), FRAME);  // writable (sanitizers catch overruns)
    }
    size_t sum = 0;
    for (size_t i = 0; i < b.count; i++) {
        ASSERT(b.used[i] <= VOICE_BUF_CHUNK_BYTES);
        sum += b.used[i];
    }
    ASSERT(sum == b.total);
    voice_buf_free(&b);
    return 0;
}

TEST(full_buffer_refuses_more)
{
    voice_buf_t b;
    memset(&b, 0, sizeof(b));
    ASSERT(voice_buf_alloc(&b, 3 * FRAME, FRAME));
    ASSERT(b.count == 1);
    size_t fits = VOICE_BUF_CHUNK_BYTES / FRAME;
    for (size_t i = 0; i < fits; i++) {
        ASSERT(voice_buf_append(&b, FRAME) != NULL);
    }
    ASSERT(voice_buf_append(&b, FRAME) == NULL);
    ASSERT(b.total == fits * FRAME);
    voice_buf_free(&b);
    return 0;
}

TEST(trim_frees_chunks_without_data)
{
    voice_buf_t b;
    memset(&b, 0, sizeof(b));
    ASSERT(voice_buf_alloc(&b, CAPACITY, FRAME));
    ASSERT(b.count >= 3);
    ASSERT(voice_buf_append(&b, 4) != NULL);
    for (int i = 0; i < 150; i++) {  // 1.5 s: two chunks
        ASSERT(voice_buf_append(&b, FRAME) != NULL);
    }
    voice_buf_trim(&b);
    ASSERT(b.count == 2);
    ASSERT(b.used[0] + b.used[1] == b.total);
    voice_buf_free(&b);
    return 0;
}

TEST(free_empties_and_allows_reuse)
{
    voice_buf_t b;
    memset(&b, 0, sizeof(b));
    ASSERT(voice_buf_alloc(&b, CAPACITY, FRAME));
    ASSERT(voice_buf_append(&b, FRAME) != NULL);
    voice_buf_free(&b);
    ASSERT(b.count == 0);
    ASSERT(b.total == 0);
    ASSERT(voice_buf_append(&b, FRAME) == NULL);
    voice_buf_free(&b);  // twice is harmless
    ASSERT(voice_buf_alloc(&b, CAPACITY, FRAME));
    ASSERT(voice_buf_append(&b, FRAME) == b.chunk[0]);
    voice_buf_free(&b);
    return 0;
}

TEST(rejects_impossible_sizes)
{
    voice_buf_t b;
    memset(&b, 0, sizeof(b));
    ASSERT(!voice_buf_alloc(&b, CAPACITY, 0));
    ASSERT(!voice_buf_alloc(&b, CAPACITY, VOICE_BUF_CHUNK_BYTES + 1));
    ASSERT(!voice_buf_alloc(&b, (size_t)VOICE_BUF_MAX_CHUNKS * VOICE_BUF_CHUNK_BYTES + 1, FRAME));
    ASSERT(b.count == 0);
    return 0;
}

int test_voice_buf_all(void)
{
    int failures = 0;

    printf("\nVoice Buffer Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(holds_header_and_ten_seconds);
    RUN(append_never_straddles_chunks);
    RUN(full_buffer_refuses_more);
    RUN(trim_frees_chunks_without_data);
    RUN(free_empties_and_allows_reuse);
    RUN(rejects_impossible_sizes);

#undef RUN
    return failures;
}
