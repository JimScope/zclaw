#include "display.h"

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
}

void display_notify_wifi(bool connected)
{
    (void)connected;
}
