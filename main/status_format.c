#include "status_format.h"

#include <stdio.h>
#include <string.h>

#include "text_latin1.h"

// UTF-8 as octal escapes ("mié", "sáb"): octal stops after 3 digits, unlike \x.
static const char *const s_days[7] = {
    "dom", "lun", "mar", "mi\303\251", "jue", "vie", "s\303\241b",
};
static const char *const s_months[12] = {
    "ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic",
};

static int clock_synced(const struct tm *tm)
{
    return tm && tm->tm_year + 1900 >= 2024;
}

void status_format_clock(const struct tm *tm, char *buf, size_t len)
{
    if (!clock_synced(tm)) {
        snprintf(buf, len, "--:--");
        return;
    }
    snprintf(buf, len, "%02d:%02d", tm->tm_hour, tm->tm_min);
}

void status_format_date(const struct tm *tm, char *buf, size_t len)
{
    if (!clock_synced(tm) || tm->tm_wday < 0 || tm->tm_wday > 6 ||
        tm->tm_mon < 0 || tm->tm_mon > 11) {
        snprintf(buf, len, "--");
        return;
    }
    snprintf(buf, len, "%s %d %s", s_days[tm->tm_wday], tm->tm_mday, s_months[tm->tm_mon]);
}

void status_format_uptime(uint64_t seconds, char *buf, size_t len)
{
    unsigned long minutes = (unsigned long)((seconds / 60) % 60);
    unsigned long hours = (unsigned long)((seconds / 3600) % 24);
    unsigned long days = (unsigned long)(seconds / 86400);

    if (seconds < 3600) {
        snprintf(buf, len, "%lum", (unsigned long)(seconds / 60));
    } else if (seconds < 86400) {
        snprintf(buf, len, "%luh%02lum", hours, minutes);
    } else {
        snprintf(buf, len, "%lud %02luh", days, hours);
    }
}

void status_format_kib(uint32_t bytes, char *buf, size_t len)
{
    snprintf(buf, len, "%luK", (unsigned long)(bytes / 1024u));
}

void status_format_temp_c(int tenths, char *buf, size_t len)
{
    int whole = tenths >= 0 ? (tenths + 5) / 10 : -((-tenths + 5) / 10);
    snprintf(buf, len, "%dC", whole);
}

size_t status_truncate_utf8(const char *utf8, size_t max_chars, char *buf, size_t len)
{
    size_t chars = 0;
    size_t used = 0;
    const char *p = utf8;

    if (!buf || len == 0) {
        return 0;
    }
    buf[0] = '\0';
    if (!p) {
        return 0;
    }

    while (chars < max_chars) {
        const char *start = p;
        if (text_latin1_next(&p) == 0) {
            break;
        }
        size_t n = (size_t)(p - start);
        if (used + n >= len) {
            break;
        }
        memcpy(buf + used, start, n);
        used += n;
        chars++;
    }
    buf[used] = '\0';
    return chars;
}

const char *status_model_short(const char *model)
{
    if (!model) {
        return "--";
    }
    const char *slash = strrchr(model, '/');
    return slash ? slash + 1 : model;
}
