/*
 * Host tests for status page formatting helpers.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "status_format.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)
#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("  FAIL: '%s' != '%s' (line %d)\n", (a), (b), __LINE__); \
        return 1; \
    } \
} while(0)

static struct tm make_tm(int year, int mon, int mday, int wday, int hour, int min)
{
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = year - 1900;
    tm.tm_mon = mon;
    tm.tm_mday = mday;
    tm.tm_wday = wday;
    tm.tm_hour = hour;
    tm.tm_min = min;
    return tm;
}

TEST(clock_and_date_when_synced)
{
    char buf[24];
    struct tm tm = make_tm(2026, 9, 11, 0, 14, 5);
    status_format_clock(&tm, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "2:05 PM");
    tm = make_tm(2026, 9, 11, 0, 0, 0);
    status_format_clock(&tm, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "12:00 AM");
    tm = make_tm(2026, 9, 11, 0, 12, 30);
    status_format_clock(&tm, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "12:30 PM");
    tm = make_tm(2026, 9, 11, 0, 9, 7);
    status_format_clock(&tm, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "9:07 AM");
    tm = make_tm(2026, 9, 11, 0, 14, 5);
    status_format_date(&tm, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "dom 11 oct");
    tm = make_tm(2026, 0, 2, 3, 0, 0);
    status_format_date(&tm, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "mi\xC3\xA9 2 ene");
    return 0;
}

TEST(clock_and_date_before_sntp_sync)
{
    char buf[24];
    struct tm tm = make_tm(1970, 0, 1, 4, 0, 3);
    status_format_clock(&tm, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "--:--");
    status_format_date(&tm, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "--");
    return 0;
}

TEST(uptime_ranges)
{
    char buf[24];
    status_format_uptime(0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0m");
    status_format_uptime(59 * 60 + 59, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "59m");
    status_format_uptime(3600 + 3 * 60, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1h03m");
    status_format_uptime(2ULL * 86400 + 5 * 3600 + 59, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "2d 05h");
    status_format_uptime(400ULL * 86400, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "400d 00h");
    return 0;
}

TEST(kib_and_temperature)
{
    char buf[24];
    status_format_kib(150784, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "147K");
    status_format_kib(0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0K");
    status_format_temp_c(414, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "41C");
    status_format_temp_c(415, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "42C");
    status_format_temp_c(-26, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "-3C");
    return 0;
}

TEST(truncate_counts_characters_and_keeps_utf8)
{
    char buf[32];
    ASSERT(status_truncate_utf8("se\xC3\xB1" "al fuerte", 4, buf, sizeof(buf)) == 4);
    ASSERT_STR_EQ(buf, "se\xC3\xB1" "a");
    ASSERT(status_truncate_utf8("abc", 10, buf, sizeof(buf)) == 3);
    ASSERT_STR_EQ(buf, "abc");
    ASSERT(status_truncate_utf8("\xC3\xB1\xC3\xB1\xC3\xB1", 3, buf, 5) == 2);
    ASSERT_STR_EQ(buf, "\xC3\xB1\xC3\xB1");
    ASSERT(status_truncate_utf8(NULL, 3, buf, sizeof(buf)) == 0);
    ASSERT_STR_EQ(buf, "");
    return 0;
}

TEST(model_short_name)
{
    ASSERT_STR_EQ(status_model_short("nvidia/nemotron-3-super-120b-a12b"), "nemotron-3-super-120b-a12b");
    ASSERT_STR_EQ(status_model_short("gpt-4o"), "gpt-4o");
    ASSERT_STR_EQ(status_model_short(NULL), "--");
    return 0;
}

int test_status_format_all(void)
{
    int failures = 0;

    printf("\nStatus Format Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(clock_and_date_when_synced);
    RUN(clock_and_date_before_sntp_sync);
    RUN(uptime_ranges);
    RUN(kib_and_temperature);
    RUN(truncate_counts_characters_and_keeps_utf8);
    RUN(model_short_name);

#undef RUN
    return failures;
}
