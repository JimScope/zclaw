// Backup tool: exports the user's own "u_*" memories (notes, location,
// lists). With Telegram configured they go to the owner's allowlisted chat;
// otherwise to a text file on the SD card. System entries (API keys, tokens,
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

#if CONFIG_ZCLAW_SD_ENABLE
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

#include "backup_name.h"
#include "esp_timer.h"
#include "sd_card.h"
#endif

#define EXPORT_CHUNK_SIZE 1200
#define EXPORT_VALUE_SIZE 512

#ifndef NVS_NAMESPACE
#define NVS_NAMESPACE "storage"
#endif

typedef void (*export_emit_fn)(const char *key, const char *value, void *ctx);

// Calls emit for every user memory. Returns how many, or -1 without NVS.
static int for_each_user_memory(export_emit_fn emit, void *ctx)
{
    static char value[EXPORT_VALUE_SIZE];
    nvs_handle_t handle;
    int count = 0;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return -1;
    }
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find_in_handle(handle, NVS_TYPE_STR, &it);
    while (err == ESP_OK && it != NULL) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        // Whitelist: user-created memories only ("u_" prefix).
        if (strncmp(info.key, "u_", 2) == 0 && memory_get(info.key, value, sizeof(value))) {
            emit(info.key, value, ctx);
            count++;
        }
        err = nvs_entry_next(&it);
    }
    if (it) {
        nvs_release_iterator(it);
    }
    nvs_close(handle);
    return count;
}

typedef struct {
    char chunk[EXPORT_CHUNK_SIZE];
    size_t len;
    int sent;
} telegram_export_t;

static void telegram_flush(telegram_export_t *t)
{
    if (t->len > 0 && telegram_send(t->chunk) == ESP_OK) {
        t->sent++;
    }
    t->len = 0;
    t->chunk[0] = '\0';
}

static void telegram_emit(const char *key, const char *value, void *ctx)
{
    telegram_export_t *t = ctx;
    size_t needed = strlen(key) + strlen(value) + 4;

    if (t->len + needed >= sizeof(t->chunk)) {
        telegram_flush(t);
    }
    t->len += (size_t)snprintf(t->chunk + t->len, sizeof(t->chunk) - t->len, "%s = %s\n", key, value);
}

static bool export_to_telegram(char *result, size_t result_len)
{
    static telegram_export_t t;

    t.len = (size_t)snprintf(t.chunk, sizeof(t.chunk), "MEMORY BACKUP\n");
    t.sent = 0;
    int count = for_each_user_memory(telegram_emit, &t);
    if (count < 0) {
        snprintf(result, result_len, "No stored memories");
        return true;
    }
    if (count == 0) {
        snprintf(result, result_len, "No user memories to export");
        return true;
    }
    telegram_flush(&t);
    snprintf(result, result_len, "Exported %d user memories in %d Telegram message(s).", count, t.sent);
    return true;
}

#if CONFIG_ZCLAW_SD_ENABLE
static void file_emit(const char *key, const char *value, void *ctx)
{
    fprintf((FILE *)ctx, "%s = %s\n", key, value);
}

static bool export_to_sd(char *result, size_t result_len)
{
    char name[48];
    char path[96];
    time_t now = time(NULL);
    struct tm tm;

    if (!sd_card_is_mounted()) {
        snprintf(result, result_len, "Error: no SD card mounted (and Telegram is not configured)");
        return false;
    }
    if (mkdir(SD_MOUNT_POINT "/backup", 0775) != 0 && errno != EEXIST) {
        snprintf(result, result_len, "Error: cannot create " SD_MOUNT_POINT "/backup");
        return false;
    }
    localtime_r(&now, &tm);
    if (!backup_name_format(name, sizeof(name), &tm, (uint64_t)(esp_timer_get_time() / 1000000))) {
        snprintf(result, result_len, "Error: backup file name too long");
        return false;
    }
    snprintf(path, sizeof(path), SD_MOUNT_POINT "/backup/%s", name);
    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(result, result_len, "Error: cannot write %s", path);
        return false;
    }
    fputs("MEMORY BACKUP\n", f);
    int count = for_each_user_memory(file_emit, f);
    if (fclose(f) != 0) {
        snprintf(result, result_len, "Error: writing %s failed", path);
        return false;
    }
    if (count <= 0) {
        remove(path);
        snprintf(result, result_len, "No user memories to export");
        return true;
    }
    snprintf(result, result_len, "Exported %d user memories to %s", count, path);
    return true;
}
#else
static bool export_to_sd(char *result, size_t result_len)
{
    snprintf(result, result_len, "Error: Telegram not configured and this build has no SD card");
    return false;
}
#endif

bool tools_memory_export_handler(const cJSON *input, char *result, size_t result_len)
{
    (void)input;

    if (telegram_is_configured()) {
        return export_to_telegram(result, result_len);
    }
    return export_to_sd(result, result_len);
}
