#ifndef BACKUP_NAME_H
#define BACKUP_NAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// File name for a memory backup: "memories-YYYYMMDD-HHMM.txt" when the clock
// is set (year >= 2024), otherwise "memories-up<uptime>s.txt". False when
// buf is too small.
bool backup_name_format(char *buf, size_t len, const struct tm *tm, uint64_t uptime_s);

#endif // BACKUP_NAME_H
