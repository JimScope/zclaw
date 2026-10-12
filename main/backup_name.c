#include "backup_name.h"

#include <stdio.h>

bool backup_name_format(char *buf, size_t len, const struct tm *tm, uint64_t uptime_s)
{
    int n;

    if (!buf || len == 0) {
        return false;
    }
    if (tm && tm->tm_year + 1900 >= 2024) {
        n = snprintf(buf, len, "memories-%04d%02d%02d-%02d%02d.txt", tm->tm_year + 1900,
                     tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
    } else {
        // newlib nano printf has no %llu: uptime fits in 32 bits for 136 years.
        n = snprintf(buf, len, "memories-up%lus.txt", (unsigned long)uptime_s);
    }
    return n > 0 && (size_t)n < len;
}
