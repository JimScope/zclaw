#include "display.h"
#include "mock_display.h"

static bool s_running = false;
static uint32_t s_last_status_ms = 0;
static int s_talking_calls = 0;

void mock_display_reset(void)
{
    s_running = false;
    s_last_status_ms = 0;
    s_talking_calls = 0;
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
