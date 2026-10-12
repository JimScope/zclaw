#include "display_answer.h"

#include "config.h"

#if CONFIG_ZCLAW_DISPLAY_ENABLE

#include <stdio.h>
#include <stdlib.h>

#include "display_gfx.h"
#include "display_hw.h"
#include "font8x16.h"
#include "text_layout.h"

#define ANSWER_MARGIN    4
#define ANSWER_HEADER_Y  2
#define ANSWER_RULE_Y    38
#define ANSWER_BODY_Y    44
#define ANSWER_PAGE_Y    224
#define ANSWER_MAX_LINES (ANSWER_LINES_PER_PAGE * ANSWER_MAX_PAGES)
#define LINE_BUF         128  // 29 columns of up to 4 UTF-8 bytes + "..."

static char *s_block;        // header '\0' body '\0', owned
static const char *s_body;
static text_line_t s_header[ANSWER_HEADER_LINES];
static size_t s_header_count;
static text_line_t s_lines[ANSWER_MAX_LINES];
static size_t s_line_count;
static uint32_t s_page_ms[ANSWER_MAX_PAGES];
static size_t s_pages;

void display_answer_clear(void)
{
    free(s_block);
    s_block = NULL;
    s_body = NULL;
    s_header_count = 0;
    s_line_count = 0;
    s_pages = 0;
}

bool display_answer_loaded(void)
{
    return s_block != NULL;
}

uint32_t display_answer_load(char *block, size_t header_len)
{
    display_answer_clear();
    if (!block) {
        return 0;
    }
    s_block = block;
    s_body = block + header_len + 1;
    s_header_count = text_layout_wrap(s_block, ANSWER_COLS, s_header, ANSWER_HEADER_LINES);
    s_line_count = text_layout_wrap(s_body, ANSWER_COLS, s_lines, ANSWER_MAX_LINES);
    s_pages = text_layout_page_count(s_line_count, ANSWER_LINES_PER_PAGE);
    return text_layout_durations(s_lines, s_line_count, ANSWER_LINES_PER_PAGE, s_page_ms,
                                 ANSWER_MAX_PAGES);
}

size_t display_answer_page_at(uint32_t elapsed_ms)
{
    return text_layout_page_at(s_page_ms, s_pages, elapsed_ms);
}

static void draw_line(int y, const char *text, const text_line_t *line, gfx_color_t color)
{
    char buf[LINE_BUF];

    text_layout_line_copy(text, line, buf, sizeof(buf));
    gfx_text(ANSWER_MARGIN, y, buf, color, GFX_BLACK, 1);
}

void display_answer_draw(size_t page)
{
    gfx_fill_rect(0, 0, DISPLAY_W, DISPLAY_H, GFX_BLACK);
    if (s_block) {
        for (size_t i = 0; i < s_header_count; i++) {
            draw_line(ANSWER_HEADER_Y + (int)i * FONT_GLYPH_H, s_block, &s_header[i], GFX_GRAY);
        }
        gfx_fill_rect(ANSWER_MARGIN, ANSWER_RULE_Y, DISPLAY_W - 2 * ANSWER_MARGIN, 1, GFX_GRAY);
        size_t first = page * ANSWER_LINES_PER_PAGE;
        for (size_t k = 0; k < ANSWER_LINES_PER_PAGE && first + k < s_line_count; k++) {
            draw_line(ANSWER_BODY_Y + (int)k * FONT_GLYPH_H, s_body, &s_lines[first + k], GFX_WHITE);
        }
        if (s_pages > 1) {
            char ind[24];
            snprintf(ind, sizeof(ind), "%u/%u", (unsigned)(page + 1), (unsigned)s_pages);
            int w = gfx_text_width(ind, 1);
            gfx_text(DISPLAY_W - ANSWER_MARGIN - w, ANSWER_PAGE_Y, ind, GFX_GRAY, GFX_BLACK, 1);
        }
    }
    display_hw_wait_idle();
}

#endif  // CONFIG_ZCLAW_DISPLAY_ENABLE
