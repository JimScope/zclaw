#include "web_page.h"

#include <string.h>

web_page_slice_t web_page_slice(size_t total_len, int page, size_t page_size)
{
    web_page_slice_t s = {0};
    s.page = page < 1 ? 1 : page;
    s.total_pages = (int)((total_len + page_size - 1) / page_size);
    if (s.total_pages < 1) {
        s.total_pages = 1;
    }
    if (s.page > s.total_pages) {
        s.past_end = true;
        return s;
    }
    s.start = (size_t)(s.page - 1) * page_size;
    s.len = total_len - s.start < page_size ? total_len - s.start : page_size;
    return s;
}

size_t web_page_trim_utf8(char *buf, size_t len)
{
    size_t start = 0;
    while (start < len && ((unsigned char)buf[start] & 0xC0) == 0x80) {
        start++;  // continuation bytes of a character cut by the previous page
    }
    if (start > 0) {
        len -= start;
        memmove(buf, buf + start, len);
    }
    // Walk back over at most 3 continuation bytes to the lead byte of the last character.
    size_t i = len;
    size_t back = 0;
    while (i > 0 && back < 4) {
        i--;
        back++;
        unsigned char c = (unsigned char)buf[i];
        if ((c & 0xC0) == 0x80) {
            continue;
        }
        size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (need > back) {
            return i;  // the last character is cut off
        }
        break;
    }
    return len;
}
