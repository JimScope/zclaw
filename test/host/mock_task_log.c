#include "task_log.h"
#include "mock_task_log.h"

#include <stdio.h>

static int s_calls = 0;
static char s_action[300] = "";
static char s_reply[1100] = "";

void mock_task_log_reset(void)
{
    s_calls = 0;
    s_action[0] = '\0';
    s_reply[0] = '\0';
}

int mock_task_log_calls(void)
{
    return s_calls;
}

const char *mock_task_log_last_action(void)
{
    return s_action;
}

const char *mock_task_log_last_reply(void)
{
    return s_reply;
}

void task_log_append(const char *action, const char *reply)
{
    s_calls++;
    snprintf(s_action, sizeof(s_action), "%s", action ? action : "");
    snprintf(s_reply, sizeof(s_reply), "%s", reply ? reply : "");
}

int task_log_read_recent(size_t max, task_log_visit_fn visit, void *ctx)
{
    (void)max;
    (void)visit;
    (void)ctx;
    return -1;
}
