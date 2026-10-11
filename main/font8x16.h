#ifndef FONT8X16_H
#define FONT8X16_H

#include <stdint.h>

#define FONT_GLYPH_W 8
#define FONT_GLYPH_H 16

// Latin-1 glyphs, one byte per row (MSB = leftmost pixel). Codes without a
// printable glyph (controls, C1) are all zero.
extern const uint8_t font8x16_latin1[256][16];

#endif // FONT8X16_H
