# Voice project — Delivery A: no Telegram, answers on the screen

Date: 2026-10-11
Branch: `fidel`
Status: design approved in conversation, pending written-spec review

## 1. Goal

Fidel's board stops using Telegram and becomes a standalone assistant with a
screen: replies are shown as text on the display, dashboards are requested in
plain words, and firmware updates arrive through a tiny web page on the LAN.
This delivery prepares the ground for voice input (Deliveries B and C); until
then the input is the USB serial console (and cron).

Success criteria:

- A message typed on the serial console shows the thinking face, then an
  answer page with the question on top and the reply wrapped below; long
  replies page by themselves; the face returns afterwards.
- "muéstrame el clima" (or cripto, panel) shows that dashboard via a tool.
- Uploading `build/zclaw.bin` from a browser with the right PIN reboots the
  board into it (rollback still active); a wrong PIN is refused.
- No Telegram traffic or memory: no getUpdates, no Telegram TLS session.
- The daily `memory_export` cron writes a file to the SD card.
- The board's web page (same PIN) lists the scheduled tasks and the last 20
  task results with their time; the results are kept on the SD card, so a
  reminder nobody saw on the screen is not lost.
- With `CONFIG_ZCLAW_TELEGRAM_ENABLE=y` (the default) everything behaves as
  before for other boards.

## 2. Scope

| Delivery | Content |
|---|---|
| **A (this spec)** | Telegram toggle, answer page, `display_dashboard` tool, web OTA, memory export to SD, IP on `/panel`, task log + tasks page |
| B | Audio capture (ADC on GPIO0), double-clap detector, recording to SD, `listening` face + red LED, audio-quality probe |
| C | Speech-to-text service on the VPS (NVIDIA Riva: Canary/Parakeet), transcript into the agent, "Dijiste: …" header |

Out of scope: rendering answers on the VPS (fallback option if the local
rendering is not liked), a speaker / text-to-speech, mDNS.

## 3. Hardware state (as of this spec)

Display RES moved to GPIO19 (`CONFIG_ZCLAW_DISPLAY_RST_GPIO=19`); MAX4466 OUT
on GPIO0 (ADC1_CH0), VCC 3V3. GPIO3 and GPIO4 are board-reserved
(`CONFIG_ZCLAW_BOARD_RESERVED_GPIOS="3,4"`): on this board either one, even
with an analog signal, leaves WiFi with zero visible networks. GPIO0 is not
used by Delivery A; Delivery B reserves it.

## 4. Telegram toggle

New `CONFIG_ZCLAW_TELEGRAM_ENABLE` (bool, default `y`). Fidel's board sets it
to `n` in its git-ignored `sdkconfig.defaults.esp32c6`.

When `n`:

- `main.c` neither initialises nor starts Telegram (same path as
  `CONFIG_ZCLAW_STUB_TELEGRAM`); no startup message.
- `telegram.c` stays compiled: `telegram_pause_polling()`/`resume` (used by
  `display_remote`) are harmless counters and `telegram_is_configured()`
  returns false.
- Agent replies go to the serial channel and the screen; the Telegram output
  queue is NULL, which `queue_telegram_response()` already ignores.
- OTA by Telegram document is unavailable (replaced by §7). The `ota_update`
  tool (URL) is untouched.

## 5. Answer page

### 5.1 Trigger and data flow

Every reply that goes through `send_response()` (LLM answers, command output,
errors) calls `display_show_answer(prompt, reply)` instead of
`display_notify_talking()`. The agent keeps the current request's text so the
prompt is available there. Header label by source: serial → `Tú:`, cron →
`Tarea:`; Delivery C adds `Dijiste:`. `/panel`, `/clima`, `/cripto` keep
showing their dashboards (they do not call `send_response`).

`display_show_answer` copies prompt (≤ 160 bytes) and reply (≤ 1024 bytes,
`CHANNEL_TX_BUF_SIZE`) into one heap block handed to the display task through
the event queue (receiver frees; on a full queue the caller frees it and the
reply still reaches serial). It is a no-op when the display is disabled.

The answer is a requested page (`PAGE_ANSWER`) with the same activity rule as
the remote page: requested during Fidel's turn, it survives that turn; the
next activity (new message) cancels it.

### 5.2 Layout (8x16 font, local rendering)

```
y=2    Tú: ¿qué hora es en Madrid?      header, grey, ≤ 2 lines, "…" if longer
y=38   ─────────────────────────────    divider
y=44   Son las 12:45 AM en Madrid,      body, white, 29 columns × 11 lines
       compañero. Allí ya es otro día.
...
y=224                            1/2    page indicator, only when > 1 page
```

29 columns = 232 px with 4 px margins. Words wrap at spaces; a word longer
than a line is split; `\n` in the reply starts a new line; control
characters are dropped; non-Latin-1 characters render as `?` (existing
decoder).

### 5.3 Timing

Total = 70 ms × reply characters, clamped to 8–45 s. With several pages,
each page gets time proportional to its characters, at least 3 s (the total
grows if needed). After the last page the face returns.

### 5.4 Pure module

`text_layout.c` (no ESP-IDF): wraps UTF-8 into lines of N Latin-1 columns
(line = byte offset + length + character count), splits lines into pages,
truncates the header with an ellipsis, and computes page durations. All of
it is host-tested.

## 6. `display_dashboard` tool

New LLM tool (only when the display is enabled):
`display_dashboard({"name": "clima" | "cripto" | "panel"})` → reuses
`display_request_dashboard()` / `display_request_status()` for 30 s and
returns "Mostrando <name> 30 s." or an error. The serial commands remain.

The system prompt gains one sentence when the display is enabled: replies are
shown on a small 240x240 screen (~29 characters × 11 lines per page), so keep
them short; use `display_dashboard`/`display_image` to show things.

## 7. Web OTA

`esp_http_server` on port 80, started after WiFi connects, only when
`CONFIG_ZCLAW_WEB_OTA_ENABLE=y` and `CONFIG_ZCLAW_WEB_OTA_PIN` has at least 4
characters (both set in the git-ignored board defaults).

- `GET /` — one inline HTML page (Spanish, ~2 KB): firmware version, running
  partition, PIN field, file input, "Actualizar" button; JavaScript sends the
  file with `fetch` and shows the result.
- `POST /ota` — header `X-OTA-PIN`, raw `.bin` body with `Content-Length`.
  - PIN compared in constant time; a wrong PIN waits 1 s, then 401.
  - 409 if an update is already running; 413 if larger than the target
    partition; 400 without `Content-Length`.
  - Streams 4 KB chunks (heap buffer) into `esp_ota_begin(next partition,
    OTA_WITH_SEQUENTIAL_WRITES)` / `esp_ota_write`; `esp_ota_end` validates
    the image (magic, chip, SHA); `esp_ota_set_boot_partition`; replies 200
    and restarts after 1 s. Any failure calls `esp_ota_abort` and returns an
    error without rebooting.
- Server limits: `max_open_sockets = 2`, task stack 6 KB.
- After reboot the existing pending-verify/rollback logic applies.

`/panel` gains an `IP` row so the address of the page is visible on screen.

## 7b. Task log and tasks page

Added after the spec review (user request): without Telegram, a scheduled
task's reply is only on the screen for 8–45 s.

- Every reply of a cron-triggered turn is appended to
  `/sdcard/log/tasks.log`, one line per record:
  `<unix seconds>\t<action>\t<reply>\n`. Inside the fields a backslash,
  newline and tab are written as `\\`, `\n` and `\t`, and `\r` is dropped. The action
  (without `[CRON n]`) is clipped to 256 bytes and the reply to 1024, never
  inside a UTF-8 character. Normal (serial) replies are not logged.
- Past 64 KB the file is renamed to `tasks.old.log` (replacing the previous
  one) before the next append. Without a mounted SD nothing is logged.
- The web page gains a "Tareas" section. The button "Ver tareas y avisos"
  fetches, with the same `X-OTA-PIN` header:
  - `GET /tasks`: the `cron_list` JSON (id, type, time or interval, action,
    enabled);
  - `GET /log`: the last 20 records, newest first, as plain text lines.

  A wrong PIN waits 1 s and returns 401.
- The page renders everything with `textContent` (stored LLM text is never
  parsed as HTML). Times before 2020 show "sin hora".

## 8. Memory export to SD

When Telegram is unavailable, `memory_export` writes the memories to
`/sdcard/backup/memories-YYYYMMDD-HHMM.txt` (or `memories-up<uptime>s.txt`
before SNTP sync), creating the folder if needed, and returns the path. Without
a mounted SD it returns an error.

## 9. Configuration

| Option | Default | Fidel's board |
|---|---|---|
| `ZCLAW_TELEGRAM_ENABLE` | y | n |
| `ZCLAW_WEB_OTA_ENABLE` | n | y |
| `ZCLAW_WEB_OTA_PIN` | "" | secret, ≥ 4 chars |

## 10. Error handling

- Display disabled: replies only on serial; the tools return "no display".
- Reply longer than 1024 bytes: truncated with "…".
- Answer event dropped (queue full): the reply is still on serial.
- Web OTA: every failure leaves the running firmware untouched.
- Memory export without SD: error message, nothing lost (memories stay in NVS).
- Task log without SD: nothing logged; the page says there are no results.

## 11. Resource budget

- RAM: Telegram's TLS session and tasks disappear (expected +30–40 KB free);
  answer buffer ≤ 1.2 KB while shown; HTTP server ~10–15 KB.
- Flash: `esp_http_server` ~25 KB; text layout and tool code a few KB.

## 12. Testing

Host (TDD):

- `task_log_format`: line format, escaping, clipping without splitting UTF-8.
- Agent: a cron reply is logged with its action; a serial reply is not.
- `text_layout`: Spanish text with accents, words longer than a line,
  explicit newlines, empty reply, header truncation, page count, durations
  (clamp, proportional split, 3 s minimum).
- Agent: a reply shows `display_show_answer` with prompt and reply and does
  not call `display_notify_talking`; `/panel`/`/clima`/`/cripto` unchanged.

On the board:

1. Short and long replies over serial: header, wrapping, paging, timing,
   face returns.
2. "muéstrame el clima" → dashboard via `display_dashboard`.
3. Web OTA: wrong PIN → 401; right PIN with `build/zclaw.bin` → reboot,
   running from the other slot, image confirmed after the stable window.
4. `memory_export` creates the file on the SD.
4b. A one-off task fires: its answer page says `Tarea:`, `/log` returns it,
   and the page shows it under "Últimos avisos".
5. No Telegram in the log; `/status` heap compared with the current
   108,100 free / 68,020 min.
6. Builds with `ZCLAW_TELEGRAM_ENABLE=y` and with display disabled still
   compile and behave as before.
