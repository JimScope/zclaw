/*
 * Host tests for agent command helpers.
 */

#include <stdio.h>
#include <string.h>

#include "agent_commands.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(cron_trigger_body_strips_prefix)
{
    ASSERT(strcmp(agent_cron_trigger_body("[CRON 12] regar las plantas"), "regar las plantas") == 0);
    ASSERT(strcmp(agent_cron_trigger_body("  [CRON 1]   hola"), "hola") == 0);
    return 0;
}

TEST(cron_trigger_body_keeps_other_text)
{
    ASSERT(strcmp(agent_cron_trigger_body("hola"), "hola") == 0);
    ASSERT(strcmp(agent_cron_trigger_body("[CRON 5"), "[CRON 5") == 0);
    ASSERT(agent_cron_trigger_body(NULL) == NULL);
    return 0;
}

int test_agent_commands_all(void)
{
    int failures = 0;

    printf("\nAgent Command Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(cron_trigger_body_strips_prefix);
    RUN(cron_trigger_body_keeps_other_text);

#undef RUN
    return failures;
}
