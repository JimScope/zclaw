#include "text_layout.h"

#include <string.h>

#include "text_latin1.h"

static bool is_blank(unsigned char c)
{
    return c == ' ' || c == '\t';
}

// Control characters ('\n' included) take no column; a tab counts as a space.
static bool is_control(unsigned char c)
{
    return (c < 0x20 && c != '\t') || c == 0x7F;
}

// Shorten `line` to at most max_cols columns.
static void clip_line(const char *text, text_line_t *line, size_t max_cols)
{
    const char *start = text + line->offset;
    const char *end = start + line->len;
    const char *p = start;
    size_t chars = 0;

    while (p < end) {
        size_t col = is_control((unsigned char)*p) ? 0 : 1;
        if (chars + col > max_cols) {
            break;
        }
        chars += col;
        (void)text_latin1_next(&p);
    }
    line->len = (uint16_t)(p - start);
    line->chars = (uint8_t)chars;
}

size_t text_layout_wrap(const char *text, size_t cols, text_line_t *lines, size_t max_lines)
{
    const char *p = text;
    size_t n = 0;

    if (!text || !lines || max_lines == 0 || cols == 0) {
        return 0;
    }
    while (*p && n < max_lines) {
        const char *start = p;
        const char *brk = NULL;  // last blank inside the line
        size_t brk_chars = 0;
        size_t chars = 0;
        bool wrapped = false;

        while (*p && *p != '\n') {
            unsigned char c = (unsigned char)*p;
            size_t col = is_control(c) ? 0 : 1;
            if (chars + col > cols) {
                wrapped = true;
                break;
            }
            if (is_blank(c)) {
                brk = p;
                brk_chars = chars;
            }
            chars += col;
            (void)text_latin1_next(&p);
        }

        text_line_t *line = &lines[n++];
        line->offset = (uint16_t)(start - text);
        line->ellipsis = false;
        if (wrapped && brk && brk > start) {
            line->len = (uint16_t)(brk - start);
            line->chars = (uint8_t)brk_chars;
            p = brk;
        } else {
            line->len = (uint16_t)(p - start);
            line->chars = (uint8_t)chars;
        }
        if (wrapped) {
            while (is_blank((unsigned char)*p)) {
                p++;
            }
        }
        if (*p == '\n') {
            p++;
        }
    }

    const char *rest = p;
    while (*rest && (is_blank((unsigned char)*rest) || is_control((unsigned char)*rest))) {
        rest++;
    }
    if (*rest && n > 0) {
        text_line_t *last = &lines[n - 1];
        if (last->chars + TEXT_LAYOUT_ELLIPSIS_COLS > cols) {
            clip_line(text, last, cols > TEXT_LAYOUT_ELLIPSIS_COLS ? cols - TEXT_LAYOUT_ELLIPSIS_COLS : 0);
        }
        last->ellipsis = true;
    } else {
        while (n > 0 && lines[n - 1].chars == 0) {
            n--;
        }
    }
    return n;
}

size_t text_layout_line_copy(const char *text, const text_line_t *line, char *out, size_t out_len)
{
    size_t w = 0;

    if (!out || out_len == 0) {
        return 0;
    }
    if (text && line) {
        for (size_t i = 0; i < line->len && w + 1 < out_len; i++) {
            unsigned char c = (unsigned char)text[line->offset + i];
            if (c == '\t') {
                out[w++] = ' ';
            } else if (!is_control(c)) {
                out[w++] = (char)c;
            }
        }
        if (line->ellipsis) {
            for (const char *e = TEXT_LAYOUT_ELLIPSIS; *e && w + 1 < out_len; e++) {
                out[w++] = *e;
            }
        }
    }
    out[w] = '\0';
    return w;
}

// Largest cut <= n that does not split a UTF-8 character of s.
static size_t utf8_cut(const char *s, size_t n)
{
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) {
        n--;
    }
    return n;
}

size_t text_layout_clip_utf8(const char *src, size_t max_bytes, char *dst, size_t dst_len)
{
    size_t len;
    size_t cut;

    if (!dst || dst_len == 0) {
        return 0;
    }
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    if (max_bytes >= dst_len) {
        max_bytes = dst_len - 1;
    }
    len = strlen(src);
    if (len <= max_bytes) {
        memcpy(dst, src, len + 1);
        return len;
    }
    if (max_bytes < TEXT_LAYOUT_ELLIPSIS_COLS) {
        cut = utf8_cut(src, max_bytes);
        memcpy(dst, src, cut);
        dst[cut] = '\0';
        return cut;
    }
    cut = utf8_cut(src, max_bytes - TEXT_LAYOUT_ELLIPSIS_COLS);
    memcpy(dst, src, cut);
    memcpy(dst + cut, TEXT_LAYOUT_ELLIPSIS, sizeof(TEXT_LAYOUT_ELLIPSIS));
    return cut + TEXT_LAYOUT_ELLIPSIS_COLS;
}

size_t text_layout_page_count(size_t line_count, size_t lines_per_page)
{
    if (line_count == 0 || lines_per_page == 0) {
        return 1;
    }
    return (line_count + lines_per_page - 1) / lines_per_page;
}

uint32_t text_layout_durations(const text_line_t *lines, size_t line_count, size_t lines_per_page,
                               uint32_t *page_ms, size_t max_pages)
{
    size_t pages = text_layout_page_count(line_count, lines_per_page);
    uint32_t total_chars = 0;
    uint32_t total;
    uint32_t sum = 0;

    if (!page_ms || max_pages == 0) {
        return 0;
    }
    if (pages > max_pages) {
        pages = max_pages;
    }
    for (size_t i = 0; lines && i < line_count; i++) {
        total_chars += lines[i].chars;
    }
    uint64_t raw = (uint64_t)total_chars * TEXT_LAYOUT_MS_PER_CHAR;
    total = raw < TEXT_LAYOUT_MIN_MS ? TEXT_LAYOUT_MIN_MS
          : raw > TEXT_LAYOUT_MAX_MS ? TEXT_LAYOUT_MAX_MS : (uint32_t)raw;
    if (pages == 1) {
        page_ms[0] = total;
        return total;
    }
    for (size_t p = 0; p < pages; p++) {
        uint32_t chars = 0;
        for (size_t i = p * lines_per_page; i < line_count && i < (p + 1) * lines_per_page; i++) {
            chars += lines[i].chars;
        }
        uint32_t ms = total_chars ? (uint32_t)((uint64_t)total * chars / total_chars)
                                  : total / (uint32_t)pages;
        if (ms < TEXT_LAYOUT_PAGE_MIN_MS) {
            ms = TEXT_LAYOUT_PAGE_MIN_MS;
        }
        page_ms[p] = ms;
        sum += ms;
    }
    return sum;
}

size_t text_layout_page_at(const uint32_t *page_ms, size_t pages, uint32_t elapsed_ms)
{
    uint64_t end = 0;

    if (!page_ms || pages == 0) {
        return 0;
    }
    for (size_t p = 0; p < pages; p++) {
        end += page_ms[p];
        if (elapsed_ms < end) {
            return p;
        }
    }
    return pages - 1;
}
