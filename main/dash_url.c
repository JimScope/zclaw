#include "dash_url.h"

#include <stdio.h>
#include <string.h>

// Copy the number that follows `marker` in `text` ("-75.83") into out.
static bool number_after(const char *text, const char *marker, char *out, size_t out_len)
{
    const char *p = text ? strstr(text, marker) : NULL;
    size_t n = 0;

    if (!p) {
        return false;
    }
    p += strlen(marker);
    while (*p && (*p == '-' || *p == '.' || (*p >= '0' && *p <= '9')) && n + 1 < out_len) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n > 0;
}

static bool name_is_valid(const char *name)
{
    if (!name || !*name) {
        return false;
    }
    for (const char *p = name; *p; p++) {
        if (*p < 'a' || *p > 'z') {
            return false;
        }
    }
    return true;
}

bool dash_url_build(char *buf, size_t len, const char *base, const char *name,
                    const char *location)
{
    char lat[16];
    char lon[16];
    int written;

    if (!buf || len == 0 || !base || !name_is_valid(name)) {
        return false;
    }

    if (strcmp(name, "clima") == 0 && number_after(location, "lat", lat, sizeof(lat)) &&
        number_after(location, "lon", lon, sizeof(lon))) {
        written = snprintf(buf, len, "%s/%s.jpg?lat=%s&lon=%s", base, name, lat, lon);
    } else {
        written = snprintf(buf, len, "%s/%s.jpg", base, name);
    }
    return written > 0 && (size_t)written < len;
}
