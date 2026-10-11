#include "display.h"
#include "mock_display.h"

#include <string.h>

static bool s_running = false;
static uint32_t s_last_status_ms = 0;
static int s_talking_calls = 0;
static char s_last_dashboard[16] = "";
static uint32_t s_last_dashboard_ms = 0;

void mock_display_reset(void)
{
    s_running = false;
    s_last_status_ms = 0;
    s_talking_calls = 0;
    s_last_dashboard[0] = '\0';
    s_last_dashboard_ms = 0;
}

void mock_display_set_running(bool running)
{
    s_running = running;
}

uint32_t mock_display_last_status_ms(void)
{
    return s_last_status_ms;
}

int mock_display_talking_calls(void)
{
    return s_talking_calls;
}

const char *mock_display_last_dashboard(void)
{
    return s_last_dashboard;
}

uint32_t mock_display_last_dashboard_ms(void)
{
    return s_last_dashboard_ms;
}

esp_err_t display_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void display_notify_thinking(bool active)
{
    (void)active;
}

void display_notify_talking(size_t reply_chars)
{
    (void)reply_chars;
    s_talking_calls++;
}

void display_notify_wifi(bool connected)
{
    (void)connected;
}

bool display_request_status(uint32_t duration_ms)
{
    if (!s_running) {
        return false;
    }
    s_last_status_ms = duration_ms;
    return true;
}

bool display_request_dashboard(const char *name, uint32_t duration_ms)
{
    if (!s_running || !name) {
        return false;
    }
    strncpy(s_last_dashboard, name, sizeof(s_last_dashboard) - 1);
    s_last_dashboard[sizeof(s_last_dashboard) - 1] = '\0';
    s_last_dashboard_ms = duration_ms;
    return true;
}
