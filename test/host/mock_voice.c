#include "voice.h"
#include "mock_voice.h"

static bool s_running = false;
static int s_listen_calls = 0;
static uint32_t s_probe_ms = 0;

void mock_voice_reset(void)
{
    s_running = false;
    s_listen_calls = 0;
    s_probe_ms = 0;
}

void mock_voice_set_running(bool running)
{
    s_running = running;
}

int mock_voice_listen_calls(void)
{
    return s_listen_calls;
}

uint32_t mock_voice_last_probe_ms(void)
{
    return s_probe_ms;
}

esp_err_t voice_start(QueueHandle_t agent_queue)
{
    (void)agent_queue;
    return ESP_ERR_NOT_SUPPORTED;
}

bool voice_request_listen(void)
{
    s_listen_calls++;
    return s_running;
}

bool voice_request_probe(uint32_t duration_ms)
{
    if (!s_running) {
        return false;
    }
    s_probe_ms = duration_ms;
    return true;
}
