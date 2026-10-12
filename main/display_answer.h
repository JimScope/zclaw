#ifndef DISPLAY_ANSWER_H
#define DISPLAY_ANSWER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ANSWER_COLS           29
#define ANSWER_LINES_PER_PAGE 11
#define ANSWER_MAX_PAGES      6
#define ANSWER_HEADER_LINES   2

// Takes ownership of block = header '\0' body '\0' (header_len bytes of
// header), lays it out, frees any previous answer and returns the total
// display time in ms.
uint32_t display_answer_load(char *block, size_t header_len);
void display_answer_clear(void);
bool display_answer_loaded(void);

// Page to show elapsed_ms after the answer appeared.
size_t display_answer_page_at(uint32_t elapsed_ms);

// Draw one page (header, divider, body, n/m) and wait for the blit.
void display_answer_draw(size_t page);

#endif // DISPLAY_ANSWER_H
