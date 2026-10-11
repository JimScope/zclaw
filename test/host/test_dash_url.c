/*
 * Host tests for dashboard URL construction.
 */

#include <stdio.h>
#include <string.h>

#include "dash_url.h"

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

TEST(plain_dashboard)
{
    char url[128];
    ASSERT(dash_url_build(url, sizeof(url), "https://x.org/dash", "cripto", NULL));
    ASSERT_STR_EQ(url, "https://x.org/dash/cripto.jpg");
    return 0;
}

TEST(weather_adds_saved_location)
{
    char url[128];
    ASSERT(dash_url_build(url, sizeof(url), "https://x.org/dash", "clima", "lat20.02lon-75.83"));
    ASSERT_STR_EQ(url, "https://x.org/dash/clima.jpg?lat=20.02&lon=-75.83");
    return 0;
}

TEST(unparsable_location_is_ignored)
{
    char url[128];
    ASSERT(dash_url_build(url, sizeof(url), "https://x.org/dash", "clima", "Santiago"));
    ASSERT_STR_EQ(url, "https://x.org/dash/clima.jpg");
    ASSERT(dash_url_build(url, sizeof(url), "https://x.org/dash", "clima", "lat20.02"));
    ASSERT_STR_EQ(url, "https://x.org/dash/clima.jpg");
    return 0;
}

TEST(rejects_bad_names_and_overflow)
{
    char url[128];
    char tiny[16];
    ASSERT(!dash_url_build(url, sizeof(url), "https://x.org/dash", "../etc", NULL));
    ASSERT(!dash_url_build(url, sizeof(url), "https://x.org/dash", "", NULL));
    ASSERT(!dash_url_build(tiny, sizeof(tiny), "https://x.org/dash", "cripto", NULL));
    return 0;
}

int test_dash_url_all(void)
{
    int failures = 0;

    printf("\nDash URL Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(plain_dashboard);
    RUN(weather_adds_saved_location);
    RUN(unparsable_location_is_ignored);
    RUN(rejects_bad_names_and_overflow);

#undef RUN
    return failures;
}
