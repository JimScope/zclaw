#include "task_log_format.h"

#include <stdio.h>
#include <string.h>

// Bytes of s to keep: at most max, never cutting a UTF-8 character.
static size_t utf8_prefix(const char *s, size_t max)
{
    size_t n = strlen(s);

    if (n <= max) {
        return n;
    }
    n = max;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) {
        n--;
    }
    return n;
}

static size_t escape_field(char *out, const char *src, size_t max)
{
    size_t w = 0;

    if (!src) {
        return 0;
    }
    size_t n = utf8_prefix(src, max);
    for (size_t i = 0; i < n; i++) {
        char c = src[i];
        if (c == '\\') {
            out[w++] = '\\';
            out[w++] = '\\';
        } else if (c == '\n') {
            out[w++] = '\\';
            out[w++] = 'n';
        } else if (c == '\t') {
            out[w++] = '\\';
            out[w++] = 't';
        } else if (c != '\r') {
            out[w++] = c;
        }
    }
    return w;
}

size_t task_log_format_line(char *out, size_t out_len, uint32_t when, const char *action,
                            const char *reply)
{
    size_t w;

    if (!out || out_len == 0) {
        return 0;
    }
    if (out_len < TASK_LOG_LINE_MAX) {
        out[0] = '\0';
        return 0;
    }
    // newlib nano printf has no %llu; 32-bit seconds last until 2106.
    w = (size_t)snprintf(out, out_len, "%lu\t", (unsigned long)when);
    w += escape_field(out + w, action, TASK_LOG_ACTION_MAX);
    out[w++] = '\t';
    w += escape_field(out + w, reply, TASK_LOG_REPLY_MAX);
    out[w++] = '\n';
    out[w] = '\0';
    return w;
}
