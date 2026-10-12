#ifndef TASK_LOG_FORMAT_H
#define TASK_LOG_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#define TASK_LOG_ACTION_MAX 256   // bytes of the cron action kept
#define TASK_LOG_REPLY_MAX  1024  // bytes of the reply kept
// Worst case: 10-digit time, two tabs, every byte escaped, '\n' and NUL.
#define TASK_LOG_LINE_MAX   (10 + 1 + 2 * TASK_LOG_ACTION_MAX + 1 + 2 * TASK_LOG_REPLY_MAX + 2)

// One record: "<unix seconds>\t<action>\t<reply>\n". Inside the fields a
// backslash, newline and tab become "\\", "\n" and "\t" (two characters)
// and '\r' is dropped, so a record is always one line. Fields are clipped,
// never inside a UTF-8 character, to TASK_LOG_ACTION_MAX / TASK_LOG_REPLY_MAX
// bytes. Returns the line length, or 0 (empty string) when out_len is
// smaller than TASK_LOG_LINE_MAX.
size_t task_log_format_line(char *out, size_t out_len, uint32_t when, const char *action,
                            const char *reply);

#endif // TASK_LOG_FORMAT_H
