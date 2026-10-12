# Delivery A — No Telegram, Answers on the Screen: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fidel's board runs without Telegram: every reply is shown as paged text on the ST7789, dashboards are requested in plain words through a tool, firmware arrives through a PIN-protected LAN page, and memory backups go to the SD card.

**Architecture:** A pure, host-tested `text_layout` module wraps UTF-8 into 29-column Latin-1 lines, clips the header, and computes page timings. `page_rotation` gains a `PAGE_ANSWER` page that never covers a page a tool requested in the same turn. The agent hands `(label, prompt, reply)` to `display_show_answer()`, which copies them into one heap block for the display task. `display_answer.c` owns and draws that block. Web OTA is `esp_http_server` plus a pure policy module (PIN check, request status); `esp_ota_*` streams the image. A Kconfig switch keeps Telegram off on this board and on by default everywhere else.

**Tech Stack:** C (ESP-IDF v5.5.5, FreeRTOS) on ESP32-C6, `esp_lcd` ST7789, FATFS on SD, `esp_http_server`, `app_update`; host tests in C99 with gcc and the repo's mocks.

**Spec:** `docs/superpowers/specs/2026-10-11-screen-answers-design.md`

## Global Constraints

- Repo `~/Projects/zclaw`, branch `fidel`. ESP-IDF v5.5.5 at `~/esp/esp-idf`. Firmware build: `source ~/esp/esp-idf/export.sh >/dev/null 2>&1 && idf.py build > build/idf-build.log 2>&1; grep -E "Project build complete|error:" build/idf-build.log | head -5`.
- Identifiers, comments and commit messages are in English. Text shown on the screen or the web page is in Spanish (UTF-8 written with `\xNN` escapes in C sources, as the existing code does).
- Never commit or print secrets. `sdkconfig` and `sdkconfig.defaults.esp32c6` are git-ignored and hold the dashboard token, API keys and the OTA PIN. Never `cat` or `grep` them without filtering the value, never delete `sdkconfig`, and never create dated `*.bak` copies.
- Ask the user before flashing over USB (Task 8). Changes that need sudo on the VPS are run by the user (none are expected here).
- Host tests must stay warning-clean: `-std=c99 -Wall -Wextra -Werror -Wshadow -Wformat=2`, with ASan/UBSan where `scripts/test.sh` enables them.
- **Host gate.** Before Task 1, copy `.superpowers/sdd/2026-10-11-display-sd/host-gate.sh` and `baseline-failures.txt` into this plan's workspace `.superpowers/sdd/2026-10-11-screen-answers/`. The gate command is `bash .superpowers/sdd/2026-10-11-screen-answers/host-gate.sh` and its expected output starts with `host gate OK`.
  - Only the two baseline failures are tolerated: `test_agent.c` line 784 (`Device target is`) and `test_builtin_tools_registry.c:103`.
  - New tests go in new files, or are appended to `test_agent.c` after line 784, so the baseline line numbers do not move. If they move anyway, update `baseline-failures.txt` with the new number and record a ruling.
- Logic with no ESP-IDF dependency goes in pure modules with host tests. `main/CMakeLists.txt` compiles every source in every build, so each ESP-only file wraps its body in `#if CONFIG_ZCLAW_<FEATURE>` and gives a stub in `#else`, as `display.c` already does.
- Spec values, verbatim:
  - prompt ≤ 160 bytes; reply ≤ 1024 bytes (`CHANNEL_TX_BUF_SIZE`);
  - 29 columns × 11 lines with 4 px margins; header grey at y=2, ≤ 2 lines; divider at y=38; body at y=44; page indicator `n/m` at y=224, only with more than one page;
  - 70 ms per character clamped to 8–45 s, split in proportion to each page's characters, at least 3 s per page;
  - dashboards stay up for 30 s;
  - web OTA: port 80, `max_open_sockets = 2`, 6 KB task stack, 4 KB heap chunks, `X-OTA-PIN` header, wrong PIN → wait 1 s then 401, 409 if busy, 413 if too large, 400 without `Content-Length`;
  - PIN of at least 4 characters;
  - backup file `/sdcard/backup/memories-YYYYMMDD-HHMM.txt`, or `memories-up<uptime>s.txt` before SNTP.
- Header labels: serial `Tú:`, cron `Tarea:` (Delivery C adds `Dijiste:`).
- Every commit message ends with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  ```

### Design decisions this plan takes (spec silent or impossible as written)

- **D1, the answer yields to a tool page.** When `display_dashboard` or `display_image` shows a page during a turn, that turn's text reply does not replace it; the reply still goes to serial. Without this, "muéstrame el clima" would flash the dashboard and replace it with "Aquí tienes el clima" a moment later.
- **D2, "..." instead of "…".** The 8x16 font only covers Latin-1, and U+2026 is outside it, so every ellipsis is drawn as `...` (3 columns).
- **D3, at most 6 pages.** The body is capped at 66 lines (6 pages). A reply made of very many short lines is cut, and its last line ends in `...`.
- **D4, the screen hint survives the prompt overflow.** The composed system prompt already exceeds `s_system_prompt_buf[2048]` and falls back to the bare `SYSTEM_PROMPT`; this is the pre-existing baseline failure at line 784. The screen hint is therefore also appended on that fallback path. The overflow itself is out of scope.
- **D5, clear upload errors.**
  - The web page checks the file size in JavaScript before uploading.
  - 400 and 413 replies close the connection.
  - A wrong PIN, or an image rejected on the first write, drains the remaining body so the browser shows the message instead of a reset connection.
- **D6, cron header.** The `Tarea:` header shows the cron action without its `[CRON n] ` prefix.

## Review Focus

1. **Emoji or other non-Latin-1 text** in an LLM reply. Each character must render as one `?` column, and wrapping must never split its UTF-8 bytes. Covered by Task 2 `emoji_is_one_column_and_never_split`.
2. **A reply made of dozens of short lines** (lists). It must stop at 6 pages with `...` on the last kept line, never overflow the line array. Covered by Task 2 `too_many_lines_end_with_ellipsis`.
3. **A long prompt.** The header must stay within 2 lines and end in `...`, without pushing the body down. Covered by Task 2 `header_clipped_to_two_lines`.
4. **A tool page followed by the text reply** ("muéstrame el clima"). The dashboard must stay for its 30 s. Covered by Task 3 `answer_does_not_cover_tool_page_of_same_turn`.
5. **A wrong file uploaded to the OTA page** (a JPEG, or a truncated `.bin`). The page must show an error, the board must not reboot, and the running firmware must stay untouched. This is ESP-only code, so it has no host test; Task 8 step 6 checks it on the board.

---

### Task 1: Telegram toggle

**Files:**
- Modify: `main/Kconfig.projbuild:37` (new option before `ZCLAW_STUB_TELEGRAM`)
- Modify: `main/main.c` (sections 7 and the `telegram_enabled` block)
- Modify (git-ignored, local only): `sdkconfig.defaults.esp32c6`

**Interfaces:**
- Consumes: nothing.
- Produces: `CONFIG_ZCLAW_TELEGRAM_ENABLE` (bool, default `y`). With `n`, `telegram_is_configured()` returns false at runtime because `telegram_init()` never runs. Task 6 relies on this.

This task is configuration and wiring only, so there is no host test (TDD exception: configuration). The build and the board log in Task 8 verify it.

- [ ] **Step 1: Add the Kconfig option**

In `main/Kconfig.projbuild`, insert before `    config ZCLAW_STUB_TELEGRAM`:

```
    config ZCLAW_TELEGRAM_ENABLE
        bool "Enable the Telegram channel"
        default y
        help
            When off, the board never initialises or starts Telegram: no
            getUpdates polling, no TLS session, no startup message. Replies go
            to the serial console and the screen, and memory_export writes to
            the SD card instead.

```

- [ ] **Step 2: Gate Telegram in `main.c`**

After the `#include` block of `main/main.c`, add:

```c
// Telegram runs only when enabled and not stubbed (QEMU).
#if CONFIG_ZCLAW_TELEGRAM_ENABLE && !CONFIG_ZCLAW_STUB_TELEGRAM
#define ZCLAW_TELEGRAM_ACTIVE 1
#else
#define ZCLAW_TELEGRAM_ACTIVE 0
#endif
```

Replace section 7:

```c
    // 7. Initialize Telegram config state
#if CONFIG_ZCLAW_STUB_TELEGRAM
    ESP_LOGW(TAG, "Telegram stub mode enabled; skipping Telegram startup");
#else
```

with:

```c
    // 7. Initialize Telegram config state
#if !ZCLAW_TELEGRAM_ACTIVE
    ESP_LOGW(TAG, "Telegram disabled (CONFIG_ZCLAW_TELEGRAM_ENABLE=n or stub); skipping Telegram startup");
#else
```

Then replace:

```c
#if CONFIG_ZCLAW_STUB_TELEGRAM
    bool telegram_enabled = false;
#else
```

with:

```c
#if !ZCLAW_TELEGRAM_ACTIVE
    bool telegram_enabled = false;
#else
```

Leave the rest unchanged: `telegram_enabled == false` already skips the output queue, `telegram_start()` and `telegram_send_startup()`.

- [ ] **Step 3: Turn Telegram off in the local board defaults**

Run:
```bash
cd ~/Projects/zclaw && git check-ignore -q sdkconfig.defaults.esp32c6 && \
  { grep -q '^CONFIG_ZCLAW_TELEGRAM_ENABLE=' sdkconfig.defaults.esp32c6 || echo 'CONFIG_ZCLAW_TELEGRAM_ENABLE=n' >> sdkconfig.defaults.esp32c6; } && echo "defaults updated (ignored file)"
```
Expected: `defaults updated (ignored file)`.

- [ ] **Step 4: Build and check the resolved config**

Run the firmware build from Global Constraints, then:
```bash
cd ~/Projects/zclaw && grep -E '^(# )?CONFIG_ZCLAW_TELEGRAM_ENABLE' sdkconfig
```
Expected: `Project build complete` and `# CONFIG_ZCLAW_TELEGRAM_ENABLE is not set`.

If it shows `=y`, the existing `sdkconfig` did not pick up the default. Change that one line in place, then rebuild:
```bash
sed -i '' 's/^CONFIG_ZCLAW_TELEGRAM_ENABLE=y$/# CONFIG_ZCLAW_TELEGRAM_ENABLE is not set/' sdkconfig
```

- [ ] **Step 5: Host gate and commit**

Run: `bash .superpowers/sdd/2026-10-11-screen-answers/host-gate.sh`
Expected: `host gate OK` (no host code changed).

```bash
git add main/Kconfig.projbuild main/main.c
git commit -m "feat: CONFIG_ZCLAW_TELEGRAM_ENABLE switch (default on)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `text_layout` pure module

**Files:**
- Create: `main/text_layout.h`, `main/text_layout.c`
- Create: `test/host/test_text_layout.c`
- Modify: `scripts/test.sh` (add `test_text_layout.c` after `test_dash_state.c`, and `../../main/text_layout.c` after `../../main/dash_state.c`)
- Modify: `test/host/test_runner.c` (extern + call)
- Modify: `main/CMakeLists.txt` (add `"text_layout.c"` after `"status_format.c"`)

**Interfaces:**
- Consumes: `uint8_t text_latin1_next(const char **cursor)` from `main/text_latin1.h`.
- Produces (used by Task 4):
  - `typedef struct { uint16_t offset; uint16_t len; uint8_t chars; bool ellipsis; } text_line_t;`
  - `size_t text_layout_wrap(const char *text, size_t cols, text_line_t *lines, size_t max_lines);`
  - `size_t text_layout_line_copy(const char *text, const text_line_t *line, char *out, size_t out_len);`
  - `size_t text_layout_clip_utf8(const char *src, size_t max_bytes, char *dst, size_t dst_len);`
  - `size_t text_layout_page_count(size_t line_count, size_t lines_per_page);`
  - `uint32_t text_layout_durations(const text_line_t *lines, size_t line_count, size_t lines_per_page, uint32_t *page_ms, size_t max_pages);`
  - `size_t text_layout_page_at(const uint32_t *page_ms, size_t pages, uint32_t elapsed_ms);`
  - macros `TEXT_LAYOUT_ELLIPSIS "..."` and `TEXT_LAYOUT_ELLIPSIS_COLS 3`.

- [ ] **Step 1: Write the header**

`main/text_layout.h`:

```c
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
```

- [ ] **Step 2: Write the failing tests**

`test/host/test_text_layout.c`:

```c
/*
 * Host tests for the answer page text layout.
 */

#include <stdio.h>
#include <string.h>

#include "text_layout.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

static const char *line_text(const char *text, const text_line_t *line)
{
    static char buf[160];
    text_layout_line_copy(text, line, buf, sizeof(buf));
    return buf;
}

TEST(wraps_at_spaces)
{
    const char *t = "el veloz murcielago hindu comia feliz";
    text_line_t l[8];
    ASSERT(text_layout_wrap(t, 10, l, 8) == 5);
    ASSERT(strcmp(line_text(t, &l[0]), "el veloz") == 0);
    ASSERT(strcmp(line_text(t, &l[1]), "murcielago") == 0);
    ASSERT(strcmp(line_text(t, &l[2]), "hindu") == 0);
    ASSERT(strcmp(line_text(t, &l[3]), "comia") == 0);
    ASSERT(strcmp(line_text(t, &l[4]), "feliz") == 0);
    ASSERT(!l[4].ellipsis);
    return 0;
}

TEST(accented_letter_is_one_column)
{
    const char *t = "canci\xC3\xB3n peque\xC3\xB1" "a";
    text_line_t l[4];
    ASSERT(text_layout_wrap(t, 7, l, 4) == 2);
    ASSERT(l[0].chars == 7 && l[0].len == 8);
    ASSERT(strcmp(line_text(t, &l[1]), "peque\xC3\xB1" "a") == 0);
    return 0;
}

TEST(splits_word_longer_than_line)
{
    const char *t = "abcdefghij";
    text_line_t l[4];
    ASSERT(text_layout_wrap(t, 4, l, 4) == 3);
    ASSERT(strcmp(line_text(t, &l[0]), "abcd") == 0);
    ASSERT(strcmp(line_text(t, &l[1]), "efgh") == 0);
    ASSERT(strcmp(line_text(t, &l[2]), "ij") == 0);
    return 0;
}

TEST(newline_starts_line_and_trailing_newlines_dropped)
{
    const char *t = "uno\n\ndos\n\n";
    text_line_t l[8];
    ASSERT(text_layout_wrap(t, 10, l, 8) == 3);
    ASSERT(strcmp(line_text(t, &l[0]), "uno") == 0);
    ASSERT(l[1].chars == 0);
    ASSERT(strcmp(line_text(t, &l[2]), "dos") == 0);
    return 0;
}

TEST(break_at_end_of_line_does_not_add_blank_line)
{
    const char *t = "abcd \nefg";
    text_line_t l[4];
    ASSERT(text_layout_wrap(t, 4, l, 4) == 2);
    ASSERT(strcmp(line_text(t, &l[0]), "abcd") == 0);
    ASSERT(strcmp(line_text(t, &l[1]), "efg") == 0);
    return 0;
}

TEST(control_characters_take_no_column)
{
    const char *t = "a\rb\x01" "c";
    const char *tab = "x\ty";
    text_line_t l[2];
    ASSERT(text_layout_wrap(t, 3, l, 2) == 1);
    ASSERT(l[0].chars == 3);
    ASSERT(strcmp(line_text(t, &l[0]), "abc") == 0);
    ASSERT(text_layout_wrap(tab, 3, l, 2) == 1);
    ASSERT(strcmp(line_text(tab, &l[0]), "x y") == 0);
    return 0;
}

TEST(emoji_is_one_column_and_never_split)
{
    const char *t = "ab\xF0\x9F\x98\x80" "cd";
    text_line_t l[4];
    ASSERT(text_layout_wrap(t, 3, l, 4) == 2);
    ASSERT(l[0].chars == 3 && l[0].len == 6);
    ASSERT(strcmp(line_text(t, &l[1]), "cd") == 0);
    return 0;
}

TEST(empty_text_has_no_lines)
{
    text_line_t l[2];
    ASSERT(text_layout_wrap("", 29, l, 2) == 0);
    ASSERT(text_layout_wrap("\n\n", 29, l, 2) == 0);
    return 0;
}

TEST(too_many_lines_end_with_ellipsis)
{
    char t[256];
    text_line_t l[66];
    size_t n = 0;
    for (int i = 0; i < 100; i++) {
        t[n++] = 'a';
        t[n++] = '\n';
    }
    t[n] = '\0';
    ASSERT(text_layout_wrap(t, 29, l, 66) == 66);
    ASSERT(!l[64].ellipsis);
    ASSERT(l[65].ellipsis);
    ASSERT(strcmp(line_text(t, &l[65]), "a...") == 0);
    return 0;
}

TEST(header_clipped_to_two_lines)
{
    const char *t = "T\xC3\xBA: cu\xC3\xA9ntame con todo detalle la historia completa de "
                    "la isla desde la llegada de Col\xC3\xB3n hasta hoy";
    text_line_t l[2];
    ASSERT(text_layout_wrap(t, 29, l, 2) == 2);
    ASSERT(!l[0].ellipsis);
    ASSERT(l[1].ellipsis);
    ASSERT(l[1].chars + TEXT_LAYOUT_ELLIPSIS_COLS <= 29);
    const char *last = line_text(t, &l[1]);
    ASSERT(strcmp(last + strlen(last) - 3, "...") == 0);
    return 0;
}

TEST(text_that_fits_exactly_has_no_ellipsis)
{
    text_line_t l[2];
    ASSERT(text_layout_wrap("abcd efgh", 4, l, 2) == 2);
    ASSERT(!l[1].ellipsis);
    ASSERT(text_layout_wrap("abcd efgh  \n\n", 4, l, 2) == 2);
    ASSERT(!l[1].ellipsis);
    return 0;
}

TEST(clip_utf8_keeps_short_text)
{
    char out[16];
    ASSERT(text_layout_clip_utf8("hola", 10, out, sizeof(out)) == 4);
    ASSERT(strcmp(out, "hola") == 0);
    return 0;
}

TEST(clip_utf8_cuts_with_ellipsis)
{
    char out[16];
    ASSERT(text_layout_clip_utf8("abcdefghij", 8, out, sizeof(out)) == 8);
    ASSERT(strcmp(out, "abcde...") == 0);
    return 0;
}

TEST(clip_utf8_never_splits_character)
{
    char out[16];
    /* the cut at byte 6 would split the two-byte n-tilde at bytes 5-6 */
    ASSERT(text_layout_clip_utf8("aaaaa\xC3\xB1" "bbbb", 9, out, sizeof(out)) == 8);
    ASSERT(strcmp(out, "aaaaa...") == 0);
    return 0;
}

TEST(page_count)
{
    ASSERT(text_layout_page_count(0, 11) == 1);
    ASSERT(text_layout_page_count(11, 11) == 1);
    ASSERT(text_layout_page_count(12, 11) == 2);
    return 0;
}

static void fill_lines(text_line_t *l, size_t n, uint8_t chars)
{
    for (size_t i = 0; i < n; i++) {
        l[i].offset = 0;
        l[i].len = chars;
        l[i].chars = chars;
        l[i].ellipsis = false;
    }
}

TEST(short_reply_gets_minimum_time)
{
    text_line_t l[2];
    uint32_t ms[6];
    fill_lines(l, 2, 25);
    ASSERT(text_layout_durations(l, 2, 11, ms, 6) == 8000);
    ASSERT(ms[0] == 8000);
    return 0;
}

TEST(one_page_reply_gets_70ms_per_char)
{
    text_line_t l[11];
    uint32_t ms[6];
    fill_lines(l, 11, 29);
    ASSERT(text_layout_durations(l, 11, 11, ms, 6) == 22330);
    return 0;
}

TEST(long_reply_capped_and_split_evenly)
{
    text_line_t l[33];
    uint32_t ms[6];
    fill_lines(l, 33, 29);
    ASSERT(text_layout_durations(l, 33, 11, ms, 6) == 45000);
    ASSERT(ms[0] == 15000 && ms[1] == 15000 && ms[2] == 15000);
    return 0;
}

TEST(short_last_page_gets_three_seconds)
{
    text_line_t l[12];
    uint32_t ms[6];
    fill_lines(l, 11, 29);
    fill_lines(&l[11], 1, 5);
    ASSERT(text_layout_durations(l, 12, 11, ms, 6) == 25330);
    ASSERT(ms[0] == 22330);
    ASSERT(ms[1] == 3000);
    return 0;
}

TEST(page_at_follows_durations)
{
    const uint32_t ms[2] = {5000, 3000};
    ASSERT(text_layout_page_at(ms, 2, 0) == 0);
    ASSERT(text_layout_page_at(ms, 2, 4999) == 0);
    ASSERT(text_layout_page_at(ms, 2, 5000) == 1);
    ASSERT(text_layout_page_at(ms, 2, 7999) == 1);
    ASSERT(text_layout_page_at(ms, 2, 100000) == 1);
    return 0;
}

int test_text_layout_all(void)
{
    int failures = 0;

    printf("\nText Layout Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(wraps_at_spaces);
    RUN(accented_letter_is_one_column);
    RUN(splits_word_longer_than_line);
    RUN(newline_starts_line_and_trailing_newlines_dropped);
    RUN(break_at_end_of_line_does_not_add_blank_line);
    RUN(control_characters_take_no_column);
    RUN(emoji_is_one_column_and_never_split);
    RUN(empty_text_has_no_lines);
    RUN(too_many_lines_end_with_ellipsis);
    RUN(header_clipped_to_two_lines);
    RUN(text_that_fits_exactly_has_no_ellipsis);
    RUN(clip_utf8_keeps_short_text);
    RUN(clip_utf8_cuts_with_ellipsis);
    RUN(clip_utf8_never_splits_character);
    RUN(page_count);
    RUN(short_reply_gets_minimum_time);
    RUN(one_page_reply_gets_70ms_per_char);
    RUN(long_reply_capped_and_split_evenly);
    RUN(short_last_page_gets_three_seconds);
    RUN(page_at_follows_durations);

#undef RUN
    return failures;
}
```

Register the suite:
- In `test/host/test_runner.c`, add `extern int test_text_layout_all(void);` after `extern int test_dash_state_all(void);`.
- In the same file, add `    failures += test_text_layout_all();` after `    failures += test_dash_state_all();`.
- In `scripts/test.sh`, add `        test_text_layout.c \` after `        test_dash_state.c \`.
- In `scripts/test.sh`, add `        ../../main/text_layout.c \` after `        ../../main/dash_state.c \`.

Create `main/text_layout.c` holding only `#include "text_layout.h"`, so the suite links but fails.

- [ ] **Step 3: Run the tests to see them fail**

Run: `bash .superpowers/sdd/2026-10-11-screen-answers/host-gate.sh`
Expected: `host gate FAILED`. Either the link fails with undefined `text_layout_*` symbols, or the diff lists the new `FAIL:` lines from `test_text_layout.c`.

- [ ] **Step 4: Implement**

Write `main/text_layout.c`:

```c
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
```

Add `"text_layout.c"` to `main/CMakeLists.txt` SRCS after `"status_format.c"`.

- [ ] **Step 5: Run the tests to see them pass**

Run: `bash .superpowers/sdd/2026-10-11-screen-answers/host-gate.sh`
Expected: `host gate OK`, and `grep -A21 "Text Layout Tests" .superpowers/sdd/2026-10-11-screen-answers/gate.log` shows 20 `... OK` lines.

- [ ] **Step 6: Commit**

```bash
git add main/text_layout.h main/text_layout.c main/CMakeLists.txt test/host/test_text_layout.c test/host/test_runner.c scripts/test.sh
git commit -m "feat(display): text_layout module for the answer page

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `PAGE_ANSWER` in `page_rotation`

**Files:**
- Modify: `main/page_rotation.h`, `main/page_rotation.c`
- Test: `test/host/test_page_rotation.c` (append four tests + `RUN` lines)

**Interfaces:**
- Consumes: the existing `page_rotation_t` and `page_rotation_update()`.
- Produces (used by Task 4):
  - `PAGE_ANSWER` in `page_t`;
  - `bool page_rotation_show(page_rotation_t *r, page_t page, uint32_t duration_ms, int64_t now_ms);`. It used to return `void`; existing callers ignore the result. It returns `false` only for a `PAGE_ANSWER` refused under D1.
  - new field `bool requested_in_turn`.

- [ ] **Step 1: Write the failing tests**

Append before `int test_page_rotation_all(void)` in `test/host/test_page_rotation.c`:

```c
TEST(answer_page_shows_for_its_duration)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 0, 0);
    ASSERT(page_rotation_show(&r, PAGE_ANSWER, 8000, 1000));
    ASSERT(page_rotation_update(&r, false, 1000) == PAGE_ANSWER);
    ASSERT(page_rotation_update(&r, false, 8999) == PAGE_ANSWER);
    ASSERT(page_rotation_update(&r, false, 9000) == PAGE_FACE);
    return 0;
}

TEST(answer_does_not_cover_tool_page_of_same_turn)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 0, 0);
    ASSERT(page_rotation_update(&r, true, 0) == PAGE_FACE);       /* turn starts */
    ASSERT(page_rotation_show(&r, PAGE_REMOTE, 30000, 100));      /* display_dashboard tool */
    ASSERT(!page_rotation_show(&r, PAGE_ANSWER, 8000, 200));      /* the turn's text reply */
    ASSERT(page_rotation_update(&r, true, 300) == PAGE_REMOTE);
    ASSERT(page_rotation_update(&r, false, 400) == PAGE_REMOTE);  /* turn over */
    ASSERT(page_rotation_update(&r, false, 30099) == PAGE_REMOTE);
    ASSERT(page_rotation_update(&r, false, 30100) == PAGE_FACE);
    return 0;
}

TEST(answer_replaces_page_from_an_earlier_turn)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 0, 0);
    ASSERT(page_rotation_update(&r, true, 0) == PAGE_FACE);
    ASSERT(page_rotation_show(&r, PAGE_REMOTE, 30000, 100));
    ASSERT(page_rotation_update(&r, false, 200) == PAGE_REMOTE);  /* turn over */
    /* a command reply (no thinking) while that dashboard is still up */
    ASSERT(page_rotation_show(&r, PAGE_ANSWER, 8000, 300));
    ASSERT(page_rotation_update(&r, false, 300) == PAGE_ANSWER);
    return 0;
}

TEST(answer_replaces_previous_answer)
{
    page_rotation_t r;
    page_rotation_init(&r, 10000, 0, 0);
    ASSERT(page_rotation_update(&r, true, 0) == PAGE_FACE);
    ASSERT(page_rotation_show(&r, PAGE_ANSWER, 8000, 100));
    ASSERT(page_rotation_show(&r, PAGE_ANSWER, 20000, 200));
    ASSERT(page_rotation_update(&r, false, 8100) == PAGE_ANSWER);  /* the second, longer one */
    ASSERT(page_rotation_update(&r, false, 20200) == PAGE_FACE);
    return 0;
}
```

Append to the `RUN` list in `test_page_rotation_all`:

```c
    RUN(answer_page_shows_for_its_duration);
    RUN(answer_does_not_cover_tool_page_of_same_turn);
    RUN(answer_replaces_page_from_an_earlier_turn);
    RUN(answer_replaces_previous_answer);
```

- [ ] **Step 2: Run to see it fail**

Run: `bash .superpowers/sdd/2026-10-11-screen-answers/host-gate.sh`
Expected: `host gate FAILED`, because the compile fails on `PAGE_ANSWER` being undeclared and on `page_rotation_show` returning `void`.

- [ ] **Step 3: Implement**

In `main/page_rotation.h`, change the enum, the struct and the `show` declaration:

```c
typedef enum {
    PAGE_FACE = 0,
    PAGE_STATUS,   // device status (/panel), drawn locally
    PAGE_REMOTE,   // dashboard image downloaded from the VPS (/clima, /cripto)
    PAGE_ANSWER    // Fidel's reply as text (question on top)
} page_t;

typedef struct {
    page_t page;
    int64_t since_ms;
    uint32_t face_ms;
    uint32_t status_ms;
    page_t requested;         // page shown on demand
    int64_t requested_until_ms;  // 0 = nothing requested
    bool requested_in_turn;      // requested while an activity was in progress
    bool prev_forced;            // to detect the start of a new activity
} page_rotation_t;
```

```c
// Show `page` for duration_ms (/panel, /clima, /cripto, answers). A new
// request replaces the previous one. Only an activity that starts after the
// request cancels it, so a page requested during Fidel's turn survives that
// turn's reply. An answer never covers a page requested during the same
// turn (a tool showed it): returns false and changes nothing.
bool page_rotation_show(page_rotation_t *r, page_t page, uint32_t duration_ms, int64_t now_ms);
```

In `main/page_rotation.c`:
- In `page_rotation_init`, add `r->requested_in_turn = false;`.
- Replace `page_rotation_show` with:

```c
bool page_rotation_show(page_rotation_t *r, page_t page, uint32_t duration_ms, int64_t now_ms)
{
    bool tool_page_up = r->requested_until_ms != 0 && now_ms < r->requested_until_ms &&
                        r->requested != PAGE_ANSWER && r->requested_in_turn;
    if (page == PAGE_ANSWER && tool_page_up && r->prev_forced) {
        return false;
    }
    r->requested = page;
    r->requested_until_ms = now_ms + duration_ms;
    r->requested_in_turn = r->prev_forced;
    return true;
}
```

- [ ] **Step 4: Run to see it pass**

Run: `bash .superpowers/sdd/2026-10-11-screen-answers/host-gate.sh`
Expected: `host gate OK`, with the 12 page-rotation tests printing `OK`.

- [ ] **Step 5: Commit**

```bash
git add main/page_rotation.h main/page_rotation.c test/host/test_page_rotation.c
git commit -m "feat(display): PAGE_ANSWER that yields to a tool page of the same turn

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Answer page end to end

**Files:**
- Modify: `main/display.h`, `main/display.c`
- Create: `main/display_answer.h`, `main/display_answer.c`
- Modify: `main/agent_commands.h`, `main/agent_commands.c` (`agent_cron_trigger_body`)
- Modify: `main/agent.c` (`send_response`, reply header state, `agent_task`)
- Modify: `test/host/mock_display.h`, `test/host/mock_display.c`
- Create: `test/host/test_agent_commands.c`
- Modify: `test/host/test_agent.c` (append 4 tests at the end, plus registration)
- Modify: `test/host/test_runner.c`, `scripts/test.sh` (register `test_agent_commands.c`)
- Modify: `main/CMakeLists.txt` (add `"display_answer.c"` after `"display_remote.c"`)

**Interfaces:**
- Consumes:
  - from Task 2: `text_layout_wrap`, `text_layout_line_copy`, `text_layout_clip_utf8`, `text_layout_page_count`, `text_layout_durations`, `text_layout_page_at`, `text_line_t`;
  - from Task 3: `PAGE_ANSWER`, `bool page_rotation_show(...)`.
- Produces:
  - `bool display_show_answer(const char *label, const char *prompt, const char *reply);` (display.h);
  - `bool display_is_running(void);` (display.h, used by Task 5);
  - `const char *agent_cron_trigger_body(const char *message);` (agent_commands.h);
  - mock controls `mock_display_answer_calls()`, `mock_display_last_answer_label()`, `mock_display_last_answer_prompt()`, `mock_display_last_answer_reply()`.

- [ ] **Step 1: Write the failing tests**

`test/host/test_agent_commands.c`:

```c
/*
 * Host tests for agent command helpers.
 */

#include <stdio.h>
#include <string.h>

#include "agent_commands.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(cron_trigger_body_strips_prefix)
{
    ASSERT(strcmp(agent_cron_trigger_body("[CRON 12] regar las plantas"), "regar las plantas") == 0);
    ASSERT(strcmp(agent_cron_trigger_body("  [CRON 1]   hola"), "hola") == 0);
    return 0;
}

TEST(cron_trigger_body_keeps_other_text)
{
    ASSERT(strcmp(agent_cron_trigger_body("hola"), "hola") == 0);
    ASSERT(strcmp(agent_cron_trigger_body("[CRON 5"), "[CRON 5") == 0);
    ASSERT(agent_cron_trigger_body(NULL) == NULL);
    return 0;
}

int test_agent_commands_all(void)
{
    int failures = 0;

    printf("\nAgent Command Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(cron_trigger_body_strips_prefix);
    RUN(cron_trigger_body_keeps_other_text);

#undef RUN
    return failures;
}
```

Register it:
- `test/host/test_runner.c`: add `extern int test_agent_commands_all(void);` and `    failures += test_agent_commands_all();` next to the `text_layout` lines.
- `scripts/test.sh`: add `        test_agent_commands.c \` after `        test_text_layout.c \`. `agent_commands.c` is already linked.

Append to `test/host/test_agent.c`, just before `int test_agent_all(void)` (after line 784, so the baseline does not move):

```c
TEST(reply_shows_answer_page_with_prompt)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];
    const char *reply =
        "{\"content\":[{\"type\":\"text\",\"text\":\"Son las 12:45 en Madrid.\"}],"
        "\"stop_reason\":\"end_turn\"}";

    reset_state();
    mock_display_reset();
    mock_display_set_running(true);
    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q, NULL);
    ASSERT(mock_llm_push_result(ESP_OK, reply));

    agent_test_process_message("que hora es en Madrid");

    ASSERT(mock_display_answer_calls() == 1);
    ASSERT_STR_EQ(mock_display_last_answer_label(), "T\xC3\xBA:");
    ASSERT_STR_EQ(mock_display_last_answer_prompt(), "que hora es en Madrid");
    ASSERT_STR_EQ(mock_display_last_answer_reply(), "Son las 12:45 en Madrid.");
    ASSERT(mock_display_talking_calls() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "Son las 12:45 en Madrid.");

    vQueueDelete(channel_q);
    mock_display_reset();
    return 0;
}

TEST(cron_reply_shows_task_header)
{
    QueueHandle_t channel_q;
    const char *reply =
        "{\"content\":[{\"type\":\"text\",\"text\":\"Hoy hace sol.\"}],\"stop_reason\":\"end_turn\"}";

    reset_state();
    mock_display_reset();
    mock_display_set_running(true);
    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q, NULL);
    ASSERT(mock_llm_push_result(ESP_OK, reply));

    agent_test_process_message("[CRON 3] dime el clima de hoy");

    ASSERT(mock_display_answer_calls() == 1);
    ASSERT_STR_EQ(mock_display_last_answer_label(), "Tarea:");
    ASSERT_STR_EQ(mock_display_last_answer_prompt(), "dime el clima de hoy");
    ASSERT_STR_EQ(mock_display_last_answer_reply(), "Hoy hace sol.");

    vQueueDelete(channel_q);
    mock_display_reset();
    return 0;
}

TEST(command_reply_shows_answer_page)
{
    QueueHandle_t channel_q;

    reset_state();
    mock_display_reset();
    mock_display_set_running(true);
    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q, NULL);

    agent_test_process_message("/help");

    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_display_answer_calls() == 1);
    ASSERT_STR_EQ(mock_display_last_answer_prompt(), "/help");
    ASSERT(strncmp(mock_display_last_answer_reply(), "zclaw online.", 13) == 0);

    vQueueDelete(channel_q);
    mock_display_reset();
    return 0;
}

TEST(dashboard_commands_do_not_show_answer)
{
    QueueHandle_t channel_q;

    reset_state();
    mock_display_reset();
    mock_display_set_running(true);
    channel_q = xQueueCreate(8, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q, NULL);

    agent_test_process_message("/panel");
    agent_test_process_message("/clima");
    agent_test_process_message("/cripto");

    ASSERT(mock_display_last_status_ms() == 30000);
    ASSERT_STR_EQ(mock_display_last_dashboard(), "cripto");
    ASSERT(mock_display_answer_calls() == 0);

    vQueueDelete(channel_q);
    mock_display_reset();
    return 0;
}
```

Append to the end of `test_agent_all`, before `return failures;`:

```c
    printf("  reply_shows_answer_page_with_prompt... ");
    if (test_reply_shows_answer_page_with_prompt() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  cron_reply_shows_task_header... ");
    if (test_cron_reply_shows_task_header() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  command_reply_shows_answer_page... ");
    if (test_command_reply_shows_answer_page() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  dashboard_commands_do_not_show_answer... ");
    if (test_dashboard_commands_do_not_show_answer() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }
```

- [ ] **Step 2: Add the API declarations and mock so the tests compile**

In `main/display.h`, add before `#endif`:

```c
// Show `reply` as text under a grey header "<label> <prompt>" (prompt
// clipped to 160 bytes, reply to 1024, both copied). The page lasts 70 ms
// per character (8-45 s) and pages itself. False when the display is
// disabled, not running, out of memory or its queue is full.
bool display_show_answer(const char *label, const char *prompt, const char *reply);

// True once display_start() succeeded.
bool display_is_running(void);
```

In `test/host/mock_display.h`, add:

```c
int mock_display_answer_calls(void);
const char *mock_display_last_answer_label(void);
const char *mock_display_last_answer_prompt(void);
const char *mock_display_last_answer_reply(void);
```

In `test/host/mock_display.c`:
- add `#include <stdio.h>` and, with the other statics:

```c
static int s_answer_calls = 0;
static char s_answer_label[16] = "";
static char s_answer_prompt[256] = "";
static char s_answer_reply[1100] = "";
```

- in `mock_display_reset()`, add:

```c
    s_answer_calls = 0;
    s_answer_label[0] = '\0';
    s_answer_prompt[0] = '\0';
    s_answer_reply[0] = '\0';
```

- at the end of the file, add:

```c
int mock_display_answer_calls(void)
{
    return s_answer_calls;
}

const char *mock_display_last_answer_label(void)
{
    return s_answer_label;
}

const char *mock_display_last_answer_prompt(void)
{
    return s_answer_prompt;
}

const char *mock_display_last_answer_reply(void)
{
    return s_answer_reply;
}

bool display_show_answer(const char *label, const char *prompt, const char *reply)
{
    s_answer_calls++;
    snprintf(s_answer_label, sizeof(s_answer_label), "%s", label ? label : "");
    snprintf(s_answer_prompt, sizeof(s_answer_prompt), "%s", prompt ? prompt : "");
    snprintf(s_answer_reply, sizeof(s_answer_reply), "%s", reply ? reply : "");
    return s_running;
}

bool display_is_running(void)
{
    return s_running;
}
```

In `main/agent_commands.h`, add after `agent_is_cron_trigger_message`:

```c
// Text of a "[CRON n] action" trigger without its prefix; any other message
// (or a malformed trigger) is returned unchanged.
const char *agent_cron_trigger_body(const char *message);
```

In `main/agent_commands.c`, add a stub so the suite links: `const char *agent_cron_trigger_body(const char *message) { return message; }`.

In `main/display.c`, add stubs to the `#else  // !CONFIG_ZCLAW_DISPLAY_ENABLE` block, so the firmware still links with the display disabled:

```c
bool display_show_answer(const char *label, const char *prompt, const char *reply)
{
    (void)label;
    (void)prompt;
    (void)reply;
    return false;
}
bool display_is_running(void) { return false; }
```

- [ ] **Step 3: Run to see the right failures**

Run: `bash .superpowers/sdd/2026-10-11-screen-answers/host-gate.sh`
Expected: `host gate FAILED`, and the diff shows only these new failures:
- `cron_trigger_body_strips_prefix`: the stub returns the whole text;
- `mock_display_answer_calls() == 1` in the three answer tests: the agent does not call it yet.

`dashboard_commands_do_not_show_answer` already passes; it is a regression guard.

- [ ] **Step 4: Implement `agent_cron_trigger_body`**

Replace the stub in `main/agent_commands.c`, below `agent_is_cron_trigger_message` (it uses the file's existing `is_whitespace_char`):

```c
const char *agent_cron_trigger_body(const char *message)
{
    const char *close;

    if (!agent_is_cron_trigger_message(message)) {
        return message;
    }
    close = strchr(message, ']');
    if (!close) {
        return message;
    }
    close++;
    while (*close && is_whitespace_char(*close)) {
        close++;
    }
    return close;
}
```

If `agent_commands.c` does not include `<string.h>` yet, add it.

- [ ] **Step 5: Call `display_show_answer` from the agent**

In `main/agent.c`:
- Add `#include "agent_commands.h"` if not already included.
- Next to the other file statics (around line 41), add:

```c
// Screen header of the reply being produced: who asked and what. Set at the
// start of process_message; points into the message being processed.
static const char *s_reply_label = "";
static const char *s_reply_prompt = "";
```

- Replace `send_response`:

```c
static void send_response(const char *text, int64_t chat_id)
{
    (void)display_show_answer(s_reply_label, s_reply_prompt, text ? text : "");
    queue_channel_response(text);
    queue_telegram_response(text, chat_id);
}
```

- In `process_message`, right after `bool is_cron_trigger = agent_is_cron_trigger_message(user_message);`:

```c
    s_reply_label = is_cron_trigger ? "Tarea:" : "T\xC3\xBA:";
    s_reply_prompt = is_cron_trigger ? agent_cron_trigger_body(user_message) : user_message;
```

- In `agent_task`, after the `display_notify_thinking(false);` that follows `process_message(...)`, add `s_reply_prompt = "";` (do not keep a pointer into the old message).

- [ ] **Step 6: Run the host tests to see them pass**

Run: `bash .superpowers/sdd/2026-10-11-screen-answers/host-gate.sh`
Expected: `host gate OK`.

- [ ] **Step 7: Display side: `display_answer` module**

`main/display_answer.h`:

```c
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
```

`main/display_answer.c`:

```c
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
```

Add `"display_answer.c"` to `main/CMakeLists.txt` after `"display_remote.c"`.

- [ ] **Step 8: Display side: event, page and API in `display.c`**

In `main/display.c` (inside `#if CONFIG_ZCLAW_DISPLAY_ENABLE`):

1. Add the includes `#include "display_answer.h"` and `#include "text_layout.h"`, and the defines:

```c
#define ANSWER_PROMPT_MAX  160
#define ANSWER_REPLY_MAX   CHANNEL_TX_BUF_SIZE
```

2. Append `DISPLAY_EV_SHOW_ANSWER,` to `display_event_type_t`, and change the `ptr` comment to `// DASH_DONE: JPEG buffer; SHOW_ANSWER: answer block (owned by the receiver)`.

3. Add these statics next to `s_dash_title`:

```c
// Answer page (PAGE_ANSWER): when it appeared, the page shown, a new answer to draw.
static int64_t s_answer_since = 0;
static size_t s_answer_page = 0;
static bool s_answer_fresh = false;
```

4. In `apply_event`, add:

```c
    case DISPLAY_EV_SHOW_ANSWER: {
        uint32_t ms = display_answer_load(ev->ptr, ev->extra);
        if (page_rotation_show(&s_pages, PAGE_ANSWER, ms, now)) {
            s_answer_fresh = true;
        } else {
            display_answer_clear();  // a tool's page from this turn stays on screen
        }
        break;
    }
```

5. In `display_task`, right after `dash_state_page(&s_dash, page == PAGE_REMOTE);`:

```c
        if (page != PAGE_ANSWER && display_answer_loaded()) {
            display_answer_clear();  // expired, or cancelled by a new activity
        }
```

6. In the page-change block, add a branch before the final `} else {` that draws the face:

```c
            } else if (page == PAGE_ANSWER) {
                s_answer_since = now;
                s_answer_page = 0;
                s_answer_fresh = false;
                display_answer_draw(0);
```

7. After the existing `} else if (page == PAGE_REMOTE && (s_dash.jpeg || s_dash.failed)) { ... }`, chain:

```c
        } else if (page == PAGE_ANSWER) {
            if (s_answer_fresh) {
                s_answer_fresh = false;  // replaced while shown: start over
                s_answer_since = now;
                s_answer_page = 0;
                fade_out();
                display_answer_draw(0);
                fade_in();
            } else {
                size_t next = display_answer_page_at((uint32_t)(now - s_answer_since));
                if (next != s_answer_page) {
                    s_answer_page = next;
                    fade_out();
                    display_answer_draw(next);
                    fade_in();
                }
            }
        }
```

8. Add the public functions after `display_notify_wifi`:

```c
bool display_is_running(void)
{
    return s_queue != NULL;
}

bool display_show_answer(const char *label, const char *prompt, const char *reply)
{
    if (!s_queue || !label || !prompt || !reply) {
        return false;
    }
    size_t label_len = strlen(label);
    char *block = malloc(label_len + 1 + ANSWER_PROMPT_MAX + 1 + ANSWER_REPLY_MAX + 1);
    if (!block) {
        return false;
    }
    memcpy(block, label, label_len);
    block[label_len] = ' ';
    size_t header_len = label_len + 1 +
        text_layout_clip_utf8(prompt, ANSWER_PROMPT_MAX, block + label_len + 1, ANSWER_PROMPT_MAX + 1);
    (void)text_layout_clip_utf8(reply, ANSWER_REPLY_MAX, block + header_len + 1, ANSWER_REPLY_MAX + 1);

    display_event_t ev = {
        .type = DISPLAY_EV_SHOW_ANSWER, .value = 0, .extra = (uint32_t)header_len, .ptr = block,
    };
    if (xQueueSend(s_queue, &ev, 0) != pdTRUE) {
        free(block);  // the reply still reached the serial console
        return false;
    }
    return true;
}
```

- [ ] **Step 9: Firmware build and host gate**

Run the firmware build from Global Constraints.
Expected: `Project build complete`, no `error:`.

Run: `bash .superpowers/sdd/2026-10-11-screen-answers/host-gate.sh`
Expected: `host gate OK`.

- [ ] **Step 10: Commit**

```bash
git add main/display.h main/display.c main/display_answer.h main/display_answer.c main/agent.c main/agent_commands.h main/agent_commands.c main/CMakeLists.txt test/host/mock_display.h test/host/mock_display.c test/host/test_agent_commands.c test/host/test_agent.c test/host/test_runner.c scripts/test.sh
git commit -m "feat(display): show every reply as a paged answer page

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: `display_dashboard` tool and screen hint

**Files:**
- Create: `main/tools_dashboard.c`
- Modify: `main/tools_handlers.h`, `main/builtin_tools.def`, `main/agent_prompt.c`, `main/CMakeLists.txt` (add `"tools_dashboard.c"` after `"tools_display.c"`)
- Create: `test/host/test_tools_dashboard.c`
- Modify: `test/host/test_agent.c` (append 1 test + registration), `test/host/test_runner.c`, `scripts/test.sh`

**Interfaces:**
- Consumes:
  - from Task 4: `bool display_is_running(void)`;
  - existing: `bool display_request_dashboard(const char *name, uint32_t duration_ms)` and `bool display_request_status(uint32_t duration_ms)`.
- Produces: `bool tools_display_dashboard_handler(const cJSON *input, char *result, size_t result_len);`, and the tool `display_dashboard` (registered only when `CONFIG_ZCLAW_DISPLAY_ENABLE`).

- [ ] **Step 1: Write the failing tests**

`test/host/test_tools_dashboard.c`:

```c
/*
 * Host tests for the display_dashboard tool.
 */

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "mock_display.h"
#include "tools_handlers.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

static bool run(const char *json, char *result, size_t len)
{
    cJSON *input = cJSON_Parse(json);
    bool ok = tools_display_dashboard_handler(input, result, len);
    cJSON_Delete(input);
    return ok;
}

TEST(shows_clima_for_30_seconds)
{
    char result[128];
    mock_display_reset();
    mock_display_set_running(true);
    ASSERT(run("{\"name\":\"clima\"}", result, sizeof(result)));
    ASSERT(strcmp(mock_display_last_dashboard(), "clima") == 0);
    ASSERT(mock_display_last_dashboard_ms() == 30000);
    ASSERT(strstr(result, "clima") != NULL);
    return 0;
}

TEST(shows_cripto)
{
    char result[128];
    mock_display_reset();
    mock_display_set_running(true);
    ASSERT(run("{\"name\":\"cripto\"}", result, sizeof(result)));
    ASSERT(strcmp(mock_display_last_dashboard(), "cripto") == 0);
    return 0;
}

TEST(panel_shows_status_page)
{
    char result[128];
    mock_display_reset();
    mock_display_set_running(true);
    ASSERT(run("{\"name\":\"panel\"}", result, sizeof(result)));
    ASSERT(mock_display_last_status_ms() == 30000);
    ASSERT(strcmp(mock_display_last_dashboard(), "") == 0);
    return 0;
}

TEST(rejects_unknown_or_missing_name)
{
    char result[128];
    mock_display_reset();
    mock_display_set_running(true);
    ASSERT(!run("{\"name\":\"tiempo\"}", result, sizeof(result)));
    ASSERT(strncmp(result, "Error", 5) == 0);
    ASSERT(!run("{}", result, sizeof(result)));
    ASSERT(strncmp(result, "Error", 5) == 0);
    ASSERT(strcmp(mock_display_last_dashboard(), "") == 0);
    ASSERT(mock_display_last_status_ms() == 0);
    return 0;
}

TEST(reports_missing_display)
{
    char result[128];
    mock_display_reset();
    ASSERT(!run("{\"name\":\"clima\"}", result, sizeof(result)));
    ASSERT(strstr(result, "pantalla") != NULL);
    return 0;
}

int test_tools_dashboard_all(void)
{
    int failures = 0;

    printf("\nDashboard Tool Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(shows_clima_for_30_seconds);
    RUN(shows_cripto);
    RUN(panel_shows_status_page);
    RUN(rejects_unknown_or_missing_name);
    RUN(reports_missing_display);

#undef RUN
    mock_display_reset();
    return failures;
}
```

Append to `test/host/test_agent.c` before `int test_agent_all(void)`:

```c
TEST(screen_hint_in_prompt_only_with_display)
{
    QueueHandle_t channel_q;
    const char *reply =
        "{\"content\":[{\"type\":\"text\",\"text\":\"ok\"}],\"stop_reason\":\"end_turn\"}";

    reset_state();
    mock_display_reset();
    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q, NULL);

    mock_display_set_running(true);
    ASSERT(mock_llm_push_result(ESP_OK, reply));
    agent_test_process_message("hola");
    ASSERT(strstr(mock_llm_last_request_json(), "240x240 screen") != NULL);
    ASSERT(strstr(mock_llm_last_request_json(), "never use markdown") != NULL);

    mock_display_set_running(false);
    ASSERT(mock_llm_push_result(ESP_OK, reply));
    agent_test_process_message("adios");
    ASSERT(strstr(mock_llm_last_request_json(), "240x240 screen") == NULL);

    vQueueDelete(channel_q);
    mock_display_reset();
    return 0;
}
```

Register it at the end of `test_agent_all`, before `return failures;`:

```c
    printf("  screen_hint_in_prompt_only_with_display... ");
    if (test_screen_hint_in_prompt_only_with_display() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }
```

Register the new suite:
- `test_runner.c`: add `extern int test_tools_dashboard_all(void);` and `    failures += test_tools_dashboard_all();`.
- `scripts/test.sh`: add `        test_tools_dashboard.c \` after `        test_agent_commands.c \`, and `        ../../main/tools_dashboard.c \` after `        ../../main/text_layout.c \`.

In `main/tools_handlers.h`, add next to `tools_display_image_handler`:

```c
bool tools_display_dashboard_handler(const cJSON *input, char *result, size_t result_len);
```

Create `main/tools_dashboard.c` with only `#include "tools_handlers.h"`.

- [ ] **Step 2: Run to see it fail**

Run: `bash .superpowers/sdd/2026-10-11-screen-answers/host-gate.sh`
Expected: `host gate FAILED`. Either the link fails on undefined `tools_display_dashboard_handler`, or the diff shows the dashboard suite and `240x240 screen` failures.

- [ ] **Step 3: Implement the tool**

`main/tools_dashboard.c`:

```c
// display_dashboard tool: shows the weather or crypto dashboard (rendered by
// the VPS) or the local status panel on the device screen for 30 s.

#include "tools_handlers.h"

#include <stdio.h>
#include <string.h>

#include "display.h"

#define DASHBOARD_MS 30000

bool tools_display_dashboard_handler(const cJSON *input, char *result, size_t result_len)
{
    const cJSON *name = cJSON_GetObjectItem(input, "name");
    const char *value;
    bool shown;

    if (!name || !cJSON_IsString(name)) {
        snprintf(result, result_len, "Error: 'name' must be clima, cripto or panel");
        return false;
    }
    value = name->valuestring;
    if (strcmp(value, "panel") == 0) {
        shown = display_request_status(DASHBOARD_MS);
    } else if (strcmp(value, "clima") == 0 || strcmp(value, "cripto") == 0) {
        shown = display_request_dashboard(value, DASHBOARD_MS);
    } else {
        snprintf(result, result_len, "Error: unknown dashboard (use clima, cripto or panel)");
        return false;
    }
    if (!shown) {
        snprintf(result, result_len, "Error: la pantalla no est\xC3\xA1 disponible");
        return false;
    }
    snprintf(result, result_len, "Mostrando %s 30 s.", value);
    return true;
}
```

In `main/builtin_tools.def`, inside the existing `#if defined(CONFIG_ZCLAW_DISPLAY_ENABLE) && CONFIG_ZCLAW_DISPLAY_ENABLE` block, after the `display_image` entry:

```c
TOOL_ENTRY("display_dashboard",
           "Show a dashboard on the device's own screen for 30 s: 'clima' (weather now, next hours and 3 days), 'cripto' (BNB, SOL, TRX and BTC prices) or 'panel' (clock, WiFi, IP, heap, uptime). Use it whenever the user asks to see the weather, crypto prices or the device status on the screen.",
           "{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\",\"enum\":[\"clima\",\"cripto\",\"panel\"]}},\"required\":[\"name\"]}",
           tools_display_dashboard_handler)
```

Add `"tools_dashboard.c"` to `main/CMakeLists.txt` after `"tools_display.c"`.

- [ ] **Step 4: Implement the screen hint (D4)**

In `main/agent_prompt.c`, add `#include "display.h"` and, above `agent_build_system_prompt`:

```c
#define DISPLAY_PROMPT_HINT \
    " Your replies are shown as plain text on a small 240x240 screen (about 29 " \
    "characters x 11 lines per page): keep them short and never use markdown. " \
    "To show the weather, crypto prices or the device panel use display_dashboard; " \
    "for pictures use display_image."

static const char *display_hint(void)
{
    return display_is_running() ? DISPLAY_PROMPT_HINT : "";
}
```

In `agent_build_system_prompt`:
- Append `%s` to the end of the format string, after `"Keep responses short unless the user explicitly asks for more detail."`.
- Add `display_hint()` as the last argument.
- Replace the overflow branch with:

```c
    if (written < 0 || (size_t)written >= buf_len) {
        // Keep at least the base prompt and the screen hint, which changes
        // how every reply must be written.
        ESP_LOGW(TAG, "Persona prompt composition overflow, using base system prompt");
        written = snprintf(buf, buf_len, "%s%s", SYSTEM_PROMPT, display_hint());
        if (written < 0 || (size_t)written >= buf_len) {
            return SYSTEM_PROMPT;
        }
    }
```

- [ ] **Step 5: Run to see it pass**

Run: `bash .superpowers/sdd/2026-10-11-screen-answers/host-gate.sh`
Expected: `host gate OK`. The baseline failure at line 784 must still be the same line. If the line moved, see Global Constraints.

Run the firmware build. Expected: `Project build complete`.

- [ ] **Step 6: Commit**

```bash
git add main/tools_dashboard.c main/tools_handlers.h main/builtin_tools.def main/agent_prompt.c main/CMakeLists.txt test/host/test_tools_dashboard.c test/host/test_agent.c test/host/test_runner.c scripts/test.sh
git commit -m "feat(display): display_dashboard tool and screen hint in the prompt

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: `memory_export` to the SD card

**Files:**
- Create: `main/backup_name.h`, `main/backup_name.c`
- Create: `test/host/test_backup_name.c`
- Modify: `main/tools_export.c` (full rewrite below)
- Modify: `main/CMakeLists.txt` (add `"backup_name.c"` after `"tools_export.c"`), `test/host/test_runner.c`, `scripts/test.sh`

**Interfaces:**
- Consumes:
  - `telegram_is_configured()` (false when Task 1 disabled Telegram);
  - `bool sd_card_is_mounted(void)` and `SD_MOUNT_POINT` from `sd_card.h`;
  - `memory_get()`.
- Produces: `bool backup_name_format(char *buf, size_t len, const struct tm *tm, uint64_t uptime_s);`.

- [ ] **Step 1: Write the failing tests**

`main/backup_name.h`:

```c
#ifndef BACKUP_NAME_H
#define BACKUP_NAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// File name for a memory backup: "memories-YYYYMMDD-HHMM.txt" when the clock
// is set (year >= 2024), otherwise "memories-up<uptime>s.txt". False when
// buf is too small.
bool backup_name_format(char *buf, size_t len, const struct tm *tm, uint64_t uptime_s);

#endif // BACKUP_NAME_H
```

`test/host/test_backup_name.c`:

```c
/*
 * Host tests for memory backup file names.
 */

#include <stdio.h>
#include <string.h>

#include "backup_name.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(uses_date_when_clock_is_set)
{
    struct tm tm;
    char buf[48];
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 9;
    tm.tm_mday = 11;
    tm.tm_hour = 20;
    tm.tm_min = 5;
    ASSERT(backup_name_format(buf, sizeof(buf), &tm, 99));
    ASSERT(strcmp(buf, "memories-20261011-2005.txt") == 0);
    return 0;
}

TEST(uses_uptime_before_clock_sync)
{
    struct tm tm;
    char buf[48];
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 70;
    ASSERT(backup_name_format(buf, sizeof(buf), &tm, 1234));
    ASSERT(strcmp(buf, "memories-up1234s.txt") == 0);
    ASSERT(backup_name_format(buf, sizeof(buf), NULL, 7));
    ASSERT(strcmp(buf, "memories-up7s.txt") == 0);
    return 0;
}

TEST(rejects_small_buffer)
{
    char buf[10];
    ASSERT(!backup_name_format(buf, sizeof(buf), NULL, 7));
    return 0;
}

int test_backup_name_all(void)
{
    int failures = 0;

    printf("\nBackup Name Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(uses_date_when_clock_is_set);
    RUN(uses_uptime_before_clock_sync);
    RUN(rejects_small_buffer);

#undef RUN
    return failures;
}
```

Register the suite:
- `test_runner.c`: add `extern int test_backup_name_all(void);` and `    failures += test_backup_name_all();`.
- `scripts/test.sh`: add `        test_backup_name.c \` after `        test_tools_dashboard.c \`, and `        ../../main/backup_name.c \` after `        ../../main/tools_dashboard.c \`.
- Create `main/backup_name.c` with only `#include "backup_name.h"`.

- [ ] **Step 2: Run to see it fail**

Run: `bash .superpowers/sdd/2026-10-11-screen-answers/host-gate.sh`
Expected: `host gate FAILED`, because of the undefined `backup_name_format`.

- [ ] **Step 3: Implement `backup_name.c`**

```c
#include "backup_name.h"

#include <stdio.h>

bool backup_name_format(char *buf, size_t len, const struct tm *tm, uint64_t uptime_s)
{
    int n;

    if (!buf || len == 0) {
        return false;
    }
    if (tm && tm->tm_year + 1900 >= 2024) {
        n = snprintf(buf, len, "memories-%04d%02d%02d-%02d%02d.txt", tm->tm_year + 1900,
                     tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min);
    } else {
        // newlib nano printf has no %llu: uptime fits in 32 bits for 136 years.
        n = snprintf(buf, len, "memories-up%lus.txt", (unsigned long)uptime_s);
    }
    return n > 0 && (size_t)n < len;
}
```

Run the host gate. Expected: `host gate OK`.

- [ ] **Step 4: Rewrite `tools_export.c`**

Replace the whole of `main/tools_export.c` with:

```c
// Backup tool: exports the user's own "u_*" memories (notes, location,
// lists). With Telegram configured they go to the owner's allowlisted chat;
// otherwise to a text file on the SD card. System entries (API keys, tokens,
// WiFi credentials) are never included: only keys created through the
// memory_set tool, which enforces the "u_" prefix, are exported.

#include "tools_handlers.h"
#include "tools_common.h"
#include "config.h"
#include "memory.h"
#include "telegram.h"

#include "nvs.h"

#include <stdio.h>
#include <string.h>

#if CONFIG_ZCLAW_SD_ENABLE
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

#include "backup_name.h"
#include "esp_timer.h"
#include "sd_card.h"
#endif

#define EXPORT_CHUNK_SIZE 1200
#define EXPORT_VALUE_SIZE 512

#ifndef NVS_NAMESPACE
#define NVS_NAMESPACE "storage"
#endif

typedef void (*export_emit_fn)(const char *key, const char *value, void *ctx);

// Calls emit for every user memory. Returns how many, or -1 without NVS.
static int for_each_user_memory(export_emit_fn emit, void *ctx)
{
    static char value[EXPORT_VALUE_SIZE];
    nvs_handle_t handle;
    int count = 0;

    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return -1;
    }
    nvs_iterator_t it = NULL;
    esp_err_t err = nvs_entry_find_in_handle(handle, NVS_TYPE_STR, &it);
    while (err == ESP_OK && it != NULL) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        // Whitelist: user-created memories only ("u_" prefix).
        if (strncmp(info.key, "u_", 2) == 0 && memory_get(info.key, value, sizeof(value))) {
            emit(info.key, value, ctx);
            count++;
        }
        err = nvs_entry_next(&it);
    }
    if (it) {
        nvs_release_iterator(it);
    }
    nvs_close(handle);
    return count;
}

typedef struct {
    char chunk[EXPORT_CHUNK_SIZE];
    size_t len;
    int sent;
} telegram_export_t;

static void telegram_flush(telegram_export_t *t)
{
    if (t->len > 0 && telegram_send(t->chunk) == ESP_OK) {
        t->sent++;
    }
    t->len = 0;
    t->chunk[0] = '\0';
}

static void telegram_emit(const char *key, const char *value, void *ctx)
{
    telegram_export_t *t = ctx;
    size_t needed = strlen(key) + strlen(value) + 4;

    if (t->len + needed >= sizeof(t->chunk)) {
        telegram_flush(t);
    }
    t->len += (size_t)snprintf(t->chunk + t->len, sizeof(t->chunk) - t->len, "%s = %s\n", key, value);
}

static bool export_to_telegram(char *result, size_t result_len)
{
    static telegram_export_t t;

    t.len = (size_t)snprintf(t.chunk, sizeof(t.chunk), "MEMORY BACKUP\n");
    t.sent = 0;
    int count = for_each_user_memory(telegram_emit, &t);
    if (count < 0) {
        snprintf(result, result_len, "No stored memories");
        return true;
    }
    if (count == 0) {
        snprintf(result, result_len, "No user memories to export");
        return true;
    }
    telegram_flush(&t);
    snprintf(result, result_len, "Exported %d user memories in %d Telegram message(s).", count, t.sent);
    return true;
}

#if CONFIG_ZCLAW_SD_ENABLE
static void file_emit(const char *key, const char *value, void *ctx)
{
    fprintf((FILE *)ctx, "%s = %s\n", key, value);
}

static bool export_to_sd(char *result, size_t result_len)
{
    char name[48];
    char path[96];
    time_t now = time(NULL);
    struct tm tm;

    if (!sd_card_is_mounted()) {
        snprintf(result, result_len, "Error: no SD card mounted (and Telegram is not configured)");
        return false;
    }
    if (mkdir(SD_MOUNT_POINT "/backup", 0775) != 0 && errno != EEXIST) {
        snprintf(result, result_len, "Error: cannot create " SD_MOUNT_POINT "/backup");
        return false;
    }
    localtime_r(&now, &tm);
    if (!backup_name_format(name, sizeof(name), &tm, (uint64_t)(esp_timer_get_time() / 1000000))) {
        snprintf(result, result_len, "Error: backup file name too long");
        return false;
    }
    snprintf(path, sizeof(path), SD_MOUNT_POINT "/backup/%s", name);
    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(result, result_len, "Error: cannot write %s", path);
        return false;
    }
    fputs("MEMORY BACKUP\n", f);
    int count = for_each_user_memory(file_emit, f);
    if (fclose(f) != 0) {
        snprintf(result, result_len, "Error: writing %s failed", path);
        return false;
    }
    if (count <= 0) {
        remove(path);
        snprintf(result, result_len, "No user memories to export");
        return true;
    }
    snprintf(result, result_len, "Exported %d user memories to %s", count, path);
    return true;
}
#else
static bool export_to_sd(char *result, size_t result_len)
{
    snprintf(result, result_len, "Error: Telegram not configured and this build has no SD card");
    return false;
}
#endif

bool tools_memory_export_handler(const cJSON *input, char *result, size_t result_len)
{
    (void)input;

    if (telegram_is_configured()) {
        return export_to_telegram(result, result_len);
    }
    return export_to_sd(result, result_len);
}
```

Add `"backup_name.c"` to `main/CMakeLists.txt` after `"tools_export.c"`.

- [ ] **Step 5: Build and gate**

Run the firmware build. Expected: `Project build complete`.
Run the host gate. Expected: `host gate OK`.

- [ ] **Step 6: Commit**

```bash
git add main/backup_name.h main/backup_name.c main/tools_export.c main/CMakeLists.txt test/host/test_backup_name.c test/host/test_runner.c scripts/test.sh
git commit -m "feat: memory_export writes to the SD card when Telegram is off

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Web OTA and IP on `/panel`

**Files:**
- Create: `main/web_ota_policy.h`, `main/web_ota_policy.c`, `main/web_ota.h`, `main/web_ota.c`
- Create: `test/host/test_web_ota_policy.c`
- Modify: `main/Kconfig.projbuild` (two options after `ZCLAW_STUB_TELEGRAM`)
- Modify: `main/main.c` (start after cron init), `main/CMakeLists.txt` (sources + `esp_http_server`)
- Modify: `main/local_admin.h`, `main/local_admin.c` (`local_admin_current_ip`)
- Modify: `main/display_status.c` (IP row)
- Modify: `test/host/test_runner.c`, `scripts/test.sh`
- Modify (git-ignored, local only): `sdkconfig.defaults.esp32c6`

**Interfaces:**
- Consumes: `ota_get_version()`, the `esp_ota_*` and `esp_http_server` APIs (IDF 5.5).
- Produces:
  - `bool web_ota_pin_matches(const char *given, const char *expected);`
  - `int web_ota_request_status(size_t content_len, size_t partition_size, bool pin_ok, bool busy);`
  - `esp_err_t web_ota_start(void);`
  - `const char *local_admin_current_ip(void);`

- [ ] **Step 1: Write the failing tests**

`main/web_ota_policy.h`:

```c
#ifndef WEB_OTA_POLICY_H
#define WEB_OTA_POLICY_H

#include <stdbool.h>
#include <stddef.h>

#define WEB_OTA_PIN_MIN 4
#define WEB_OTA_PIN_MAX 32

// Constant-time PIN check (time depends only on the expected PIN's length).
// An empty expected PIN never matches.
bool web_ota_pin_matches(const char *given, const char *expected);

// HTTP status for an upload request, checked in this order: 400 without a
// body length, 413 larger than the target partition, 401 wrong PIN, 409
// another update running; 0 when the upload may start.
int web_ota_request_status(size_t content_len, size_t partition_size, bool pin_ok, bool busy);

#endif // WEB_OTA_POLICY_H
```

`test/host/test_web_ota_policy.c`:

```c
/*
 * Host tests for the web OTA request policy.
 */

#include <stdio.h>

#include "web_ota_policy.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(pin_matches_only_the_exact_pin)
{
    ASSERT(web_ota_pin_matches("4821", "4821"));
    ASSERT(!web_ota_pin_matches("4822", "4821"));
    ASSERT(!web_ota_pin_matches("482", "4821"));
    ASSERT(!web_ota_pin_matches("48210", "4821"));
    ASSERT(!web_ota_pin_matches("", "4821"));
    ASSERT(!web_ota_pin_matches(NULL, "4821"));
    ASSERT(!web_ota_pin_matches("", ""));
    return 0;
}

TEST(request_status_checks_in_order)
{
    ASSERT(web_ota_request_status(0, 1000, true, false) == 400);
    ASSERT(web_ota_request_status(1001, 1000, true, false) == 413);
    ASSERT(web_ota_request_status(1001, 1000, false, false) == 413);
    ASSERT(web_ota_request_status(500, 1000, false, false) == 401);
    ASSERT(web_ota_request_status(500, 1000, false, true) == 401);
    ASSERT(web_ota_request_status(500, 1000, true, true) == 409);
    ASSERT(web_ota_request_status(1000, 1000, true, false) == 0);
    return 0;
}

int test_web_ota_policy_all(void)
{
    int failures = 0;

    printf("\nWeb OTA Policy Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(pin_matches_only_the_exact_pin);
    RUN(request_status_checks_in_order);

#undef RUN
    return failures;
}
```

Register the new suite:
- `test_runner.c`: add `extern int test_web_ota_policy_all(void);` and `    failures += test_web_ota_policy_all();`.
- `scripts/test.sh`: add `        test_web_ota_policy.c \` after `        test_backup_name.c \`, and `        ../../main/web_ota_policy.c \` after `        ../../main/backup_name.c \`.
- Create `main/web_ota_policy.c` with only `#include "web_ota_policy.h"`.

- [ ] **Step 2: Run to see it fail**

Run: `bash .superpowers/sdd/2026-10-11-screen-answers/host-gate.sh`
Expected: `host gate FAILED`, because of the undefined `web_ota_pin_matches` and `web_ota_request_status`.

- [ ] **Step 3: Implement the policy and the IP getter**

`main/web_ota_policy.c`:

```c
#include "web_ota_policy.h"

#include <string.h>

bool web_ota_pin_matches(const char *given, const char *expected)
{
    size_t glen;
    size_t elen;
    unsigned diff;

    if (!given || !expected) {
        return false;
    }
    glen = strlen(given);
    elen = strlen(expected);
    diff = (unsigned)(glen ^ elen);
    for (size_t i = 0; i < elen; i++) {
        unsigned char g = i < glen ? (unsigned char)given[i] : 0;
        diff |= (unsigned)(g ^ (unsigned char)expected[i]);
    }
    return elen > 0 && diff == 0;
}

int web_ota_request_status(size_t content_len, size_t partition_size, bool pin_ok, bool busy)
{
    if (content_len == 0) {
        return 400;
    }
    if (content_len > partition_size) {
        return 413;
    }
    if (!pin_ok) {
        return 401;
    }
    if (busy) {
        return 409;
    }
    return 0;
}
```

`s_current_ip` exists only in the device build: `main/local_admin.c` lines 33–463 are `#ifndef TEST_BUILD`. So the getter is device-only, and nothing on the host calls it; `display_status.c` is not host-compiled. A host test could only check a stub. Task 8 step 4 checks the real value on the board instead (TDD exception: a one-line device-only accessor).

In `main/local_admin.h`, add:

```c
// Station IPv4 address as text ("" while not connected). Device build only.
const char *local_admin_current_ip(void);
```

In `main/local_admin.c`, just before the `#else` at line 463 (the end of the device-only section), add:

```c
const char *local_admin_current_ip(void)
{
    return s_current_ip;
}
```

Run the host gate. Expected: `host gate OK`.

- [ ] **Step 4: Kconfig options**

In `main/Kconfig.projbuild`, after the `ZCLAW_STUB_TELEGRAM` entry:

```
    config ZCLAW_WEB_OTA_ENABLE
        bool "Firmware upload page on the LAN (port 80)"
        default n
        help
            Serves a small page where a browser uploads build/zclaw.bin. Needs
            ZCLAW_WEB_OTA_PIN; the new image still goes through the rollback
            check after reboot.

    config ZCLAW_WEB_OTA_PIN
        string "PIN for the firmware upload page (4-32 characters)"
        default ""
        depends on ZCLAW_WEB_OTA_ENABLE
        help
            Keep it in the git-ignored sdkconfig.defaults.<target>. The page is
            not started with a PIN shorter than 4 or longer than 32 characters.

```

- [ ] **Step 5: Implement the server**

`main/web_ota.h`:

```c
#ifndef WEB_OTA_H
#define WEB_OTA_H

#include "esp_err.h"

// Start the LAN firmware upload page (port 80): GET / and POST /ota with an
// X-OTA-PIN header. ESP_ERR_NOT_SUPPORTED when CONFIG_ZCLAW_WEB_OTA_ENABLE
// is off; ESP_ERR_INVALID_ARG when the PIN length is outside 4-32.
esp_err_t web_ota_start(void);

#endif // WEB_OTA_H
```

`main/web_ota.c`:

```c
#include "web_ota.h"

#include "config.h"

#if CONFIG_ZCLAW_WEB_OTA_ENABLE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota.h"
#include "web_ota_policy.h"

#define WEB_OTA_CHUNK      4096
#define WEB_OTA_STACK      6144
#define WEB_OTA_DRAIN_BUF  512
#define WEB_OTA_BAD_PIN_MS 1000
#define WEB_OTA_REBOOT_US  1000000

static const char *TAG = "web_ota";
static httpd_handle_t s_server = NULL;
static bool s_busy = false;

static const char PAGE_HEAD[] =
    "<!doctype html><html lang=es><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>Fidel OTA</title><style>body{font-family:sans-serif;max-width:26em;"
    "margin:2em auto;padding:0 1em}input,button{display:block;width:100%;"
    "font-size:1em;margin:.5em 0;padding:.4em;box-sizing:border-box}</style>"
    "<h1>Fidel</h1><p>Versi\xC3\xB3n <b>";
static const char PAGE_MID[] = "</b> &middot; partici\xC3\xB3n <b>";
static const char PAGE_TAIL[] =
    "<input id=p type=password placeholder=PIN autocomplete=off>"
    "<input id=f type=file accept=.bin><button id=b>Actualizar</button><p id=r></p>"
    "<script>b.onclick=async()=>{const x=f.files[0];"
    "if(!x){r.textContent='Elige el archivo .bin';return}"
    "if(x.size>MAX){r.textContent='El archivo no cabe en la partici\xC3\xB3n';return}"
    "b.disabled=true;r.textContent='Subiendo '+Math.round(x.size/1024)+' KB...';"
    "try{const s=await fetch('/ota',{method:'POST',headers:{'X-OTA-PIN':p.value},body:x});"
    "r.textContent=await s.text()}catch(e){r.textContent='Error de red: '+e}"
    "b.disabled=false}</script>";

static esp_err_t page_get(httpd_req_t *req)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    char max[64];

    snprintf(max, sizeof(max), "</b></p><script>const MAX=%lu</script>",
             (unsigned long)(next ? next->size : 0));
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    httpd_resp_sendstr_chunk(req, ota_get_version());
    httpd_resp_sendstr_chunk(req, PAGE_MID);
    httpd_resp_sendstr_chunk(req, running ? running->label : "?");
    httpd_resp_sendstr_chunk(req, max);
    httpd_resp_sendstr_chunk(req, PAGE_TAIL);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t reply(httpd_req_t *req, const char *status, const char *text)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, text);
}

// httpd_req_recv with a few retries on socket timeouts; <= 0 on failure.
static int recv_some(httpd_req_t *req, char *buf, size_t len)
{
    for (int tries = 0; tries < 3; tries++) {
        int n = httpd_req_recv(req, buf, len);
        if (n != HTTPD_SOCK_ERR_TIMEOUT) {
            return n;
        }
    }
    return HTTPD_SOCK_ERR_TIMEOUT;
}

// Reads and drops the rest of the body so the browser gets the reply instead
// of a reset connection.
static void drain_body(httpd_req_t *req, size_t left)
{
    char buf[WEB_OTA_DRAIN_BUF];

    while (left > 0) {
        int n = recv_some(req, buf, left < sizeof(buf) ? left : sizeof(buf));
        if (n <= 0) {
            return;
        }
        left -= (size_t)n;
    }
}

static void reboot_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

static void schedule_reboot(void)
{
    const esp_timer_create_args_t args = { .callback = reboot_cb, .name = "web_ota_reboot" };
    esp_timer_handle_t timer;

    if (esp_timer_create(&args, &timer) != ESP_OK ||
        esp_timer_start_once(timer, WEB_OTA_REBOOT_US) != ESP_OK) {
        esp_restart();
    }
}

static esp_err_t ota_post(httpd_req_t *req)
{
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    char pin[WEB_OTA_PIN_MAX + 2];
    bool pin_ok = httpd_req_get_hdr_value_str(req, "X-OTA-PIN", pin, sizeof(pin)) == ESP_OK &&
                  web_ota_pin_matches(pin, CONFIG_ZCLAW_WEB_OTA_PIN);

    if (!next) {
        reply(req, "500 Internal Server Error", "Error: no hay partici\xC3\xB3n OTA");
        return ESP_FAIL;  // closes the connection
    }
    switch (web_ota_request_status(req->content_len, next->size, pin_ok, s_busy)) {
    case 0:
        break;
    case 400:
        reply(req, "400 Bad Request", "Error: falta Content-Length");
        return ESP_FAIL;
    case 413:
        reply(req, "413 Payload Too Large", "Error: el archivo no cabe en la partici\xC3\xB3n");
        return ESP_FAIL;
    case 401:
        ESP_LOGW(TAG, "Wrong PIN");
        vTaskDelay(pdMS_TO_TICKS(WEB_OTA_BAD_PIN_MS));
        drain_body(req, req->content_len);
        return reply(req, "401 Unauthorized", "PIN incorrecto");
    default:
        reply(req, "409 Conflict", "Ya hay una actualizaci\xC3\xB3n en curso");
        return ESP_FAIL;
    }

    s_busy = true;
    ESP_LOGI(TAG, "Receiving %u bytes into %s", (unsigned)req->content_len, next->label);
    char *buf = malloc(WEB_OTA_CHUNK);
    esp_ota_handle_t handle = 0;
    esp_err_t err = buf ? esp_ota_begin(next, OTA_WITH_SEQUENTIAL_WRITES, &handle) : ESP_ERR_NO_MEM;
    bool begun = err == ESP_OK;
    bool recv_failed = false;
    size_t left = req->content_len;

    while (err == ESP_OK && left > 0) {
        int n = recv_some(req, buf, left < WEB_OTA_CHUNK ? left : WEB_OTA_CHUNK);
        if (n <= 0) {
            recv_failed = true;
            err = ESP_ERR_TIMEOUT;
            break;
        }
        err = esp_ota_write(handle, buf, (size_t)n);
        left -= (size_t)n;
    }
    if (begun) {
        if (err == ESP_OK) {
            err = esp_ota_end(handle);  // validates magic, chip and SHA-256
        } else {
            esp_ota_abort(handle);
        }
    }
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(next);
    }
    free(buf);
    s_busy = false;

    if (err != ESP_OK) {
        char msg[96];
        ESP_LOGE(TAG, "Update failed: %s", esp_err_to_name(err));
        if (recv_failed) {
            return ESP_FAIL;  // the connection is gone
        }
        drain_body(req, left);
        snprintf(msg, sizeof(msg), "Error: %s (el firmware actual sigue)", esp_err_to_name(err));
        return reply(req, "500 Internal Server Error", msg);
    }
    ESP_LOGI(TAG, "Update written to %s, rebooting", next->label);
    reply(req, "200 OK", "Actualizado. Reiniciando...");
    schedule_reboot();
    return ESP_OK;
}

esp_err_t web_ota_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    const httpd_uri_t page = { .uri = "/", .method = HTTP_GET, .handler = page_get };
    const httpd_uri_t upload = { .uri = "/ota", .method = HTTP_POST, .handler = ota_post };
    size_t pin_len = strlen(CONFIG_ZCLAW_WEB_OTA_PIN);

    if (s_server) {
        return ESP_OK;
    }
    if (pin_len < WEB_OTA_PIN_MIN || pin_len > WEB_OTA_PIN_MAX) {
        ESP_LOGW(TAG, "Web OTA off: CONFIG_ZCLAW_WEB_OTA_PIN must have %d-%d characters",
                 WEB_OTA_PIN_MIN, WEB_OTA_PIN_MAX);
        return ESP_ERR_INVALID_ARG;
    }
    config.max_open_sockets = 2;
    config.stack_size = WEB_OTA_STACK;
    config.max_uri_handlers = 2;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        s_server = NULL;
        return err;
    }
    httpd_register_uri_handler(s_server, &page);
    httpd_register_uri_handler(s_server, &upload);
    ESP_LOGI(TAG, "Firmware upload page on port %d", config.server_port);
    return ESP_OK;
}

#else  // !CONFIG_ZCLAW_WEB_OTA_ENABLE

esp_err_t web_ota_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
```

In `main/CMakeLists.txt`:
- add `"web_ota.c"` and `"web_ota_policy.c"` after `"ota.c"`;
- add `esp_http_server` to `REQUIRES`, after `esp_https_ota`.

In `main/main.c`:
- add `#include "web_ota.h"`;
- right after `ESP_ERROR_CHECK(cron_init());`, add:

```c
    // 12b. Optional firmware upload page on the LAN. Never fatal.
    esp_err_t web_ota_err = web_ota_start();
    if (web_ota_err != ESP_OK && web_ota_err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "Web OTA unavailable: %s", esp_err_to_name(web_ota_err));
    }
```

- [ ] **Step 6: IP row on the status page**

In `main/display_status.c`:
- add `#include "local_admin.h"`;
- in `display_status_draw`, the WiFi/Señal block ends with `y += ROW_H;`. Right after that line, and before the CPU row, insert:

```c
    const char *ip = local_admin_current_ip();
    draw_row(y, "IP", wifi_up && ip[0] ? ip : "--");
    y += ROW_H;
```

The rows become WiFi 86, Señal 106, IP 126, CPU 146, Heap 166, Uptime 186, and the version line 206 (it ends at 222 < 240).

- [ ] **Step 7: Enable the page in the local board defaults (PIN never printed)**

Run:
```bash
cd ~/Projects/zclaw && git check-ignore -q sdkconfig.defaults.esp32c6 && \
  if ! grep -q '^CONFIG_ZCLAW_WEB_OTA_PIN=' sdkconfig.defaults.esp32c6; then \
    PIN=$(python3 -c 'import secrets; print(f"{secrets.randbelow(10**6):06d}")'); \
    printf 'CONFIG_ZCLAW_WEB_OTA_ENABLE=y\nCONFIG_ZCLAW_WEB_OTA_PIN="%s"\n' "$PIN" >> sdkconfig.defaults.esp32c6; \
    unset PIN; fi; \
  grep -c '^CONFIG_ZCLAW_WEB_OTA_PIN="[0-9]\{6\}"$' sdkconfig.defaults.esp32c6
```
Expected: `1`.

- [ ] **Step 8: Build, check the resolved config, gate**

Run the firmware build. Then:
```bash
cd ~/Projects/zclaw && grep -E '^CONFIG_ZCLAW_WEB_OTA_ENABLE=y$' sdkconfig && grep -c '^CONFIG_ZCLAW_WEB_OTA_PIN=".\{4,32\}"$' sdkconfig && ls -l build/zclaw.bin
```
Expected:
- `Project build complete`;
- `CONFIG_ZCLAW_WEB_OTA_ENABLE=y`, and `1` for the PIN;
- `zclaw.bin` well under the 0x170000 (1,507,328-byte) slot. It is about 1.17 MB today; expect roughly +30 KB.

If `sdkconfig` did not pick up the new options, append the two lines from the defaults file to `sdkconfig` in the same way (never print them) and rebuild.

Run the host gate. Expected: `host gate OK`.

- [ ] **Step 9: Commit**

```bash
git add main/web_ota.h main/web_ota.c main/web_ota_policy.h main/web_ota_policy.c main/Kconfig.projbuild main/main.c main/CMakeLists.txt main/local_admin.h main/local_admin.c main/display_status.c test/host/test_web_ota_policy.c test/host/test_runner.c scripts/test.sh
git commit -m "feat: PIN-protected web OTA page and IP row on /panel

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Verification on the board and the other configuration

**Files:** none (verification only; findings become fixes with tests, or ledger rulings).

The user watches the screen, so ask them for each visual check. Port: `/dev/cu.usbmodem21201` (ESP32-C6 USB-JTAG). Serial helper (does not reset the board): `$SCRATCH/serial_cmd.py`, where `SCRATCH=/private/tmp/claude-501/-Users-jianpedi-Projects/e49efb90-9a94-4db4-b892-c83271a5df4b/scratchpad`. Run it with the ESP-IDF Python: `source ~/esp/esp-idf/export.sh >/dev/null 2>&1 && python "$SCRATCH/serial_cmd.py" /dev/cu.usbmodem21201 "<text>" <seconds>`.

- [ ] **Step 1: Configuration with Telegram on and the display off compiles**

Uses only the tracked defaults: Telegram `y`, display, SD and web OTA off.
```bash
cd ~/Projects/zclaw && source ~/esp/esp-idf/export.sh >/dev/null 2>&1 && \
  mkdir -p "$SCRATCH/alt" && cp sdkconfig.defaults "$SCRATCH/alt/defaults" && \
  idf.py -B "$SCRATCH/alt/build" -D SDKCONFIG="$SCRATCH/alt/sdkconfig" \
         -D SDKCONFIG_DEFAULTS="$SCRATCH/alt/defaults" -D IDF_TARGET=esp32c6 build \
         > "$SCRATCH/alt/build.log" 2>&1; grep -E "Project build complete|error:" "$SCRATCH/alt/build.log" | head -5; \
  grep -E '^CONFIG_ZCLAW_TELEGRAM_ENABLE=y|^# CONFIG_ZCLAW_DISPLAY_ENABLE is not set' "$SCRATCH/alt/sdkconfig"
```
Expected: `Project build complete`, and both config lines printed.

- [ ] **Step 2: Flash (ask first)**

Ask the user: "¿Flasheo por USB el firmware de la entrega A?" Only after a yes, run:
```bash
cd ~/Projects/zclaw && source ~/esp/esp-idf/export.sh >/dev/null 2>&1 && idf.py -p /dev/cu.usbmodem21201 flash > build/flash.log 2>&1; tail -2 build/flash.log
perl -e 'alarm shift; exec @ARGV' 25 script -q /dev/null idf.py -p /dev/cu.usbmodem21201 monitor < /dev/null > build/monitor.log 2>&1
grep -aiE "telegram|web_ota|Display task started|Ready! Free heap|Connected:" build/monitor.log | head -20
```
Expected:
- `Telegram disabled (...)`, with no `getUpdates` or `telegram_start` lines;
- `web_ota: Firmware upload page on port 80`;
- `Display task started`, `Connected: <IP>`, and the `Ready! Free heap: …` value. Write the heap down.

- [ ] **Step 3: Short and long answers**

```bash
python "$SCRATCH/serial_cmd.py" /dev/cu.usbmodem21201 "hola, ¿qué hora es?" 40
python "$SCRATCH/serial_cmd.py" /dev/cu.usbmodem21201 "cuéntame en 12 líneas la historia del son cubano" 60
```
Ask the user to confirm each of these:
- thinking face first;
- the answer page shows `Tú: …` in grey, the divider, and white text wrapped without cutting words;
- the long reply pages with `1/2` and `2/2`, each page staying in proportion to its text;
- the face returns at the end, and the next message cancels the page.

- [ ] **Step 4: Dashboard through the tool**

```bash
python "$SCRATCH/serial_cmd.py" /dev/cu.usbmodem21201 "muéstrame el clima" 40
```
Expected: the serial log shows the `display_dashboard` tool call. The user confirms the weather dashboard stays for ~30 s and the text answer does not cover it (D1).

Then send `/panel`. The user confirms the `IP` row shows the address from `Connected:`.

- [ ] **Step 5: memory_export to the SD**

```bash
python "$SCRATCH/serial_cmd.py" /dev/cu.usbmodem21201 "haz un respaldo de mis memorias" 40
```
Expected: the reply names `/sdcard/backup/memories-YYYYMMDD-HHMM.txt` (the clock is synced by then).

- [ ] **Step 6: Web OTA**

Use `IP` from step 2. The PIN is read into a variable and never printed.
```bash
cd ~/Projects/zclaw && IP=<board ip> && \
  curl -s -o /dev/null -w '%{http_code}\n' "http://$IP/" && \
  time curl -s -w ' %{http_code}\n' -H 'X-OTA-PIN: 0000' --data-binary @build/zclaw.bin "http://$IP/ota"
PIN=$(sed -n 's/^CONFIG_ZCLAW_WEB_OTA_PIN="\(.*\)"$/\1/p' sdkconfig.defaults.esp32c6)
curl -s -w ' %{http_code}\n' -H "X-OTA-PIN: $PIN" --data-binary @README.md "http://$IP/ota"
```
Expected:
- `200` for the page;
- `PIN incorrecto 401` after ≥ 1 s;
- for README.md: `Error: ESP_ERR_OTA_VALIDATE_FAILED (el firmware actual sigue) 500`, and the board does **not** reboot (Review Focus 5).

Then the real update:
```bash
curl -s -w ' %{http_code}\n' -H "X-OTA-PIN: $PIN" --data-binary @build/zclaw.bin "http://$IP/ota"; unset PIN
```
Expected: `Actualizado. Reiniciando... 200`. After the reboot, `/status` over serial works, and the monitor log shows the board running from the other `ota_N` slot and the image marked valid after the stable window. Ask the user to open `http://<IP>/` in a browser once, to confirm the page renders.

If the Mac cannot reach the board's LAN, ask the user to run the browser checks instead, and ledger it.

- [ ] **Step 7: Heap**

```bash
python "$SCRATCH/serial_cmd.py" /dev/cu.usbmodem21201 "/status" 8
```
Compare "Heap libre … (min …)" with the earlier 108,100 free / 68,020 min. The spec expects +30–40 KB minus ~10–15 KB for the HTTP server. Write both numbers in the ledger.

- [ ] **Step 8: Record**

Append the results of steps 1–7 to the ledger. No commit unless a fix was needed; a fix follows TDD and gets its own commit.
