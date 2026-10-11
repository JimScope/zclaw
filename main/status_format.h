#ifndef STATUS_FORMAT_H
#define STATUS_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

// 12-hour clock ("2:05 PM", "12:00 AM"), or "--:--" while not synced (year < 2024).
void status_format_clock(const struct tm *tm, char *buf, size_t len);
// Spanish short date, e.g. "dom 11 oct" (UTF-8), or "--" while not synced.
void status_format_date(const struct tm *tm, char *buf, size_t len);
// "45m", "1h03m", "2d 05h".
void status_format_uptime(uint64_t seconds, char *buf, size_t len);
// Bytes as whole KiB, e.g. "147K".
void status_format_kib(uint32_t bytes, char *buf, size_t len);
// Tenths of a degree, rounded half away from zero, e.g. 415 -> "42C".
void status_format_temp_c(int tenths, char *buf, size_t len);
// Copy at most max_chars characters (UTF-8 kept intact) into buf. Returns
// the number of characters copied.
size_t status_truncate_utf8(const char *utf8, size_t max_chars, char *buf, size_t len);
// Model id without the provider prefix ("nvidia/x" -> "x"); "--" for NULL.
const char *status_model_short(const char *model);

#endif // STATUS_FORMAT_H
