#include "task_log.h"

#include "config.h"

#if CONFIG_ZCLAW_SD_ENABLE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "esp_log.h"
#include "sd_card.h"
#include "task_log_format.h"

#define TASK_LOG_DIR          SD_MOUNT_POINT "/log"
#define TASK_LOG_FILE         TASK_LOG_DIR "/tasks.log"
#define TASK_LOG_OLD_FILE     TASK_LOG_DIR "/tasks.old.log"
#define TASK_LOG_ROTATE_BYTES (64 * 1024)
#define TASK_LOG_TAIL_BYTES   ((TASK_LOG_RECENT + 1) * TASK_LOG_LINE_MAX)

static const char *TAG = "task_log";

void task_log_append(const char *action, const char *reply)
{
    struct stat st;

    if (!sd_card_is_mounted()) {
        return;
    }
    if (mkdir(TASK_LOG_DIR, 0775) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "Cannot create %s", TASK_LOG_DIR);
        return;
    }
    if (stat(TASK_LOG_FILE, &st) == 0 && st.st_size > TASK_LOG_ROTATE_BYTES) {
        remove(TASK_LOG_OLD_FILE);
        rename(TASK_LOG_FILE, TASK_LOG_OLD_FILE);
    }
    char *line = malloc(TASK_LOG_LINE_MAX);
    if (!line) {
        return;
    }
    size_t len = task_log_format_line(line, TASK_LOG_LINE_MAX, (uint32_t)time(NULL), action, reply);
    FILE *f = fopen(TASK_LOG_FILE, "a");
    if (!f || fwrite(line, 1, len, f) != len) {
        ESP_LOGW(TAG, "Cannot write %s", TASK_LOG_FILE);
    }
    if (f) {
        fclose(f);
    }
    free(line);
}

int task_log_read_recent(size_t max, task_log_visit_fn visit, void *ctx)
{
    long starts[TASK_LOG_RECENT];
    size_t count = 0;
    size_t head = 0;
    struct stat st;

    if (!visit || max == 0 || !sd_card_is_mounted() || stat(TASK_LOG_FILE, &st) != 0) {
        return -1;
    }
    if (max > TASK_LOG_RECENT) {
        max = TASK_LOG_RECENT;
    }
    FILE *f = fopen(TASK_LOG_FILE, "r");
    char *line = malloc(TASK_LOG_LINE_MAX);
    if (!f || !line) {
        if (f) {
            fclose(f);
        }
        free(line);
        return -1;
    }
    long pos = st.st_size > TASK_LOG_TAIL_BYTES ? (long)(st.st_size - TASK_LOG_TAIL_BYTES) : 0;
    if (pos > 0 && fseek(f, pos, SEEK_SET) == 0 && fgets(line, TASK_LOG_LINE_MAX, f)) {
        pos = ftell(f);  // skip the partial first line
    }
    // Ring with the start offsets of the last `max` lines.
    while (fgets(line, TASK_LOG_LINE_MAX, f)) {
        starts[head] = pos;
        head = (head + 1) % max;
        if (count < max) {
            count++;
        }
        pos = ftell(f);
    }
    size_t visited = 0;
    for (size_t i = 0; i < count; i++) {
        size_t idx = (head + max - 1 - i) % max;
        if (fseek(f, starts[idx], SEEK_SET) != 0 || !fgets(line, TASK_LOG_LINE_MAX, f)) {
            break;
        }
        line[strcspn(line, "\n")] = '\0';
        visited++;
        if (!visit(line, ctx)) {
            break;
        }
    }
    fclose(f);
    free(line);
    return (int)visited;
}

#else  // !CONFIG_ZCLAW_SD_ENABLE

void task_log_append(const char *action, const char *reply)
{
    (void)action;
    (void)reply;
}

int task_log_read_recent(size_t max, task_log_visit_fn visit, void *ctx)
{
    (void)max;
    (void)visit;
    (void)ctx;
    return -1;
}

#endif
