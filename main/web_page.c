#include "web_page.h"

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
