#include "tools_handlers.h"
#include "tools_common.h"
#include "config.h"
#include "http_gate.h"
#include "telegram.h"

#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

#define OTA_HTTP_TIMEOUT_MS  30000
#define OTA_RESTART_DELAY_MS 25000

static const char *TAG = "tools_ota";

static void ota_restart_task(void *arg)
{
    (void)arg;
    // Give the agent time to deliver its final reply before rebooting.
    vTaskDelay(pdMS_TO_TICKS(OTA_RESTART_DELAY_MS));
    // Make sure the message that asked for the update is not redelivered after boot.
    telegram_ack_processed_updates();
    ESP_LOGW(TAG, "Restarting into new firmware");
    esp_restart();
}

bool tools_ota_update_handler(const cJSON *input, char *result, size_t result_len)
{
    const cJSON *url = cJSON_GetObjectItem(input, "url");

    if (!url || !cJSON_IsString(url) ||
        (strncmp(url->valuestring, "http://", 7) != 0 &&
         strncmp(url->valuestring, "https://", 8) != 0)) {
        snprintf(result, result_len, "Error: 'url' required (http/https to zclaw.bin)");
        return false;
    }

    // Hold the gate for the whole download to avoid TLS heap contention.
    if (!http_gate_acquire("ota_update", pdMS_TO_TICKS(15000))) {
        snprintf(result, result_len, "Error: HTTP busy, try again");
        return false;
    }

    ESP_LOGW(TAG, "OTA update from %s", url->valuestring);
    esp_http_client_config_t http_cfg = {
        .url = url->valuestring,
        .timeout_ms = OTA_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t err = esp_https_ota(&ota_cfg);
    http_gate_release();

    if (err != ESP_OK) {
        snprintf(result, result_len, "Error: OTA failed (%s)", esp_err_to_name(err));
        return false;
    }

    if (xTaskCreate(ota_restart_task, "ota_restart", 2048, NULL, 5, NULL) != pdPASS) {
        snprintf(result, result_len,
                 "OTA written but restart task failed; reboot manually to apply");
        return true;
    }

    snprintf(result, result_len,
             "OTA update downloaded OK; device restarts in ~%d seconds",
             OTA_RESTART_DELAY_MS / 1000);
    return true;
}
