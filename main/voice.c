#include "voice.h"

// Voice disabled until the capture pipeline lands (Task 7 of the voice plan).

esp_err_t voice_start(QueueHandle_t agent_queue)
{
    (void)agent_queue;
    return ESP_ERR_NOT_SUPPORTED;
}

bool voice_request_listen(void)
{
    return false;
}

bool voice_request_probe(uint32_t duration_ms)
{
    (void)duration_ms;
    return false;
}
