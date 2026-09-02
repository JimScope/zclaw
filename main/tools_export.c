// Backup tool: sends the user's own "u_*" memories (notes, location, lists)
// to the owner's allowlisted Telegram chat. System entries (API keys, tokens,
// WiFi credentials) are never included: only keys created through the
// memory_set tool, which enforces the "u_" prefix, are exported.

#include "tools_handlers.h"
#include "tools_common.h"
#include "config.h"
#include "memory.h"
#include "telegram.h"

#include "nvs.h"

#include <stdio.h>
#include <string.h>

#define EXPORT_CHUNK_SIZE 1200
#define EXPORT_VALUE_SIZE 512

#ifndef NVS_NAMESPACE
#define NVS_NAMESPACE "storage"
#endif

bool tools_memory_export_handler(const cJSON *input, char *result, size_t result_len)
{
    (void)input;

    if (!telegram_is_configured()) {
        snprintf(result, result_len, "Error: Telegram not configured");
        return false;
    }

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        snprintf(result, result_len, "No stored memories");
        return true;
    }

    static char chunk[EXPORT_CHUNK_SIZE];
    static char value[EXPORT_VALUE_SIZE];
    size_t chunk_len = snprintf(chunk, sizeof(chunk), "MEMORY BACKUP\n");
    int count = 0;
    int sent = 0;

    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find_in_handle(handle, NVS_TYPE_STR, &it);

    while (err == ESP_OK && it != NULL) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        // Whitelist: user-created memories only ("u_" prefix).
        if (strncmp(info.key, "u_", 2) == 0 &&
            memory_get(info.key, value, sizeof(value))) {
            size_t needed = strlen(info.key) + strlen(value) + 4;
            if (chunk_len + needed >= sizeof(chunk)) {
                if (telegram_send(chunk) == ESP_OK) {
                    sent++;
                }
                chunk_len = 0;
                chunk[0] = '\0';
            }
            chunk_len += snprintf(chunk + chunk_len, sizeof(chunk) - chunk_len,
                                  "%s = %s\n", info.key, value);
            count++;
        }
        err = nvs_entry_next(&it);
    }
    if (it) {
        nvs_release_iterator(it);
    }
    nvs_close(handle);

    if (chunk_len > 0 && count > 0) {
        if (telegram_send(chunk) == ESP_OK) {
            sent++;
        }
    }

    if (count == 0) {
        snprintf(result, result_len, "No user memories to export");
        return true;
    }

    snprintf(result, result_len,
             "Exported %d user memories in %d Telegram message(s).",
             count, sent);
    return true;
}
