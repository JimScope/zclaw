/*
 * Host tests for the display_dashboard tool.
 */

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "mock_display.h"
#include "tools_handlers.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

static bool run(const char *json, char *result, size_t len)
{
    cJSON *input = cJSON_Parse(json);
    bool ok = tools_display_dashboard_handler(input, result, len);
    cJSON_Delete(input);
    return ok;
}

TEST(shows_clima_for_30_seconds)
{
    char result[128];
    mock_display_reset();
    mock_display_set_running(true);
    ASSERT(run("{\"name\":\"clima\"}", result, sizeof(result)));
    ASSERT(strcmp(mock_display_last_dashboard(), "clima") == 0);
    ASSERT(mock_display_last_dashboard_ms() == 30000);
    ASSERT(strstr(result, "clima") != NULL);
    return 0;
}

TEST(shows_cripto)
{
    char result[128];
    mock_display_reset();
    mock_display_set_running(true);
    ASSERT(run("{\"name\":\"cripto\"}", result, sizeof(result)));
    ASSERT(strcmp(mock_display_last_dashboard(), "cripto") == 0);
    return 0;
}

TEST(panel_shows_status_page)
{
    char result[128];
    mock_display_reset();
    mock_display_set_running(true);
    ASSERT(run("{\"name\":\"panel\"}", result, sizeof(result)));
    ASSERT(mock_display_last_status_ms() == 30000);
    ASSERT(strcmp(mock_display_last_dashboard(), "") == 0);
    return 0;
}

TEST(rejects_unknown_or_missing_name)
{
    char result[128];
    mock_display_reset();
    mock_display_set_running(true);
    ASSERT(!run("{\"name\":\"tiempo\"}", result, sizeof(result)));
    ASSERT(strncmp(result, "Error", 5) == 0);
    ASSERT(!run("{}", result, sizeof(result)));
    ASSERT(strncmp(result, "Error", 5) == 0);
    ASSERT(strcmp(mock_display_last_dashboard(), "") == 0);
    ASSERT(mock_display_last_status_ms() == 0);
    return 0;
}

TEST(reports_missing_display)
{
    char result[128];
    mock_display_reset();
    ASSERT(!run("{\"name\":\"clima\"}", result, sizeof(result)));
    ASSERT(strstr(result, "pantalla") != NULL);
    return 0;
}

int test_tools_dashboard_all(void)
{
    int failures = 0;

    printf("\nDashboard Tool Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(shows_clima_for_30_seconds);
    RUN(shows_cripto);
    RUN(panel_shows_status_page);
    RUN(rejects_unknown_or_missing_name);
    RUN(reports_missing_display);

#undef RUN
    mock_display_reset();
    return failures;
}
