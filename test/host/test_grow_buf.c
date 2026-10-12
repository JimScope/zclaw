/*
 * Host tests for the growable text buffer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grow_buf.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(starts_empty_and_appends)
{
    grow_buf_t b;
    ASSERT(grow_buf_init(&b, 16, 64));
    ASSERT(b.len == 0 && b.data[0] == '\0');
    ASSERT(grow_buf_append(&b, "hola", 4));
    ASSERT(strcmp(b.data, "hola") == 0 && b.len == 4);
    grow_buf_free(&b);
    ASSERT(b.data == NULL);
    return 0;
}

TEST(grows_past_initial_capacity)
{
    static char chunk[1000];
    grow_buf_t b;
    memset(chunk, 'a', sizeof(chunk));
    ASSERT(grow_buf_init(&b, 4096, 16384));
    for (int i = 0; i < 10; i++) {
        ASSERT(grow_buf_append(&b, chunk, sizeof(chunk)));
    }
    ASSERT(b.len == 10000 && strlen(b.data) == 10000);
    ASSERT(b.cap >= 10001 && b.cap <= 16384);
    ASSERT(!b.truncated);
    grow_buf_free(&b);
    return 0;
}

TEST(refuses_beyond_max)
{
    static char chunk[100];
    grow_buf_t b;
    memset(chunk, 'z', sizeof(chunk));
    ASSERT(grow_buf_init(&b, 64, 256));
    ASSERT(grow_buf_append(&b, chunk, 100));
    ASSERT(grow_buf_append(&b, chunk, 100));
    ASSERT(!grow_buf_append(&b, chunk, 100));   // 300 > 255
    ASSERT(b.truncated);
    ASSERT(b.len == 255 && strlen(b.data) == 255);
    ASSERT(!grow_buf_append(&b, "x", 1));
    grow_buf_free(&b);
    return 0;
}

TEST(take_hands_over_the_string)
{
    grow_buf_t b;
    ASSERT(grow_buf_init(&b, 8, 64));
    ASSERT(grow_buf_append(&b, "respuesta", 9));
    char *s = grow_buf_take(&b);
    ASSERT(s && strcmp(s, "respuesta") == 0);
    ASSERT(b.data == NULL && b.len == 0);
    free(s);
    grow_buf_free(&b);  // safe after take
    return 0;
}

int test_grow_buf_all(void)
{
    int failures = 0;

    printf("\nGrow Buffer Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(starts_empty_and_appends);
    RUN(grows_past_initial_capacity);
    RUN(refuses_beyond_max);
    RUN(take_hands_over_the_string);

#undef RUN
    return failures;
}
