/*
 * Host tests for the task log record format.
 */

#include <stdio.h>
#include <string.h>

#include "task_log_format.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

static char s_line[TASK_LOG_LINE_MAX];

TEST(formats_time_action_and_reply)
{
    size_t n = task_log_format_line(s_line, sizeof(s_line), 1760216400u, "dime el clima", "Soleado, 31 C.");
    ASSERT(strcmp(s_line, "1760216400\tdime el clima\tSoleado, 31 C.\n") == 0);
    ASSERT(n == strlen(s_line));
    return 0;
}

TEST(escapes_newline_tab_and_backslash)
{
    task_log_format_line(s_line, sizeof(s_line), 7, "a\tb", "uno\ndos\\tres\r\n");
    ASSERT(strcmp(s_line, "7\ta\\tb\tuno\\ndos\\\\tres\\n\n") == 0);
    return 0;
}

TEST(clips_fields_without_splitting_utf8)
{
    static char action[300];
    static char reply[1100];
    memset(action, 'a', 299);
    action[299] = '\0';
    memset(reply, 'b', 1023);
    memcpy(reply + 1023, "\xC3\xB1", 3);  /* 1025 bytes: the cut at 1024 would split the n-tilde */
    task_log_format_line(s_line, sizeof(s_line), 1, action, reply);
    const char *tab1 = strchr(s_line, '\t');
    ASSERT(tab1 != NULL);
    const char *tab2 = strchr(tab1 + 1, '\t');
    ASSERT(tab2 != NULL);
    ASSERT(tab2 - tab1 - 1 == 256);
    ASSERT(strlen(tab2 + 1) == 1023 + 1);  /* 1023 'b' + '\n' */
    return 0;
}

TEST(null_fields_are_empty)
{
    task_log_format_line(s_line, sizeof(s_line), 5, NULL, NULL);
    ASSERT(strcmp(s_line, "5\t\t\n") == 0);
    return 0;
}

TEST(rejects_small_buffer)
{
    char small[64];
    ASSERT(task_log_format_line(small, sizeof(small), 1, "a", "b") == 0);
    ASSERT(small[0] == '\0');
    return 0;
}

int test_task_log_format_all(void)
{
    int failures = 0;

    printf("\nTask Log Format Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(formats_time_action_and_reply);
    RUN(escapes_newline_tab_and_backslash);
    RUN(clips_fields_without_splitting_utf8);
    RUN(null_fields_are_empty);
    RUN(rejects_small_buffer);

#undef RUN
    return failures;
}
