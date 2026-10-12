#ifndef TASK_LOG_H
#define TASK_LOG_H

#include <stdbool.h>
#include <stddef.h>

#define TASK_LOG_RECENT 20

// Append a scheduled task's reply to /sdcard/log/tasks.log. No-op without a
// mounted SD card (or when CONFIG_ZCLAW_SD_ENABLE is off).
void task_log_append(const char *action, const char *reply);

// Calls visit(line) for up to `max` (<= TASK_LOG_RECENT) most recent records,
// newest first, each without its '\n'; stops early when visit returns false.
// Returns how many were visited, or -1 without SD card or log file.
typedef bool (*task_log_visit_fn)(const char *line, void *ctx);
int task_log_read_recent(size_t max, task_log_visit_fn visit, void *ctx);

#endif // TASK_LOG_H
