/*
 * Host tests for memory backup file names.
 */

#include <stdio.h>
#include <string.h>

#include "backup_name.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(uses_date_when_clock_is_set)
{
    struct tm tm;
    char buf[48];
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 9;
    tm.tm_mday = 11;
    tm.tm_hour = 20;
    tm.tm_min = 5;
    ASSERT(backup_name_format(buf, sizeof(buf), &tm, 99));
    ASSERT(strcmp(buf, "memories-20261011-2005.txt") == 0);
    return 0;
}

TEST(uses_uptime_before_clock_sync)
{
    struct tm tm;
    char buf[48];
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 70;
    ASSERT(backup_name_format(buf, sizeof(buf), &tm, 1234));
    ASSERT(strcmp(buf, "memories-up1234s.txt") == 0);
    ASSERT(backup_name_format(buf, sizeof(buf), NULL, 7));
    ASSERT(strcmp(buf, "memories-up7s.txt") == 0);
    return 0;
}

TEST(rejects_small_buffer)
{
    char buf[10];
    ASSERT(!backup_name_format(buf, sizeof(buf), NULL, 7));
    return 0;
}

int test_backup_name_all(void)
{
    int failures = 0;

    printf("\nBackup Name Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(uses_date_when_clock_is_set);
    RUN(uses_uptime_before_clock_sync);
    RUN(rejects_small_buffer);

#undef RUN
    return failures;
}
