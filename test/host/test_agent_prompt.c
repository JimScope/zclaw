/*
 * Host tests for system prompt composition.
 */

#include <stdio.h>
#include <string.h>

#include "agent_prompt.h"
#include "mock_display.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

// With the screen hint the full prompt must fit the agent's buffer whole:
// the overflow fallback drops the pin policy and "keep it short".
TEST(full_prompt_fits_agent_buffer_with_display)
{
    static char buf[AGENT_SYSTEM_PROMPT_BUF_SIZE];

    mock_display_reset();
    mock_display_set_running(true);
    const char *prompt = agent_build_system_prompt(buf, sizeof(buf));
    ASSERT(prompt == buf);
    ASSERT(strstr(prompt, "Device target is") != NULL);
    ASSERT(strstr(prompt, "Keep responses short") != NULL);
    ASSERT(strstr(prompt, "display_image") != NULL);
    ASSERT(strstr(prompt, "Persona mode") == NULL);
    mock_display_reset();
    return 0;
}

int test_agent_prompt_all(void)
{
    int failures = 0;

    printf("\nAgent Prompt Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(full_prompt_fits_agent_buffer_with_display);

#undef RUN
    return failures;
}
