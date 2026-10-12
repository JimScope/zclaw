# Firmware slimming: drop unused features, free RAM

Date: 2026-10-12
Branch: `fidel`
Status: design agreed in conversation, pending written-spec review
Depends on: voice input (Deliveries B+C), already on the branch (193eb95)

## 1. Goal

The fork runs on one board: Fidel's ESP32-C6 with display, SD card and microphone. The user wants two things:

1. **More RAM.** The heap has ~145 KB free at idle and fragments with use. The largest free block is 64 KB after a few turns. Our code holds ~78 KB of static RAM, and a further 16 KB of heap sits in a queue for a disabled feature.
2. **More maintainable code.** Remove the features Fidel does not use, together with their tests, scripts, docs and CI.

The user also asked to use the 4 GB microSD wherever it frees RAM.

Success criteria:

- **RAM.** Free heap at idle is at least 200 KB, up from 145 KB, measured with `/diag` after boot. After a session of voice, web search, read_web, dashboard and cron turns, the heap minimum stays at or above 40 KB.
- **Same behaviour.** Everything Fidel uses still works on the board:
  - serial chat;
  - voice (double clap and `/escucha`);
  - answers on the screen and the dashboards;
  - web OTA with PIN, `/panel`, `/tasks` and `/log`;
  - cron;
  - the tools `memory_*`, `read_web` (with paging), `web_search`, `weather`, `display_image`, `sms_send`, GPIO/LED and the user tools;
  - `memory_export` to the SD card.
- **Builds and tests.** `idf.py build` is clean for esp32c6 and `scripts/test.sh host` passes with no failures. This also fixes `required_core_tools_exist`, which fails today. The STT server tests pass too.
- **Less code.** The removed features leave no dead references (`grep -ri telegram main/ test/ scripts/` returns nothing).

## 2. What the board uses today (facts checked)

- **LLM.** NVS has `llm_backend = openai` and the dummy API key `proxy`. The Caddy proxy on the VPS adds the real NVIDIA key. Model `nvidia/nemotron-3-super-120b-a12b`, with fallback `openai/gpt-oss-20b`.
- **Telegram.** Already off (`CONFIG_ZCLAW_TELEGRAM_ENABLE` not set). OTA goes through the web page (`web_ota.c`) or the `ota_update` URL tool.
- **Inputs.** USB serial, voice and cron. The web relay is not used.
- **Tools already compiled out.** I2C and DHT (`#if 0` in `builtin_tools.def`) and the persona tools (`#if 0`). The persona NVS key may still change the prompt wording at boot.

## 3. Part A: remove unused features

Each item removes the code, its Kconfig, NVS keys, tests (C and Python), scripts, CI references and docs references. File maps come from the exploration of 2026-10-12. The implementation plan lists them line by line.

| # | Feature | Removed | Kept, or done another way |
|---|---|---|---|
| A1 | **Telegram** | `main/telegram*.{c,h}` (16 files, ~1700 lines), `scripts/telegram-clear-backlog.sh`, 5 host test files, `MSG_SOURCE_TELEGRAM`, `chat_id`, `telegram_msg_t`, the Telegram queue and its pause/resume calls in agent.c, `ota_apply_from_url` (only Telegram used it), `/cmd@bot` parsing in agent_commands.c, Telegram options in provision scripts | `memory_export` writes only to the SD card. OTA through `web_ota.c` and the `ota_update` tool. The LLM HTTP gate wait was derived from the Telegram poll timeout; it becomes a constant `LLM_HTTP_GATE_WAIT_MS` (31 s, today's value). `START_COMMAND_COOLDOWN_MS` and `MESSAGE_REPLAY_COOLDOWN_MS` move to the agent section of config.h. |
| A2 | **LLM backends other than OpenAI-compatible** (Anthropic, OpenRouter, Ollama) | Backend enum, per-backend URLs, models and headers, the Anthropic request builder and parser in json_util.c, `ZCLAW_CLAUDE_API_KEY`, backend menus in provision scripts, `test/api/` Anthropic/OpenRouter tests | One request format (OpenAI chat completions). The Bearer header is sent only when a key is set, so a keyless setup still works. The NVS key `llm_backend` is ignored. The token-limit field stays `max_completion_tokens`, which the board sends today. |
| A3 | **Personas** | `tools_persona.c`, persona enum and instructions in agent_prompt, `NVS_KEY_PERSONA`, the persona hooks in agent.c and their tests | Fidel's personality stays in `SYSTEM_PROMPT`. `agent_build_system_prompt(buf, len)` loses its persona argument. |
| A4 | **I2C and DHT tools** | `tools_i2c.c`, `tools_dht.c`, their test files, `mock_i2c.c`, `test/host/driver/i2c.h`, the `#if 0` blocks | The required-tools test drops the four I2C/DHT names, which fixes the test that fails today. |
| A5 | **Emulator, QEMU, stub LLM, UART channel** | `ZCLAW_EMULATOR_MODE`, `ZCLAW_EMULATOR_LIVE_LLM`, `ZCLAW_STUB_LLM`, `ZCLAW_STUB_TELEGRAM`, `ZCLAW_CHANNEL_UART`, `sdkconfig.qemu.defaults`, `sdkconfig.test`, emulate/exit-emulator/qemu bridge scripts and their tests | Host coverage of llm.c stays. The code that cannot build on the host is gated on `TEST_BUILD` instead of `STUB_LLM`, and the canned reply becomes OpenAI format. |
| A6 | **Other board presets** | `sdkconfig.esp32-t-relay.defaults`, `sdkconfig.esp32s3-box-3.defaults` and their options in build/flash/flash-secure scripts, the box-3 CI job | `sdkconfig.secure` (flash and NVS encryption) stays. The fork targets ESP32-C6 only; CI builds esp32c6. |
| A7 | **Web relay and latency benchmark** | `web_relay.py`, `web-relay.sh`, `bootstrap-web-relay.sh`, `requirements-web-relay.txt`, `benchmark.sh`, `benchmark_latency.py` and their tests, relay steps in `release.yml` | Nothing on the board depends on them. |
| A8 | **Upstream docs site** | `docs-site/` and `scripts/docs-site.sh`. They describe the upstream product, including every removed feature. | `README.md` loses the references to removed features. A short "Fidel fork" section points to `docs/superpowers/` for the current design. `CHANGELOG.md` and old specs stay as history. |

Kept on purpose: user tools (`create_tool`, `list_user_tools`, `delete_user_tool`), `sms_send` (Vendel), rate limiting, cron, GPIO/LED tools, `sdkconfig.secure`, the provision/install/flash scripts (pruned, not removed).

The local, git-ignored `sdkconfig.defaults.esp32c6` loses its `CONFIG_ZCLAW_TELEGRAM_ENABLE=n` line by hand.

## 4. Part B: free RAM

Today's large static buffers (nm on the C6 build) and the plan for each:

| Buffer | Today | Change | Saved at idle |
|---|---|---|---|
| `s_history` (agent.c) | 27,312 B: 24 fixed slots of 1138 B | B1: packed history ring | ~19 KB |
| `s_response_buf` (agent.c, LLM_RESPONSE_BUF_SIZE) | 16,384 B | B2: heap during the request only | 16 KB |
| Telegram out queue (main.c) and `s_send_msg` | 16.4 KB of heap and 4 KB static | removed with A1 | ~20 KB |
| `html` (tools_net.c, web_search) | 6,144 B | B3: heap during the call only | 6 KB |
| `s_web_cache` (tools_net.c, read_web) | 4,096 B | B4: page kept on the SD card | 4 KB |
| `s_pending_fires` (cron.c) | 4,112 B | B5: store entry ids, not copies | ~4 KB |
| `s_system_prompt_buf` (agent.c) | 2,816 B | B3: heap around the request build | 2.8 KB |
| `status_buf` (agent.c) | 1,024 B | shrink to 256 B | 0.8 KB |

Expected: about 70 KB more free heap at idle, so ~215 KB, against the 200 KB target.

### B1. Conversation history: packed ring in RAM

Today's model gives every message a fixed slot: 1024 B of content plus tool id and name. Most messages are under 300 B.

The new module `history.c` stores records back to back in one static 8 KB ring:

- role, flags, an optional tool id and name, and content of up to 1023 B;
- writing a new record drops the oldest records until it fits;
- turns stay capped at `MAX_HISTORY_TURNS`, which keeps the request size bounded.

`json_build_request` walks the ring through an iterator instead of indexing an array. The orphaned tool_result rule stays.

Rollback uses a sequence number, not an array index. This fixes today's bug: once the history is full, a failed turn's user message is never rolled back.

This is a pure module with host tests: wrap-around, eviction, the rollback after eviction, the iterator order, and the 801-character reply that test_agent.c expects to survive.

**Why not the SD card for the history.** The SD would save another ~8 KB and keep the conversation across reboots. But the agent must work without a card, so it would need a second, RAM-only implementation as fallback, plus file truncation for rollback and handling for a card pulled out mid-session. For 8 KB that complexity works against the maintainability goal. If persistence across reboots becomes wanted, it can be added later behind the same `history.c` interface.

### B2. LLM response buffer on the heap

`llm_request` receives into a buffer that starts at 4 KB and doubles up to 16 KB, today's maximum. Agent.c frees it right after `json_parse_response`, because the parser copies the strings it keeps. The usual reply is 600–800 B, so a 4 KB block is enough almost always.

An overflow beyond 16 KB fails as it does today.

### B3. Other on-demand buffers

`web_search` HTML (6 KB) and the composed system prompt (2.8 KB) are malloc'd for the duration of the call or the request build, then freed. On allocation failure the tool returns its usual error text, and the agent turn fails the way an LLM error does today.

### B4. read_web page on the SD card

`read_web` streams the reader's text into `/sdcard/web/page.txt` and the URL into `/sdcard/web/url.txt`. Pages are served by seeking to the requested offset. Because nothing is held in RAM, the cap rises from 4 KB to 64 KB, so whole articles become readable page by page.

Without a card, every call fetches again into a temporary heap buffer, capped at 4 KB as today, and then frees it. Paging still works, just slower.

SD access goes through `sd_card_begin/end`, like the task log.

### B5. Cron pending fires

`check_entries` copies each due entry (276 B) into a static array so it can fire them after releasing the mutex. It will record only the entry index and the action's generation counter. Then it re-reads the action under the mutex at fire time, and skips the entry if it was deleted or changed meanwhile.

### B6. Configuration: TLS and FAT buffers

These go in the tracked `sdkconfig.defaults` and are verified on the board:

- `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`, plus `..._DYNAMIC_FREE_CONFIG_DATA` and `..._DYNAMIC_FREE_CA_CERT`. TLS allocates its 16 KB in-buffer and 4 KB out-buffer only while data moves, which lowers the peak of every HTTPS request: LLM, STT, reader, dashboards, OTA.
- `CONFIG_FATFS_SECTOR_512` instead of 4096. There is no flash FAT partition, so the per-mount and per-open-file windows drop from 4 KB to 512 B. The SD card uses 512 B sectors anyway.

If a setting breaks TLS with the proxy or the SD card on the board, it is reverted and noted in the plan's ledger. The rest of the work does not depend on it.

## 5. Order of work

1. **A1–A8, the removals.** One commit per feature, each with a clean build and green host tests. Removals first, so the RAM work touches less code.
2. **B1–B5, the RAM changes.** Test-first for the pure modules: history ring, growable buffer, cron pending ids.
3. **B6, the configuration.**
4. **Board verification.** `/diag` at idle, then the full feature checklist of §1, with the heap minimum written down after each step. Done with the user watching the screen.

## 6. Testing

- **Host tests.** `scripts/test.sh host` stays green after every commit. New tests:
  - `history` ring: wrap, eviction, rollback by sequence and iterator, the long reply;
  - growable response buffer: growth, the cap and freeing;
  - cron pending fires: an entry deleted between selection and fire is skipped.
- **Build.** `idf.py build` for esp32c6 after every commit, with no warnings in the touched files.
- **STT server.** `python3 -m unittest` in `server/stt` (untouched by this work).
- **Board checklist.** The §1 success list, ending with the RAM numbers.

## 7. Risks

- **Large test rewrites.** About 20 mock LLM replies in test_agent.c are in Anthropic format and must become OpenAI format, and the Telegram queue disappears from about 36 test calls. Mitigation: do it in A1/A2 with the build and tests green before moving on.
- **Persona wording.** If the board stored a persona other than neutral, the prompt loses that extra tone line. Fidel's personality in `SYSTEM_PROMPT` is unaffected.
- **mbedTLS dynamic buffers.** These add allocation churn during TLS and could fragment the heap differently. B6 is separate and reversible.
- **Scripts used by the user.** The provision and flash scripts are pruned, not removed. Their Python tests must stay green.

## 8. Out of scope

- Keeping the history on the SD card or across reboots (see B1).
- Moving the fallback WiFi and the Vendel key to NVS (`TODO.md`).
- Playwright for read_web.
- The 60 Hz hum and the false-trigger margin of the clap detector.
