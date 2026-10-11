#include "text_latin1.h"

uint8_t text_latin1_next(const char **cursor)
{
    if (!cursor || !*cursor) {
        return 0;
    }

    const unsigned char *p = (const unsigned char *)*cursor;
    unsigned int lead = *p;
    uint8_t out;

    if (lead == 0) {
        return 0;
    }
    p++;

    if (lead < 0x80) {
        out = (lead < 0x20 || lead == 0x7F) ? '?' : (uint8_t)lead;
    } else if ((lead & 0xE0) == 0xC0) {
        if ((*p & 0xC0) != 0x80) {
            out = '?';
        } else {
            unsigned int cp = ((lead & 0x1F) << 6) | (*p & 0x3F);
            p++;
            out = (cp >= 0xA0 && cp <= 0xFF) ? (uint8_t)cp : '?';
        }
    } else {
        int extra = ((lead & 0xF0) == 0xE0) ? 2 : ((lead & 0xF8) == 0xF0) ? 3 : 0;
        while (extra-- > 0 && (*p & 0xC0) == 0x80) {
            p++;
        }
        out = '?';
    }

    *cursor = (const char *)p;
    return out;
}

size_t text_latin1_length(const char *utf8)
{
    size_t count = 0;
    const char *p = utf8;

    if (!p) {
        return 0;
    }
    while (text_latin1_next(&p) != 0) {
        count++;
    }
    return count;
}
