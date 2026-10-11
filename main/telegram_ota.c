// OTA-by-Telegram: the owner sends zclaw.bin as a chat document and the
// firmware updates itself from Telegram's file API. Only reachable from
// allowlisted chats (caller enforces auth before invoking).

#include "telegram_ota.h"

#include "http_gate.h"
#include "ota.h"
#include "telegram.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

#define TG_OTA_GETFILE_TIMEOUT_MS 15000
#define TG_OTA_RESP_MAX           1024
#define TG_OTA_URL_MAX            320

static const char *TAG = "telegram_ota";

typedef struct {
    char *buf;
    size_t len;
    size_t max;
} tg_ota_resp_t;

static esp_err_t tg_ota_http_event(esp_http_client_event_t *evt)
{
    tg_ota_resp_t *ctx = (tg_ota_resp_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx && ctx->buf && evt->data_len > 0) {
        size_t space = ctx->max - 1 - ctx->len;
        size_t n = ((size_t)evt->data_len < space) ? (size_t)evt->data_len : space;
        if (n > 0) {
            memcpy(ctx->buf + ctx->len, evt->data, n);
            ctx->len += n;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

// Resolves file_id to Telegram's file_path via getFile.
static bool tg_ota_get_file_path(const char *bot_token,
                                 const char *file_id,
                                 char *out,
                                 size_t out_len)
{
    char url[TG_OTA_URL_MAX];
    static char resp[TG_OTA_RESP_MAX];
    tg_ota_resp_t ctx = { .buf = resp, .len = 0, .max = sizeof(resp) };

    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/getFile?file_id=%s",
             bot_token, file_id);

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = tg_ota_http_event,
        .user_data = &ctx,
        .timeout_ms = TG_OTA_GETFILE_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return false;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "getFile failed: err=%s status=%d", esp_err_to_name(err), status);
        return false;
    }

    bool found = false;
    cJSON *root = cJSON_Parse(resp);
    if (root) {
        cJSON *result = cJSON_GetObjectItem(root, "result");
        cJSON *file_path = result ? cJSON_GetObjectItem(result, "file_path") : NULL;
        if (file_path && cJSON_IsString(file_path) &&
            strlen(file_path->valuestring) < out_len) {
            strcpy(out, file_path->valuestring);
            found = true;
        }
        cJSON_Delete(root);
    }
    return found;
}

void telegram_ota_handle_document(const char *bot_token,
                                  const char *file_id,
                                  const char *file_name)
{
    size_t name_len = file_name ? strlen(file_name) : 0;

    if (name_len < 5 || strcmp(file_name + name_len - 4, ".bin") != 0) {
        telegram_send("Documento ignorado: para actualizar firmware envia un archivo .bin");
        return;
    }

    ESP_LOGW(TAG, "Firmware document received: %s", file_name);
    telegram_send("Firmware recibido. Descargando y aplicando OTA...");

    char file_path[160];
    char file_url[TG_OTA_URL_MAX];

    if (!http_gate_acquire("telegram_ota", pdMS_TO_TICKS(15000))) {
        telegram_send("Error OTA: HTTP ocupado, reintenta en un momento");
        return;
    }

    bool ok = tg_ota_get_file_path(bot_token, file_id, file_path, sizeof(file_path));
    if (!ok) {
        http_gate_release();
        telegram_send("Error OTA: no pude resolver el archivo en Telegram (getFile)");
        return;
    }

    snprintf(file_url, sizeof(file_url), "https://api.telegram.org/file/bot%s/%s",
             bot_token, file_path);

    esp_err_t err = ota_apply_from_url(file_url);
    http_gate_release();

    if (err != ESP_OK) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Error OTA: %s", esp_err_to_name(err));
        telegram_send(msg);
        return;
    }

    // We restart from inside the poll task, so the next getUpdates that would
    // confirm this .bin never runs: ack it now or it gets re-applied after boot.
    telegram_ack_processed_updates();
    telegram_send("OTA aplicada con exito. Reiniciando en 5 segundos...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
}
