/*
 * Host tests for the answer page text layout.
 */

#include <stdio.h>
#include <string.h>

#include "text_layout.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

static const char *line_text(const char *text, const text_line_t *line)
{
    static char buf[160];
    text_layout_line_copy(text, line, buf, sizeof(buf));
    return buf;
}

TEST(wraps_at_spaces)
{
    const char *t = "el veloz murcielago hindu comia feliz";
    text_line_t l[8];
    ASSERT(text_layout_wrap(t, 10, l, 8) == 5);
    ASSERT(strcmp(line_text(t, &l[0]), "el veloz") == 0);
    ASSERT(strcmp(line_text(t, &l[1]), "murcielago") == 0);
    ASSERT(strcmp(line_text(t, &l[2]), "hindu") == 0);
    ASSERT(strcmp(line_text(t, &l[3]), "comia") == 0);
    ASSERT(strcmp(line_text(t, &l[4]), "feliz") == 0);
    ASSERT(!l[4].ellipsis);
    return 0;
}

TEST(accented_letter_is_one_column)
{
    const char *t = "canci\xC3\xB3n peque\xC3\xB1" "a";
    text_line_t l[4];
    ASSERT(text_layout_wrap(t, 7, l, 4) == 2);
    ASSERT(l[0].chars == 7 && l[0].len == 8);
    ASSERT(strcmp(line_text(t, &l[1]), "peque\xC3\xB1" "a") == 0);
    return 0;
}

TEST(splits_word_longer_than_line)
{
    const char *t = "abcdefghij";
    text_line_t l[4];
    ASSERT(text_layout_wrap(t, 4, l, 4) == 3);
    ASSERT(strcmp(line_text(t, &l[0]), "abcd") == 0);
    ASSERT(strcmp(line_text(t, &l[1]), "efgh") == 0);
    ASSERT(strcmp(line_text(t, &l[2]), "ij") == 0);
    return 0;
}

TEST(newline_starts_line_and_trailing_newlines_dropped)
{
    const char *t = "uno\n\ndos\n\n";
    text_line_t l[8];
    ASSERT(text_layout_wrap(t, 10, l, 8) == 3);
    ASSERT(strcmp(line_text(t, &l[0]), "uno") == 0);
    ASSERT(l[1].chars == 0);
    ASSERT(strcmp(line_text(t, &l[2]), "dos") == 0);
    return 0;
}

TEST(break_at_end_of_line_does_not_add_blank_line)
{
    const char *t = "abcd \nefg";
    text_line_t l[4];
    ASSERT(text_layout_wrap(t, 4, l, 4) == 2);
    ASSERT(strcmp(line_text(t, &l[0]), "abcd") == 0);
    ASSERT(strcmp(line_text(t, &l[1]), "efg") == 0);
    return 0;
}

TEST(control_characters_take_no_column)
{
    const char *t = "a\rb\x01" "c";
    const char *tab = "x\ty";
    text_line_t l[2];
    ASSERT(text_layout_wrap(t, 3, l, 2) == 1);
    ASSERT(l[0].chars == 3);
    ASSERT(strcmp(line_text(t, &l[0]), "abc") == 0);
    ASSERT(text_layout_wrap(tab, 3, l, 2) == 1);
    ASSERT(strcmp(line_text(tab, &l[0]), "x y") == 0);
    return 0;
}

TEST(emoji_is_one_column_and_never_split)
{
    const char *t = "ab\xF0\x9F\x98\x80" "cd";
    text_line_t l[4];
    ASSERT(text_layout_wrap(t, 3, l, 4) == 2);
    ASSERT(l[0].chars == 3 && l[0].len == 6);
    ASSERT(strcmp(line_text(t, &l[1]), "cd") == 0);
    return 0;
}

TEST(empty_text_has_no_lines)
{
    text_line_t l[2];
    ASSERT(text_layout_wrap("", 29, l, 2) == 0);
    ASSERT(text_layout_wrap("\n\n", 29, l, 2) == 0);
    return 0;
}

TEST(too_many_lines_end_with_ellipsis)
{
    char t[256];
    text_line_t l[66];
    size_t n = 0;
    for (int i = 0; i < 100; i++) {
        t[n++] = 'a';
        t[n++] = '\n';
    }
    t[n] = '\0';
    ASSERT(text_layout_wrap(t, 29, l, 66) == 66);
    ASSERT(!l[64].ellipsis);
    ASSERT(l[65].ellipsis);
    ASSERT(strcmp(line_text(t, &l[65]), "a...") == 0);
    return 0;
}

TEST(header_clipped_to_two_lines)
{
    const char *t = "T\xC3\xBA: cu\xC3\xA9ntame con todo detalle la historia completa de "
                    "la isla desde la llegada de Col\xC3\xB3n hasta hoy";
    text_line_t l[2];
    ASSERT(text_layout_wrap(t, 29, l, 2) == 2);
    ASSERT(!l[0].ellipsis);
    ASSERT(l[1].ellipsis);
    ASSERT(l[1].chars + TEXT_LAYOUT_ELLIPSIS_COLS <= 29);
    const char *last = line_text(t, &l[1]);
    ASSERT(strcmp(last + strlen(last) - 3, "...") == 0);
    return 0;
}

TEST(text_that_fits_exactly_has_no_ellipsis)
{
    text_line_t l[2];
    ASSERT(text_layout_wrap("abcd efgh", 4, l, 2) == 2);
    ASSERT(!l[1].ellipsis);
    ASSERT(text_layout_wrap("abcd efgh  \n\n", 4, l, 2) == 2);
    ASSERT(!l[1].ellipsis);
    return 0;
}

TEST(clip_utf8_keeps_short_text)
{
    char out[16];
    ASSERT(text_layout_clip_utf8("hola", 10, out, sizeof(out)) == 4);
    ASSERT(strcmp(out, "hola") == 0);
    return 0;
}

TEST(clip_utf8_cuts_with_ellipsis)
{
    char out[16];
    ASSERT(text_layout_clip_utf8("abcdefghij", 8, out, sizeof(out)) == 8);
    ASSERT(strcmp(out, "abcde...") == 0);
    return 0;
}

TEST(clip_utf8_never_splits_character)
{
    char out[16];
    /* the cut at byte 6 would split the two-byte n-tilde at bytes 5-6 */
    ASSERT(text_layout_clip_utf8("aaaaa\xC3\xB1" "bbbb", 9, out, sizeof(out)) == 8);
    ASSERT(strcmp(out, "aaaaa...") == 0);
    return 0;
}

TEST(page_count)
{
    ASSERT(text_layout_page_count(0, 11) == 1);
    ASSERT(text_layout_page_count(11, 11) == 1);
    ASSERT(text_layout_page_count(12, 11) == 2);
    return 0;
}

static void fill_lines(text_line_t *l, size_t n, uint8_t chars)
{
    for (size_t i = 0; i < n; i++) {
        l[i].offset = 0;
        l[i].len = chars;
        l[i].chars = chars;
        l[i].ellipsis = false;
    }
}

TEST(short_reply_gets_minimum_time)
{
    text_line_t l[2];
    uint32_t ms[6];
    fill_lines(l, 2, 25);
    ASSERT(text_layout_durations(l, 2, 11, ms, 6) == 8000);
    ASSERT(ms[0] == 8000);
    return 0;
}

TEST(one_page_reply_gets_70ms_per_char)
{
    text_line_t l[11];
    uint32_t ms[6];
    fill_lines(l, 11, 29);
    ASSERT(text_layout_durations(l, 11, 11, ms, 6) == 22330);
    return 0;
}

TEST(long_reply_capped_and_split_evenly)
{
    text_line_t l[33];
    uint32_t ms[6];
    fill_lines(l, 33, 29);
    ASSERT(text_layout_durations(l, 33, 11, ms, 6) == 45000);
    ASSERT(ms[0] == 15000 && ms[1] == 15000 && ms[2] == 15000);
    return 0;
}

TEST(short_last_page_gets_three_seconds)
{
    text_line_t l[12];
    uint32_t ms[6];
    fill_lines(l, 11, 29);
    fill_lines(&l[11], 1, 5);
    ASSERT(text_layout_durations(l, 12, 11, ms, 6) == 25330);
    ASSERT(ms[0] == 22330);
    ASSERT(ms[1] == 3000);
    return 0;
}

TEST(page_at_follows_durations)
{
    const uint32_t ms[2] = {5000, 3000};
    ASSERT(text_layout_page_at(ms, 2, 0) == 0);
    ASSERT(text_layout_page_at(ms, 2, 4999) == 0);
    ASSERT(text_layout_page_at(ms, 2, 5000) == 1);
    ASSERT(text_layout_page_at(ms, 2, 7999) == 1);
    ASSERT(text_layout_page_at(ms, 2, 100000) == 1);
    return 0;
}

int test_text_layout_all(void)
{
    int failures = 0;

    printf("\nText Layout Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(wraps_at_spaces);
    RUN(accented_letter_is_one_column);
    RUN(splits_word_longer_than_line);
    RUN(newline_starts_line_and_trailing_newlines_dropped);
    RUN(break_at_end_of_line_does_not_add_blank_line);
    RUN(control_characters_take_no_column);
    RUN(emoji_is_one_column_and_never_split);
    RUN(empty_text_has_no_lines);
    RUN(too_many_lines_end_with_ellipsis);
    RUN(header_clipped_to_two_lines);
    RUN(text_that_fits_exactly_has_no_ellipsis);
    RUN(clip_utf8_keeps_short_text);
    RUN(clip_utf8_cuts_with_ellipsis);
    RUN(clip_utf8_never_splits_character);
    RUN(page_count);
    RUN(short_reply_gets_minimum_time);
    RUN(one_page_reply_gets_70ms_per_char);
    RUN(long_reply_capped_and_split_evenly);
    RUN(short_last_page_gets_three_seconds);
    RUN(page_at_follows_durations);

#undef RUN
    return failures;
}
