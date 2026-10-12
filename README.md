# zclaw

<img
  src="docs/images/lobster_xiao_cropped_left.png"
  alt="Lobster soldering a Seeed Studio XIAO ESP32-C3"
  height="200"
  align="right"
/>

The smallest possible AI personal assistant for ESP32.

zclaw is written in C and runs on ESP32 boards with a strict all-in firmware budget target of **<= 888 KiB** on the default build. It supports scheduled tasks, GPIO control, persistent memory, and custom tool composition through natural language.

The **888 KiB** cap is all-in firmware size, not just app code.
It includes `zclaw` logic plus ESP-IDF/FreeRTOS runtime, Wi-Fi/networking, TLS/crypto, and cert bundle overhead.

Fun to use, fun to hack on.
<br clear="right" />

## Fidel fork

This branch runs one board: an ESP32-C6 with an ST7789 display, a microSD card and a MAX4466
microphone. It talks to an OpenAI-compatible LLM through a proxy, listens by voice (double clap)
and answers on the screen. Chat over USB serial; update over the web page with a PIN.

Current design documents live in `docs/superpowers/specs/`.

## Quick Start

One-line bootstrap (macOS/Linux):

```bash
bash <(curl -fsSL https://raw.githubusercontent.com/tnm/zclaw/main/scripts/bootstrap.sh)
```

Already cloned?

```bash
./install.sh
```

Non-interactive install:

```bash
./install.sh -y
```

<details>
<summary>Setup notes</summary>

- `bootstrap.sh` clones/updates the repo and then runs `./install.sh`. You can inspect/verify the bootstrap flow first (including `ZCLAW_BOOTSTRAP_SHA256` integrity checks).
- Linux dependency installs auto-detect `apt-get`, `pacman`, `dnf`, or `zypper` during `install.sh` runs.
- In non-interactive mode, unanswered install prompts default to `no` unless you pass `-y` (or saved preferences/explicit flags apply).
- For encrypted credentials in flash, use secure mode (`--flash-mode secure` in install flow, or `./scripts/flash-secure.sh` directly).
- After flashing, provision WiFi + LLM credentials with `./scripts/provision.sh`.
- You can re-run either `./scripts/provision.sh` or `./scripts/provision-dev.sh` at any time (no reflash required) to update runtime credentials: WiFi SSID/password, LLM model/API key and API URL.
- Default LLM rate limits are `100/hour` and `1000/day`; change compile-time limits in `main/config.h` (`RATELIMIT_*`).
- Quick validation path: open the serial monitor (`./scripts/monitor.sh`) and send a test message to confirm the device can answer.
- If serial port is busy, run `./scripts/release-port.sh` and retry.
- For repeat local reprovisioning without retyping secrets, use `./scripts/provision-dev.sh` with a local profile file (`provision-dev.sh` wraps `provision.sh --yes`).

</details>

## Highlights

- Chat over USB serial; web OTA page with a PIN
- Timezone-aware schedules (`daily`, `periodic`, and one-shot `once`)
- Built-in + user-defined tools
- For brand-new built-in capabilities, add a firmware tool (C handler + registry entry) via the Build Your Own Tool docs.
- Runtime diagnostics via `get_diagnostics` (quick/runtime/memory/rates/time/all scopes)
- GPIO control with guardrails (including `gpio_read_all`)
- USB local admin console for recovery, safe mode, and pre-network bring-up
- Persistent memory across reboots
- OpenAI-compatible LLM endpoint (custom API URL)

## Hardware

Target: **ESP32-C6** with an ST7789 display, a microSD card and a MAX4466 microphone (see the Fidel fork section).

## Local Dev & Hacking

Typical fast loop:

```bash
./scripts/test.sh host
./scripts/build.sh
./scripts/flash.sh --kill-monitor /dev/cu.usbmodem1101
./scripts/provision-dev.sh --port /dev/cu.usbmodem1101
./scripts/monitor.sh /dev/cu.usbmodem1101
```

Profile setup once, then re-use:

```bash
./scripts/provision-dev.sh --write-template
# edit ~/.config/zclaw/dev.env
./scripts/provision-dev.sh --show-config
./scripts/provision-dev.sh
```

### Other Useful Scripts

<details>
<summary>Show scripts</summary>

- `./scripts/flash-secure.sh` - Flash with encryption
- `./scripts/provision.sh` - Provision credentials to NVS
- `./scripts/provision-dev.sh` - Local profile wrapper for repeat provisioning
- `./scripts/erase.sh` - Erase NVS only (`--nvs`) or full flash (`--all`) with guardrails
- `./scripts/monitor.sh` - Serial monitor
- `./scripts/test.sh` - Run host tests
- `./scripts/test-api.sh` - Run live provider API checks (manual/local)

</details>

## Local Admin Console

When the board is in safe mode, unprovisioned, or the LLM path is unavailable, you can still operate it over USB serial without Wi-Fi or an LLM round trip.

```bash
./scripts/monitor.sh /dev/cu.usbmodem1101
# then type:
/wifi status
/wifi scan
/bootcount
/gpio all
/reboot
```

Available local-only commands:

- `/gpio [all|pin|pin high|pin low]`
- `/diag [scope] [verbose]`
- `/reboot`
- `/wifi [status|scan]`
- `/bootcount`
- `/factory-reset confirm` (destructive; wipes NVS and reboots)

## Size Breakdown

Current default `esp32` breakdown (grouped image bytes from `idf.py -B build size-components`):

| Segment | Bytes | Size | Share |
| --- | ---: | ---: | ---: |
| zclaw app logic (`libmain.a`) | `39276` | ~38.4 KiB | ~4.6% |
| Wi-Fi + networking stack | `378624` | ~369.8 KiB | ~44.4% |
| TLS/crypto stack | `134923` | ~131.8 KiB | ~15.8% |
| cert bundle + app metadata | `98425` | ~96.1 KiB | ~11.5% |
| other ESP-IDF/runtime/drivers/libc | `201786` | ~197.1 KiB | ~23.7% |

Total image size from this build is `853034` bytes; padded `zclaw.bin` is `853184` bytes (~833.2 KiB), leaving `56128` bytes (~54.8 KiB) under the 888 KiB cap.

## License

MIT
