#include "tools.h"
#include "mock_tools.h"
#include "tools_handlers.h"
#include <stdio.h>
#include <string.h>

static int s_execute_calls = 0;

void mock_tools_reset(void)
{
    s_execute_calls = 0;
}

int mock_tools_execute_calls(void)
{
    return s_execute_calls;
}

void tools_init(void)
{
}

const tool_def_t *tools_get_all(int *count)
{
    if (count) {
        *count = 0;
    }
    return NULL;
}

bool tools_execute(const char *name, const cJSON *input, char *result, size_t result_len)
{
    (void)name;
    (void)input;
    s_execute_calls++;
    if (result && result_len > 0) {
        snprintf(result, result_len, "mock tool executed");
    }
    return true;
}

void led_thinking_start(void)
{
}

void led_thinking_stop(void)
{
}

bool tools_chip_temp_handler(const cJSON *input, char *result, size_t result_len)
{
    (void)input;
    if (result && result_len > 0) {
        snprintf(result, result_len, "Chip temperature: 40.0 C");
    }
    return true;
}
