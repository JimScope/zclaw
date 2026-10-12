#ifndef WEB_PAGE_H
#define WEB_PAGE_H

#include <stdbool.h>
#include <stddef.h>

#define WEB_PAGE_SIZE 450

typedef struct {
    size_t start;      // byte offset of the page
    size_t len;        // bytes in the page (0 when past the end)
    int total_pages;   // >= 1
    int page;          // the page served, clamped to >= 1
    bool past_end;     // page > total_pages
} web_page_slice_t;

// Slices total_len bytes of text into pages of page_size bytes.
web_page_slice_t web_page_slice(size_t total_len, int page, size_t page_size);

#endif // WEB_PAGE_H
