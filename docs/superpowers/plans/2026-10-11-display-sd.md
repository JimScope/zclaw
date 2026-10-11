# Display + SD (Delivery 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fidel shows an animated photo face (frames read from an SD card) alternating with a status page on a 1.54" ST7789 display that shares SPI2 with the SD card.

**Architecture:** Pure, host-tested logic modules (`frame_format`, `face_state`, `page_rotation`, `status_format`, `text_latin1`) sit under thin ESP-IDF layers (`board_spi`, `display_hw`, `display_gfx`, `sd_card`). `display.c` runs one low-priority task that drains an event queue (thinking/talking/WiFi), services the SD mount, rotates pages and draws the face or the status page. Face frames are produced on the Mac by `scripts/face_assets.py` as raw RGB565 `.r565` files (full frames and small eye/mouth patches) and copied to `/sdcard/face/`.

**Tech Stack:** C99, ESP-IDF v5.5.5 (`esp_lcd` ST7789, `esp_driver_spi`, `esp_driver_sdspi`, `fatfs`, `esp_driver_ledc`), FreeRTOS; Python 3 + Pillow + numpy for host tooling; GNU Unifont (OFL) for the 8x16 font.

**Spec:** `docs/superpowers/specs/2026-10-11-display-sd-design.md`

## Global Constraints

- Repo: `~/Projects/zclaw`, branch `fidel` (or a feature branch chosen at execution start). ESP-IDF v5.5.5 at `~/esp/esp-idf`, target `esp32c6`.
- Build: `source ~/esp/esp-idf/export.sh >/dev/null 2>&1 && idf.py build` from the repo root. Host tests: `./scripts/test.sh host` (C99, `-Wall -Wextra -Werror -Wshadow -Wformat=2`, ASan).
- English for identifiers, paths, file names and Kconfig; on-screen text in Spanish (matches `/status`).
- Never delete `sdkconfig`. Secrets live in the git-ignored `sdkconfig.defaults.esp32c6`; never commit it, never print its values.
- Firmware uses newlib nano printf: no `%f`, `%lld` or `%llu` in firmware `snprintf`; format integers only.
- Face frames live only on the SD card; nothing image-related is embedded in the firmware.
- No VPS work.
- With `CONFIG_ZCLAW_DISPLAY_ENABLE=n` and `CONFIG_ZCLAW_SD_ENABLE=n` the firmware must behave exactly as before.
- Pins: SCLK 6, MOSI 5, MISO 2, display CS 7, DC 1, RST 0, backlight 10, SD CS 3.
- `/status` minimum free heap must stay ≥ 100 KB.
- The first USB flash overwrites the v5.4 bootloader and the `ota_0` backup: ask the user before it. Capture logs only through `idf.py monitor` (it resets the board on purpose).
- Commit steps run only after the user has authorised committing for this work.
- Deliberate refinement of spec §4.1 (weather): the refresh task pauses Telegram polling and waits up to 40 s for the HTTP gate inside its own task (the poll task re-takes the gate mutex immediately, so a no-wait attempt would almost never succeed). Nobody else is blocked longer than one weather request (~1–3 s).
- Spec §13 says "pytest"; the repo's Python tests use `unittest`, so the face tool tests follow that convention.

## Review Focus

1. Non-Latin-1 or malformed UTF-8 in SSIDs, model names or labels (emoji, truncated multibyte at end of string) must render as `?` and never read past the terminator → `text_latin1` tests (Task 2) and `status_truncate_utf8` tests (Task 6).
2. Clock not yet synced by SNTP (year 1970) and extreme uptimes/heap values must still produce readable strings → `status_format` tests (Task 6).
3. A partially copied or hand-edited `.r565` file (wrong length, rectangle outside the screen, patch flag on a full-frame name) must be skipped, not drawn → `frame_format` tests (Task 3) and the type check in the catalog scan (Task 12).
4. An LLM tool asking for a display/SD pin (e.g. `gpio_write` on 6) must get a clear "reserved" error instead of breaking the bus → `gpio_policy` tests (Task 7).
5. SD card pulled out while the face is animating must fall back to text and recover after re-insertion → hardware step in Task 12 (I/O error path calls `sd_card_mark_failed()`).

---

### Task 1: Restore the host test build

The host runner fails to link today because `agent.c` calls `led_thinking_start/stop` and `tools_chip_temp_handler`, which have no host stubs. Every later task depends on a green host build.

**Files:**
- Modify: `test/host/mock_tools.c`

**Interfaces:**
- Produces: host stubs `void led_thinking_start(void)`, `void led_thinking_stop(void)`, `bool tools_chip_temp_handler(const cJSON *input, char *result, size_t result_len)`.

- [ ] **Step 1: Confirm the failure**

Run: `./scripts/test.sh host 2>&1 | tail -8`
Expected: `Undefined symbols ... _led_thinking_start ... _led_thinking_stop ... _tools_chip_temp_handler`.

- [ ] **Step 2: Add the stubs**

Add `#include "tools_handlers.h"` after `#include "mock_tools.h"` in `test/host/mock_tools.c`, and append:

```c
void led_thinking_start(void)
{
}

void led_thinking_stop(void)
{
}

bool tools_chip_temp_handler(const cJSON *input, char *result, size_t result_len)
{
    (void)input;
    if (result && result_len > 0) {
        snprintf(result, result_len, "Chip temperature: 40.0 C");
    }
    return true;
}
```

- [ ] **Step 3: Run the host tests**

Run: `./scripts/test.sh host 2>&1 | tail -15`
Expected: the C runner prints `All tests passed!`, followed by the llm runtime, ratelimit and Python suites passing. If an unrelated pre-existing test fails, stop and report it before continuing.

- [ ] **Step 4: Commit**

```bash
git add test/host/mock_tools.c
git commit -m "test: add host stubs for LED thinking and chip temp tools"
```

---

### Task 2: UTF-8 → Latin-1 decoder and the 8x16 font

**Files:**
- Create: `main/text_latin1.h`, `main/text_latin1.c`
- Create: `scripts/gen_font.py`
- Create (generated): `main/font8x16.h`, `main/font8x16.c`
- Create: `test/host/test_text_latin1.c`
- Modify: `scripts/test.sh` (runner sources), `test/host/test_runner.c`

**Interfaces:**
- Produces:
  - `uint8_t text_latin1_next(const char **cursor);` — next character as a Latin-1 code (0x20–0xFF), `'?'` for anything else, `0` at end of string; advances `*cursor`, never past the NUL.
  - `size_t text_latin1_length(const char *utf8);` — number of characters `text_latin1_next` would return.
  - `extern const uint8_t font8x16_latin1[256][16];` — one byte per row, MSB = leftmost pixel; `#define FONT_GLYPH_W 8`, `#define FONT_GLYPH_H 16`.

- [ ] **Step 1: Write the failing test** — `test/host/test_text_latin1.c`

```c
/*
 * Host tests for the UTF-8 to Latin-1 decoder and the generated 8x16 font.
 */

#include <stdio.h>
#include <string.h>

#include "font8x16.h"
#include "text_latin1.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(ascii_passes_through)
{
    const char *p = "Hi";
    ASSERT(text_latin1_next(&p) == 'H');
    ASSERT(text_latin1_next(&p) == 'i');
    ASSERT(text_latin1_next(&p) == 0);
    ASSERT(text_latin1_next(&p) == 0);
    return 0;
}

TEST(spanish_letters_decode_to_latin1)
{
    const char *p = "\xC3\xB1\xC3\xA1\xC2\xBF\xC2\xA1";  /* ñ á ¿ ¡ */
    ASSERT(text_latin1_next(&p) == 0xF1);
    ASSERT(text_latin1_next(&p) == 0xE1);
    ASSERT(text_latin1_next(&p) == 0xBF);
    ASSERT(text_latin1_next(&p) == 0xA1);
    ASSERT(text_latin1_next(&p) == 0);
    return 0;
}

TEST(emoji_and_non_latin_become_question_mark)
{
    const char *p = "a\xF0\x9F\x98\x80" "b\xE2\x82\xAC" "c";  /* a 😀 b € c */
    ASSERT(text_latin1_next(&p) == 'a');
    ASSERT(text_latin1_next(&p) == '?');
    ASSERT(text_latin1_next(&p) == 'b');
    ASSERT(text_latin1_next(&p) == '?');
    ASSERT(text_latin1_next(&p) == 'c');
    ASSERT(text_latin1_next(&p) == 0);
    return 0;
}

TEST(truncated_sequences_stop_at_terminator)
{
    char buf[4] = { 'x', (char)0xC3, '\0', 'Z' };
    const char *p = buf;
    ASSERT(text_latin1_next(&p) == 'x');
    ASSERT(text_latin1_next(&p) == '?');
    ASSERT(*p == '\0');
    ASSERT(text_latin1_next(&p) == 0);

    char buf3[4] = { (char)0xE2, (char)0x82, '\0', 'Z' };
    p = buf3;
    ASSERT(text_latin1_next(&p) == '?');
    ASSERT(text_latin1_next(&p) == 0);
    return 0;
}

TEST(control_and_c1_characters_become_question_mark)
{
    const char *p = "\t\xC2\x85";
    ASSERT(text_latin1_next(&p) == '?');
    ASSERT(text_latin1_next(&p) == '?');
    ASSERT(text_latin1_next(&p) == 0);
    return 0;
}

TEST(length_counts_characters_not_bytes)
{
    ASSERT(text_latin1_length("") == 0);
    ASSERT(text_latin1_length(NULL) == 0);
    ASSERT(text_latin1_length("se\xC3\xB1" "al") == 5);
    ASSERT(text_latin1_length("\xF0\x9F\x98\x80") == 1);
    return 0;
}

TEST(font_has_expected_glyphs)
{
    static const uint8_t glyph_a[16] = {
        0x00, 0x00, 0x00, 0x00, 0x18, 0x24, 0x24, 0x42,
        0x42, 0x7E, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00,
    };
    static const uint8_t glyph_enye[16] = {
        0x00, 0x00, 0x32, 0x4C, 0x00, 0x00, 0x5C, 0x62,
        0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00, 0x00,
    };
    static const uint8_t blank[16] = {0};
    ASSERT(memcmp(font8x16_latin1['A'], glyph_a, 16) == 0);
    ASSERT(memcmp(font8x16_latin1[0xF1], glyph_enye, 16) == 0);
    ASSERT(memcmp(font8x16_latin1[0x01], blank, 16) == 0);
    ASSERT(memcmp(font8x16_latin1[0x85], blank, 16) == 0);
    return 0;
}

int test_text_latin1_all(void)
{
    int failures = 0;

    printf("\nText Latin-1 Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(ascii_passes_through);
    RUN(spanish_letters_decode_to_latin1);
    RUN(emoji_and_non_latin_become_question_mark);
    RUN(truncated_sequences_stop_at_terminator);
    RUN(control_and_c1_characters_become_question_mark);
    RUN(length_counts_characters_not_bytes);
    RUN(font_has_expected_glyphs);

#undef RUN
    return failures;
}
```

- [ ] **Step 2: Register the test**

In `test/host/test_runner.c` add `extern int test_text_latin1_all(void);` next to the other externs and `failures += test_text_latin1_all();` after `test_wifi_credentials_all()`.

In `scripts/test.sh` (`run_host_tests`, first `gcc` command) add `test_text_latin1.c \` after `test_wifi_credentials.c \`, and add `../../main/text_latin1.c \` and `../../main/font8x16.c \` after `../../main/tools_system.c \`.

- [ ] **Step 3: Run to verify it fails**

Run: `./scripts/test.sh host 2>&1 | grep -E "font8x16|text_latin1" | head -5`
Expected: compile error, `text_latin1.h` / `font8x16.h` not found.

- [ ] **Step 4: Implement the decoder** — `main/text_latin1.h`

```c
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
```

`main/text_latin1.c`:

```c
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
```

- [ ] **Step 5: Write the font generator** — `scripts/gen_font.py`

```python
#!/usr/bin/env python3
"""Generate main/font8x16.c (Latin-1, 8x16 pixels) from a GNU Unifont .hex file.

GNU Unifont is dual-licensed (SIL Open Font License 1.1, or GPLv2+ with the
font embedding exception); this generator uses it under the OFL 1.1.

Download the source font, then run:
    curl -LO https://unifoundry.com/pub/unifont/unifont-16.0.04/font-builds/unifont-16.0.04.hex.gz
    python3 scripts/gen_font.py unifont-16.0.04.hex.gz
"""

from __future__ import annotations

import argparse
import gzip
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
PRINTABLE = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100))
# Unifont draws U+00AD (soft hyphen) wider than 8 px; show it as '-'.
FALLBACKS = {0xAD: 0x2D}


def load_hex(path: Path) -> dict[int, bytes]:
    opener = gzip.open if path.suffix == ".gz" else open
    glyphs: dict[int, bytes] = {}
    with opener(path, "rt", encoding="ascii") as handle:
        for line in handle:
            code_hex, _, bitmap = line.strip().partition(":")
            if not bitmap:
                continue
            code = int(code_hex, 16)
            if code > 0xFF:
                break
            if code in PRINTABLE and len(bitmap) == 32:
                glyphs[code] = bytes.fromhex(bitmap)
    for code, source in FALLBACKS.items():
        if code not in glyphs and source in glyphs:
            glyphs[code] = glyphs[source]
    return glyphs


def render_c(glyphs: dict[int, bytes], source_name: str) -> str:
    out = [
        f"// Generated by scripts/gen_font.py from {source_name}. Do not edit.",
        "// Glyphs: GNU Unifont (https://unifoundry.com/unifont/), SIL Open Font License 1.1.",
        "",
        '#include "font8x16.h"',
        "",
        "const uint8_t font8x16_latin1[256][16] = {",
    ]
    for code in sorted(glyphs):
        row = ", ".join(f"0x{b:02X}" for b in glyphs[code])
        label = chr(code) if code != 0x5C else "backslash"
        out.append(f"    [0x{code:02X}] = {{{row}}},  // {label}")
    out.append("};")
    out.append("")
    return "\n".join(out)


HEADER = """#ifndef FONT8X16_H
#define FONT8X16_H

#include <stdint.h>

#define FONT_GLYPH_W 8
#define FONT_GLYPH_H 16

// Latin-1 glyphs, one byte per row (MSB = leftmost pixel). Codes without a
// printable glyph (controls, C1) are all zero.
extern const uint8_t font8x16_latin1[256][16];

#endif // FONT8X16_H
"""


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("hex_file", type=Path, help="unifont-*.hex or .hex.gz")
    parser.add_argument("--out-dir", type=Path, default=PROJECT_ROOT / "main")
    args = parser.parse_args(argv)

    glyphs = load_hex(args.hex_file)
    missing = sorted(PRINTABLE - glyphs.keys())
    if missing:
        print(f"error: {len(missing)} Latin-1 glyphs missing or not 8x16 "
              f"(first: U+{missing[0]:04X})", file=sys.stderr)
        return 1

    (args.out_dir / "font8x16.c").write_text(render_c(glyphs, args.hex_file.name), encoding="utf-8")
    (args.out_dir / "font8x16.h").write_text(HEADER, encoding="utf-8")
    print(f"wrote {len(glyphs)} glyphs to {args.out_dir / 'font8x16.c'}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
```

Note on the comment label: `chr(code)` for 0x5C would end the C line comment with a backslash (line continuation), hence the explicit `backslash` label.

- [ ] **Step 6: Generate the font**

```bash
cd ~/Projects/zclaw
curl -sL -o build/unifont-16.0.04.hex.gz https://unifoundry.com/pub/unifont/unifont-16.0.04/font-builds/unifont-16.0.04.hex.gz
python3 scripts/gen_font.py build/unifont-16.0.04.hex.gz
```

Expected: `wrote 191 glyphs to .../main/font8x16.c` (0x20–0x7E is 95, 0xA0–0xFF is 96). If any of them is missing or not 8x16 the script exits with an error instead.

- [ ] **Step 7: Run the tests**

Run: `./scripts/test.sh host 2>&1 | grep -A9 "Text Latin-1"`
Expected: all seven tests `OK`.

- [ ] **Step 8: Commit**

```bash
git add main/text_latin1.[ch] main/font8x16.[ch] scripts/gen_font.py test/host/test_text_latin1.c test/host/test_runner.c scripts/test.sh
git commit -m "feat(display): add UTF-8 to Latin-1 decoder and Unifont 8x16 font"
```

---

### Task 3: `.r565` frame header parsing

**Files:**
- Create: `main/frame_format.h`, `main/frame_format.c`
- Create: `test/host/test_frame_format.c`
- Modify: `scripts/test.sh`, `test/host/test_runner.c`

**Interfaces:**
- Produces:
  - `#define FRAME_HEADER_SIZE 16`, `#define FRAME_FLAG_PATCH 0x0001u`
  - `typedef struct { uint16_t x, y, w, h, flags; } frame_header_t;`
  - `typedef enum { FRAME_OK = 0, FRAME_ERR_SHORT, FRAME_ERR_MAGIC, FRAME_ERR_BOUNDS, FRAME_ERR_SIZE } frame_err_t;`
  - `frame_err_t frame_header_parse(const uint8_t *buf, size_t buf_len, size_t file_size, uint16_t screen_w, uint16_t screen_h, frame_header_t *out);`
  - `size_t frame_pixel_offset(const frame_header_t *hdr, uint16_t col, uint16_t row);`
  - `bool frame_is_full_screen(const frame_header_t *hdr, uint16_t screen_w, uint16_t screen_h);`

- [ ] **Step 1: Write the failing test** — `test/host/test_frame_format.c`

```c
/*
 * Host tests for the .r565 frame header format.
 */

#include <stdio.h>
#include <string.h>

#include "frame_format.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void make_header(uint8_t *buf, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t flags)
{
    memcpy(buf, "R565", 4);
    put16(buf + 4, x);
    put16(buf + 6, y);
    put16(buf + 8, w);
    put16(buf + 10, h);
    put16(buf + 12, flags);
    put16(buf + 14, 0);
}

static size_t file_size_for(uint16_t w, uint16_t h)
{
    return FRAME_HEADER_SIZE + (size_t)w * h * 2u;
}

TEST(full_frame_parses)
{
    uint8_t buf[FRAME_HEADER_SIZE];
    frame_header_t hdr;
    make_header(buf, 0, 0, 240, 240, 0);
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(240, 240), 240, 240, &hdr) == FRAME_OK);
    ASSERT(hdr.w == 240 && hdr.h == 240 && hdr.flags == 0);
    ASSERT(frame_is_full_screen(&hdr, 240, 240));
    return 0;
}

TEST(patch_parses)
{
    uint8_t buf[FRAME_HEADER_SIZE];
    frame_header_t hdr;
    make_header(buf, 82, 74, 110, 40, FRAME_FLAG_PATCH);
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(110, 40), 240, 240, &hdr) == FRAME_OK);
    ASSERT(hdr.x == 82 && hdr.y == 74 && hdr.w == 110 && hdr.h == 40);
    ASSERT((hdr.flags & FRAME_FLAG_PATCH) != 0);
    ASSERT(!frame_is_full_screen(&hdr, 240, 240));
    return 0;
}

TEST(short_buffer_is_rejected)
{
    uint8_t buf[FRAME_HEADER_SIZE];
    frame_header_t hdr;
    make_header(buf, 0, 0, 240, 240, 0);
    ASSERT(frame_header_parse(buf, 10, file_size_for(240, 240), 240, 240, &hdr) == FRAME_ERR_SHORT);
    ASSERT(frame_header_parse(NULL, 16, file_size_for(240, 240), 240, 240, &hdr) == FRAME_ERR_SHORT);
    return 0;
}

TEST(bad_magic_is_rejected)
{
    uint8_t buf[FRAME_HEADER_SIZE];
    frame_header_t hdr;
    make_header(buf, 0, 0, 240, 240, 0);
    buf[0] = 'X';
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(240, 240), 240, 240, &hdr) == FRAME_ERR_MAGIC);
    return 0;
}

TEST(out_of_bounds_rectangle_is_rejected)
{
    uint8_t buf[FRAME_HEADER_SIZE];
    frame_header_t hdr;
    make_header(buf, 200, 0, 50, 10, FRAME_FLAG_PATCH);
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(50, 10), 240, 240, &hdr) == FRAME_ERR_BOUNDS);
    make_header(buf, 0, 0, 0, 10, 0);
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(0, 10), 240, 240, &hdr) == FRAME_ERR_BOUNDS);
    make_header(buf, 0, 0xFFFF, 10, 10, 0);
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(10, 10), 240, 240, &hdr) == FRAME_ERR_BOUNDS);
    return 0;
}

TEST(truncated_or_padded_file_is_rejected)
{
    uint8_t buf[FRAME_HEADER_SIZE];
    frame_header_t hdr;
    make_header(buf, 0, 0, 240, 240, 0);
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(240, 240) - 2, 240, 240, &hdr) == FRAME_ERR_SIZE);
    ASSERT(frame_header_parse(buf, sizeof(buf), file_size_for(240, 240) + 1, 240, 240, &hdr) == FRAME_ERR_SIZE);
    return 0;
}

TEST(pixel_offset_is_row_major)
{
    frame_header_t hdr = { .x = 10, .y = 20, .w = 100, .h = 50, .flags = 0 };
    ASSERT(frame_pixel_offset(&hdr, 0, 0) == FRAME_HEADER_SIZE);
    ASSERT(frame_pixel_offset(&hdr, 3, 0) == FRAME_HEADER_SIZE + 6);
    ASSERT(frame_pixel_offset(&hdr, 0, 2) == FRAME_HEADER_SIZE + 400);
    return 0;
}

int test_frame_format_all(void)
{
    int failures = 0;

    printf("\nFrame Format Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(full_frame_parses);
    RUN(patch_parses);
    RUN(short_buffer_is_rejected);
    RUN(bad_magic_is_rejected);
    RUN(out_of_bounds_rectangle_is_rejected);
    RUN(truncated_or_padded_file_is_rejected);
    RUN(pixel_offset_is_row_major);

#undef RUN
    return failures;
}
```

- [ ] **Step 2: Register the test** — add `extern int test_frame_format_all(void);` / `failures += test_frame_format_all();` to `test_runner.c`; add `test_frame_format.c \` and `../../main/frame_format.c \` to the first `gcc` command in `scripts/test.sh`.

- [ ] **Step 3: Run to verify it fails**

Run: `./scripts/test.sh host 2>&1 | grep -m2 frame_format`
Expected: `frame_format.h` not found.

- [ ] **Step 4: Implement** — `main/frame_format.h`

```c
#ifndef FRAME_FORMAT_H
#define FRAME_FORMAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// .r565 frame file: 16-byte header (little-endian fields) + w*h RGB565
// big-endian pixels, row-major. See docs/superpowers/specs/2026-10-11-display-sd-design.md.
#define FRAME_HEADER_SIZE 16
#define FRAME_FLAG_PATCH  0x0001u

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint16_t flags;
} frame_header_t;

typedef enum {
    FRAME_OK = 0,
    FRAME_ERR_SHORT,
    FRAME_ERR_MAGIC,
    FRAME_ERR_BOUNDS,
    FRAME_ERR_SIZE,
} frame_err_t;

frame_err_t frame_header_parse(const uint8_t *buf, size_t buf_len, size_t file_size,
                               uint16_t screen_w, uint16_t screen_h, frame_header_t *out);

// File offset of pixel (col, row) relative to the frame rectangle.
size_t frame_pixel_offset(const frame_header_t *hdr, uint16_t col, uint16_t row);

bool frame_is_full_screen(const frame_header_t *hdr, uint16_t screen_w, uint16_t screen_h);

#endif // FRAME_FORMAT_H
```

`main/frame_format.c`:

```c
#include "frame_format.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

frame_err_t frame_header_parse(const uint8_t *buf, size_t buf_len, size_t file_size,
                               uint16_t screen_w, uint16_t screen_h, frame_header_t *out)
{
    frame_header_t hdr;

    if (!buf || !out || buf_len < FRAME_HEADER_SIZE || file_size < FRAME_HEADER_SIZE) {
        return FRAME_ERR_SHORT;
    }
    if (memcmp(buf, "R565", 4) != 0) {
        return FRAME_ERR_MAGIC;
    }

    hdr.x = read_le16(buf + 4);
    hdr.y = read_le16(buf + 6);
    hdr.w = read_le16(buf + 8);
    hdr.h = read_le16(buf + 10);
    hdr.flags = read_le16(buf + 12);

    if (hdr.w == 0 || hdr.h == 0 ||
        (uint32_t)hdr.x + hdr.w > screen_w ||
        (uint32_t)hdr.y + hdr.h > screen_h) {
        return FRAME_ERR_BOUNDS;
    }
    if (file_size != FRAME_HEADER_SIZE + (size_t)hdr.w * hdr.h * 2u) {
        return FRAME_ERR_SIZE;
    }

    *out = hdr;
    return FRAME_OK;
}

size_t frame_pixel_offset(const frame_header_t *hdr, uint16_t col, uint16_t row)
{
    return FRAME_HEADER_SIZE + ((size_t)row * hdr->w + col) * 2u;
}

bool frame_is_full_screen(const frame_header_t *hdr, uint16_t screen_w, uint16_t screen_h)
{
    return hdr->x == 0 && hdr->y == 0 && hdr->w == screen_w && hdr->h == screen_h;
}
```

- [ ] **Step 5: Run the tests** — `./scripts/test.sh host 2>&1 | grep -A8 "Frame Format"` → all `OK`.

- [ ] **Step 6: Commit**

```bash
git add main/frame_format.[ch] test/host/test_frame_format.c test/host/test_runner.c scripts/test.sh
git commit -m "feat(display): add .r565 frame header parser"
```

---

### Task 4: Face state machine

**Files:**
- Create: `main/face_state.h`, `main/face_state.c`
- Create: `test/host/test_face_state.c`
- Modify: `scripts/test.sh`, `test/host/test_runner.c`

**Interfaces:**
- Produces (exact API used by Task 12):

```c
#define FACE_MAX_VARIANTS 8
typedef enum { FACE_EXPR_NEUTRAL = 0, FACE_EXPR_THINKING, FACE_EXPR_SLEEPING,
               FACE_EXPR_HAPPY, FACE_EXPR_ANGRY, FACE_EXPR_SPEECH, FACE_EXPR_COUNT } face_expr_t;
typedef enum { FACE_PATCH_NONE = 0, FACE_PATCH_EYES_CLOSED, FACE_PATCH_SPEAKING, FACE_PATCH_COUNT } face_patch_t;
typedef enum { FACE_MODE_IDLE = 0, FACE_MODE_TALKING, FACE_MODE_THINKING, FACE_MODE_OFFLINE } face_mode_t;
typedef struct { bool text_fallback; face_expr_t expr; uint8_t expr_variant;
                 face_patch_t patch; uint8_t patch_variant; } face_frame_t;
typedef struct { ... } face_state_t;   // fields below
void face_state_init(face_state_t *s, uint32_t seed, int64_t now_ms);
void face_state_set_catalog(face_state_t *s, const uint8_t expr_variants[FACE_EXPR_COUNT],
                            const uint8_t patch_variants[FACE_PATCH_COUNT]);
void face_state_set_wifi(face_state_t *s, bool up);
void face_state_set_thinking(face_state_t *s, bool active);
void face_state_start_talking(face_state_t *s, size_t reply_chars, int64_t now_ms);
int64_t face_state_talk_duration_ms(size_t reply_chars);
face_mode_t face_state_mode(const face_state_t *s, int64_t now_ms);
bool face_state_is_active(const face_state_t *s, int64_t now_ms);
face_frame_t face_state_frame(face_state_t *s, int64_t now_ms);
```

- [ ] **Step 1: Write the failing test** — `test/host/test_face_state.c`

```c
/*
 * Host tests for the face animation state machine.
 */

#include <stdio.h>
#include <string.h>

#include "face_state.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

static void full_catalog(face_state_t *s, uint8_t speaking_variants, uint8_t thinking_variants)
{
    uint8_t expr[FACE_EXPR_COUNT] = {1, 1, 1, 1, 1, 1};
    uint8_t patch[FACE_PATCH_COUNT] = {0, 1, 1};
    expr[FACE_EXPR_THINKING] = thinking_variants;
    patch[FACE_PATCH_SPEAKING] = speaking_variants;
    face_state_set_catalog(s, expr, patch);
}

static void online_state(face_state_t *s)
{
    face_state_init(s, 12345u, 0);
    full_catalog(s, 1, 1);
    face_state_set_wifi(s, true);
}

TEST(talk_duration_is_clamped)
{
    ASSERT(face_state_talk_duration_ms(0) == FACE_TALK_MIN_MS);
    ASSERT(face_state_talk_duration_ms(50) == 50 * FACE_TALK_MS_PER_CHAR);
    ASSERT(face_state_talk_duration_ms(100000) == FACE_TALK_MAX_MS);
    return 0;
}

TEST(priority_offline_thinking_talking_idle)
{
    face_state_t s;
    face_state_init(&s, 1u, 0);
    full_catalog(&s, 1, 1);
    ASSERT(face_state_mode(&s, 0) == FACE_MODE_OFFLINE);

    face_state_set_thinking(&s, true);
    face_state_start_talking(&s, 50, 0);
    ASSERT(face_state_mode(&s, 10) == FACE_MODE_OFFLINE);

    face_state_set_wifi(&s, true);
    ASSERT(face_state_mode(&s, 10) == FACE_MODE_THINKING);

    face_state_set_thinking(&s, false);
    ASSERT(face_state_mode(&s, 10) == FACE_MODE_TALKING);
    ASSERT(face_state_is_active(&s, 10));

    ASSERT(face_state_mode(&s, 3000) == FACE_MODE_IDLE);
    ASSERT(!face_state_is_active(&s, 3000));
    return 0;
}

TEST(offline_shows_sleeping_and_thinking_shows_thinking)
{
    face_state_t s;
    face_state_init(&s, 1u, 0);
    full_catalog(&s, 1, 1);
    face_frame_t f = face_state_frame(&s, 0);
    ASSERT(!f.text_fallback && f.expr == FACE_EXPR_SLEEPING && f.patch == FACE_PATCH_NONE);

    face_state_set_wifi(&s, true);
    face_state_set_thinking(&s, true);
    f = face_state_frame(&s, 100);
    ASSERT(f.expr == FACE_EXPR_THINKING && f.patch == FACE_PATCH_NONE);
    return 0;
}

TEST(talking_alternates_mouth_patch_and_cycles_variants)
{
    face_state_t s;
    face_state_init(&s, 1u, 0);
    full_catalog(&s, 2, 1);
    face_state_set_wifi(&s, true);
    face_state_start_talking(&s, 50, 1000);  /* 3000 ms */

    face_frame_t f = face_state_frame(&s, 1000);
    ASSERT(f.expr == FACE_EXPR_NEUTRAL && f.patch == FACE_PATCH_SPEAKING && f.patch_variant == 0);
    f = face_state_frame(&s, 1000 + FACE_TALK_TOGGLE_MS);
    ASSERT(f.patch == FACE_PATCH_NONE);
    f = face_state_frame(&s, 1000 + 2 * FACE_TALK_TOGGLE_MS);
    ASSERT(f.patch == FACE_PATCH_SPEAKING && f.patch_variant == 1);
    f = face_state_frame(&s, 1000 + 4 * FACE_TALK_TOGGLE_MS);
    ASSERT(f.patch == FACE_PATCH_SPEAKING && f.patch_variant == 0);

    ASSERT(face_state_mode(&s, 3999) == FACE_MODE_TALKING);
    ASSERT(face_state_mode(&s, 4000) == FACE_MODE_IDLE);
    return 0;
}

TEST(longer_reply_extends_talking)
{
    face_state_t s;
    online_state(&s);
    face_state_start_talking(&s, 100, 0);   /* 6000 ms */
    face_state_start_talking(&s, 0, 1000);  /* 2000 ms would end earlier: keep 6000 */
    ASSERT(face_state_mode(&s, 5999) == FACE_MODE_TALKING);
    return 0;
}

TEST(idle_blinks_with_bounded_gaps)
{
    face_state_t s;
    online_state(&s);

    int64_t last_start = -1;
    int64_t first_start = -1;
    int blink_frames = 0;
    int blinks = 0;
    bool in_blink = false;

    for (int64_t t = 0; t <= 60000; t += 10) {
        face_frame_t f = face_state_frame(&s, t);
        bool closed = f.patch == FACE_PATCH_EYES_CLOSED;
        if (closed && !in_blink) {
            if (first_start < 0) {
                first_start = t;
            }
            if (last_start >= 0) {
                int64_t gap = t - last_start;
                ASSERT(gap >= FACE_BLINK_MIN_GAP_MS + FACE_BLINK_MS);
                ASSERT(gap <= FACE_BLINK_MAX_GAP_MS + FACE_BLINK_MS + 10);
            }
            last_start = t;
            blinks++;
            blink_frames = 0;
        }
        if (closed) {
            blink_frames++;
        } else if (in_blink) {
            ASSERT(blink_frames == FACE_BLINK_MS / 10);
        }
        in_blink = closed;
    }

    ASSERT(first_start >= 0 && first_start <= FACE_BLINK_MAX_GAP_MS + 10);
    ASSERT(blinks >= 60000 / (FACE_BLINK_MAX_GAP_MS + FACE_BLINK_MS));
    return 0;
}

TEST(thinking_cycles_variants)
{
    face_state_t s;
    face_state_init(&s, 1u, 0);
    full_catalog(&s, 1, 3);
    face_state_set_wifi(&s, true);
    face_state_set_thinking(&s, true);
    ASSERT(face_state_frame(&s, 500).expr_variant == 0);
    ASSERT(face_state_frame(&s, 500 + FACE_CYCLE_MS).expr_variant == 1);
    ASSERT(face_state_frame(&s, 500 + 2 * FACE_CYCLE_MS).expr_variant == 2);
    ASSERT(face_state_frame(&s, 500 + 3 * FACE_CYCLE_MS).expr_variant == 0);
    return 0;
}

TEST(missing_frames_fall_back)
{
    face_state_t s;
    uint8_t expr[FACE_EXPR_COUNT] = {1, 0, 0, 0, 0, 0};
    uint8_t patch[FACE_PATCH_COUNT] = {0, 0, 0};

    face_state_init(&s, 1u, 0);
    face_state_set_catalog(&s, expr, patch);
    face_state_set_wifi(&s, true);
    face_state_set_thinking(&s, true);
    face_frame_t f = face_state_frame(&s, 0);
    ASSERT(!f.text_fallback && f.expr == FACE_EXPR_NEUTRAL);

    face_state_set_thinking(&s, false);
    for (int64_t t = 0; t <= 20000; t += 10) {
        ASSERT(face_state_frame(&s, t).patch == FACE_PATCH_NONE);
    }

    expr[FACE_EXPR_NEUTRAL] = 0;
    face_state_set_catalog(&s, expr, patch);
    f = face_state_frame(&s, 20010);
    ASSERT(f.text_fallback);

    expr[FACE_EXPR_THINKING] = 1;
    face_state_set_catalog(&s, expr, patch);
    face_state_set_thinking(&s, true);
    f = face_state_frame(&s, 20020);
    ASSERT(!f.text_fallback && f.expr == FACE_EXPR_THINKING);
    return 0;
}

int test_face_state_all(void)
{
    int failures = 0;

    printf("\nFace State Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(talk_duration_is_clamped);
    RUN(priority_offline_thinking_talking_idle);
    RUN(offline_shows_sleeping_and_thinking_shows_thinking);
    RUN(talking_alternates_mouth_patch_and_cycles_variants);
    RUN(longer_reply_extends_talking);
    RUN(idle_blinks_with_bounded_gaps);
    RUN(thinking_cycles_variants);
    RUN(missing_frames_fall_back);

#undef RUN
    return failures;
}
```

- [ ] **Step 2: Register** — `test_runner.c` extern + call `test_face_state_all()`; `scripts/test.sh`: add `test_face_state.c \` and `../../main/face_state.c \`.

- [ ] **Step 3: Run to verify it fails** — `./scripts/test.sh host 2>&1 | grep -m2 face_state` → header not found.

- [ ] **Step 4: Implement** — `main/face_state.h`

```c
#ifndef FACE_STATE_H
#define FACE_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FACE_MAX_VARIANTS      8
#define FACE_BLINK_MS          150
#define FACE_BLINK_MIN_GAP_MS  3000
#define FACE_BLINK_MAX_GAP_MS  7000
#define FACE_TALK_TOGGLE_MS    180
#define FACE_TALK_MS_PER_CHAR  60
#define FACE_TALK_MIN_MS       2000
#define FACE_TALK_MAX_MS       6000
#define FACE_CYCLE_MS          800

typedef enum {
    FACE_EXPR_NEUTRAL = 0,
    FACE_EXPR_THINKING,
    FACE_EXPR_SLEEPING,
    FACE_EXPR_HAPPY,
    FACE_EXPR_ANGRY,
    FACE_EXPR_SPEECH,
    FACE_EXPR_COUNT
} face_expr_t;

typedef enum {
    FACE_PATCH_NONE = 0,
    FACE_PATCH_EYES_CLOSED,
    FACE_PATCH_SPEAKING,
    FACE_PATCH_COUNT
} face_patch_t;

typedef enum {
    FACE_MODE_IDLE = 0,
    FACE_MODE_TALKING,
    FACE_MODE_THINKING,
    FACE_MODE_OFFLINE
} face_mode_t;

typedef struct {
    bool text_fallback;      // no usable frame: draw the mode as text
    face_expr_t expr;        // full frame to show
    uint8_t expr_variant;    // 0-based index into the available variants
    face_patch_t patch;      // patch over neutral, or FACE_PATCH_NONE
    uint8_t patch_variant;
} face_frame_t;

typedef struct {
    bool wifi_up;
    bool thinking;
    int64_t talk_until_ms;
    face_mode_t last_mode;
    int64_t mode_since_ms;
    int64_t next_blink_ms;
    int64_t blink_until_ms;
    uint32_t rng;
    uint8_t expr_variants[FACE_EXPR_COUNT];
    uint8_t patch_variants[FACE_PATCH_COUNT];
} face_state_t;

void face_state_init(face_state_t *s, uint32_t seed, int64_t now_ms);
void face_state_set_catalog(face_state_t *s, const uint8_t expr_variants[FACE_EXPR_COUNT],
                            const uint8_t patch_variants[FACE_PATCH_COUNT]);
void face_state_set_wifi(face_state_t *s, bool up);
void face_state_set_thinking(face_state_t *s, bool active);
void face_state_start_talking(face_state_t *s, size_t reply_chars, int64_t now_ms);
int64_t face_state_talk_duration_ms(size_t reply_chars);
face_mode_t face_state_mode(const face_state_t *s, int64_t now_ms);
bool face_state_is_active(const face_state_t *s, int64_t now_ms);

// Frame to show at now_ms. Advances the blink schedule and animation phase.
face_frame_t face_state_frame(face_state_t *s, int64_t now_ms);

#endif // FACE_STATE_H
```

`main/face_state.c`:

```c
#include "face_state.h"

#include <string.h>

static uint32_t next_random(face_state_t *s)
{
    uint32_t x = s->rng ? s->rng : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x;
    return x;
}

static int64_t blink_gap_ms(face_state_t *s)
{
    uint32_t span = (uint32_t)(FACE_BLINK_MAX_GAP_MS - FACE_BLINK_MIN_GAP_MS + 1);
    return FACE_BLINK_MIN_GAP_MS + (int64_t)(next_random(s) % span);
}

static uint8_t cycle_variant(int64_t elapsed_ms, uint8_t count)
{
    if (count <= 1 || elapsed_ms < 0) {
        return 0;
    }
    return (uint8_t)((elapsed_ms / FACE_CYCLE_MS) % count);
}

void face_state_init(face_state_t *s, uint32_t seed, int64_t now_ms)
{
    memset(s, 0, sizeof(*s));
    s->rng = seed;
    s->last_mode = FACE_MODE_OFFLINE;
    s->mode_since_ms = now_ms;
    s->next_blink_ms = now_ms + blink_gap_ms(s);
}

void face_state_set_catalog(face_state_t *s, const uint8_t expr_variants[FACE_EXPR_COUNT],
                            const uint8_t patch_variants[FACE_PATCH_COUNT])
{
    for (int i = 0; i < FACE_EXPR_COUNT; i++) {
        s->expr_variants[i] = expr_variants[i] > FACE_MAX_VARIANTS ? FACE_MAX_VARIANTS : expr_variants[i];
    }
    for (int i = 0; i < FACE_PATCH_COUNT; i++) {
        s->patch_variants[i] = patch_variants[i] > FACE_MAX_VARIANTS ? FACE_MAX_VARIANTS : patch_variants[i];
    }
    s->patch_variants[FACE_PATCH_NONE] = 0;
}

void face_state_set_wifi(face_state_t *s, bool up)
{
    s->wifi_up = up;
}

void face_state_set_thinking(face_state_t *s, bool active)
{
    s->thinking = active;
}

int64_t face_state_talk_duration_ms(size_t reply_chars)
{
    int64_t ms = (int64_t)reply_chars * FACE_TALK_MS_PER_CHAR;
    if (ms < FACE_TALK_MIN_MS) {
        return FACE_TALK_MIN_MS;
    }
    if (ms > FACE_TALK_MAX_MS) {
        return FACE_TALK_MAX_MS;
    }
    return ms;
}

void face_state_start_talking(face_state_t *s, size_t reply_chars, int64_t now_ms)
{
    int64_t until = now_ms + face_state_talk_duration_ms(reply_chars);
    if (until > s->talk_until_ms) {
        s->talk_until_ms = until;
    }
}

face_mode_t face_state_mode(const face_state_t *s, int64_t now_ms)
{
    if (!s->wifi_up) {
        return FACE_MODE_OFFLINE;
    }
    if (s->thinking) {
        return FACE_MODE_THINKING;
    }
    if (now_ms < s->talk_until_ms) {
        return FACE_MODE_TALKING;
    }
    return FACE_MODE_IDLE;
}

bool face_state_is_active(const face_state_t *s, int64_t now_ms)
{
    face_mode_t mode = face_state_mode(s, now_ms);
    return mode == FACE_MODE_THINKING || mode == FACE_MODE_TALKING;
}

face_frame_t face_state_frame(face_state_t *s, int64_t now_ms)
{
    face_frame_t f = {
        .text_fallback = false,
        .expr = FACE_EXPR_NEUTRAL,
        .expr_variant = 0,
        .patch = FACE_PATCH_NONE,
        .patch_variant = 0,
    };
    face_mode_t mode = face_state_mode(s, now_ms);

    if (mode != s->last_mode) {
        s->last_mode = mode;
        s->mode_since_ms = now_ms;
    }
    int64_t elapsed = now_ms - s->mode_since_ms;

    switch (mode) {
    case FACE_MODE_OFFLINE:
        f.expr = FACE_EXPR_SLEEPING;
        f.expr_variant = cycle_variant(elapsed, s->expr_variants[FACE_EXPR_SLEEPING]);
        break;
    case FACE_MODE_THINKING:
        f.expr = FACE_EXPR_THINKING;
        f.expr_variant = cycle_variant(elapsed, s->expr_variants[FACE_EXPR_THINKING]);
        break;
    case FACE_MODE_TALKING: {
        int64_t step = elapsed / FACE_TALK_TOGGLE_MS;
        uint8_t count = s->patch_variants[FACE_PATCH_SPEAKING];
        if (step % 2 == 0 && count > 0) {
            f.patch = FACE_PATCH_SPEAKING;
            f.patch_variant = (uint8_t)((step / 2) % count);
        }
        break;
    }
    case FACE_MODE_IDLE:
    default:
        if (now_ms >= s->blink_until_ms && now_ms >= s->next_blink_ms) {
            s->blink_until_ms = now_ms + FACE_BLINK_MS;
            s->next_blink_ms = s->blink_until_ms + blink_gap_ms(s);
        }
        if (now_ms < s->blink_until_ms && s->patch_variants[FACE_PATCH_EYES_CLOSED] > 0) {
            f.patch = FACE_PATCH_EYES_CLOSED;
        }
        break;
    }

    if (s->expr_variants[f.expr] == 0) {
        f.expr = FACE_EXPR_NEUTRAL;
        f.expr_variant = 0;
    }
    if (f.expr != FACE_EXPR_NEUTRAL) {
        f.patch = FACE_PATCH_NONE;
        f.patch_variant = 0;
    }
    if (f.expr == FACE_EXPR_NEUTRAL && s->expr_variants[FACE_EXPR_NEUTRAL] == 0) {
        f.text_fallback = true;
        f.patch = FACE_PATCH_NONE;
        f.patch_variant = 0;
    }
    return f;
}
```

- [ ] **Step 5: Run the tests** — `./scripts/test.sh host 2>&1 | grep -A9 "Face State"` → all `OK`.

- [ ] **Step 6: Commit**

```bash
git add main/face_state.[ch] test/host/test_face_state.c test/host/test_runner.c scripts/test.sh
git commit -m "feat(display): add face animation state machine"
```

---

### Task 5: Page rotation

**Files:**
- Create: `main/page_rotation.h`, `main/page_rotation.c`
- Create: `test/host/test_page_rotation.c`
- Modify: `scripts/test.sh`, `test/host/test_runner.c`

**Interfaces:**
- Produces:

```c
typedef enum { PAGE_FACE = 0, PAGE_STATUS } page_t;
typedef struct { page_t page; int64_t since_ms; uint32_t face_ms; uint32_t status_ms; } page_rotation_t;
void page_rotation_init(page_rotation_t *r, uint32_t face_ms, uint32_t status_ms, int64_t now_ms);
page_t page_rotation_update(page_rotation_t *r, bool face_forced, int64_t now_ms);
```

- [ ] **Step 1: Write the failing test** — `test/host/test_page_rotation.c`

```c
/*
 * Host tests for face/status page rotation.
 */

#include <stdio.h>

#include "page_rotation.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(rotates_face_then_status)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 5000, 0);
    ASSERT(page_rotation_update(&r, false, 0) == PAGE_FACE);
    ASSERT(page_rotation_update(&r, false, 9999) == PAGE_FACE);
    ASSERT(page_rotation_update(&r, false, 10000) == PAGE_STATUS);
    ASSERT(page_rotation_update(&r, false, 14999) == PAGE_STATUS);
    ASSERT(page_rotation_update(&r, false, 15000) == PAGE_FACE);
    return 0;
}

TEST(activity_forces_face_and_pauses_rotation)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 5000, 0);
    ASSERT(page_rotation_update(&r, false, 11000) == PAGE_STATUS);
    ASSERT(page_rotation_update(&r, true, 12000) == PAGE_FACE);
    ASSERT(page_rotation_update(&r, true, 40000) == PAGE_FACE);
    /* rotation resumes with a full face period after activity ends */
    ASSERT(page_rotation_update(&r, false, 49999) == PAGE_FACE);
    ASSERT(page_rotation_update(&r, false, 50000) == PAGE_STATUS);
    return 0;
}

TEST(zero_status_time_keeps_face)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 0, 0);
    ASSERT(page_rotation_update(&r, false, 10000) == PAGE_FACE);
    ASSERT(page_rotation_update(&r, false, 100000) == PAGE_FACE);
    return 0;
}

int test_page_rotation_all(void)
{
    int failures = 0;

    printf("\nPage Rotation Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(rotates_face_then_status);
    RUN(activity_forces_face_and_pauses_rotation);
    RUN(zero_status_time_keeps_face);

#undef RUN
    return failures;
}
```

- [ ] **Step 2: Register** — extern/call `test_page_rotation_all()`; add `test_page_rotation.c \` and `../../main/page_rotation.c \` to `scripts/test.sh`.

- [ ] **Step 3: Run to verify it fails** — header not found.

- [ ] **Step 4: Implement** — `main/page_rotation.h`

```c
#ifndef PAGE_ROTATION_H
#define PAGE_ROTATION_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PAGE_FACE = 0,
    PAGE_STATUS
} page_t;

typedef struct {
    page_t page;
    int64_t since_ms;
    uint32_t face_ms;
    uint32_t status_ms;
} page_rotation_t;

void page_rotation_init(page_rotation_t *r, uint32_t face_ms, uint32_t status_ms, int64_t now_ms);

// face_forced: thinking/talking in progress. Keeps the face page and restarts
// its period, so rotation resumes with a full face period afterwards.
page_t page_rotation_update(page_rotation_t *r, bool face_forced, int64_t now_ms);

#endif // PAGE_ROTATION_H
```

`main/page_rotation.c`:

```c
#include "page_rotation.h"

void page_rotation_init(page_rotation_t *r, uint32_t face_ms, uint32_t status_ms, int64_t now_ms)
{
    r->page = PAGE_FACE;
    r->since_ms = now_ms;
    r->face_ms = face_ms;
    r->status_ms = status_ms;
}

page_t page_rotation_update(page_rotation_t *r, bool face_forced, int64_t now_ms)
{
    if (face_forced || r->status_ms == 0) {
        if (face_forced || r->page != PAGE_FACE) {
            r->since_ms = now_ms;
        }
        r->page = PAGE_FACE;
        return r->page;
    }

    uint32_t period = (r->page == PAGE_FACE) ? r->face_ms : r->status_ms;
    if (now_ms - r->since_ms >= (int64_t)period) {
        r->page = (r->page == PAGE_FACE) ? PAGE_STATUS : PAGE_FACE;
        r->since_ms = now_ms;
    }
    return r->page;
}
```

- [ ] **Step 5: Run the tests** — all three `OK`.

- [ ] **Step 6: Commit**

```bash
git add main/page_rotation.[ch] test/host/test_page_rotation.c test/host/test_runner.c scripts/test.sh
git commit -m "feat(display): add face/status page rotation"
```

---

### Task 6: Status page formatting helpers

**Files:**
- Create: `main/status_format.h`, `main/status_format.c`
- Create: `test/host/test_status_format.c`
- Modify: `scripts/test.sh`, `test/host/test_runner.c`

**Interfaces:**
- Consumes: `text_latin1_next()` (Task 2).
- Produces:

```c
void status_format_clock(const struct tm *tm, char *buf, size_t len);   // "14:32" or "--:--"
void status_format_date(const struct tm *tm, char *buf, size_t len);    // "dom 11 oct" or "--"
void status_format_uptime(uint64_t seconds, char *buf, size_t len);     // "45m", "1h03m", "2d 05h"
void status_format_kib(uint32_t bytes, char *buf, size_t len);          // "150K"
void status_format_temp_c(int tenths, char *buf, size_t len);           // "41C", "-3C"
size_t status_truncate_utf8(const char *utf8, size_t max_chars, char *buf, size_t len);
const char *status_model_short(const char *model);                       // text after the last '/'
```

- [ ] **Step 1: Write the failing test** — `test/host/test_status_format.c`

```c
/*
 * Host tests for status page formatting helpers.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "status_format.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)
#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("  FAIL: '%s' != '%s' (line %d)\n", (a), (b), __LINE__); \
        return 1; \
    } \
} while(0)

static struct tm make_tm(int year, int mon, int mday, int wday, int hour, int min)
{
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = year - 1900;
    tm.tm_mon = mon;
    tm.tm_mday = mday;
    tm.tm_wday = wday;
    tm.tm_hour = hour;
    tm.tm_min = min;
    return tm;
}

TEST(clock_and_date_when_synced)
{
    char buf[24];
    struct tm tm = make_tm(2026, 9, 11, 0, 14, 5);
    status_format_clock(&tm, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "14:05");
    status_format_date(&tm, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "dom 11 oct");
    tm = make_tm(2026, 0, 2, 3, 0, 0);
    status_format_date(&tm, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "mi\xC3\xA9 2 ene");
    return 0;
}

TEST(clock_and_date_before_sntp_sync)
{
    char buf[24];
    struct tm tm = make_tm(1970, 0, 1, 4, 0, 3);
    status_format_clock(&tm, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "--:--");
    status_format_date(&tm, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "--");
    return 0;
}

TEST(uptime_ranges)
{
    char buf[24];
    status_format_uptime(0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0m");
    status_format_uptime(59 * 60 + 59, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "59m");
    status_format_uptime(3600 + 3 * 60, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "1h03m");
    status_format_uptime(2ULL * 86400 + 5 * 3600 + 59, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "2d 05h");
    status_format_uptime(400ULL * 86400, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "400d 00h");
    return 0;
}

TEST(kib_and_temperature)
{
    char buf[24];
    status_format_kib(150784, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "147K");
    status_format_kib(0, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "0K");
    status_format_temp_c(414, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "41C");
    status_format_temp_c(415, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "42C");
    status_format_temp_c(-26, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "-3C");
    return 0;
}

TEST(truncate_counts_characters_and_keeps_utf8)
{
    char buf[32];
    ASSERT(status_truncate_utf8("se\xC3\xB1" "al fuerte", 4, buf, sizeof(buf)) == 4);
    ASSERT_STR_EQ(buf, "se\xC3\xB1" "a");
    ASSERT(status_truncate_utf8("abc", 10, buf, sizeof(buf)) == 3);
    ASSERT_STR_EQ(buf, "abc");
    ASSERT(status_truncate_utf8("\xC3\xB1\xC3\xB1\xC3\xB1", 3, buf, 5) == 2);
    ASSERT_STR_EQ(buf, "\xC3\xB1\xC3\xB1");
    ASSERT(status_truncate_utf8(NULL, 3, buf, sizeof(buf)) == 0);
    ASSERT_STR_EQ(buf, "");
    return 0;
}

TEST(model_short_name)
{
    ASSERT_STR_EQ(status_model_short("nvidia/nemotron-3-super-120b-a12b"), "nemotron-3-super-120b-a12b");
    ASSERT_STR_EQ(status_model_short("gpt-4o"), "gpt-4o");
    ASSERT_STR_EQ(status_model_short(NULL), "--");
    return 0;
}

int test_status_format_all(void)
{
    int failures = 0;

    printf("\nStatus Format Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(clock_and_date_when_synced);
    RUN(clock_and_date_before_sntp_sync);
    RUN(uptime_ranges);
    RUN(kib_and_temperature);
    RUN(truncate_counts_characters_and_keeps_utf8);
    RUN(model_short_name);

#undef RUN
    return failures;
}
```

- [ ] **Step 2: Register** — extern/call `test_status_format_all()`; add `test_status_format.c \` and `../../main/status_format.c \` to `scripts/test.sh`.

- [ ] **Step 3: Run to verify it fails** — header not found.

- [ ] **Step 4: Implement** — `main/status_format.h`

```c
#ifndef STATUS_FORMAT_H
#define STATUS_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

// "HH:MM", or "--:--" while the clock is not synced (year < 2024).
void status_format_clock(const struct tm *tm, char *buf, size_t len);
// Spanish short date, e.g. "dom 11 oct" (UTF-8), or "--" while not synced.
void status_format_date(const struct tm *tm, char *buf, size_t len);
// "45m", "1h03m", "2d 05h".
void status_format_uptime(uint64_t seconds, char *buf, size_t len);
// Bytes as whole KiB, e.g. "147K".
void status_format_kib(uint32_t bytes, char *buf, size_t len);
// Tenths of a degree, rounded half away from zero, e.g. 415 -> "42C".
void status_format_temp_c(int tenths, char *buf, size_t len);
// Copy at most max_chars characters (UTF-8 kept intact) into buf. Returns
// the number of characters copied.
size_t status_truncate_utf8(const char *utf8, size_t max_chars, char *buf, size_t len);
// Model id without the provider prefix ("nvidia/x" -> "x"); "--" for NULL.
const char *status_model_short(const char *model);

#endif // STATUS_FORMAT_H
```

`main/status_format.c`:

```c
#include "status_format.h"

#include <stdio.h>
#include <string.h>

#include "text_latin1.h"

// UTF-8 as octal escapes ("mié", "sáb"): octal stops after 3 digits, unlike \x.
static const char *const s_days[7] = {
    "dom", "lun", "mar", "mi\303\251", "jue", "vie", "s\303\241b",
};
static const char *const s_months[12] = {
    "ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic",
};

static int clock_synced(const struct tm *tm)
{
    return tm && tm->tm_year + 1900 >= 2024;
}

void status_format_clock(const struct tm *tm, char *buf, size_t len)
{
    if (!clock_synced(tm)) {
        snprintf(buf, len, "--:--");
        return;
    }
    snprintf(buf, len, "%02d:%02d", tm->tm_hour, tm->tm_min);
}

void status_format_date(const struct tm *tm, char *buf, size_t len)
{
    if (!clock_synced(tm) || tm->tm_wday < 0 || tm->tm_wday > 6 ||
        tm->tm_mon < 0 || tm->tm_mon > 11) {
        snprintf(buf, len, "--");
        return;
    }
    snprintf(buf, len, "%s %d %s", s_days[tm->tm_wday], tm->tm_mday, s_months[tm->tm_mon]);
}

void status_format_uptime(uint64_t seconds, char *buf, size_t len)
{
    unsigned long minutes = (unsigned long)((seconds / 60) % 60);
    unsigned long hours = (unsigned long)((seconds / 3600) % 24);
    unsigned long days = (unsigned long)(seconds / 86400);

    if (seconds < 3600) {
        snprintf(buf, len, "%lum", (unsigned long)(seconds / 60));
    } else if (seconds < 86400) {
        snprintf(buf, len, "%luh%02lum", hours, minutes);
    } else {
        snprintf(buf, len, "%lud %02luh", days, hours);
    }
}

void status_format_kib(uint32_t bytes, char *buf, size_t len)
{
    snprintf(buf, len, "%luK", (unsigned long)(bytes / 1024u));
}

void status_format_temp_c(int tenths, char *buf, size_t len)
{
    int whole = tenths >= 0 ? (tenths + 5) / 10 : -((-tenths + 5) / 10);
    snprintf(buf, len, "%dC", whole);
}

size_t status_truncate_utf8(const char *utf8, size_t max_chars, char *buf, size_t len)
{
    size_t chars = 0;
    size_t used = 0;
    const char *p = utf8;

    if (!buf || len == 0) {
        return 0;
    }
    buf[0] = '\0';
    if (!p) {
        return 0;
    }

    while (chars < max_chars) {
        const char *start = p;
        if (text_latin1_next(&p) == 0) {
            break;
        }
        size_t n = (size_t)(p - start);
        if (used + n >= len) {
            break;
        }
        memcpy(buf + used, start, n);
        used += n;
        chars++;
    }
    buf[used] = '\0';
    return chars;
}

const char *status_model_short(const char *model)
{
    if (!model) {
        return "--";
    }
    const char *slash = strrchr(model, '/');
    return slash ? slash + 1 : model;
}
```

- [ ] **Step 5: Run the tests** — all six `OK`.

- [ ] **Step 6: Commit**

```bash
git add main/status_format.[ch] test/host/test_status_format.c test/host/test_runner.c scripts/test.sh
git commit -m "feat(display): add status page formatting helpers"
```

---

### Task 7: Kconfig options and reserved GPIO pins

**Files:**
- Modify: `main/Kconfig.projbuild` (new `Display and SD card` menu)
- Modify: `main/gpio_policy.h`, `main/gpio_policy.c`
- Modify: `main/agent_prompt.c:46-61` (`build_gpio_policy_summary`)
- Modify: `test/host/test_tools_gpio_policy.c`

**Interfaces:**
- Produces:
  - Kconfig symbols: `CONFIG_ZCLAW_DISPLAY_ENABLE`, `CONFIG_ZCLAW_SD_ENABLE`, `CONFIG_ZCLAW_SPI_SCLK_GPIO`, `CONFIG_ZCLAW_SPI_MOSI_GPIO`, `CONFIG_ZCLAW_SPI_MISO_GPIO`, `CONFIG_ZCLAW_DISPLAY_CS_GPIO`, `CONFIG_ZCLAW_DISPLAY_DC_GPIO`, `CONFIG_ZCLAW_DISPLAY_RST_GPIO`, `CONFIG_ZCLAW_DISPLAY_BL_GPIO`, `CONFIG_ZCLAW_DISPLAY_INVERT_COLORS`, `CONFIG_ZCLAW_DISPLAY_X_GAP`, `CONFIG_ZCLAW_DISPLAY_Y_GAP`, `CONFIG_ZCLAW_DISPLAY_BRIGHTNESS`, `CONFIG_ZCLAW_FACE_SECONDS`, `CONFIG_ZCLAW_STATUS_SECONDS`, `CONFIG_ZCLAW_SD_CS_GPIO`.
  - `#define GPIO_POLICY_MAX_RESERVED 8`
  - `size_t gpio_policy_reserved_pins(int *out, size_t max);`
  - TEST_BUILD only: `void gpio_policy_test_set_reserved(const int *pins, size_t count);` (`count == 0` restores the Kconfig-derived list).

- [ ] **Step 1: Write the failing tests** — append to `test/host/test_tools_gpio_policy.c` (before its `_all` function), and add `#include "gpio_policy.h"` at the top if missing:

```c
bool tools_validate_allowed_gpio_pin(int pin, const char *field_name, char *error, size_t error_len);

TEST(reserved_pins_are_rejected_for_tools)
{
    static const int reserved[] = {5, 6, 7, 10, 2, 3};
    char err[160];

    gpio_policy_test_set_reserved(reserved, sizeof(reserved) / sizeof(reserved[0]));
    ASSERT(!gpio_policy_pin_is_allowed(5));
    ASSERT(!gpio_policy_pin_is_allowed(2));
    ASSERT(gpio_policy_pin_is_allowed(4));
    ASSERT(gpio_policy_pin_is_allowed(9));
    ASSERT(!gpio_policy_runtime_input_pin_is_safe(6));
    ASSERT(gpio_policy_runtime_input_pin_is_safe(9));

    ASSERT(gpio_policy_pin_forbidden_hint(7, err, sizeof(err)));
    ASSERT_STR_CONTAINS(err, "reserved for the display/SD SPI bus");

    err[0] = '\0';
    ASSERT(!tools_validate_allowed_gpio_pin(6, "pin", err, sizeof(err)));
    ASSERT_STR_CONTAINS(err, "reserved");

    int out[GPIO_POLICY_MAX_RESERVED];
    ASSERT(gpio_policy_reserved_pins(out, GPIO_POLICY_MAX_RESERVED) == 6);

    gpio_policy_test_set_reserved(NULL, 0);
    ASSERT(gpio_policy_pin_is_allowed(5));
    ASSERT(gpio_policy_reserved_pins(out, GPIO_POLICY_MAX_RESERVED) == 0);
    return 0;
}

TEST(reserved_list_deduplicates_shared_pins)
{
    static const int reserved[] = {6, 5, 6, 5, 7};
    int out[GPIO_POLICY_MAX_RESERVED];

    gpio_policy_test_set_reserved(reserved, sizeof(reserved) / sizeof(reserved[0]));
    ASSERT(gpio_policy_reserved_pins(out, GPIO_POLICY_MAX_RESERVED) == 3);
    gpio_policy_test_set_reserved(NULL, 0);
    return 0;
}
```

and register both in that file's `test_tools_gpio_policy_all()` using its existing print/OK pattern.

- [ ] **Step 2: Run to verify it fails** — `./scripts/test.sh host 2>&1 | grep -m3 -E "gpio_policy_test_set_reserved|GPIO_POLICY_MAX_RESERVED"` → undeclared.

- [ ] **Step 3: Add the Kconfig menu** — in `main/Kconfig.projbuild`, insert before the `menu "Factory Reset"` block:

```kconfig
    menu "Display and SD card"
        config ZCLAW_DISPLAY_ENABLE
            bool "Enable ST7789 240x240 SPI display"
            default n
            help
                Animated face (frames from the SD card) alternating with a
                status page. Shares SPI2 with the SD card.

        config ZCLAW_SD_ENABLE
            bool "Enable SD card (FAT) on the shared SPI bus"
            default n

        config ZCLAW_SPI_SCLK_GPIO
            int "Shared SPI SCLK GPIO"
            range 0 48
            default 6
            depends on ZCLAW_DISPLAY_ENABLE || ZCLAW_SD_ENABLE

        config ZCLAW_SPI_MOSI_GPIO
            int "Shared SPI MOSI GPIO"
            range 0 48
            default 5
            depends on ZCLAW_DISPLAY_ENABLE || ZCLAW_SD_ENABLE

        config ZCLAW_SPI_MISO_GPIO
            int "Shared SPI MISO GPIO (SD card only)"
            range 0 48
            default 2
            depends on ZCLAW_SD_ENABLE

        config ZCLAW_DISPLAY_CS_GPIO
            int "Display CS GPIO"
            range 0 48
            default 7
            depends on ZCLAW_DISPLAY_ENABLE

        config ZCLAW_DISPLAY_DC_GPIO
            int "Display DC GPIO"
            range 0 48
            default 1
            depends on ZCLAW_DISPLAY_ENABLE

        config ZCLAW_DISPLAY_RST_GPIO
            int "Display reset GPIO"
            range 0 48
            default 0
            depends on ZCLAW_DISPLAY_ENABLE

        config ZCLAW_DISPLAY_BL_GPIO
            int "Display backlight GPIO (PWM)"
            range 0 48
            default 10
            depends on ZCLAW_DISPLAY_ENABLE

        config ZCLAW_DISPLAY_INVERT_COLORS
            bool "Invert display colours (most IPS ST7789 modules need this)"
            default y
            depends on ZCLAW_DISPLAY_ENABLE

        config ZCLAW_DISPLAY_X_GAP
            int "Display column offset"
            range 0 80
            default 0
            depends on ZCLAW_DISPLAY_ENABLE

        config ZCLAW_DISPLAY_Y_GAP
            int "Display row offset"
            range 0 80
            default 0
            depends on ZCLAW_DISPLAY_ENABLE

        config ZCLAW_DISPLAY_BRIGHTNESS
            int "Backlight brightness (%)"
            range 0 100
            default 80
            depends on ZCLAW_DISPLAY_ENABLE

        config ZCLAW_FACE_SECONDS
            int "Seconds on the face page"
            range 1 600
            default 10
            depends on ZCLAW_DISPLAY_ENABLE

        config ZCLAW_STATUS_SECONDS
            int "Seconds on the status page (0 = never)"
            range 0 600
            default 5
            depends on ZCLAW_DISPLAY_ENABLE

        config ZCLAW_SD_CS_GPIO
            int "SD card CS GPIO"
            range 0 48
            default 3
            depends on ZCLAW_SD_ENABLE
    endmenu
```

- [ ] **Step 4: Implement reserved pins** — in `main/gpio_policy.h` add after the existing prototypes:

```c
#define GPIO_POLICY_MAX_RESERVED 8

// Pins owned by enabled peripherals (display / SD SPI bus). Returns the count copied.
size_t gpio_policy_reserved_pins(int *out, size_t max);
```

and inside the `#ifdef TEST_BUILD` block:

```c
// Override the reserved list; count == 0 restores the Kconfig-derived list.
void gpio_policy_test_set_reserved(const int *pins, size_t count);
```

In `main/gpio_policy.c`, add `#include "config.h"` if not already included, then above `pin_is_allowed_impl`:

```c
static int s_reserved[GPIO_POLICY_MAX_RESERVED];
static size_t s_reserved_count;
static bool s_reserved_ready;

static void reserved_add(int pin)
{
    if (pin < 0) {
        return;
    }
    for (size_t i = 0; i < s_reserved_count; i++) {
        if (s_reserved[i] == pin) {
            return;
        }
    }
    if (s_reserved_count < GPIO_POLICY_MAX_RESERVED) {
        s_reserved[s_reserved_count++] = pin;
    }
}

static void reserved_load_from_config(void)
{
    s_reserved_count = 0;
#if CONFIG_ZCLAW_DISPLAY_ENABLE || CONFIG_ZCLAW_SD_ENABLE
    reserved_add(CONFIG_ZCLAW_SPI_SCLK_GPIO);
    reserved_add(CONFIG_ZCLAW_SPI_MOSI_GPIO);
#endif
#if CONFIG_ZCLAW_DISPLAY_ENABLE
    reserved_add(CONFIG_ZCLAW_DISPLAY_CS_GPIO);
    reserved_add(CONFIG_ZCLAW_DISPLAY_DC_GPIO);
    reserved_add(CONFIG_ZCLAW_DISPLAY_RST_GPIO);
    reserved_add(CONFIG_ZCLAW_DISPLAY_BL_GPIO);
#endif
#if CONFIG_ZCLAW_SD_ENABLE
    reserved_add(CONFIG_ZCLAW_SPI_MISO_GPIO);
    reserved_add(CONFIG_ZCLAW_SD_CS_GPIO);
#endif
    s_reserved_ready = true;
}

static bool pin_is_reserved(int pin)
{
    if (!s_reserved_ready) {
        reserved_load_from_config();
    }
    for (size_t i = 0; i < s_reserved_count; i++) {
        if (s_reserved[i] == pin) {
            return true;
        }
    }
    return false;
}

size_t gpio_policy_reserved_pins(int *out, size_t max)
{
    size_t n;

    if (!s_reserved_ready) {
        reserved_load_from_config();
    }
    n = s_reserved_count < max ? s_reserved_count : max;
    for (size_t i = 0; out && i < n; i++) {
        out[i] = s_reserved[i];
    }
    return out ? n : 0;
}
```

In `pin_is_allowed_impl`, right after the `if (pin < 0) { return false; }` block, add:

```c
    if (pin_is_reserved(pin)) {
        return false;
    }
```

Replace the body of `gpio_policy_pin_forbidden_hint` with:

```c
bool gpio_policy_pin_forbidden_hint(int pin, char *result, size_t result_len)
{
    if (pin >= 0 && pin_is_reserved(pin)) {
        snprintf(result, result_len,
                 "Error: pin %d is reserved for the display/SD SPI bus; choose a different pin",
                 pin);
        return true;
    }
#if defined(CONFIG_IDF_TARGET_ESP32)
    if (pin >= 6 && pin <= 11) {
        snprintf(result, result_len,
                 "Error: pin %d is reserved for ESP32 flash/PSRAM (GPIO6-11); choose a different pin",
                 pin);
        return true;
    }
#endif
    return false;
}
```

At the end of the existing `#ifdef TEST_BUILD` section add:

```c
void gpio_policy_test_set_reserved(const int *pins, size_t count)
{
    if (!pins || count == 0) {
        reserved_load_from_config();
        return;
    }
    s_reserved_count = 0;
    for (size_t i = 0; i < count; i++) {
        reserved_add(pins[i]);
    }
    s_reserved_ready = true;
}
```

- [ ] **Step 5: Tell the LLM about reserved pins** — in `main/agent_prompt.c`, add `#include "gpio_policy.h"` and replace `build_gpio_policy_summary` with:

```c
static void append_reserved_pins(char *buf, size_t buf_len)
{
    int pins[GPIO_POLICY_MAX_RESERVED];
    size_t count = gpio_policy_reserved_pins(pins, GPIO_POLICY_MAX_RESERVED);
    size_t used = strlen(buf);

    if (count == 0 || used + 1 >= buf_len) {
        return;
    }
    used += (size_t)snprintf(buf + used, buf_len - used, " Pins");
    for (size_t i = 0; i < count && used < buf_len; i++) {
        used += (size_t)snprintf(buf + used, buf_len - used, "%s %d", i == 0 ? "" : ",", pins[i]);
    }
    if (used < buf_len) {
        snprintf(buf + used, buf_len - used, " are reserved for the display/SD bus.");
    }
}

static void build_gpio_policy_summary(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) {
        return;
    }

    if (GPIO_ALLOWED_PINS_CSV[0] != '\0') {
        snprintf(buf, buf_len,
                 "Tool-safe GPIO pins on this device are restricted to allowlist: %s.",
                 GPIO_ALLOWED_PINS_CSV);
    } else {
        snprintf(buf, buf_len,
                 "Tool-safe GPIO pins on this device are restricted to range %d-%d.",
                 GPIO_MIN_PIN, GPIO_MAX_PIN);
    }
    append_reserved_pins(buf, buf_len);
}
```

(`agent_prompt.c` already includes `<string.h>` via its existing includes; if the compiler reports `strlen` undeclared, add `#include <string.h>`.)

- [ ] **Step 6: Run the tests** — `./scripts/test.sh host 2>&1 | tail -5` → `All tests passed!` (the reserved list is empty in host builds unless a test overrides it, so existing prompt/GPIO tests are unaffected).

- [ ] **Step 7: Firmware build still compiles with the feature off** — `source ~/esp/esp-idf/export.sh >/dev/null 2>&1 && idf.py build 2>&1 | tail -3` → `Project build complete`.

- [ ] **Step 8: Commit**

```bash
git add main/Kconfig.projbuild main/gpio_policy.[ch] main/agent_prompt.c test/host/test_tools_gpio_policy.c
git commit -m "feat(display): add display/SD Kconfig and reserve bus pins from GPIO tools"
```

---

### Task 8: Host-side face asset tool

**Files:**
- Create: `scripts/face_assets.py`, `scripts/requirements-face.txt`
- Create: `assets/face/README.md`
- Create: `test/host/test_face_assets.py`
- Modify: `.gitignore`

**Interfaces:**
- Produces: `.r565` files in the output folder (`neutral.r565`, `thinking.r565`, `sleeping.r565`, `happy.r565`, `angry.r565`, `speech.r565` as full frames; `eyes_closed.r565`, `speaking.r565` and numbered variants as patches) plus `contact.png`. Python API used by the tests: `face_assets.run(argv: list[str]) -> int`.

- [ ] **Step 1: Ignore source photos** — append to `.gitignore`:

```gitignore
# Face source photos (AI-edited portraits) stay local; only the tool is versioned
/assets/face/*
!/assets/face/README.md
```

Run: `git status --short assets/` → nothing listed (the PNGs are now ignored).

- [ ] **Step 2: Dependencies file** — `scripts/requirements-face.txt`:

```text
Pillow>=10.0
numpy>=1.26
```

Create the tool venv (outside the repo):

```bash
python3 -m venv ~/.venvs/zclaw-face && ~/.venvs/zclaw-face/bin/pip install -q -r scripts/requirements-face.txt
```

- [ ] **Step 3: Write the failing test** — `test/host/test_face_assets.py`

```python
#!/usr/bin/env python3
"""Host tests for scripts/face_assets.py (skipped when Pillow/numpy are missing)."""

from __future__ import annotations

import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

TEST_DIR = Path(__file__).resolve().parent
SCRIPT = TEST_DIR.parent.parent / "scripts" / "face_assets.py"

try:
    import numpy as np
    from PIL import Image, ImageDraw
    HAVE_DEPS = True
except ImportError:  # pragma: no cover - depends on the environment
    HAVE_DEPS = False


def load_tool():
    spec = importlib.util.spec_from_file_location("face_assets", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_frame(path: Path):
    data = path.read_bytes()
    magic = data[:4]
    x, y, w, h, flags, _ = struct.unpack("<HHHHHH", data[4:16])
    pixels = np.frombuffer(data[16:], dtype=">u2").reshape(h, w)
    return magic, x, y, w, h, flags, pixels, len(data)


def portrait(eyes_open: bool = True, mouth_open: bool = False) -> "Image.Image":
    img = Image.new("RGB", (320, 420), (40, 50, 50))
    draw = ImageDraw.Draw(img)
    draw.ellipse((70, 60, 250, 330), fill=(205, 160, 130))           # face
    draw.rectangle((70, 260, 250, 400), fill=(30, 25, 20))            # beard
    eye = (250, 250, 250) if eyes_open else (180, 130, 110)
    draw.ellipse((110, 160, 150, 185), fill=eye)
    draw.ellipse((170, 160, 210, 185), fill=eye)
    if mouth_open:
        draw.ellipse((135, 280, 185, 315), fill=(90, 20, 20))
    return img


@unittest.skipUnless(HAVE_DEPS, "Pillow and numpy are required")
class FaceAssetsTest(unittest.TestCase):
    def setUp(self):
        self.tool = load_tool()
        self.tmp = tempfile.TemporaryDirectory()
        self.src = Path(self.tmp.name) / "in"
        self.out = Path(self.tmp.name) / "out"
        self.src.mkdir()
        portrait().save(self.src / "neutral.png")
        portrait(eyes_open=False).save(self.src / "eyes_closed.png")
        portrait(mouth_open=True).save(self.src / "speaking.png")
        portrait(mouth_open=True).save(self.src / "speaking_2.png")
        portrait().save(self.src / "thinking.png")

    def tearDown(self):
        self.tmp.cleanup()

    def run_tool(self, *extra: str) -> int:
        return self.tool.run(["--input", str(self.src), "--output", str(self.out), *extra])

    def test_full_frames_have_valid_headers(self):
        self.assertEqual(self.run_tool(), 0)
        for name in ("neutral", "thinking"):
            magic, x, y, w, h, flags, _, size = read_frame(self.out / f"{name}.r565")
            self.assertEqual(magic, b"R565")
            self.assertEqual((x, y, w, h, flags), (0, 0, 240, 240, 0))
            self.assertEqual(size, 16 + 240 * 240 * 2)

    def test_patches_are_small_and_flagged(self):
        self.assertEqual(self.run_tool(), 0)
        for name in ("eyes_closed", "speaking", "speaking_2"):
            magic, x, y, w, h, flags, _, size = read_frame(self.out / f"{name}.r565")
            self.assertEqual(magic, b"R565")
            self.assertEqual(flags & 1, 1)
            self.assertLess(w * h, 240 * 240 // 4)
            self.assertLessEqual(x + w, 240)
            self.assertLessEqual(y + h, 240)
            self.assertEqual(size, 16 + w * h * 2)

    def test_patch_border_matches_neutral(self):
        self.assertEqual(self.run_tool(), 0)
        *_, neutral, _ = read_frame(self.out / "neutral.r565")
        _, x, y, w, h, _, patch, _ = read_frame(self.out / "eyes_closed.r565")
        np.testing.assert_array_equal(patch[0, :], neutral[y, x:x + w])
        np.testing.assert_array_equal(patch[:, 0], neutral[y:y + h, x])
        self.assertFalse(np.array_equal(patch, neutral[y:y + h, x:x + w]))

    def test_manual_band_override(self):
        self.assertEqual(self.run_tool("--eyes", "60,80,120,30"), 0)
        _, x, y, w, h, *_ = read_frame(self.out / "eyes_closed.r565")
        self.assertEqual((x, y, w, h), (60, 80, 120, 30))

    def test_missing_neutral_fails_cleanly(self):
        (self.src / "neutral.png").unlink()
        self.assertEqual(self.run_tool(), 2)
        self.assertFalse(self.out.exists() and any(self.out.glob("*.r565")))

    def test_contact_sheet_written(self):
        self.assertEqual(self.run_tool(), 0)
        self.assertTrue((self.out / "contact.png").exists())


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 4: Run to verify it fails**

Run: `cd test/host && ~/.venvs/zclaw-face/bin/python -m unittest -q test_face_assets.py; cd ../..`
Expected: errors loading `scripts/face_assets.py` (file not found).

- [ ] **Step 5: Implement the tool** — `scripts/face_assets.py`

```python
#!/usr/bin/env python3
"""Convert face photos into .r565 frames for Fidel's ST7789 display.

Input folder: neutral.png (required) plus any of eyes_closed, speaking,
thinking, sleeping, happy, angry, speech (.png/.jpg), and numbered variants
such as speaking_2.png ... speaking_8.png.

Output: <name>.r565 files to copy into the SD card's face/ folder, and a
contact sheet for review. eyes_closed/speaking become small patches drawn
over neutral, so blinking and talking never shimmer.

    ~/.venvs/zclaw-face/bin/python scripts/face_assets.py \\
        --input assets/face --output build/face
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

FULL_FRAMES = ("neutral", "thinking", "sleeping", "happy", "angry", "speech")
PATCH_FRAMES = {"eyes_closed": "eyes", "speaking": "mouth"}
ALIGNED_FRAMES = ("eyes_closed", "speaking", "happy", "angry")
MAX_VARIANTS = 8
EXTENSIONS = (".png", ".jpg", ".jpeg")
FLAG_PATCH = 0x0001
BAND_PAD = 10
FEATHER = 6
# Search windows for automatic band detection, as fractions of the frame.
BAND_WINDOWS = {"eyes": (0.25, 0.60), "mouth": (0.50, 0.85)}
BAND_X_WINDOW = (0.15, 0.85)


def find_inputs(folder: Path) -> dict[str, list[tuple[int, Path]]]:
    """Map base name -> [(variant number, path)] sorted by variant."""
    found: dict[str, list[tuple[int, Path]]] = {}
    for base in (*FULL_FRAMES, *PATCH_FRAMES):
        for variant in range(1, MAX_VARIANTS + 1):
            stem = base if variant == 1 else f"{base}_{variant}"
            for ext in EXTENSIONS:
                path = folder / f"{stem}{ext}"
                if path.exists():
                    found.setdefault(base, []).append((variant, path))
                    break
    return found


def output_name(base: str, variant: int) -> str:
    return f"{base}.r565" if variant == 1 else f"{base}_{variant}.r565"


def border_color(img: Image.Image) -> tuple[int, int, int]:
    top = np.asarray(img)[:5].reshape(-1, 3)
    return tuple(int(v) for v in np.median(top, axis=0))


def blurred_gray(img: Image.Image) -> np.ndarray:
    return np.asarray(img.convert("L").filter(ImageFilter.GaussianBlur(2)), dtype=np.float32)


def phase_shift(ref: np.ndarray, moving: np.ndarray) -> tuple[int, int, float]:
    """Integer (dx, dy) that aligns `moving` onto `ref`, and the correlation peak."""
    h, w = ref.shape
    window = np.outer(np.hanning(h), np.hanning(w)).astype(np.float32)
    a = np.fft.fft2((ref - ref.mean()) * window)
    b = np.fft.fft2((moving - moving.mean()) * window)
    cross = a * np.conj(b)
    corr = np.fft.ifft2(cross / (np.abs(cross) + 1e-9)).real
    dy, dx = np.unravel_index(int(np.argmax(corr)), corr.shape)
    if dy > h // 2:
        dy -= h
    if dx > w // 2:
        dx -= w
    return int(dx), int(dy), float(corr.max())


def align_to(ref: Image.Image, img: Image.Image, min_peak: float, name: str) -> Image.Image:
    fill = border_color(ref)
    canvas = Image.new("RGB", ref.size, fill)
    canvas.paste(img, (0, 0))
    dx, dy, peak = phase_shift(blurred_gray(ref), blurred_gray(canvas))
    if peak < min_peak:
        print(f"warning: {name}: alignment not confident (peak {peak:.3f}), using as is")
        return canvas
    return canvas.transform(canvas.size, Image.AFFINE, (1, 0, -dx, 0, 1, -dy), fillcolor=fill)


def square_crop(img: Image.Image, size: int, center_y: float) -> Image.Image:
    w, h = img.size
    side = min(w, h)
    left = (w - side) // 2
    top = int(round(h * center_y - side / 2))
    top = max(0, min(top, h - side))
    return img.crop((left, top, left + side, top + side)).resize((size, size), Image.LANCZOS)


def detect_band(base: Image.Image, frame: Image.Image, region: str) -> tuple[int, int, int, int]:
    """(x, y, w, h) of the strongest difference inside the region's window."""
    size = base.size[0]
    diff = np.abs(np.asarray(base, dtype=np.float32) - np.asarray(frame, dtype=np.float32)).sum(axis=2)
    diff8 = np.clip(diff / 3, 0, 255).astype(np.uint8)
    blurred = np.asarray(Image.fromarray(diff8, "L").filter(ImageFilter.BoxBlur(6)), dtype=np.float32)

    y0, y1 = (int(size * f) for f in BAND_WINDOWS[region])
    x0, x1 = (int(size * f) for f in BAND_X_WINDOW)
    window = blurred[y0:y1, x0:x1]
    if window.max() <= 0:
        raise ValueError(f"no difference found in the {region} window")
    ys, xs = np.nonzero(window > window.max() * 0.45)
    left = max(int(xs.min()) + x0 - BAND_PAD, 0)
    top = max(int(ys.min()) + y0 - BAND_PAD, 0)
    right = min(int(xs.max()) + x0 + BAND_PAD + 1, size)
    bottom = min(int(ys.max()) + y0 + BAND_PAD + 1, size)
    return left, top, right - left, bottom - top


def feathered_mask(size: int, box: tuple[int, int, int, int]) -> Image.Image:
    x, y, w, h = box
    mask = Image.new("L", (size, size), 0)
    inner = (x + FEATHER, y + FEATHER, x + w - FEATHER, y + h - FEATHER)
    if inner[2] > inner[0] and inner[3] > inner[1]:
        mask.paste(255, inner)
    mask = mask.filter(ImageFilter.GaussianBlur(FEATHER / 2))
    clipped = Image.new("L", (size, size), 0)
    clipped.paste(mask.crop((x, y, x + w, y + h)), (x, y))
    # The blur tail can leave a few levels on the box edge; force the edge to
    # neutral so restoring the patch area is seamless.
    ImageDraw.Draw(clipped).rectangle((x, y, x + w - 1, y + h - 1), outline=0)
    return clipped


def to_rgb565_be(img: Image.Image) -> bytes:
    a = np.asarray(img.convert("RGB"), dtype=np.uint16)
    v = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
    return v.astype(">u2").tobytes()


def write_r565(path: Path, img: Image.Image, x: int = 0, y: int = 0, flags: int = 0) -> None:
    w, h = img.size
    header = b"R565" + struct.pack("<HHHHHH", x, y, w, h, flags, 0)
    path.write_bytes(header + to_rgb565_be(img))


def parse_box(text: str | None) -> tuple[int, int, int, int] | None:
    if not text:
        return None
    parts = [int(p) for p in text.split(",")]
    if len(parts) != 4 or parts[2] <= 0 or parts[3] <= 0:
        raise argparse.ArgumentTypeError("expected X,Y,W,H with positive W and H")
    return parts[0], parts[1], parts[2], parts[3]


def contact_sheet(tiles: list[tuple[str, Image.Image]], path: Path) -> None:
    size = tiles[0][1].size[0]
    cols = 4
    rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * size, rows * (size + 16)), "white")
    draw = ImageDraw.Draw(sheet)
    for i, (label, img) in enumerate(tiles):
        x, y = (i % cols) * size, (i // cols) * (size + 16)
        sheet.paste(img, (x, y + 16))
        draw.text((x + 4, y + 2), label, fill="black")
    sheet.save(path)


def run(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Build .r565 face frames for the SD card.")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--size", type=int, default=240)
    parser.add_argument("--center-y", type=float, default=0.5,
                        help="vertical crop centre as a fraction of the image height")
    parser.add_argument("--eyes", type=parse_box, help="eye band X,Y,W,H (overrides detection)")
    parser.add_argument("--mouth", type=parse_box, help="mouth band X,Y,W,H (overrides detection)")
    parser.add_argument("--min-peak", type=float, default=0.02,
                        help="minimum phase-correlation peak to apply alignment")
    parser.add_argument("--contact-sheet", type=Path, default=None)
    args = parser.parse_args(argv)

    inputs = find_inputs(args.input)
    if "neutral" not in inputs:
        print(f"error: {args.input}/neutral.png (or .jpg) is required", file=sys.stderr)
        return 2

    args.output.mkdir(parents=True, exist_ok=True)
    neutral_src = Image.open(inputs["neutral"][0][1]).convert("RGB")
    neutral = square_crop(neutral_src, args.size, args.center_y)
    overrides = {"eyes": args.eyes, "mouth": args.mouth}
    tiles: list[tuple[str, Image.Image]] = []

    for base, variants in inputs.items():
        for variant, path in variants:
            name = output_name(base, variant)
            src = Image.open(path).convert("RGB")
            if base == "neutral" and variant == 1:
                frame = neutral
            else:
                if base in ALIGNED_FRAMES:
                    src = align_to(neutral_src, src, args.min_peak, name)
                frame = square_crop(src, args.size, args.center_y)

            if base in PATCH_FRAMES:
                region = PATCH_FRAMES[base]
                try:
                    box = overrides[region] or detect_band(neutral, frame, region)
                except ValueError as exc:
                    print(f"error: {name}: {exc}; pass --{region} X,Y,W,H", file=sys.stderr)
                    return 2
                x, y, w, h = box
                if x < 0 or y < 0 or x + w > args.size or y + h > args.size:
                    print(f"error: {region} band {box} is outside the {args.size}px frame",
                          file=sys.stderr)
                    return 2
                composed = Image.composite(frame, neutral, feathered_mask(args.size, box))
                write_r565(args.output / name, composed.crop((x, y, x + w, y + h)), x, y, FLAG_PATCH)
                tiles.append((f"{name} {box}", composed))
                print(f"{name}: patch {box}")
            else:
                write_r565(args.output / name, frame)
                tiles.append((name, frame))
                print(f"{name}: full frame")

    contact_sheet(tiles, args.contact_sheet or (args.output / "contact.png"))
    print(f"done: {len(tiles)} frames in {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
```

- [ ] **Step 6: Run the tests**

Run: `cd test/host && ~/.venvs/zclaw-face/bin/python -m unittest -v test_face_assets.py 2>&1 | tail -10; cd ../..`
Expected: 6 tests `ok`. Also confirm the system Python skips them cleanly: `cd test/host && python3 -m unittest -q test_face_assets.py; cd ../..` → `OK (skipped=6)`.

- [ ] **Step 7: Register in the Python suite** — in `scripts/test.sh`, add `test_face_assets.py` to the `python3 -m unittest -q` list (it self-skips without Pillow). Run `./scripts/test.sh host 2>&1 | tail -4` → passes.

- [ ] **Step 8: Document inputs** — `assets/face/README.md`:

```markdown
# Face source photos

Put the source photos for Fidel's face here (they are git-ignored). Then run:

    python3 -m venv ~/.venvs/zclaw-face
    ~/.venvs/zclaw-face/bin/pip install -r scripts/requirements-face.txt
    ~/.venvs/zclaw-face/bin/python scripts/face_assets.py --input assets/face --output build/face

Copy `build/face/*.r565` into a `face/` folder at the root of the SD card and
review `build/face/contact.png`.

| File | Use |
|---|---|
| `neutral` (required) | Idle face; base for the patches |
| `eyes_closed` | Same photo with eyes closed → blink patch |
| `speaking` (+ `speaking_2`…) | Mouth open → talking patch |
| `thinking` (+ variants) | While the LLM works |
| `sleeping` | WiFi down |
| `happy`, `angry`, `speech` | Expressions for the LLM (Delivery 3) |

Guidelines: same base photo for all expressions, only the expression edited
(head, framing, background and light unchanged); square-ish close-up with the
face filling ~70% of the frame; plain background. If the automatic eye/mouth
detection picks the wrong area, pass `--eyes X,Y,W,H` / `--mouth X,Y,W,H`
(coordinates in the 240×240 output).
```

- [ ] **Step 9: Build the real face set and review it**

```bash
~/.venvs/zclaw-face/bin/python scripts/face_assets.py --input assets/face --output build/face
ls -la build/face
```

Expected: 8 `.r565` files (`neutral`, `eyes_closed`, `speaking`, `thinking`, `sleeping`, `happy`, `angry`, `speech`), full frames 115,216 bytes, patches a few KB, plus `contact.png`. Open `build/face/contact.png` and show it to the user; the blink and talk composites must look seamless.

- [ ] **Step 10: Commit**

```bash
git add .gitignore scripts/face_assets.py scripts/requirements-face.txt assets/face/README.md test/host/test_face_assets.py scripts/test.sh
git commit -m "feat(display): add face asset tool producing .r565 frames"
```

---

### Task 9: SPI bus, ST7789 driver and text rendering (hardware bring-up 1)

**Files:**
- Create: `main/board_spi.h`, `main/board_spi.c`
- Create: `main/display_hw.h`, `main/display_hw.c`
- Create: `main/display_gfx.h`, `main/display_gfx.c`
- Create: `main/display.h`, `main/display.c` (temporary self-test body, replaced in Task 12)
- Modify: `main/CMakeLists.txt`, `main/main.c`, `sdkconfig.defaults` (tracked), local `sdkconfig.defaults.esp32c6` (git-ignored)

**Interfaces:**
- Consumes: `font8x16_latin1`, `FONT_GLYPH_W/H` (Task 2); `text_latin1_next`, `text_latin1_length` (Task 2); Kconfig symbols (Task 7).
- Produces:

```c
// board_spi.h
#define BOARD_SPI_HOST SPI2_HOST
#define BOARD_SPI_MAX_TRANSFER (240 * 16 * 2 + 64)
esp_err_t board_spi_init(void);               // idempotent; ESP_ERR_NOT_SUPPORTED when both features are off

// display_hw.h
#define DISPLAY_W 240
#define DISPLAY_H 240
#define DISPLAY_STRIP_PIXELS (DISPLAY_W * 16)
esp_err_t display_hw_init(void);
bool display_hw_ready(void);
uint16_t *display_hw_acquire_buffer(void);    // DMA buffer of DISPLAY_STRIP_PIXELS, blocks until free
void display_hw_release_buffer(uint16_t *buf);// give back an acquired buffer that was not blitted
esp_err_t display_hw_blit(int x, int y, int w, int h, uint16_t *buf); // big-endian RGB565
void display_hw_wait_idle(void);
void display_hw_set_brightness(uint8_t percent);

// display_gfx.h
typedef uint16_t gfx_color_t;                 // RGB565, host byte order
#define GFX_RGB(r, g, b) ((gfx_color_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define GFX_BLACK GFX_RGB(0, 0, 0)
#define GFX_WHITE GFX_RGB(255, 255, 255)
#define GFX_GRAY  GFX_RGB(150, 150, 150)
#define GFX_AMBER GFX_RGB(255, 191, 0)
void gfx_fill_rect(int x, int y, int w, int h, gfx_color_t color);
int gfx_text(int x, int y, const char *utf8, gfx_color_t fg, gfx_color_t bg, int scale);
int gfx_text_width(const char *utf8, int scale);

// display.h (final API; Task 9 implements display_start as a self-test)
esp_err_t display_start(void);
void display_notify_thinking(bool active);
void display_notify_talking(size_t reply_chars);
void display_notify_wifi(bool connected);
```

- [ ] **Step 1: `main/board_spi.h` / `main/board_spi.c`**

```c
#ifndef BOARD_SPI_H
#define BOARD_SPI_H

#include "driver/spi_common.h"
#include "esp_err.h"

#define BOARD_SPI_HOST SPI2_HOST
#define BOARD_SPI_MAX_TRANSFER (240 * 16 * 2 + 64)

// Initialise the SPI2 bus shared by the display and the SD card. Idempotent.
esp_err_t board_spi_init(void);

#endif // BOARD_SPI_H
```

```c
#include "board_spi.h"

#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "board_spi";
static bool s_ready = false;

#if CONFIG_ZCLAW_DISPLAY_ENABLE || CONFIG_ZCLAW_SD_ENABLE
static void deselect(int gpio)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(gpio, 1);
}
#endif

esp_err_t board_spi_init(void)
{
#if CONFIG_ZCLAW_DISPLAY_ENABLE || CONFIG_ZCLAW_SD_ENABLE
    if (s_ready) {
        return ESP_OK;
    }

    // Keep every chip select high before the bus clocks anything, so the SD
    // card never sees display traffic during init and vice versa.
#if CONFIG_ZCLAW_DISPLAY_ENABLE
    deselect(CONFIG_ZCLAW_DISPLAY_CS_GPIO);
#endif
#if CONFIG_ZCLAW_SD_ENABLE
    deselect(CONFIG_ZCLAW_SD_CS_GPIO);
#endif

    spi_bus_config_t cfg = {
        .sclk_io_num = CONFIG_ZCLAW_SPI_SCLK_GPIO,
        .mosi_io_num = CONFIG_ZCLAW_SPI_MOSI_GPIO,
#if CONFIG_ZCLAW_SD_ENABLE
        .miso_io_num = CONFIG_ZCLAW_SPI_MISO_GPIO,
#else
        .miso_io_num = -1,
#endif
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOARD_SPI_MAX_TRANSFER,
    };
    esp_err_t err = spi_bus_initialize(BOARD_SPI_HOST, &cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }
    s_ready = true;
    ESP_LOGI(TAG, "SPI2 ready (SCLK %d, MOSI %d)", CONFIG_ZCLAW_SPI_SCLK_GPIO,
             CONFIG_ZCLAW_SPI_MOSI_GPIO);
    return ESP_OK;
#else
    (void)TAG;
    (void)s_ready;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
```

- [ ] **Step 2: `main/display_hw.h` / `main/display_hw.c`**

```c
#ifndef DISPLAY_HW_H
#define DISPLAY_HW_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define DISPLAY_W 240
#define DISPLAY_H 240
#define DISPLAY_STRIP_PIXELS (DISPLAY_W * 16)

esp_err_t display_hw_init(void);
bool display_hw_ready(void);

// One of two DMA buffers (DISPLAY_STRIP_PIXELS pixels). Blocks until the
// buffer's previous transfer has finished. Hand it to display_hw_blit(), or
// back with display_hw_release_buffer() if it ends up unused.
uint16_t *display_hw_acquire_buffer(void);
void display_hw_release_buffer(uint16_t *buf);

// Queue w*h big-endian RGB565 pixels at (x, y). The buffer is released
// automatically when the transfer completes.
esp_err_t display_hw_blit(int x, int y, int w, int h, uint16_t *buf);

// Wait until no transfer is in flight.
void display_hw_wait_idle(void);

void display_hw_set_brightness(uint8_t percent);

#endif // DISPLAY_HW_H
```

```c
#include "display_hw.h"

#include <string.h>

#include "board_spi.h"
#include "config.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if CONFIG_ZCLAW_DISPLAY_ENABLE

#define DISPLAY_PCLK_HZ   (40 * 1000 * 1000)
#define BACKLIGHT_TIMER   LEDC_TIMER_0
#define BACKLIGHT_CHANNEL LEDC_CHANNEL_0

static const char *TAG = "display_hw";
static esp_lcd_panel_io_handle_t s_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static uint16_t *s_buf[2] = {NULL, NULL};
static int s_next_buf = 0;
static SemaphoreHandle_t s_free = NULL;
static bool s_ready = false;

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata,
                          void *ctx)
{
    (void)io;
    (void)edata;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_free, &woken);
    return woken == pdTRUE;
}

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BACKLIGHT_TIMER,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .gpio_num = CONFIG_ZCLAW_DISPLAY_BL_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BACKLIGHT_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&channel);
}

void display_hw_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_CHANNEL, (1023u * percent) / 100u);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_CHANNEL);
}

static void release_resources(void)
{
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_io) {
        esp_lcd_panel_io_del(s_io);
        s_io = NULL;
    }
    for (int i = 0; i < 2; i++) {
        heap_caps_free(s_buf[i]);
        s_buf[i] = NULL;
    }
    if (s_free) {
        vSemaphoreDelete(s_free);
        s_free = NULL;
    }
}

esp_err_t display_hw_init(void)
{
    esp_err_t err;

    if (s_ready) {
        return ESP_OK;
    }
    err = board_spi_init();
    if (err != ESP_OK) {
        return err;
    }

    s_free = xSemaphoreCreateCounting(2, 2);
    s_buf[0] = heap_caps_malloc(DISPLAY_STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
    s_buf[1] = heap_caps_malloc(DISPLAY_STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!s_free || !s_buf[0] || !s_buf[1]) {
        ESP_LOGE(TAG, "Out of memory for display buffers");
        release_resources();
        return ESP_ERR_NO_MEM;
    }

    backlight_init();

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = CONFIG_ZCLAW_DISPLAY_CS_GPIO,
        .dc_gpio_num = CONFIG_ZCLAW_DISPLAY_DC_GPIO,
        .spi_mode = 0,
        .pclk_hz = DISPLAY_PCLK_HZ,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_color_done,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_SPI_HOST, &io_cfg, &s_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel IO init failed: %s", esp_err_to_name(err));
        release_resources();
        return err;
    }

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CONFIG_ZCLAW_DISPLAY_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel);
    if (err == ESP_OK) {
        err = esp_lcd_panel_reset(s_panel);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_init(s_panel);
    }
    if (err == ESP_OK) {
#if CONFIG_ZCLAW_DISPLAY_INVERT_COLORS
        err = esp_lcd_panel_invert_color(s_panel, true);
#else
        err = esp_lcd_panel_invert_color(s_panel, false);
#endif
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_set_gap(s_panel, CONFIG_ZCLAW_DISPLAY_X_GAP, CONFIG_ZCLAW_DISPLAY_Y_GAP);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_on_off(s_panel, true);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ST7789 init failed: %s", esp_err_to_name(err));
        release_resources();
        return err;
    }

    s_ready = true;
    for (int y = 0; y < DISPLAY_H; y += 16) {
        uint16_t *buf = display_hw_acquire_buffer();
        memset(buf, 0, DISPLAY_STRIP_PIXELS * sizeof(uint16_t));
        display_hw_blit(0, y, DISPLAY_W, 16, buf);
    }
    display_hw_wait_idle();
    display_hw_set_brightness(CONFIG_ZCLAW_DISPLAY_BRIGHTNESS);
    ESP_LOGI(TAG, "ST7789 ready (CS %d, DC %d, RST %d, BL %d)", CONFIG_ZCLAW_DISPLAY_CS_GPIO,
             CONFIG_ZCLAW_DISPLAY_DC_GPIO, CONFIG_ZCLAW_DISPLAY_RST_GPIO,
             CONFIG_ZCLAW_DISPLAY_BL_GPIO);
    return ESP_OK;
}

bool display_hw_ready(void)
{
    return s_ready;
}

uint16_t *display_hw_acquire_buffer(void)
{
    if (!s_ready) {
        return NULL;
    }
    xSemaphoreTake(s_free, portMAX_DELAY);
    uint16_t *buf = s_buf[s_next_buf];
    s_next_buf ^= 1;
    return buf;
}

void display_hw_release_buffer(uint16_t *buf)
{
    if (!s_ready || !buf) {
        return;
    }
    // The unused buffer becomes the next one handed out, preserving the
    // completion order the round-robin relies on.
    s_next_buf = (buf == s_buf[0]) ? 0 : 1;
    xSemaphoreGive(s_free);
}

esp_err_t display_hw_blit(int x, int y, int w, int h, uint16_t *buf)
{
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, buf);
    if (err != ESP_OK) {
        xSemaphoreGive(s_free);  // no completion callback will fire
    }
    return err;
}

void display_hw_wait_idle(void)
{
    if (!s_ready) {
        return;
    }
    xSemaphoreTake(s_free, portMAX_DELAY);
    xSemaphoreTake(s_free, portMAX_DELAY);
    xSemaphoreGive(s_free);
    xSemaphoreGive(s_free);
}

#else  // !CONFIG_ZCLAW_DISPLAY_ENABLE

esp_err_t display_hw_init(void) { return ESP_ERR_NOT_SUPPORTED; }
bool display_hw_ready(void) { return false; }
uint16_t *display_hw_acquire_buffer(void) { return NULL; }
void display_hw_release_buffer(uint16_t *buf) { (void)buf; }
esp_err_t display_hw_blit(int x, int y, int w, int h, uint16_t *buf)
{
    (void)x; (void)y; (void)w; (void)h; (void)buf;
    return ESP_ERR_NOT_SUPPORTED;
}
void display_hw_wait_idle(void) {}
void display_hw_set_brightness(uint8_t percent) { (void)percent; }

#endif
```

- [ ] **Step 3: `main/display_gfx.h` / `main/display_gfx.c`** (text and fills; frame streaming is added in Task 10)

```c
#ifndef DISPLAY_GFX_H
#define DISPLAY_GFX_H

#include <stdint.h>

typedef uint16_t gfx_color_t;  // RGB565 in host byte order

#define GFX_RGB(r, g, b) ((gfx_color_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define GFX_BLACK GFX_RGB(0, 0, 0)
#define GFX_WHITE GFX_RGB(255, 255, 255)
#define GFX_GRAY  GFX_RGB(150, 150, 150)
#define GFX_AMBER GFX_RGB(255, 191, 0)

void gfx_fill_rect(int x, int y, int w, int h, gfx_color_t color);

// One line of UTF-8 text with the 8x16 font scaled by `scale` (1-3), clipped
// at the right edge. Paints the text box background. Returns the drawn width.
int gfx_text(int x, int y, const char *utf8, gfx_color_t fg, gfx_color_t bg, int scale);
int gfx_text_width(const char *utf8, int scale);

#endif // DISPLAY_GFX_H
```

```c
#include "display_gfx.h"

#include "display_hw.h"
#include "font8x16.h"
#include "text_latin1.h"

static uint16_t to_panel(gfx_color_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}

void gfx_fill_rect(int x, int y, int w, int h, gfx_color_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > DISPLAY_W) { w = DISPLAY_W - x; }
    if (y + h > DISPLAY_H) { h = DISPLAY_H - y; }
    if (w <= 0 || h <= 0) {
        return;
    }

    int rows_per_band = DISPLAY_STRIP_PIXELS / w;
    uint16_t value = to_panel(color);
    for (int row = 0; row < h; row += rows_per_band) {
        int rows = (h - row < rows_per_band) ? h - row : rows_per_band;
        uint16_t *buf = display_hw_acquire_buffer();
        if (!buf) {
            return;
        }
        for (int i = 0; i < rows * w; i++) {
            buf[i] = value;
        }
        display_hw_blit(x, y + row, w, rows, buf);
    }
}

int gfx_text_width(const char *utf8, int scale)
{
    return (int)text_latin1_length(utf8) * FONT_GLYPH_W * scale;
}

int gfx_text(int x, int y, const char *utf8, gfx_color_t fg, gfx_color_t bg, int scale)
{
    uint8_t chars[DISPLAY_W / FONT_GLYPH_W];
    size_t count = 0;
    const char *p = utf8;
    uint8_t c;

    if (!utf8 || scale < 1 || scale > 3 || x < 0 || y < 0 || x >= DISPLAY_W || y >= DISPLAY_H) {
        return 0;
    }
    int max_chars = (DISPLAY_W - x) / (FONT_GLYPH_W * scale);
    while ((int)count < max_chars && (c = text_latin1_next(&p)) != 0) {
        chars[count++] = c;
    }
    if (count == 0) {
        return 0;
    }

    int w = (int)count * FONT_GLYPH_W * scale;
    int h = FONT_GLYPH_H * scale;
    if (y + h > DISPLAY_H) {
        h = DISPLAY_H - y;
    }
    int rows_per_band = DISPLAY_STRIP_PIXELS / w;
    uint16_t fg_px = to_panel(fg);
    uint16_t bg_px = to_panel(bg);

    for (int row = 0; row < h; row += rows_per_band) {
        int rows = (h - row < rows_per_band) ? h - row : rows_per_band;
        uint16_t *buf = display_hw_acquire_buffer();
        if (!buf) {
            return 0;
        }
        for (int r = 0; r < rows; r++) {
            int glyph_row = (row + r) / scale;
            uint16_t *out = buf + r * w;
            for (size_t i = 0; i < count; i++) {
                uint8_t bits = font8x16_latin1[chars[i]][glyph_row];
                for (int b = 0; b < FONT_GLYPH_W; b++) {
                    uint16_t px = (bits & (0x80 >> b)) ? fg_px : bg_px;
                    for (int s = 0; s < scale; s++) {
                        *out++ = px;
                    }
                }
            }
        }
        display_hw_blit(x, y + row, w, rows, buf);
    }
    return w;
}
```

- [ ] **Step 4: Temporary `main/display.h` / `main/display.c` self-test**

`main/display.h` (final API):

```c
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

// Start the display (and SD card) subsystem. Never fatal: returns an error
// and leaves the rest of the firmware untouched if the hardware is missing.
// ESP_ERR_NOT_SUPPORTED when CONFIG_ZCLAW_DISPLAY_ENABLE is off.
esp_err_t display_start(void);

// Non-blocking event hooks; no-ops when the display is disabled or not started.
void display_notify_thinking(bool active);
void display_notify_talking(size_t reply_chars);
void display_notify_wifi(bool connected);

#endif // DISPLAY_H
```

`main/display.c` (Task 9 version):

```c
#include "display.h"

#include "config.h"
#include "display_gfx.h"
#include "display_hw.h"
#include "esp_log.h"
#include "ota.h"

static const char *TAG = "display";

esp_err_t display_start(void)
{
#if CONFIG_ZCLAW_DISPLAY_ENABLE
    esp_err_t err = display_hw_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Display disabled: %s", esp_err_to_name(err));
        return err;
    }
    // Bring-up self-test (replaced by the display task in Task 12).
    gfx_fill_rect(0, 0, DISPLAY_W, 40, GFX_RGB(255, 0, 0));
    gfx_fill_rect(0, 40, DISPLAY_W, 40, GFX_RGB(0, 255, 0));
    gfx_fill_rect(0, 80, DISPLAY_W, 40, GFX_RGB(0, 0, 255));
    gfx_text(8, 132, "Fidel", GFX_WHITE, GFX_BLACK, 3);
    gfx_text(8, 186, "se\xC3\xB1" "al \xC2\xBF" "ma\xC3\xB1" "ana?", GFX_AMBER, GFX_BLACK, 1);
    gfx_text(8, 210, ota_get_version(), GFX_GRAY, GFX_BLACK, 1);
    display_hw_wait_idle();
    return ESP_OK;
#else
    (void)TAG;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void display_notify_thinking(bool active) { (void)active; }
void display_notify_talking(size_t reply_chars) { (void)reply_chars; }
void display_notify_wifi(bool connected) { (void)connected; }
```

- [ ] **Step 5: Wire into the build** — in `main/CMakeLists.txt` add to `SRCS`:

```cmake
        "board_spi.c"
        "display_hw.c"
        "display_gfx.c"
        "display.c"
        "text_latin1.c"
        "font8x16.c"
        "frame_format.c"
        "face_state.c"
        "page_rotation.c"
        "status_format.c"
```

and to `REQUIRES`: `esp_lcd esp_driver_spi esp_driver_ledc`.

In `main/main.c` add `#include "display.h"` and, right after `channel_init();` in the normal (non-emulator) path (step 8), insert:

```c
    // 8b. Optional display + SD card. Never fatal.
    esp_err_t display_err = display_start();
    if (display_err != ESP_OK && display_err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Display unavailable: %s", esp_err_to_name(display_err));
    }
```

- [ ] **Step 6: Enable on Fidel's board** — append to the git-ignored `sdkconfig.defaults.esp32c6`:

```text

# Display + SD card (Delivery 1)
CONFIG_ZCLAW_DISPLAY_ENABLE=y
CONFIG_ZCLAW_SD_ENABLE=y
```

The local `sdkconfig` has no entries for these new symbols, so the defaults apply on the next build. Verify: `idf.py reconfigure >/dev/null && grep -E "^CONFIG_ZCLAW_(DISPLAY|SD)_ENABLE=" sdkconfig` → both `=y`.

- [ ] **Step 7: Build** — `idf.py build 2>&1 | grep -E "main/.*(warning|error)|binary size"` → no warnings in `main/`, size line printed.

- [ ] **Step 8: Hardware check (needs the user's OK for the first USB flash)**

```bash
PORT=$(ls /dev/cu.usbmodem* | head -1)
idf.py -p "$PORT" flash
perl -e 'alarm shift; exec @ARGV' 25 script -q /dev/null idf.py -p "$PORT" monitor > build/monitor.log 2>&1
grep -E "board_spi|display" build/monitor.log
```

Expected log: `SPI2 ready (SCLK 6, MOSI 5)` and `ST7789 ready (CS 7, DC 1, RST 0, BL 10)`. Ask the user what the screen shows. Expected: red, green, blue bands top to bottom, "Fidel" in large white text, `señal ¿mañana?` in amber with correct accents, the version in grey.

Troubleshooting (adjust, rebuild, reflash):
- Colours inverted (black background looks white) → `CONFIG_ZCLAW_DISPLAY_INVERT_COLORS=n` in `sdkconfig.defaults.esp32c6` and `sed -i '' 's/^CONFIG_ZCLAW_DISPLAY_INVERT_COLORS=y/# CONFIG_ZCLAW_DISPLAY_INVERT_COLORS is not set/' sdkconfig`.
- Red and blue swapped → change `LCD_RGB_ELEMENT_ORDER_RGB` to `LCD_RGB_ELEMENT_ORDER_BGR` in `display_hw.c`.
- Image shifted / garbage band at one edge → set `CONFIG_ZCLAW_DISPLAY_Y_GAP` (try 80) or `X_GAP`.
- Nothing at all with a lit backlight → try `.spi_mode = 3`.
Record the final values in the spec §3 note when confirmed.

- [ ] **Step 9: Host tests still pass** — `./scripts/test.sh host 2>&1 | tail -3`.

- [ ] **Step 10: Commit**

```bash
git add main/board_spi.[ch] main/display_hw.[ch] main/display_gfx.[ch] main/display.[ch] main/CMakeLists.txt main/main.c
git commit -m "feat(display): bring up ST7789 on the shared SPI bus with text rendering"
```

---

### Task 10: SD card and frame streaming (hardware bring-up 2)

Prerequisite (user): SD module wired per spec §3 (SCK→6, MOSI→5, MISO→2, CS→3, VCC→5 V if the module has a regulator, GND), FAT32 card with `build/face/*.r565` copied into `/face/` at its root.

**Files:**
- Create: `main/sd_card.h`, `main/sd_card.c`
- Modify: `main/display_gfx.h`, `main/display_gfx.c` (frame streaming)
- Modify: `main/display.c` (self-test draws `neutral` from SD)
- Modify: `main/CMakeLists.txt`, `sdkconfig.defaults`, local `sdkconfig`

**Interfaces:**
- Consumes: `board_spi_init()`, `BOARD_SPI_HOST` (Task 9); `frame_header_parse`, `frame_pixel_offset`, `frame_is_full_screen` (Task 3).
- Produces:

```c
// sd_card.h
#define SD_MOUNT_POINT "/sdcard"
bool sd_card_service(int64_t now_ms);   // retries the mount every 60 s while absent; true when it just mounted
bool sd_card_is_mounted(void);
void sd_card_mark_failed(int64_t now_ms); // after an I/O error: unmount, retry in 60 s

// display_gfx.h additions
#include <stdio.h>
#include "esp_err.h"
#include "frame_format.h"
esp_err_t gfx_draw_frame_file(FILE *f, size_t file_size, frame_header_t *out_hdr);
esp_err_t gfx_draw_frame_region(FILE *full, size_t file_size, int x, int y, int w, int h);
```

- [ ] **Step 1: Enable FAT long file names**

Append to the tracked `sdkconfig.defaults`:

```text

# FAT long file names (face frames such as eyes_closed.r565)
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_MAX_LFN=255
```

The local `sdkconfig` already pins `CONFIG_FATFS_LFN_NONE=y`, so switch it in place (never delete `sdkconfig`):

```bash
sed -i '' -e 's/^CONFIG_FATFS_LFN_NONE=y/# CONFIG_FATFS_LFN_NONE is not set/' \
          -e 's/^# CONFIG_FATFS_LFN_HEAP is not set/CONFIG_FATFS_LFN_HEAP=y/' sdkconfig
idf.py reconfigure >/dev/null && grep -E "^CONFIG_FATFS_LFN_HEAP=|^CONFIG_FATFS_MAX_LFN=" sdkconfig
```

Expected: `CONFIG_FATFS_LFN_HEAP=y` and `CONFIG_FATFS_MAX_LFN=255`.

- [ ] **Step 2: `main/sd_card.h` / `main/sd_card.c`**

```c
#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdbool.h>
#include <stdint.h>

#define SD_MOUNT_POINT "/sdcard"
#define SD_RETRY_MS    60000

// Call periodically. Mounts on the first call and then every SD_RETRY_MS
// while the card is absent. Returns true right after a successful mount.
bool sd_card_service(int64_t now_ms);
bool sd_card_is_mounted(void);

// Report an I/O error: unmounts and schedules a retry.
void sd_card_mark_failed(int64_t now_ms);

#endif // SD_CARD_H
```

```c
#include "sd_card.h"

#include "board_spi.h"
#include "config.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#if CONFIG_ZCLAW_SD_ENABLE

static const char *TAG = "sd_card";
static sdmmc_card_t *s_card = NULL;
static int64_t s_next_try_ms = 0;

static bool try_mount(void)
{
    if (board_spi_init() != ESP_OK) {
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SPI_HOST;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = BOARD_SPI_HOST;
    slot.gpio_cs = CONFIG_ZCLAW_SD_CS_GPIO;

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        ESP_LOGW(TAG, "SD mount failed: %s (retry in %d s)", esp_err_to_name(err), SD_RETRY_MS / 1000);
        return false;
    }
    // nano printf: no %llu, so print MB as unsigned long.
    ESP_LOGI(TAG, "SD mounted at %s (%s, %lu MB)", SD_MOUNT_POINT, s_card->cid.name,
             (unsigned long)(((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) >> 20));
    return true;
}

bool sd_card_service(int64_t now_ms)
{
    if (s_card || now_ms < s_next_try_ms) {
        return false;
    }
    s_next_try_ms = now_ms + SD_RETRY_MS;
    return try_mount();
}

bool sd_card_is_mounted(void)
{
    return s_card != NULL;
}

void sd_card_mark_failed(int64_t now_ms)
{
    if (s_card) {
        ESP_LOGW(TAG, "SD I/O error: unmounting");
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        s_card = NULL;
    }
    s_next_try_ms = now_ms + SD_RETRY_MS;
}

#else  // !CONFIG_ZCLAW_SD_ENABLE

bool sd_card_service(int64_t now_ms) { (void)now_ms; return false; }
bool sd_card_is_mounted(void) { return false; }
void sd_card_mark_failed(int64_t now_ms) { (void)now_ms; }

#endif
```

- [ ] **Step 3: Frame streaming in `display_gfx`** — add to `main/display_gfx.h` (with `#include <stdio.h>`, `#include <stddef.h>`, `#include "esp_err.h"`, `#include "frame_format.h"`):

```c
// Draw a whole .r565 file (full frame or patch at its own rectangle).
// ESP_ERR_INVALID_SIZE for a bad header, ESP_FAIL for read errors.
esp_err_t gfx_draw_frame_file(FILE *f, size_t file_size, frame_header_t *out_hdr);

// Redraw rectangle (x, y, w, h) from a full-screen .r565 file.
esp_err_t gfx_draw_frame_region(FILE *full, size_t file_size, int x, int y, int w, int h);
```

and to `main/display_gfx.c`:

```c
static esp_err_t read_header(FILE *f, size_t file_size, frame_header_t *hdr)
{
    uint8_t raw[FRAME_HEADER_SIZE];

    if (fseek(f, 0, SEEK_SET) != 0 || fread(raw, 1, sizeof(raw), f) != sizeof(raw)) {
        return ESP_FAIL;
    }
    if (frame_header_parse(raw, sizeof(raw), file_size, DISPLAY_W, DISPLAY_H, hdr) != FRAME_OK) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t gfx_draw_frame_file(FILE *f, size_t file_size, frame_header_t *out_hdr)
{
    frame_header_t hdr;
    esp_err_t err = read_header(f, file_size, &hdr);
    if (err != ESP_OK) {
        return err;
    }

    int rows_per_band = DISPLAY_STRIP_PIXELS / hdr.w;
    for (int row = 0; row < hdr.h; row += rows_per_band) {
        int rows = (hdr.h - row < rows_per_band) ? hdr.h - row : rows_per_band;
        size_t bytes = (size_t)rows * hdr.w * sizeof(uint16_t);
        uint16_t *buf = display_hw_acquire_buffer();
        if (!buf) {
            return ESP_FAIL;
        }
        if (fread(buf, 1, bytes, f) != bytes) {
            display_hw_release_buffer(buf);
            return ESP_FAIL;
        }
        err = display_hw_blit(hdr.x, hdr.y + row, hdr.w, rows, buf);
        if (err != ESP_OK) {
            return err;
        }
    }
    if (out_hdr) {
        *out_hdr = hdr;
    }
    return ESP_OK;
}

esp_err_t gfx_draw_frame_region(FILE *full, size_t file_size, int x, int y, int w, int h)
{
    frame_header_t hdr;
    esp_err_t err = read_header(full, file_size, &hdr);
    if (err != ESP_OK) {
        return err;
    }
    if (!frame_is_full_screen(&hdr, DISPLAY_W, DISPLAY_H) || x < 0 || y < 0 || w <= 0 || h <= 0 ||
        x + w > DISPLAY_W || y + h > DISPLAY_H) {
        return ESP_ERR_INVALID_ARG;
    }

    int rows_per_band = DISPLAY_STRIP_PIXELS / w;
    for (int row = 0; row < h; row += rows_per_band) {
        int rows = (h - row < rows_per_band) ? h - row : rows_per_band;
        uint16_t *buf = display_hw_acquire_buffer();
        if (!buf) {
            return ESP_FAIL;
        }
        for (int r = 0; r < rows; r++) {
            size_t offset = frame_pixel_offset(&hdr, (uint16_t)x, (uint16_t)(y + row + r));
            size_t bytes = (size_t)w * sizeof(uint16_t);
            if (fseek(full, (long)offset, SEEK_SET) != 0 ||
                fread(buf + (size_t)r * w, 1, bytes, full) != bytes) {
                display_hw_release_buffer(buf);
                return ESP_FAIL;
            }
        }
        err = display_hw_blit(x, y + row, w, rows, buf);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
```

- [ ] **Step 4: Extend the self-test** — in `main/display.c` add includes `sd_card.h`, `<stdio.h>`, `<sys/stat.h>`, `esp_timer.h`, `freertos/FreeRTOS.h`, `freertos/task.h`, and after the text drawing in `display_start()`:

```c
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (sd_card_service(esp_timer_get_time() / 1000)) {
        struct stat st;
        const char *path = SD_MOUNT_POINT "/face/neutral.r565";
        FILE *f = (stat(path, &st) == 0) ? fopen(path, "rb") : NULL;
        if (f) {
            int64_t t0 = esp_timer_get_time();
            esp_err_t ferr = gfx_draw_frame_file(f, (size_t)st.st_size, NULL);
            display_hw_wait_idle();
            fclose(f);
            ESP_LOGI(TAG, "neutral.r565: %s in %lu ms", esp_err_to_name(ferr),
                     (unsigned long)((esp_timer_get_time() - t0) / 1000));
        } else {
            ESP_LOGW(TAG, "%s not found", path);
        }
    }
```

- [ ] **Step 5: Wire into the build** — `main/CMakeLists.txt`: add `"sd_card.c"` to `SRCS`, and `fatfs sdmmc esp_driver_sdspi` to `REQUIRES`.

- [ ] **Step 6: Build** — `idf.py build 2>&1 | grep -E "main/.*(warning|error)|binary size"` → clean.

- [ ] **Step 7: Hardware check**

```bash
PORT=$(ls /dev/cu.usbmodem* | head -1)
idf.py -p "$PORT" flash
perl -e 'alarm shift; exec @ARGV' 30 script -q /dev/null idf.py -p "$PORT" monitor > build/monitor.log 2>&1
grep -E "sd_card|neutral|display" build/monitor.log
```

Expected: `SD mounted at /sdcard (...)` and `neutral.r565: ESP_OK in <N> ms` (spec estimate ~85 ms). Ask the user: after the colour bars, the neutral photo fills the screen with natural skin tones (not blue-tinted, not inverted, not shifted). Record the measured milliseconds.

If the mount fails: check wiring/power (5 V for regulator modules), FAT32 format, and that the display CS idles high (logged in `board_spi`). If colours of the photo are wrong while text colours were right, the frame byte order is wrong: re-check `to_rgb565_be` (big-endian) against `LCD_RGB_DATA_ENDIAN_BIG`.

- [ ] **Step 8: Commit**

```bash
git add main/sd_card.[ch] main/display_gfx.[ch] main/display.c main/CMakeLists.txt sdkconfig.defaults
git commit -m "feat(display): mount SD on the shared bus and stream .r565 frames"
```

---

### Task 11: Weather and chip-temperature helpers, status page

**Files:**
- Create: `main/weather.h`
- Modify: `main/tools_net.c:339-405` (`tools_weather_handler` → shared fetch)
- Modify: `main/tools_tsens.c`, `main/tools_handlers.h` (numeric chip temperature)
- Create: `main/display_status.h`, `main/display_status.c`
- Modify: `main/display.c` (self-test shows the status page), `main/CMakeLists.txt`

**Interfaces:**
- Consumes: `status_format_*`, `status_truncate_utf8`, `status_model_short` (Task 6); `gfx_*` (Task 9); `llm_get_model()`, `ota_get_version()`; `telegram_pause_polling()/telegram_resume_polling()`.
- Produces:

```c
// weather.h
typedef struct { float temp_c; float humidity; float wind_kmh; int code; } weather_current_t;
bool weather_fetch_current(bool has_coords, float lat, float lon, TickType_t gate_wait,
                           weather_current_t *out, char *err, size_t err_len);
const char *weather_code_desc(int code);

// tools_handlers.h
bool tools_chip_temp_read(float *celsius);

// display_status.h
void display_status_draw(bool wifi_up);
void display_status_service(int64_t now_ms, bool wifi_up);
```

- [ ] **Step 1: `main/weather.h`**

```c
#ifndef WEATHER_H
#define WEATHER_H

#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"

typedef struct {
    float temp_c;
    float humidity;
    float wind_kmh;
    int code;  // WMO weather code
} weather_current_t;

// Current weather from Open-Meteo for lat/lon, or for the saved u_ubicacion
// memory when has_coords is false. Waits up to gate_wait for the HTTP gate.
bool weather_fetch_current(bool has_coords, float lat, float lon, TickType_t gate_wait,
                           weather_current_t *out, char *err, size_t err_len);

// Short Spanish description of a WMO code.
const char *weather_code_desc(int code);

#endif // WEATHER_H
```

- [ ] **Step 2: Refactor `tools_net.c`** — add `#include "weather.h"`, then replace the whole `tools_weather_handler` function with:

```c
bool weather_fetch_current(bool has_coords, float lat, float lon, TickType_t gate_wait,
                           weather_current_t *out, char *err, size_t err_len)
{
    if (!has_coords) {
        // Fall back to the saved location memory (e.g. "lat20.02lon-75.83").
        char loc[64] = {0};
        if (!memory_get("u_ubicacion", loc, sizeof(loc)) ||
            !parse_after(loc, "lat", &lat) || !parse_after(loc, "lon", &lon)) {
            snprintf(err, err_len,
                     "Error: sin ubicacion. Guarda u_ubicacion (ej lat20.02lon-75.83) "
                     "o pasa lat/lon.");
            return false;
        }
    }

    char url[224];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m",
             lat, lon);

    if (!http_gate_acquire("weather", gate_wait)) {
        snprintf(err, err_len, "Error: HTTP busy, try again");
        return false;
    }
    char resp[NET_TOOL_BODY_MAX];
    int status = net_http_request(url, HTTP_METHOD_GET, NULL, NULL, NULL,
                                  resp, sizeof(resp));
    http_gate_release();

    if (status < 0) {
        snprintf(err, err_len, "Error: no pude consultar el clima (red)");
        return false;
    }
    if (status != 200) {
        snprintf(err, err_len, "Error: clima HTTP %d", status);
        return false;
    }

    // Minimal parse of the flat "current" object from Open-Meteo.
    cJSON *root = cJSON_Parse(resp);
    cJSON *cur = root ? cJSON_GetObjectItem(root, "current") : NULL;
    if (!cur) {
        if (root) cJSON_Delete(root);
        snprintf(err, err_len, "Error: respuesta de clima invalida");
        return false;
    }
    out->temp_c = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "temperature_2m"));
    out->humidity = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "relative_humidity_2m"));
    out->wind_kmh = (float)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "wind_speed_10m"));
    out->code = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(cur, "weather_code"));
    cJSON_Delete(root);
    return true;
}

const char *weather_code_desc(int code)
{
    return wmo_desc(code);
}

bool tools_weather_handler(const cJSON *input, char *result, size_t result_len)
{
    const cJSON *jlat = cJSON_GetObjectItem(input, "lat");
    const cJSON *jlon = cJSON_GetObjectItem(input, "lon");
    bool has_coords = jlat && cJSON_IsNumber(jlat) && jlon && cJSON_IsNumber(jlon);
    weather_current_t w;

    if (!weather_fetch_current(has_coords,
                               has_coords ? (float)jlat->valuedouble : 0.0f,
                               has_coords ? (float)jlon->valuedouble : 0.0f,
                               pdMS_TO_TICKS(NET_TOOL_GATE_MS), &w, result, result_len)) {
        return false;
    }

    snprintf(result, result_len,
             "Clima ahora: %.1f C, %s, humedad %.0f%%, viento %.0f km/h",
             w.temp_c, wmo_desc(w.code), w.humidity, w.wind_kmh);
    return true;
}
```

The tool's output string is byte-for-byte what it was; only the fetch moved.

- [ ] **Step 3: Numeric chip temperature** — in `main/tools_handlers.h` add `bool tools_chip_temp_read(float *celsius);` next to `tools_chip_temp_handler`. In `main/tools_tsens.c`, replace the handler with:

```c
bool tools_chip_temp_read(float *celsius)
{
    if (!s_tsens) {
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        esp_err_t err = temperature_sensor_install(&cfg, &s_tsens);
        if (err == ESP_OK) {
            err = temperature_sensor_enable(s_tsens);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "tsens init failed: %s", esp_err_to_name(err));
            s_tsens = NULL;
            return false;
        }
    }
    return temperature_sensor_get_celsius(s_tsens, celsius) == ESP_OK;
}

bool tools_chip_temp_handler(const cJSON *input, char *result, size_t result_len)
{
    (void)input;
    float celsius = 0;

    if (!tools_chip_temp_read(&celsius)) {
        snprintf(result, result_len, "Error: temperature read failed");
        return false;
    }

    // newlib nano printf: no float support - format as integer tenths.
    int deci = (int)(celsius * 10.0f + (celsius >= 0 ? 0.5f : -0.5f));
    snprintf(result, result_len,
             "Chip temperature: %d.%d C (die runs ~10-20 C above ambient)",
             deci / 10, (deci < 0 ? -deci : deci) % 10);
    return true;
}
```

Two tasks can now read the sensor (agent via the tool, display via the status page). `temperature_sensor_get_celsius` is safe to call concurrently once installed, so `display_start()` installs it once by calling `tools_chip_temp_read()` before any second task can race on the lazy install (Step 5 here, kept in Task 12's `display_start()`).

- [ ] **Step 4: `main/display_status.h` / `main/display_status.c`**

```c
#ifndef DISPLAY_STATUS_H
#define DISPLAY_STATUS_H

#include <stdbool.h>
#include <stdint.h>

// Draw the full status page (clears the screen).
void display_status_draw(bool wifi_up);

// Call from the display task loop: schedules the 30-minute weather refresh.
void display_status_service(int64_t now_ms, bool wifi_up);

#endif // DISPLAY_STATUS_H
```

```c
#include "display_status.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "display_gfx.h"
#include "display_hw.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "llm.h"
#include "ota.h"
#include "status_format.h"
#include "telegram.h"
#include "tools_handlers.h"
#include "weather.h"

#define WEATHER_REFRESH_MS   (30LL * 60 * 1000)
#define WEATHER_RETRY_MS     (5LL * 60 * 1000)
#define WEATHER_GATE_WAIT_MS 40000
#define WEATHER_TASK_STACK   8192
#define WEATHER_TASK_PRIO    2
#define VALUE_X              (8 * 8)
#define ROW_H                20

static const char *TAG = "display_status";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_weather_valid = false;
static int s_weather_temp_c = 0;
static int s_weather_code = 0;
static volatile bool s_weather_busy = false;
static int64_t s_next_weather_ms = 0;

static void weather_task(void *arg)
{
    (void)arg;
    weather_current_t w;
    char err[96];

    // The poll task re-takes the HTTP gate immediately, so pause it and wait
    // for the gate here; only this task waits, nobody else is blocked.
    telegram_pause_polling();
    bool ok = weather_fetch_current(false, 0.0f, 0.0f, pdMS_TO_TICKS(WEATHER_GATE_WAIT_MS),
                                    &w, err, sizeof(err));
    telegram_resume_polling();

    int64_t now_ms = esp_timer_get_time() / 1000;
    taskENTER_CRITICAL(&s_lock);
    if (ok) {
        s_weather_valid = true;
        s_weather_temp_c = (int)(w.temp_c + (w.temp_c >= 0 ? 0.5f : -0.5f));
        s_weather_code = w.code;
        s_next_weather_ms = now_ms + WEATHER_REFRESH_MS;
    } else {
        s_next_weather_ms = now_ms + WEATHER_RETRY_MS;
    }
    taskEXIT_CRITICAL(&s_lock);

    if (!ok) {
        ESP_LOGW(TAG, "Weather refresh failed: %s", err);
    }
    s_weather_busy = false;
    vTaskDelete(NULL);
}

void display_status_service(int64_t now_ms, bool wifi_up)
{
    int64_t next;

    if (!wifi_up || s_weather_busy) {
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    next = s_next_weather_ms;
    taskEXIT_CRITICAL(&s_lock);
    if (now_ms < next) {
        return;
    }

    s_weather_busy = true;
    if (xTaskCreate(weather_task, "weather", WEATHER_TASK_STACK, NULL, WEATHER_TASK_PRIO, NULL) != pdPASS) {
        s_weather_busy = false;
        taskENTER_CRITICAL(&s_lock);
        s_next_weather_ms = now_ms + WEATHER_RETRY_MS;
        taskEXIT_CRITICAL(&s_lock);
        ESP_LOGW(TAG, "Could not start weather task");
    }
}

static void draw_row(int y, const char *label, const char *value)
{
    gfx_text(4, y, label, GFX_AMBER, GFX_BLACK, 1);
    gfx_text(VALUE_X, y, value, GFX_WHITE, GFX_BLACK, 1);
}

static void draw_centered(int y, const char *text, gfx_color_t color, int scale)
{
    int w = gfx_text_width(text, scale);
    gfx_text(w < DISPLAY_W ? (DISPLAY_W - w) / 2 : 0, y, text, color, GFX_BLACK, scale);
}

void display_status_draw(bool wifi_up)
{
    char a[32];
    char b[48];
    char row[64];
    bool weather_valid;
    int weather_temp;
    int weather_code;

    gfx_fill_rect(0, 0, DISPLAY_W, DISPLAY_H, GFX_BLACK);

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    status_format_clock(&tm, a, sizeof(a));
    draw_centered(6, a, GFX_WHITE, 3);
    status_format_date(&tm, a, sizeof(a));
    draw_centered(58, a, GFX_GRAY, 1);

    int y = 86;
    taskENTER_CRITICAL(&s_lock);
    weather_valid = s_weather_valid;
    weather_temp = s_weather_temp_c;
    weather_code = s_weather_code;
    taskEXIT_CRITICAL(&s_lock);
    if (weather_valid) {
        snprintf(b, sizeof(b), "%dC %s", weather_temp, weather_code_desc(weather_code));
    } else {
        snprintf(b, sizeof(b), "--");
    }
    draw_row(y, "Clima", b);
    y += ROW_H;

    wifi_ap_record_t ap;
    memset(&ap, 0, sizeof(ap));
    if (wifi_up && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        status_truncate_utf8((const char *)ap.ssid, 22, b, sizeof(b));
        draw_row(y, "WiFi", b);
        y += ROW_H;
        snprintf(b, sizeof(b), "%d dBm", (int)ap.rssi);
        draw_row(y, "Se\xC3\xB1" "al", b);
    } else {
        draw_row(y, "WiFi", "sin conexi\xC3\xB3n");
        y += ROW_H;
        draw_row(y, "Se\xC3\xB1" "al", "--");
    }
    y += ROW_H;

    float celsius = 0;
    if (tools_chip_temp_read(&celsius)) {
        status_format_temp_c((int)(celsius * 10.0f), b, sizeof(b));
    } else {
        snprintf(b, sizeof(b), "--");
    }
    draw_row(y, "CPU", b);
    y += ROW_H;

    status_format_kib(esp_get_free_heap_size(), a, sizeof(a));
    char min_heap[16];
    status_format_kib(esp_get_minimum_free_heap_size(), min_heap, sizeof(min_heap));
    snprintf(b, sizeof(b), "%s (min %s)", a, min_heap);
    draw_row(y, "Heap", b);
    y += ROW_H;

    status_format_uptime((uint64_t)(esp_timer_get_time() / 1000000), b, sizeof(b));
    draw_row(y, "Uptime", b);
    y += ROW_H;

    snprintf(row, sizeof(row), "v%s %s", ota_get_version(), status_model_short(llm_get_model()));
    status_truncate_utf8(row, 29, b, sizeof(b));
    gfx_text(4, y, b, GFX_GRAY, GFX_BLACK, 1);
}
```

- [ ] **Step 5: Self-test shows the status page** — in `main/display.c`, add `#include "display_status.h"` and `#include "tools_handlers.h"`; at the start of `display_start()` after `display_hw_init()` succeeds, add `float warm = 0; (void)tools_chip_temp_read(&warm);` (installs the sensor once); at the end of the self-test add:

```c
    vTaskDelay(pdMS_TO_TICKS(3000));
    display_status_draw(false);
    display_hw_wait_idle();
```

- [ ] **Step 6: Build** — `main/CMakeLists.txt`: add `"display_status.c"` to `SRCS`. `idf.py build 2>&1 | grep -E "main/.*(warning|error)|binary size"` → clean.

- [ ] **Step 7: Weather tool regression check** — flash, then via Telegram ask Fidel "¿qué tiempo hace?" and confirm the reply still contains `Clima ahora:` with the same format as before.

- [ ] **Step 8: Hardware check** — after bars and neutral, the status page appears: clock `--:--` (WiFi not up yet during the self-test — expected), date `--`, `Clima --`, `WiFi sin conexión`, CPU temperature, heap, uptime, version and model. Accents in `Señal` and `conexión` render correctly.

- [ ] **Step 9: Commit**

```bash
git add main/weather.h main/tools_net.c main/tools_tsens.c main/tools_handlers.h main/display_status.[ch] main/display.c main/CMakeLists.txt
git commit -m "feat(display): add status page with shared weather and chip temp helpers"
```

---

### Task 12: Face renderer, display task, event hooks (full integration)

**Files:**
- Create: `main/display_face.h`, `main/display_face.c`
- Replace: `main/display.c` (task, events, rotation; removes the self-test)
- Modify: `main/agent.c:179-183` (`send_response`), `main/agent.c:532`, `main/agent.c:812`
- Modify: `main/local_admin.c:152`, `main/local_admin.c:193` (WiFi events)
- Create: `test/host/mock_display.c`; modify `scripts/test.sh`
- Modify: `main/CMakeLists.txt`

**Interfaces:**
- Consumes: everything above. `face_state_*` (Task 4), `page_rotation_*` (Task 5), `gfx_draw_frame_file`, `gfx_draw_frame_region` (Task 10), `sd_card_service`, `sd_card_is_mounted`, `sd_card_mark_failed`, `SD_MOUNT_POINT` (Task 10), `display_status_draw`, `display_status_service` (Task 11).
- Produces:

```c
// display_face.h
void display_face_rescan(face_state_t *st);     // after mount: catalogue /sdcard/face
void display_face_clear(face_state_t *st);      // after unmount
void display_face_invalidate(void);             // force a full redraw next time
bool display_face_draw(face_state_t *st, int64_t now_ms); // false on SD I/O error
```

- [ ] **Step 1: `main/display_face.h`**

```c
#ifndef DISPLAY_FACE_H
#define DISPLAY_FACE_H

#include <stdbool.h>
#include <stdint.h>

#include "face_state.h"

// Scan SD_MOUNT_POINT/face for <name>.r565 and <name>_2..8.r565, keep only
// files with a valid header of the right type, and update the face state's
// catalogue.
void display_face_rescan(face_state_t *st);

// Forget all frames (card removed or failed).
void display_face_clear(face_state_t *st);

// Force a full redraw on the next display_face_draw() call.
void display_face_invalidate(void);

// Draw whatever changed since the last call. Returns false on an SD I/O
// error (the caller marks the card failed and clears the catalogue).
bool display_face_draw(face_state_t *st, int64_t now_ms);

#endif // DISPLAY_FACE_H
```

- [ ] **Step 2: `main/display_face.c`**

```c
#include "display_face.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "display_gfx.h"
#include "display_hw.h"
#include "esp_log.h"
#include "frame_format.h"
#include "sd_card.h"

#define FACE_DIR      SD_MOUNT_POINT "/face"
#define FACE_PATH_MAX 64

static const char *TAG = "display_face";

static const char *const s_expr_names[FACE_EXPR_COUNT] = {
    "neutral", "thinking", "sleeping", "happy", "angry", "speech",
};
static const char *const s_patch_names[FACE_PATCH_COUNT] = {
    NULL, "eyes_closed", "speaking",
};

typedef struct {
    uint8_t count;
    uint8_t suffix[FACE_MAX_VARIANTS];  // file suffix: 1 = "<name>.r565", n = "<name>_n.r565"
} variant_list_t;

typedef struct {
    bool valid;
    bool text;
    face_mode_t text_mode;
    face_expr_t expr;
    uint8_t expr_variant;
    face_patch_t patch;
    uint8_t patch_variant;
} drawn_t;

static variant_list_t s_expr[FACE_EXPR_COUNT];
static variant_list_t s_patch[FACE_PATCH_COUNT];
static frame_header_t s_patch_hdr[FACE_PATCH_COUNT][FACE_MAX_VARIANTS];
static drawn_t s_drawn;

static void frame_path(char *buf, size_t len, const char *name, uint8_t suffix)
{
    if (suffix <= 1) {
        snprintf(buf, len, FACE_DIR "/%s.r565", name);
    } else {
        snprintf(buf, len, FACE_DIR "/%s_%u.r565", name, (unsigned)suffix);
    }
}

static bool probe(const char *path, frame_header_t *hdr)
{
    struct stat st;
    uint8_t raw[FRAME_HEADER_SIZE];

    if (stat(path, &st) != 0) {
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    size_t got = fread(raw, 1, sizeof(raw), f);
    fclose(f);

    frame_err_t err = frame_header_parse(raw, got, (size_t)st.st_size, DISPLAY_W, DISPLAY_H, hdr);
    if (err != FRAME_OK) {
        ESP_LOGW(TAG, "Skipping %s (frame error %d)", path, (int)err);
        return false;
    }
    return true;
}

static void scan_list(const char *name, bool want_patch, variant_list_t *list,
                      frame_header_t *hdrs)
{
    char path[FACE_PATH_MAX];

    list->count = 0;
    for (uint8_t suffix = 1; suffix <= FACE_MAX_VARIANTS; suffix++) {
        frame_header_t hdr;
        frame_path(path, sizeof(path), name, suffix);
        if (!probe(path, &hdr)) {
            continue;
        }
        bool is_patch = (hdr.flags & FRAME_FLAG_PATCH) != 0;
        bool type_ok = want_patch ? is_patch
                                  : (!is_patch && frame_is_full_screen(&hdr, DISPLAY_W, DISPLAY_H));
        if (!type_ok) {
            ESP_LOGW(TAG, "Skipping %s (expected %s)", path, want_patch ? "a patch" : "a full frame");
            continue;
        }
        if (hdrs) {
            hdrs[list->count] = hdr;
        }
        list->suffix[list->count++] = suffix;
    }
}

void display_face_rescan(face_state_t *st)
{
    uint8_t expr_counts[FACE_EXPR_COUNT] = {0};
    uint8_t patch_counts[FACE_PATCH_COUNT] = {0};

    for (int i = 0; i < FACE_EXPR_COUNT; i++) {
        scan_list(s_expr_names[i], false, &s_expr[i], NULL);
        expr_counts[i] = s_expr[i].count;
    }
    for (int i = 1; i < FACE_PATCH_COUNT; i++) {
        scan_list(s_patch_names[i], true, &s_patch[i], s_patch_hdr[i]);
        patch_counts[i] = s_patch[i].count;
    }
    face_state_set_catalog(st, expr_counts, patch_counts);
    s_drawn.valid = false;
    ESP_LOGI(TAG, "Face catalogue: neutral=%u thinking=%u sleeping=%u eyes_closed=%u speaking=%u",
             expr_counts[FACE_EXPR_NEUTRAL], expr_counts[FACE_EXPR_THINKING],
             expr_counts[FACE_EXPR_SLEEPING], patch_counts[FACE_PATCH_EYES_CLOSED],
             patch_counts[FACE_PATCH_SPEAKING]);
}

void display_face_clear(face_state_t *st)
{
    uint8_t expr_counts[FACE_EXPR_COUNT] = {0};
    uint8_t patch_counts[FACE_PATCH_COUNT] = {0};

    memset(s_expr, 0, sizeof(s_expr));
    memset(s_patch, 0, sizeof(s_patch));
    face_state_set_catalog(st, expr_counts, patch_counts);
    s_drawn.valid = false;
}

void display_face_invalidate(void)
{
    s_drawn.valid = false;
}

static const char *mode_label(face_mode_t mode)
{
    switch (mode) {
    case FACE_MODE_THINKING:
        return "pensando...";
    case FACE_MODE_TALKING:
        return "hablando...";
    case FACE_MODE_OFFLINE:
        return "sin conexi\xC3\xB3n";
    case FACE_MODE_IDLE:
    default:
        return "Fidel";
    }
}

static void draw_text_face(face_mode_t mode)
{
    const char *label = mode_label(mode);
    const char *hint = sd_card_is_mounted() ? "faltan las caras en /face" : "sin tarjeta SD";
    int w;

    gfx_fill_rect(0, 0, DISPLAY_W, DISPLAY_H, GFX_BLACK);
    w = gfx_text_width(label, 2);
    gfx_text(w < DISPLAY_W ? (DISPLAY_W - w) / 2 : 0, 104, label, GFX_WHITE, GFX_BLACK, 2);
    w = gfx_text_width(hint, 1);
    gfx_text(w < DISPLAY_W ? (DISPLAY_W - w) / 2 : 0, 210, hint, GFX_GRAY, GFX_BLACK, 1);
}

static bool open_frame(const char *name, uint8_t suffix, FILE **f, size_t *size)
{
    char path[FACE_PATH_MAX];
    struct stat st;

    frame_path(path, sizeof(path), name, suffix);
    if (stat(path, &st) != 0) {
        return false;
    }
    *f = fopen(path, "rb");
    if (!*f) {
        return false;
    }
    *size = (size_t)st.st_size;
    return true;
}

static bool draw_file(const char *name, uint8_t suffix)
{
    FILE *f;
    size_t size;

    if (!open_frame(name, suffix, &f, &size)) {
        return false;
    }
    esp_err_t err = gfx_draw_frame_file(f, size, NULL);
    fclose(f);
    return err == ESP_OK;
}

static bool restore_region(const frame_header_t *rect)
{
    FILE *f;
    size_t size;

    if (!open_frame(s_expr_names[FACE_EXPR_NEUTRAL], s_expr[FACE_EXPR_NEUTRAL].suffix[0], &f, &size)) {
        return false;
    }
    esp_err_t err = gfx_draw_frame_region(f, size, rect->x, rect->y, rect->w, rect->h);
    fclose(f);
    return err == ESP_OK;
}

bool display_face_draw(face_state_t *st, int64_t now_ms)
{
    face_frame_t fr = face_state_frame(st, now_ms);
    face_mode_t mode = face_state_mode(st, now_ms);

    if (fr.text_fallback) {
        if (!s_drawn.valid || !s_drawn.text || s_drawn.text_mode != mode) {
            draw_text_face(mode);
            memset(&s_drawn, 0, sizeof(s_drawn));
            s_drawn.valid = true;
            s_drawn.text = true;
            s_drawn.text_mode = mode;
        }
        return true;
    }

    if (!s_drawn.valid || s_drawn.text || fr.expr != s_drawn.expr ||
        fr.expr_variant != s_drawn.expr_variant) {
        if (!draw_file(s_expr_names[fr.expr], s_expr[fr.expr].suffix[fr.expr_variant])) {
            return false;
        }
        memset(&s_drawn, 0, sizeof(s_drawn));
        s_drawn.valid = true;
        s_drawn.expr = fr.expr;
        s_drawn.expr_variant = fr.expr_variant;
        s_drawn.patch = FACE_PATCH_NONE;
    }

    if (fr.patch != s_drawn.patch || fr.patch_variant != s_drawn.patch_variant) {
        if (s_drawn.patch != FACE_PATCH_NONE &&
            !restore_region(&s_patch_hdr[s_drawn.patch][s_drawn.patch_variant])) {
            return false;
        }
        if (fr.patch != FACE_PATCH_NONE &&
            !draw_file(s_patch_names[fr.patch], s_patch[fr.patch].suffix[fr.patch_variant])) {
            return false;
        }
        s_drawn.patch = fr.patch;
        s_drawn.patch_variant = fr.patch_variant;
    }
    return true;
}
```

- [ ] **Step 3: Replace `main/display.c`**

```c
#include "display.h"

#include "config.h"

#if CONFIG_ZCLAW_DISPLAY_ENABLE

#include "display_face.h"
#include "display_hw.h"
#include "display_status.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "face_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "page_rotation.h"
#include "sd_card.h"
#include "tools_handlers.h"

#define DISPLAY_TASK_STACK 5120
#define DISPLAY_TASK_PRIO  2
#define DISPLAY_QUEUE_LEN  8
#define DISPLAY_TICK_MS    30

typedef enum {
    DISPLAY_EV_THINKING,
    DISPLAY_EV_TALKING,
    DISPLAY_EV_WIFI,
} display_event_type_t;

typedef struct {
    display_event_type_t type;
    uint32_t value;
} display_event_t;

static const char *TAG = "display";
static QueueHandle_t s_queue = NULL;
static face_state_t s_face;
static page_rotation_t s_pages;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void apply_event(const display_event_t *ev, int64_t now)
{
    switch (ev->type) {
    case DISPLAY_EV_THINKING:
        face_state_set_thinking(&s_face, ev->value != 0);
        break;
    case DISPLAY_EV_TALKING:
        face_state_start_talking(&s_face, ev->value, now);
        break;
    case DISPLAY_EV_WIFI:
        face_state_set_wifi(&s_face, ev->value != 0);
        break;
    }
}

static void display_task(void *arg)
{
    (void)arg;
    bool have_page = false;
    page_t shown = PAGE_FACE;
    display_event_t ev;

    face_state_init(&s_face, esp_random(), now_ms());
    page_rotation_init(&s_pages, CONFIG_ZCLAW_FACE_SECONDS * 1000u,
                       CONFIG_ZCLAW_STATUS_SECONDS * 1000u, now_ms());

    while (1) {
        if (xQueueReceive(s_queue, &ev, pdMS_TO_TICKS(DISPLAY_TICK_MS)) == pdTRUE) {
            do {
                apply_event(&ev, now_ms());
            } while (xQueueReceive(s_queue, &ev, 0) == pdTRUE);
        }

        int64_t now = now_ms();
        if (sd_card_service(now)) {
            display_face_rescan(&s_face);
        }
        display_status_service(now, s_face.wifi_up);

        page_t page = page_rotation_update(&s_pages, face_state_is_active(&s_face, now), now);
        if (!have_page || page != shown) {
            have_page = true;
            shown = page;
            if (page == PAGE_STATUS) {
                display_status_draw(s_face.wifi_up);
            } else {
                display_face_invalidate();
            }
        }
        if (page == PAGE_FACE && !display_face_draw(&s_face, now)) {
            sd_card_mark_failed(now);
            display_face_clear(&s_face);
        }
    }
}

esp_err_t display_start(void)
{
    if (s_queue) {
        return ESP_OK;
    }

    esp_err_t err = display_hw_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Display disabled: %s", esp_err_to_name(err));
        return err;
    }

    float warm = 0;
    (void)tools_chip_temp_read(&warm);  // install the sensor once, before two tasks use it

    s_queue = xQueueCreate(DISPLAY_QUEUE_LEN, sizeof(display_event_t));
    if (!s_queue) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(display_task, "display", DISPLAY_TASK_STACK, NULL, DISPLAY_TASK_PRIO, NULL) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Display task started");
    return ESP_OK;
}

static void post(display_event_type_t type, uint32_t value)
{
    display_event_t ev = { .type = type, .value = value };
    if (s_queue) {
        (void)xQueueSend(s_queue, &ev, 0);  // drop when full; the next event corrects the state
    }
}

void display_notify_thinking(bool active)
{
    post(DISPLAY_EV_THINKING, active ? 1u : 0u);
}

void display_notify_talking(size_t reply_chars)
{
    post(DISPLAY_EV_TALKING, reply_chars > UINT32_MAX ? UINT32_MAX : (uint32_t)reply_chars);
}

void display_notify_wifi(bool connected)
{
    post(DISPLAY_EV_WIFI, connected ? 1u : 0u);
}

#else  // !CONFIG_ZCLAW_DISPLAY_ENABLE

esp_err_t display_start(void) { return ESP_ERR_NOT_SUPPORTED; }
void display_notify_thinking(bool active) { (void)active; }
void display_notify_talking(size_t reply_chars) { (void)reply_chars; }
void display_notify_wifi(bool connected) { (void)connected; }

#endif
```

(`UINT32_MAX` comes from `<stdint.h>`, already pulled in through the FreeRTOS headers; add `#include <stdint.h>` if the compiler complains.)

- [ ] **Step 4: Event hooks** — in `main/agent.c` add `#include "display.h"` and:

```c
static void send_response(const char *text, int64_t chat_id)
{
    display_notify_talking(text ? strlen(text) : 0);
    queue_channel_response(text);
    queue_telegram_response(text, chat_id);
}
```

at line 532 (`led_thinking_start();`) add `display_notify_thinking(true);` on the next line, and at line 812 (`led_thinking_stop();`) add `display_notify_thinking(false);` on the next line.

In `main/local_admin.c` add `#include "display.h"` (top, outside the `TEST_BUILD` blocks), then inside the WiFi event handler: right after `s_wifi_connected = false;` in the `WIFI_EVENT_STA_DISCONNECTED` branch add `display_notify_wifi(false);`, and right after `s_wifi_connected = true;` in the `IP_EVENT_STA_GOT_IP` branch add `display_notify_wifi(true);`.

- [ ] **Step 5: Host stubs** — `test/host/mock_display.c`:

```c
#include "display.h"

esp_err_t display_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

void display_notify_thinking(bool active)
{
    (void)active;
}

void display_notify_talking(size_t reply_chars)
{
    (void)reply_chars;
}

void display_notify_wifi(bool connected)
{
    (void)connected;
}
```

Add `mock_display.c \` to the first `gcc` command in `scripts/test.sh` (next to `mock_tools.c \`). If the host `esp_err.h` mock lacks `ESP_ERR_NOT_SUPPORTED`, add `#define ESP_ERR_NOT_SUPPORTED 0x106` to `test/host/esp_err.h`.

Run: `./scripts/test.sh host 2>&1 | tail -4` → `All tests passed!` and the other suites pass.

- [ ] **Step 6: Build** — add `"display_face.c"` to `SRCS` in `main/CMakeLists.txt`; `idf.py build 2>&1 | grep -E "main/.*(warning|error)|binary size"` → clean; note the binary size and free percentage.

- [ ] **Step 7: Hardware verification**

```bash
PORT=$(ls /dev/cu.usbmodem* | head -1)
idf.py -p "$PORT" flash
perl -e 'alarm shift; exec @ARGV' 40 script -q /dev/null idf.py -p "$PORT" monitor > build/monitor.log 2>&1
grep -E "display|sd_card|Face catalogue|Ready" build/monitor.log
```

Expected: `Display task started`, `SD mounted`, `Face catalogue: neutral=1 thinking=1 sleeping=1 eyes_closed=1 speaking=1`. Then check with the user, in order:
1. Before WiFi connects the face shows `sleeping`; after it connects, `neutral` with a blink every 3–7 s, no shimmer.
2. After 10 s the status page appears for 5 s with the real time, weather (after the first refresh), SSID, signal, CPU, heap, uptime, version/model; then back to the face.
3. Send a Telegram message: the face switches to `thinking` while the LLM works, then the mouth animates while the reply goes out, then idle; the status page never interrupts this.
4. Pull the SD card out during the face page: within one frame the text fallback (`Fidel` / `sin tarjeta SD`) appears and the log shows `SD I/O error: unmounting`. Re-insert: within 60 s `SD mounted` and the face returns.
5. Send `/status` (Telegram or serial): minimum free heap ≥ 100 KB. Record free/min heap.
6. Ask Fidel to `gpio_write` pin 6: the reply contains `reserved for the display/SD SPI bus`.

- [ ] **Step 8: Feature-off build check**

```bash
cp sdkconfig build/sdkconfig.bak-check
sed -i '' -e 's/^CONFIG_ZCLAW_DISPLAY_ENABLE=y/# CONFIG_ZCLAW_DISPLAY_ENABLE is not set/' \
          -e 's/^CONFIG_ZCLAW_SD_ENABLE=y/# CONFIG_ZCLAW_SD_ENABLE is not set/' sdkconfig
idf.py build 2>&1 | grep -E "main/.*(warning|error)|binary size"
cp build/sdkconfig.bak-check sdkconfig && rm build/sdkconfig.bak-check
idf.py build 2>&1 | tail -1
```

Expected: the feature-off build succeeds with no `main/` warnings (smaller binary); the restored config builds again. Do not flash the feature-off binary.

- [ ] **Step 9: Commit**

```bash
git add main/display_face.[ch] main/display.c main/agent.c main/local_admin.c main/CMakeLists.txt test/host/mock_display.c scripts/test.sh
git commit -m "feat(display): animate the face from SD and rotate with the status page"
```

- [ ] **Step 10: Report** — summarise for the user: measured frame times (full frame, patch), free/min heap, binary size, confirmed display Kconfig values (inversion/gap/order), and anything that deviated from the spec. Update `docs/superpowers/specs/2026-10-11-display-sd-design.md` §3 with the confirmed panel values and §11 with the measured numbers, and commit that doc change.
