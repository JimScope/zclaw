/*
 * Host tests for the UTF-8 to Latin-1 decoder and the generated 8x16 font.
 */

#include <stdio.h>
#include <string.h>

#include "font8x16.h"
#include "text_latin1.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(ascii_passes_through)
{
    const char *p = "Hi";
    ASSERT(text_latin1_next(&p) == 'H');
    ASSERT(text_latin1_next(&p) == 'i');
    ASSERT(text_latin1_next(&p) == 0);
    ASSERT(text_latin1_next(&p) == 0);
    return 0;
}

TEST(spanish_letters_decode_to_latin1)
{
    const char *p = "\xC3\xB1\xC3\xA1\xC2\xBF\xC2\xA1";  /* ñ á ¿ ¡ */
    ASSERT(text_latin1_next(&p) == 0xF1);
    ASSERT(text_latin1_next(&p) == 0xE1);
    ASSERT(text_latin1_next(&p) == 0xBF);
    ASSERT(text_latin1_next(&p) == 0xA1);
    ASSERT(text_latin1_next(&p) == 0);
    return 0;
}

TEST(emoji_and_non_latin_become_question_mark)
{
    const char *p = "a\xF0\x9F\x98\x80" "b\xE2\x82\xAC" "c";  /* a 😀 b € c */
    ASSERT(text_latin1_next(&p) == 'a');
    ASSERT(text_latin1_next(&p) == '?');
    ASSERT(text_latin1_next(&p) == 'b');
    ASSERT(text_latin1_next(&p) == '?');
    ASSERT(text_latin1_next(&p) == 'c');
    ASSERT(text_latin1_next(&p) == 0);
    return 0;
}

TEST(truncated_sequences_stop_at_terminator)
{
    char buf[4] = { 'x', (char)0xC3, '\0', 'Z' };
    const char *p = buf;
    ASSERT(text_latin1_next(&p) == 'x');
    ASSERT(text_latin1_next(&p) == '?');
    ASSERT(*p == '\0');
    ASSERT(text_latin1_next(&p) == 0);

    char buf3[4] = { (char)0xE2, (char)0x82, '\0', 'Z' };
    p = buf3;
    ASSERT(text_latin1_next(&p) == '?');
    ASSERT(text_latin1_next(&p) == 0);
    return 0;
}

TEST(control_and_c1_characters_become_question_mark)
{
    const char *p = "\t\xC2\x85";
    ASSERT(text_latin1_next(&p) == '?');
    ASSERT(text_latin1_next(&p) == '?');
    ASSERT(text_latin1_next(&p) == 0);
    return 0;
}

TEST(length_counts_characters_not_bytes)
{
    ASSERT(text_latin1_length("") == 0);
    ASSERT(text_latin1_length(NULL) == 0);
    ASSERT(text_latin1_length("se\xC3\xB1" "al") == 5);
    ASSERT(text_latin1_length("\xF0\x9F\x98\x80") == 1);
    return 0;
}

TEST(font_has_expected_glyphs)
{
    static const uint8_t glyph_a[16] = {
        0x00, 0x00, 0x00, 0x00, 0x18, 0x24, 0x24, 0x42,
        0x42, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00,
    };
    static const uint8_t glyph_enye[16] = {
        0x00, 0x00, 0x32, 0x4C, 0x00, 0x00, 0x5C, 0x62,
        0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00,
    };
    static const uint8_t blank[16] = {0};
    ASSERT(memcmp(font8x16_latin1['A'], glyph_a, 16) == 0);
    ASSERT(memcmp(font8x16_latin1[0xF1], glyph_enye, 16) == 0);
    ASSERT(memcmp(font8x16_latin1[0x01], blank, 16) == 0);
    ASSERT(memcmp(font8x16_latin1[0x85], blank, 16) == 0);
    return 0;
}

int test_text_latin1_all(void)
{
    int failures = 0;

    printf("\nText Latin-1 Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(ascii_passes_through);
    RUN(spanish_letters_decode_to_latin1);
    RUN(emoji_and_non_latin_become_question_mark);
    RUN(truncated_sequences_stop_at_terminator);
    RUN(control_and_c1_characters_become_question_mark);
    RUN(length_counts_characters_not_bytes);
    RUN(font_has_expected_glyphs);

#undef RUN
    return failures;
}
