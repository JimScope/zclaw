/*
 * Host tests for the packed conversation history.
 */

#include <stdio.h>
#include <string.h>

#include "history.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

static conversation_msg_t s_view[HISTORY_MAX_RECORDS];

TEST(keeps_order_and_fields)
{
    history_clear();
    history_add("user", "hola", false, false, NULL, NULL);
    history_add("assistant", "{\"pin\":2}", true, false, "call_1", "gpio_write");
    history_add("user", "ok", false, true, "call_1", NULL);
    int n = history_view(s_view, HISTORY_MAX_RECORDS);
    ASSERT(n == 3);
    ASSERT(strcmp(s_view[0].role, "user") == 0 && strcmp(s_view[0].content, "hola") == 0);
    ASSERT(strcmp(s_view[0].tool_id, "") == 0 && strcmp(s_view[0].tool_name, "") == 0);
    ASSERT(s_view[1].is_tool_use && strcmp(s_view[1].role, "assistant") == 0);
    ASSERT(strcmp(s_view[1].tool_id, "call_1") == 0 && strcmp(s_view[1].tool_name, "gpio_write") == 0);
    ASSERT(s_view[2].is_tool_result && strcmp(s_view[2].tool_id, "call_1") == 0);
    return 0;
}

TEST(caps_records_at_max)
{
    char text[16];
    history_clear();
    for (int i = 0; i < HISTORY_MAX_RECORDS + 5; i++) {
        snprintf(text, sizeof(text), "m%d", i);
        history_add("user", text, false, false, NULL, NULL);
    }
    int n = history_view(s_view, HISTORY_MAX_RECORDS);
    ASSERT(n == HISTORY_MAX_RECORDS);
    ASSERT(strcmp(s_view[0].content, "m5") == 0);
    snprintf(text, sizeof(text), "m%d", HISTORY_MAX_RECORDS + 4);
    ASSERT(strcmp(s_view[n - 1].content, text) == 0);
    return 0;
}

TEST(evicts_by_bytes_and_keeps_long_reply)
{
    static char big[MAX_MESSAGE_LEN + 200];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    history_clear();
    for (int i = 0; i < 12; i++) {
        history_add("assistant", big, false, false, NULL, NULL);  // ~1 KB each, arena is 8 KB
    }
    int n = history_view(s_view, HISTORY_MAX_RECORDS);
    ASSERT(n >= 6 && n < 12);
    ASSERT(strlen(s_view[n - 1].content) == MAX_MESSAGE_LEN - 1);  // truncated like before

    char reply[802];
    memset(reply, 'r', 801);
    reply[801] = '\0';
    history_add("assistant", reply, false, false, NULL, NULL);
    n = history_view(s_view, HISTORY_MAX_RECORDS);
    ASSERT(strlen(s_view[n - 1].content) == 801);
    return 0;
}

TEST(rollback_drops_the_turn)
{
    history_clear();
    history_add("user", "a", false, false, NULL, NULL);
    history_add("assistant", "b", false, false, NULL, NULL);
    history_mark_t mark = history_mark();
    history_add("user", "c", false, false, NULL, NULL);
    history_add("assistant", "{}", true, false, "call_9", "get_time");
    ASSERT(history_rollback(mark) == 2);
    ASSERT(history_count() == 2);
    ASSERT(history_rollback(mark) == 0);  // nothing left from that turn
    return 0;
}

TEST(rollback_after_eviction_drops_the_partial_turn)
{
    char text[16];
    history_clear();
    for (int i = 0; i < HISTORY_MAX_RECORDS; i++) {
        snprintf(text, sizeof(text), "old%d", i);
        history_add("user", text, false, false, NULL, NULL);
    }
    history_mark_t mark = history_mark();
    for (int i = 0; i < 6; i++) {  // a long tool round evicts old records and the turn start shifts
        snprintf(text, sizeof(text), "turn%d", i);
        history_add("user", text, false, false, NULL, NULL);
    }
    ASSERT(history_rollback(mark) == 6);
    int n = history_view(s_view, HISTORY_MAX_RECORDS);
    ASSERT(n == HISTORY_MAX_RECORDS - 6);
    for (int i = 0; i < n; i++) {
        ASSERT(strncmp(s_view[i].content, "old", 3) == 0);
    }
    return 0;
}

TEST(rollback_when_the_turn_evicted_its_own_records)
{
    char text[16];
    history_clear();
    for (int i = 0; i < 3; i++) {
        history_add("user", "old", false, false, NULL, NULL);
    }
    history_mark_t mark = history_mark();
    for (int i = 0; i < HISTORY_MAX_RECORDS + 4; i++) {  // the turn outgrows the whole history
        snprintf(text, sizeof(text), "turn%d", i);
        history_add("user", text, false, false, NULL, NULL);
    }
    ASSERT(history_rollback(mark) == HISTORY_MAX_RECORDS);  // everything left belongs to the turn
    ASSERT(history_count() == 0);
    return 0;
}

TEST(clear_empties)
{
    history_add("user", "x", false, false, NULL, NULL);
    history_clear();
    ASSERT(history_count() == 0);
    ASSERT(history_view(s_view, HISTORY_MAX_RECORDS) == 0);
    return 0;
}

int test_history_all(void)
{
    int failures = 0;

    printf("\nHistory Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(keeps_order_and_fields);
    RUN(caps_records_at_max);
    RUN(evicts_by_bytes_and_keeps_long_reply);
    RUN(rollback_drops_the_turn);
    RUN(rollback_after_eviction_drops_the_partial_turn);
    RUN(rollback_when_the_turn_evicted_its_own_records);
    RUN(clear_empties);

#undef RUN
    return failures;
}
