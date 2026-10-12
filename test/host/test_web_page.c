/*
 * Host tests for read_web paging.
 */

#include <stdio.h>
#include <string.h>

#include "web_page.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(first_middle_and_last_page)
{
    web_page_slice_t s = web_page_slice(1000, 1, 450);
    ASSERT(s.total_pages == 3 && s.start == 0 && s.len == 450 && !s.past_end);
    s = web_page_slice(1000, 2, 450);
    ASSERT(s.start == 450 && s.len == 450);
    s = web_page_slice(1000, 3, 450);
    ASSERT(s.start == 900 && s.len == 100 && !s.past_end);
    return 0;
}

TEST(past_the_end)
{
    web_page_slice_t s = web_page_slice(1000, 4, 450);
    ASSERT(s.past_end && s.len == 0 && s.total_pages == 3);
    return 0;
}

TEST(empty_text_is_one_empty_page)
{
    web_page_slice_t s = web_page_slice(0, 1, 450);
    ASSERT(s.total_pages == 1 && s.len == 0 && !s.past_end);
    return 0;
}

TEST(page_below_one_is_page_one)
{
    web_page_slice_t s = web_page_slice(1000, 0, 450);
    ASSERT(s.page == 1 && s.start == 0 && s.len == 450);
    s = web_page_slice(1000, -3, 450);
    ASSERT(s.page == 1);
    return 0;
}

TEST(exact_multiple_has_no_extra_page)
{
    web_page_slice_t s = web_page_slice(900, 2, 450);
    ASSERT(s.total_pages == 2 && s.len == 450 && !s.past_end);
    return 0;
}

TEST(trim_drops_leading_continuation_bytes)
{
    char buf[] = "\x80\xA9" "ab";
    size_t n = web_page_trim_utf8(buf, 4);
    ASSERT(n == 2 && memcmp(buf, "ab", 2) == 0);
    return 0;
}

TEST(trim_drops_trailing_partial_sequences)
{
    char two[] = "ab\xC3";            // lead of a 2-byte sequence, cut
    ASSERT(web_page_trim_utf8(two, 3) == 2);
    char three_a[] = "ab\xE2";        // 3-byte, only the lead
    ASSERT(web_page_trim_utf8(three_a, 3) == 2);
    char three_b[] = "ab\xE2\x82";   // 3-byte, lead + 1 continuation
    ASSERT(web_page_trim_utf8(three_b, 4) == 2);
    return 0;
}

TEST(trim_keeps_complete_trailing_sequence)
{
    char buf[] = "ab\xC3\xA9";       // e acute
    ASSERT(web_page_trim_utf8(buf, 4) == 4);
    char euro[] = "a\xE2\x82\xAC";   // euro sign
    ASSERT(web_page_trim_utf8(euro, 4) == 4);
    return 0;
}

TEST(trim_leaves_ascii_and_empty_alone)
{
    char buf[] = "hello";
    ASSERT(web_page_trim_utf8(buf, 5) == 5);
    ASSERT(web_page_trim_utf8(buf, 0) == 0);
    return 0;
}

int test_web_page_all(void)
{
    int failures = 0;

    printf("\nWeb Page Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(first_middle_and_last_page);
    RUN(past_the_end);
    RUN(empty_text_is_one_empty_page);
    RUN(page_below_one_is_page_one);
    RUN(exact_multiple_has_no_extra_page);
    RUN(trim_drops_leading_continuation_bytes);
    RUN(trim_drops_trailing_partial_sequences);
    RUN(trim_keeps_complete_trailing_sequence);
    RUN(trim_leaves_ascii_and_empty_alone);

#undef RUN
    return failures;
}
