#include "stt_client.h"

#include "config.h"

#if CONFIG_ZCLAW_VOICE_ENABLE

#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "http_gate.h"
#include "stt_reply.h"

#define STT_TIMEOUT_MS   15000
#define STT_GATE_WAIT_MS 20000
#define STT_REPLY_MAX    1024

static const char *TAG = "stt";

bool stt_client_configured(void)
{
    return CONFIG_ZCLAW_STT_URL[0] != '\0' && CONFIG_ZCLAW_STT_TOKEN[0] != '\0';
}

static esp_err_t post(const uint8_t *body, size_t len, char *reply, size_t reply_len, int *status)
{
    esp_http_client_config_t cfg = {
        .url = CONFIG_ZCLAW_STT_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = STT_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "X-Stt-Token", CONFIG_ZCLAW_STT_TOKEN);
    esp_http_client_set_header(client, "X-Audio-Format", "ima-adpcm; rate=16000");
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_err_t err = esp_http_client_open(client, (int)len);
    size_t sent = 0;
    while (err == ESP_OK && sent < len) {
        int n = esp_http_client_write(client, (const char *)body + sent, (int)(len - sent));
        if (n <= 0) {
            err = ESP_FAIL;
            break;
        }
        sent += (size_t)n;
    }
    if (err == ESP_OK) {
        (void)esp_http_client_fetch_headers(client);
        *status = esp_http_client_get_status_code(client);
        int total = 0;
        while (total < (int)reply_len - 1) {
            int n = esp_http_client_read(client, reply + total, (int)reply_len - 1 - total);
            if (n <= 0) {
                break;
            }
            total += n;
        }
        reply[total] = '\0';
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t stt_client_transcribe(const uint8_t *body, size_t len, char *text, size_t text_len, int *status)
{
    *status = 0;
    if (text_len > 0) {
        text[0] = '\0';
    }
    if (!stt_client_configured()) {
        return ESP_ERR_INVALID_STATE;
    }
    char *reply = malloc(STT_REPLY_MAX);
    if (!reply) {
        return ESP_ERR_NO_MEM;
    }
    reply[0] = '\0';
    if (!http_gate_acquire("stt", pdMS_TO_TICKS(STT_GATE_WAIT_MS))) {
        free(reply);
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = post(body, len, reply, STT_REPLY_MAX, status);
    http_gate_release();
    if (err == ESP_OK && *status != 200) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK && !stt_reply_parse(reply, text, text_len)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "HTTP %d, %u bytes sent: %s", *status, (unsigned)len, esp_err_to_name(err));
    free(reply);
    return err;
}

#else  // !CONFIG_ZCLAW_VOICE_ENABLE

bool stt_client_configured(void)
{
    return false;
}

esp_err_t stt_client_transcribe(const uint8_t *body, size_t len, char *text, size_t text_len, int *status)
{
    (void)body;
    (void)len;
    if (text_len > 0) {
        text[0] = '\0';
    }
    *status = 0;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
