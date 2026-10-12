/*
 * Host tests for parsing the STT service reply.
 */

#include <stdio.h>
#include <string.h>

#include "stt_reply.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(reads_and_trims_text)
{
    char out[64];
    ASSERT(stt_reply_parse("{\"text\": \"  hola fidel \\n\", \"ms\": 840}", out, sizeof(out)));
    ASSERT(strcmp(out, "hola fidel") == 0);
    return 0;
}

TEST(empty_transcript_is_valid)
{
    char out[64] = "x";
    ASSERT(stt_reply_parse("{\"text\": \"\"}", out, sizeof(out)));
    ASSERT(out[0] == '\0');
    ASSERT(stt_reply_parse("{\"text\": \"   \"}", out, sizeof(out)));
    ASSERT(out[0] == '\0');
    return 0;
}

TEST(rejects_missing_or_invalid)
{
    char out[64] = "x";
    ASSERT(!stt_reply_parse("{\"error\": \"busy\"}", out, sizeof(out)));
    ASSERT(out[0] == '\0');
    ASSERT(!stt_reply_parse("{\"text\": 3}", out, sizeof(out)));
    ASSERT(!stt_reply_parse("not json", out, sizeof(out)));
    ASSERT(!stt_reply_parse(NULL, out, sizeof(out)));
    return 0;
}

TEST(clips_without_splitting_utf8)
{
    char out[10];
    ASSERT(stt_reply_parse("{\"text\": \"canci\\u00f3n muy larga\"}", out, sizeof(out)));
    ASSERT(strcmp(out, "canci...") == 0);  /* the cut before the two-byte o-acute */
    return 0;
}

int test_stt_reply_all(void)
{
    int failures = 0;

    printf("\nSTT Reply Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(reads_and_trims_text);
    RUN(empty_transcript_is_valid);
    RUN(rejects_missing_or_invalid);
    RUN(clips_without_splitting_utf8);

#undef RUN
    return failures;
}
