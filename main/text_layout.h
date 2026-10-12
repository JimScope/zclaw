#ifndef TEXT_LAYOUT_H
#define TEXT_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The 8x16 font is Latin-1 only (no U+2026), so cuts are marked with "...".
#define TEXT_LAYOUT_ELLIPSIS      "..."
#define TEXT_LAYOUT_ELLIPSIS_COLS 3

#define TEXT_LAYOUT_MS_PER_CHAR 70u
#define TEXT_LAYOUT_MIN_MS      8000u
#define TEXT_LAYOUT_MAX_MS      45000u
#define TEXT_LAYOUT_PAGE_MIN_MS 3000u

typedef struct {
    uint16_t offset;  // byte offset of the line in the source text
    uint16_t len;     // bytes of the line (control characters included)
    uint8_t chars;    // columns: one per character, control characters excluded
    bool ellipsis;    // the text was cut after this line: draw "..." after it
} text_line_t;

// Wrap UTF-8 text (< 64 KiB) into at most max_lines lines of `cols` columns
// (cols <= 255). Breaks at spaces and tabs, splits words longer than a line,
// starts a new line at '\n'; other control characters take no column.
// Trailing empty lines are dropped. When text is left over, the last line is
// shortened to make room for the ellipsis and flagged. Returns the count.
size_t text_layout_wrap(const char *text, size_t cols, text_line_t *lines, size_t max_lines);

// Copy one line for drawing: control characters dropped ('\t' becomes a
// space), "..." appended when flagged, always NUL-terminated. Returns the
// bytes written.
size_t text_layout_line_copy(const char *text, const text_line_t *line, char *out, size_t out_len);

// Copy at most max_bytes bytes of UTF-8 src into dst (dst_len > max_bytes),
// never splitting a character; a cut text ends in "..." within max_bytes.
// Returns the bytes written.
size_t text_layout_clip_utf8(const char *src, size_t max_bytes, char *dst, size_t dst_len);

// Pages needed for line_count lines (at least 1).
size_t text_layout_page_count(size_t line_count, size_t lines_per_page);

// Per-page display time: total = 70 ms per character clamped to 8-45 s,
// split in proportion to each page's characters, at least 3 s per page (so
// the sum can exceed 45 s). Fills page_ms[0..pages-1] (pages capped at
// max_pages) and returns the sum.
uint32_t text_layout_durations(const text_line_t *lines, size_t line_count, size_t lines_per_page,
                               uint32_t *page_ms, size_t max_pages);

// Page to show elapsed_ms after the first page appeared (the last page once
// every page's time is used up).
size_t text_layout_page_at(const uint32_t *page_ms, size_t pages, uint32_t elapsed_ms);

#endif // TEXT_LAYOUT_H
