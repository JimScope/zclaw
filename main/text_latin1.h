#ifndef TEXT_LATIN1_H
#define TEXT_LATIN1_H

#include <stddef.h>
#include <stdint.h>

// Decode the next UTF-8 character as Latin-1 (0x20-0x7E, 0xA0-0xFF).
// Returns '?' for control, C1, non-Latin-1 or malformed input, and 0 at the
// end of the string. Advances *cursor but never past the terminating NUL.
uint8_t text_latin1_next(const char **cursor);

// Number of characters text_latin1_next() would return before 0.
size_t text_latin1_length(const char *utf8);

#endif // TEXT_LATIN1_H
