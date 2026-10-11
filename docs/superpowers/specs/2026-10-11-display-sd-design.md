# Display + SD card — Delivery 1 design

Date: 2026-10-11
Branch: `fidel`
Status: approved in conversation, pending written-spec review

## 1. Goal

Give Fidel (zclaw on ESP32-C6) a 1.54" ST7789 TFT that alternates between an
animated photo face and a status page, with the face frames read from an SD
card that shares the SPI bus with the display. The face reacts to what Fidel
is doing: idle (with blinking), thinking, talking, offline.

Success criteria:

- The status page shows real data (time, weather, WiFi, chip temperature,
  heap, model, uptime, firmware version).
- With an SD card holding the face set, sending a Telegram message makes the
  face switch to *thinking* while the LLM works and *talking* while the reply
  goes out, then back to idle.
- Without an SD card (or with missing files) everything keeps working and the
  face page falls back to a large text label of the current state.
- `/status` minimum free heap stays at or above 100 KB (today: ~130 KB).
- With `CONFIG_ZCLAW_DISPLAY_ENABLE=n` and `CONFIG_ZCLAW_SD_ENABLE=n` the
  firmware behaves exactly as today.

## 2. Scope

The full feature set is split into three deliveries; this spec covers only
Delivery 1.

| Delivery | Content |
|---|---|
| **1 (this spec)** | Shared SPI bus, ST7789 driver, SD mount (FAT), rotating pages, animated face from SD, host-side face asset tool, reserved pins |
| 2 | Data on SD: memory backups, conversation history, logs |
| 3 | LLM tools: `display` (text, expression, settings, image from URL via SD) and SD file tools |

Explicitly out of scope for now: vector/dashboard graphics, any VPS-side
processing, face images embedded in the firmware, letting the LLM choose an
expression (Delivery 3 uses the `happy`, `angry` and `speech` frames this
delivery already ships to the SD).

## 3. Hardware

ESP32-C6 has a single general-purpose SPI host (SPI2) and no SDMMC host, so
the display and the SD card share SPI2 with separate chip selects.

| Signal | GPIO | Display pin | SD pin |
|---|---|---|---|
| SCLK | 6 | SCL | SCK |
| MOSI | 5 | SDA | MOSI |
| MISO | 2 | — | MISO |
| Display CS | 7 | CS | — |
| Display DC | 1 | DC | — |
| Display RST | 0 | RES | — |
| Backlight | 10 | BLK | — |
| SD CS | 3 | — | CS |

- Display SPI clock 40 MHz (MOSI goes through the GPIO matrix; SCLK is the
  SPI2 IOMUX pin). SD clock 20 MHz (`SDMMC_FREQ_DEFAULT`).
- SD module VCC goes to 5 V when the module has an on-board regulator
  (typical 6-pin CS/SCK/MOSI/MISO/VCC/GND module), otherwise 3.3 V.
- GPIO 5 (MTDI) is a strapping pin; the display only receives on it, so boot
  strapping is unaffected. GPIO 0/1 are free because no 32 kHz crystal is used.
- The 240x240 panel sits in a 240x320 controller RAM. Colour inversion and
  the row/column gap depend on the module and the chosen rotation; they are
  Kconfig options. Initial defaults: inversion on, gaps 0/0, rotation 0
  (typical for 1.54" IPS modules); adjusted on the real board if the image is
  shifted or the colours are inverted.

## 4. Architecture

```
agent.c / local_admin.c ──events──> display.c  (own task + event queue)
 (thinking, talking, WiFi)            │  page rotation, state priority
                                      ├─ display_face.c    state -> frames, blink, talk loop
                                      ├─ display_status.c  status page content
                                      └─ display_gfx.c     text strips, frame streaming
                                            │
                       sd_card.c ───────────┤  FAT mount on SPI2, retry loop
                                            │
                                      display_hw.c         ST7789 via esp_lcd, backlight PWM
                                            │
                                      board_spi.c          SPI2 bus init (once, shared)
```

Pure logic lives in separate files with no ESP-IDF dependencies so it can be
host-tested (same pattern as `telegram_poll_policy.c`):

- `face_state.c` — given the current flags (wifi up, thinking, talking until
  T, blink schedule) and the time, decides which frame to show next.
- `page_rotation.c` — decides which page is visible and for how long.
- `frame_format.c` — parses and validates frame file headers.

### 4.1 Components

**`board_spi.c`** — initialises SPI2 once if either the display or the SD is
enabled (`spi_bus_initialize`, DMA auto). Exposes the host id. Both drivers
attach as devices; the SPI driver serialises transactions.

**`display_hw.c`** — `esp_lcd_new_panel_io_spi` + `esp_lcd_new_panel_st7789`,
reset, inversion, gap, rotation, `esp_lcd_panel_disp_on_off`. Backlight on
GPIO 10 through LEDC PWM (fixed brightness in this delivery; the setter
exists for Delivery 3). Provides `display_hw_blit(x, y, w, h, pixels)`.

**`sd_card.c`** — mounts FAT at `/sdcard` with `esp_vfs_fat_sdspi_mount`
(no formatting on failure). Tracks mounted/absent state. A retry runs every
60 s while absent. Read errors during use unmount the card and return to the
retry loop. Posts `SD_MOUNTED` / `SD_ABSENT` events to the display task.

**`display_gfx.c`** — two DMA-capable buffers of 240x16x2 bytes (7.5 KB each),
alternated so a buffer is never overwritten while its panel transfer is still
in flight. Draws:

- text with an 8x16 Latin-1 bitmap font (ASCII plus á é í ó ú ü ñ ¿ ¡ and the
  rest of Latin-1), optionally scaled 2x for large text;
- solid fills;
- a frame file streamed from SD in row chunks;
- a sub-rectangle of a full frame file (row-by-row seeks), used to restore
  the neutral pixels under a patch.

**`display_face.c`** — owns the frame catalogue: scans `/sdcard/face/` on mount
for `<expression>.r565` and variants `<expression>_2.r565` … `_8.r565`. Uses
`face_state` to pick frames and `display_gfx` to draw them.

**`display_status.c`** — renders the status page from existing APIs: local
time (SNTP; `--:--` until synced), cached weather, `esp_wifi_sta_get_ap_info`
(SSID, RSSI), chip temperature (`tools_tsens`), free/min heap,
`llm_get_model()`, uptime, `ota_get_version()`.

**`display.c`** — public API and the display task (low priority, ~4 KB stack).
Owns an event queue; producers never block.

```c
void display_notify_thinking(bool active);
void display_notify_talking(size_t reply_chars);
void display_notify_wifi(bool connected);
```

These are no-ops when the display is disabled, so callers need no `#if`.

**Weather cache** — the Open-Meteo request inside `tools_weather_handler`
(`tools_net.c`) moves into a shared `weather_fetch_current()` used by both the
tool and the status page. The status page refreshes it every 30 min from a
short-lived task (created, fetches, deletes itself) that tries the HTTP gate
without waiting; if the gate is busy it retries on the next cycle. Weather
never blocks Telegram or the LLM.

### 4.2 Event sources

| Event | Producer |
|---|---|
| thinking on/off | `agent.c`, at the existing `led_thinking_start/stop` call sites |
| talking (reply length) | `send_response()` in `agent.c` |
| WiFi up/down | WiFi/IP event handlers in `local_admin.c` |
| SD mounted/absent | `sd_card.c` |

## 5. Face behaviour

States, highest priority first:

1. **offline** (WiFi down) → `sleeping`
2. **thinking** → `thinking`, cycling through variants every ~800 ms if present
3. **talking** → alternate `speaking` patch and the neutral mouth area every
   ~180 ms for 60 ms per reply character, clamped to 2–6 s; variants
   (`speaking_2` …) rotate in
4. **idle** → `neutral`, with an `eyes_closed` patch for 150 ms every 3–7 s
   (random)

Pages: face 10 s, status 5 s. While thinking or talking the face page is
forced and rotation pauses; it resumes when activity ends.

Fallback: no SD, no `neutral` frame, or a corrupt frame → the face page shows
the state as large text (e.g. `pensando...`) on a plain background. On-screen
text is Spanish, matching the existing `/status` reply; identifiers, paths and
file names are English. A single
missing or corrupt variant is skipped.

`happy`, `angry` and `speech` are produced and copied to the SD now but are
only triggered in Delivery 3.

## 6. Frame file format (`.r565`)

All multi-byte header fields little-endian; pixels in the panel's native
order (RGB565, big-endian).

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `R565` |
| 4 | 2 | x |
| 6 | 2 | y |
| 8 | 2 | w |
| 10 | 2 | h |
| 12 | 2 | flags (bit 0: patch over `neutral`) |
| 14 | 2 | reserved (0) |
| 16 | w·h·2 | pixels, row-major |

A full frame is `0,0,240,240` (115,216 bytes). A patch has the patch flag set
and is drawn over `neutral`; restoring it means redrawing the same rectangle
from `neutral.r565`. Validation: magic, rectangle inside 240x240, non-zero
size, file length equals `16 + w*h*2`.

Estimated timings (to be measured): full frame ~85 ms (SD read + panel
write, serialised on the shared bus); eye or mouth patch ~10 ms.

## 7. Host-side face asset tool (`scripts/face_assets.py`)

Kept in the repo as a reusable tool so the face set can be replaced later.
Dependencies in `scripts/requirements-face.txt` (Pillow, numpy); run from a
virtualenv.

```
python scripts/face_assets.py --input assets/face --output build/face \
       [--size 240] [--center-y 0.5] [--eyes X,Y,W,H] [--mouth X,Y,W,H] \
       [--contact-sheet build/face/contact.png]
```

Input: PNG/JPG files named after the expressions — `neutral`, `eyes_closed`,
`speaking`, `thinking`, `sleeping`, `happy`, `angry`, `speech` — plus optional
numbered variants (`speaking_2.png`, …). `neutral` is required.

Pipeline:

1. Load every input as RGB.
2. Same-pose frames (`eyes_closed`, `speaking`, variants, and `happy`/`angry`)
   are aligned to `neutral` at source resolution: placed on a canvas of
   `neutral`'s size, then shifted by the offset found with phase correlation.
   The shift is applied only when the correlation peak is confident;
   otherwise the frame is used as is and a warning is printed. Different-pose
   frames (`thinking`, `sleeping`, `speech`) are not aligned.
3. Square crop of side `min(width, height)`, horizontally centred and
   vertically centred at `--center-y` of the image height (clamped to the
   image); resize to `--size` with Lanczos. The same geometry is applied to
   every image.
4. Patches: for `eyes_closed` and `speaking` (and their variants) only the eye
   band / mouth band is taken from the aligned frame and blended onto
   `neutral` with a feathered edge. Everything outside the band is
   bit-identical to `neutral`, so blinking and talking never shimmer. Bands
   are auto-detected from the strongest region of the aligned difference
   image; `--eyes` / `--mouth` override them.
5. Write `.r565` files: full frames for `neutral`, `thinking`, `sleeping`,
   `happy`, `angry`, `speech`; patches for the eye/mouth sets.
6. Write a contact sheet PNG for visual review.

Output goes to `build/face/` (already git-ignored) and is copied to the card's
`face/` folder. Source photos stay out of git: `/assets/face/*` is ignored
except an `assets/face/README.md` describing the expected inputs.

## 8. GPIO policy

`gpio_policy` gains a reserved-pin list built from the enabled peripherals
(display: SCLK, MOSI, CS, DC, RST, backlight; SD: MISO, CS; the shared pins
count once). Reserved pins are rejected by both `gpio_policy_pin_is_allowed`
and `gpio_policy_runtime_input_pin_is_safe` (reconfiguring an SPI pin as an
input would break the bus), and `gpio_policy_pin_forbidden_hint` explains why.
With the defaults the GPIO tools keep pins 4 and 9 in the 2–10 range.

## 9. Configuration (Kconfig)

| Option | Default |
|---|---|
| `ZCLAW_DISPLAY_ENABLE` | n |
| `ZCLAW_SD_ENABLE` | n |
| `ZCLAW_SPI_SCLK_GPIO` / `MOSI` / `MISO` | 6 / 5 / 2 |
| `ZCLAW_DISPLAY_CS_GPIO` / `DC` / `RST` / `BL` | 7 / 1 / 0 / 10 |
| `ZCLAW_DISPLAY_INVERT_COLORS` / `X_GAP` / `Y_GAP` | y / 0 / 0 (see §3) |
| `ZCLAW_SD_CS_GPIO` | 3 |
| `ZCLAW_FACE_SECONDS` / `ZCLAW_STATUS_SECONDS` | 10 / 5 |

Fidel's board enables display and SD in its local
`sdkconfig.defaults.esp32c6`. FAT long file names are enabled
(`CONFIG_FATFS_LFN_HEAP=y`) so names such as `eyes_closed.r565` work.

## 10. Error handling

- Display init failure: log and disable the display subsystem; never restart
  the board.
- SD absent or mount failure: fallback face, 60 s retry.
- SD removed while mounted: read errors unmount, fallback, retry.
- Corrupt or missing frame: skip; missing `neutral` means text fallback.
- Status data unavailable: show `--` for that field.
- Event queue full: drop the event (the next event or timer tick corrects the
  state).

## 11. Resource budget (estimates, verified on hardware)

- RAM: ~20–25 KB permanent (task stack ~4 KB, two 7.5 KB DMA buffers, FAT +
  SD driver ~4 KB). If `/status` minimum heap drops below 100 KB, shrink the
  strip height before anything else.
- Flash: ~60–90 KB for `esp_lcd`, SD/FAT and the 8x16 font (~4 KB). Current
  free space in the OTA slot: ~455 KB.
- SD: ~1 MB for the default face set.

## 12. Font

An 8x16 Latin-1 bitmap font generated by `scripts/gen_font.py` from an
OFL-licensed font, emitted as a C array (`main/font8x16.c`) with the licence
noted in the file header. The generator stays in the repo.

## 13. Testing

Host tests (`test/host`, TDD):

- Prerequisite: restore the host test build, which already fails to link
  because `agent.c` references `led_thinking_start/stop` and
  `tools_chip_temp_handler` without host stubs.
- `face_state`: priority order, blink scheduling bounds, talk duration clamp,
  variant cycling, fallback when frames are missing.
- `page_rotation`: 10/5 s cycle, forced face during activity, resume.
- `frame_format`: valid full frame, valid patch, bad magic, out-of-bounds
  rectangle, truncated file.
- `gpio_policy`: reserved pins rejected for outputs and inputs; hint text.

Face asset tool: pytest checks that outputs have valid headers and expected
sizes, that patch pixels outside the band equal `neutral`, and that the tool
fails cleanly without `neutral`.

On the board:

1. Status page appears with real values.
2. With the SD face set: idle blink, Telegram message → thinking → talking →
   idle.
3. Remove the SD → text fallback; reinsert → face back within 60 s.
4. Measure per-frame timings in the log and the minimum heap via `/status`.
5. `CONFIG_ZCLAW_DISPLAY_ENABLE=n` + `CONFIG_ZCLAW_SD_ENABLE=n` builds and
   behaves as before.

## 14. As built (2026-10-11)

Verified on Fidel's board. Where this list and the sections above disagree,
this list wins.

- **Panel values confirmed:** colour inversion on, gaps 0/0, RGB order, SPI
  mode 0 — the Kconfig defaults.
- **SD CS is GPIO 18, not 3.** On this board GPIO3 powers the RF switch:
  driving it high (as SD CS did) left WiFi with zero visible networks.
  New `CONFIG_ZCLAW_BOARD_RESERVED_GPIOS` (default `"3"` on ESP32-C6): GPIO
  tools refuse those pins with their own hint, and `board_spi` refuses to
  start the bus on them. Reserved-pin capacity is 12.
- **Status page only on demand:** `CONFIG_ZCLAW_STATUS_SECONDS` defaults to
  0; `/panel` (chat or serial, no LLM) shows it for 15 s; thinking/talking
  cancel it. Clock in 12-hour format.
- **Face frames:** `scripts/face_assets.py --fit` (default 1) keeps the whole
  photo and extends its edges; alignment also extends edges.
- **Heap:** the "≥ 100 KB minimum" criterion was unattainable (the
  display-off build already drops to 96.6 KB after one LLM TLS call).
  Measured after an LLM call: display-off 146,232 free / 96,576 min;
  display+SD 117,544 free / 76,764 min. Reached with
  `CONFIG_WL_SECTOR_SIZE_512` (FatFs sized every file buffer for 4096-byte
  sectors: SD mount 27 KB → 9 KB) and 8-row display strips.
- **Timing:** full frame from SD 225 ms (estimate was ~85 ms); patches are
  small. Binary 0x11c600 (23 % of the OTA slot free).
- `config.h` now includes `sdkconfig.h`; before, files that included it
  first silently ignored Kconfig in its `#ifdef CONFIG_*` checks.
