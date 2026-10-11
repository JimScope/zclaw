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

bool dash_url_build_param(char *buf, size_t len, const char *base, const char *name,
                          const char *key, const char *value)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t used;
    int written;

    if (!buf || len == 0 || !base || !name_is_valid(name) || !name_is_valid(key) ||
        !value || !*value) {
        return false;
    }
    written = snprintf(buf, len, "%s/%s.jpg?%s=", base, name, key);
    if (written <= 0 || (size_t)written >= len) {
        return false;
    }
    used = (size_t)written;

    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        bool plain = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                     (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~';
        size_t need = plain ? 1 : 3;
        if (used + need >= len) {
            return false;
        }
        if (plain) {
            buf[used++] = (char)*p;
        } else {
            buf[used++] = '%';
            buf[used++] = hex[*p >> 4];
            buf[used++] = hex[*p & 0x0F];
        }
    }
    buf[used] = '\0';
    return true;
}
