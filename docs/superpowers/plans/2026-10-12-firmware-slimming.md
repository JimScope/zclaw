# Firmware Slimming Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the features Fidel does not use (Telegram, other LLM backends, personas, I2C/DHT, emulator, other boards, web relay, upstream docs site). Then cut the firmware's static RAM so the idle heap goes from ~145 KB to at least 200 KB.

**Architecture:** First come eight removal tasks, one commit each, each leaving a clean build and green tests. Then five RAM tasks:

- a packed history module;
- a growable LLM response buffer;
- on-demand buffers;
- read_web pages on the SD card;
- cron copies made only when something fires.

After those come a configuration task (mbedTLS dynamic buffers, FAT 512 B sectors) and a board verification task with the user.

**Tech Stack:** C99, ESP-IDF v5.5.5 (esp32c6), FreeRTOS, cJSON, FATFS on SD, host tests with gcc and the mocks in `test/host`, Python unittest for scripts.

**Spec:** `docs/superpowers/specs/2026-10-12-firmware-slimming-design.md`

## Global Constraints

- **Target and build.** The only target is esp32c6. Build with `source ~/esp/esp-idf/export.sh && idf.py build` from the repo root. Flashing and board checks happen only in Task 15, with the user.
- **Host tests.** Run `./scripts/test.sh host`. It compiles and runs `test/host/build/test_runner` and the llm/ratelimit runners, then runs Python unittest in `test/host`. Task 0 records the baseline. From Task 4 on, the C runner must print `All tests passed!`.
- **STT server tests.** Run `cd server/stt && python3 -m unittest test_adpcm.py test_server.py test_asr.py`. They must stay green, though this plan does not touch them.
- **English names.** Identifiers, file names and paths are in English. Code comments match the density and style of the surrounding code.
- **Commits.** One commit per task, with a message in the repo's style (`fix(...)`, `feat(...)`, `refactor(...)`, `docs:`, `chore:`). Every message ends with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  ```
- **Secrets.** Never commit `sdkconfig` or `sdkconfig.defaults.esp32c6`: both are git-ignored and hold secrets. Never print their values.
- **Keep:**
  - the user tools (`create_tool`, `list_user_tools`, `delete_user_tool`);
  - `sms_send` (Vendel);
  - ratelimit, cron, and the GPIO/LED tools;
  - `sdkconfig.secure`;
  - the provision, install and flash scripts (pruned, not deleted).
- **History.** The conversation history stays in RAM (user decision). Do not put it on the SD card.
- **LLM format.** The board sends `max_completion_tokens` and `chat_template_kwargs.enable_thinking=false` today. Both stay.

## Review Focus

1. **A long tool round evicts the turn's own records, then the LLM fails.** The rollback must still remove everything the failed turn added. Today it does nothing once the history is full. Pinned by `rollback_after_eviction_drops_the_partial_turn` in Task 9.
2. **A reply over 4 KB.** A tool call with long arguments, or an HTTP error body, must grow the buffer rather than come back truncated, up to today's 16 KB. Pinned by `grows_past_initial_capacity` and `refuses_beyond_max` in Task 10.
3. **read_web with no SD card, or the card pulled out.** Paging must still return the right slice and the end-of-content message. Pinned by the `web_page` slice tests in Task 12, plus a board check in Task 15.
4. **A keyless OpenAI setup.** With an empty API key, `llm_init` succeeds and requests go out without an `Authorization` header. Pinned in Task 2 (`init_without_key_is_allowed` in test_llm_runtime.c).
5. **Several cron entries due in the same check.** All fire, in table order, and one-shots still clear. cron.c has no host tests, so this is a board check in Task 15.

---

### Task 0: Baseline

**Files:** none.

- [ ] **Step 1: Record the baseline**

```bash
cd /Users/jianpedi/Projects/zclaw
git status --short            # must be clean
./scripts/test.sh host 2>&1 | tail -30
./test/host/build/test_runner 2>/dev/null | grep -E 'FAIL|test\(s\) failed|All tests passed'
(cd test/host && python3 -m unittest -q test_benchmark_latency.py test_qemu_live_llm_bridge.py \
  test_web_relay.py test_install_provision_scripts.py test_api_provider_harness.py test_face_assets.py 2>&1 | tail -5)
source ~/esp/esp-idf/export.sh >/dev/null && idf.py build 2>&1 | grep -E 'warning:|error:|Project build complete'
```

Expected:

- The C runner has one failure, `required_core_tools_exist`, already on the branch.
- `scripts/test.sh` stops at that failure (`set -e`), so the Python suites are run by hand above.
- `test_face_assets.py` may need Pillow; skip it when Pillow is missing.

Write down each Python suite's result in the task ledger.

- [ ] **Step 2: Note the RAM figures to beat**

```bash
riscv32-esp-elf-nm -S --size-sort build/zclaw.elf | python3 -c "
import sys
for l in sys.stdin:
    p=l.split()
    if len(p)==4 and p[2] in 'bBdD' and int(p[1],16)>=1000: print(int(p[1],16),p[3])" | sort -n -r | head -12
idf.py size 2>&1 | grep -E 'DIRAM|\.bss'
```

Ledger the libmain `.bss` and the top symbols. At the start these are `s_history` 27312, `s_response_buf` 16384 and `html` 6144.

---

### Task 1: Remove Telegram

**Files:**
- Delete:
  - `main/telegram.c/.h`, `main/telegram_chat_ids.c/.h`, `main/telegram_http_diag.c/.h`, `main/telegram_ota.c/.h`
  - `main/telegram_poll_policy.c/.h`, `main/telegram_targets.c/.h`, `main/telegram_token.c/.h`, `main/telegram_update.c/.h`
  - `scripts/telegram-clear-backlog.sh`
  - `test/host/test_telegram_chat_ids.c`, `test_telegram_http_diag.c`, `test_telegram_poll_policy.c`, `test_telegram_token.c`, `test_telegram_update.c`
- Modify:
  - `main/CMakeLists.txt`, `main/Kconfig.projbuild`, `main/config.h`, `main/messages.h`, `main/nvs_keys.h`, `main/memory_keys.c`
  - `main/main.c`, `main/agent.h`, `main/agent.c`, `main/agent_commands.c`, `main/channel.c`, `main/cron.c`, `main/llm.c`
  - `main/display_remote.c`, `main/tools_ota.c`, `main/tools_export.c`, `main/builtin_tools.def`, `main/ota.c/.h`
  - `scripts/test.sh`, `test/host/test_runner.c`, `test/host/test_agent.c`, `test/host/test_runtime_utils.c`, `test/host/test_memory_keys.c`, `test/host/mock_llm.c`
  - `scripts/provision.sh`, `scripts/provision-dev.sh`, `scripts/install.sh`, `scripts/netdiag-summary.py`
  - `test/host/test_install_provision_scripts.py`
  - `.github/workflows/firmware-stack-guard.yml`, `TODO.md`

**Interfaces:**
- Produces:
  - `esp_err_t agent_start(QueueHandle_t input_queue, QueueHandle_t channel_output_queue);` (two arguments);
  - `void agent_test_set_queues(QueueHandle_t channel_output_queue);`
  - `channel_msg_t` without `chat_id`;
  - `#define LLM_HTTP_GATE_WAIT_MS 31000` in config.h.
- Message sources keep their numbers: `MSG_SOURCE_CHANNEL = 0`, `MSG_SOURCE_CRON = 2`, `MSG_SOURCE_VOICE = 3`. Write the values explicitly once TELEGRAM (1) is gone.

- [ ] **Step 1: Delete the Telegram files**

```bash
git rm -q main/telegram.c main/telegram.h main/telegram_chat_ids.c main/telegram_chat_ids.h \
  main/telegram_http_diag.c main/telegram_http_diag.h main/telegram_ota.c main/telegram_ota.h \
  main/telegram_poll_policy.c main/telegram_poll_policy.h main/telegram_targets.c main/telegram_targets.h \
  main/telegram_token.c main/telegram_token.h main/telegram_update.c main/telegram_update.h \
  scripts/telegram-clear-backlog.sh \
  test/host/test_telegram_chat_ids.c test/host/test_telegram_http_diag.c test/host/test_telegram_poll_policy.c \
  test/host/test_telegram_token.c test/host/test_telegram_update.c
```

- [ ] **Step 2: Remove the build and config entries**

- **`main/CMakeLists.txt`:** remove every `telegram*.c` source line.
- **`main/Kconfig.projbuild`:** remove `config ZCLAW_TELEGRAM_ENABLE` and `config ZCLAW_STUB_TELEGRAM` with their help text. In the `ZCLAW_DASH_BASE_URL` help, drop the sentence about the long-poll.
- **`main/config.h`:**
  - Remove every `TELEGRAM_*` define, including `TELEGRAM_POLL_TIMEOUT_OPENROUTER` and the backlog and target constants.
  - Move `START_COMMAND_COOLDOWN_MS` and `MESSAGE_REPLAY_COOLDOWN_MS` into the Agent section, unchanged.
  - Add `#define LLM_HTTP_GATE_WAIT_MS 31000  // max wait for the shared HTTP gate before an LLM call` to the LLM section.
- **`main/messages.h`:**
  - Delete `MSG_SOURCE_TELEGRAM`, the `chat_id` field of `channel_msg_t` and `telegram_msg_t`.
  - Make the enum read `MSG_SOURCE_CHANNEL = 0, MSG_SOURCE_CRON = 2, MSG_SOURCE_VOICE = 3`.
- **`main/nvs_keys.h` and `main/memory_keys.c`:** remove the Telegram token and chat-id keys from the definitions and the sensitive-key list.

- [ ] **Step 3: Remove Telegram from the runtime**

- **`main/main.c`:**
  - Remove the telegram include and the Telegram output queue creation and its startup and shutdown blocks.
  - Call `agent_start(input_queue, channel_output_queue)`.
  - Keep the emulator branch for now; Task 5 removes it.
- **`main/agent.h`:**
  - `agent_start(QueueHandle_t input_queue, QueueHandle_t channel_output_queue)`;
  - `agent_test_set_queues(QueueHandle_t channel_output_queue)`;
  - delete `agent_test_process_message_for_chat`.
- **`main/agent.c`:**
  - Remove the telegram include, `s_telegram_output_queue`, the Telegram send helpers and every `telegram_pause_polling()`/`telegram_resume_polling()` call with the `telegram_polling_paused` flag.
  - Drop the `chat_id`/`reply_chat_id` parameters through `send_response` and the `handle_*` helpers. A reply goes to the channel queue, the display and the task log exactly as it does today for non-Telegram sources.
  - Keep the `source != MSG_SOURCE_CHANNEL` guard on local admin commands.
- **`main/agent_commands.c`:** remove the `/cmd@botname` suffix stripping (Telegram group syntax).
- **`main/channel.c` and `main/cron.c`:** delete the `msg.chat_id = 0;` lines.
- **`main/llm.c`:**
  - Remove the `telegram_poll_policy.h` include.
  - Replace the gate wait computed from `telegram_poll_timeout_for_backend(...) + 1000` with `pdMS_TO_TICKS(LLM_HTTP_GATE_WAIT_MS)`.
- **`main/display_remote.c`:** remove the Telegram include and the long-poll comment and wait logic; keep the plain `http_gate_acquire` path.
- **`main/tools_ota.c`:** remove the Telegram include and the "ack before restart" Telegram send. The serial and display reply stays.
- **`main/tools_export.c`:**
  - Delete the Telegram document upload (`export_to_telegram` and its helpers) and the header comment about it.
  - The handler becomes `return export_to_sd(result, result_len);`.
  - Change the no-card message to `Error: no SD card mounted, cannot export memories`.
- **`main/builtin_tools.def`:** set the `memory_export` description to `"Write all stored memories to a dated text file on the SD card."`.
- **`main/ota.c/.h`:** delete `ota_apply_from_url`, whose only caller was telegram_ota.c.

- [ ] **Step 4: Update the host tests**

- **`scripts/test.sh` and `test/host/test_runner.c`:** remove the 5 deleted test files and their `test_*_all()` externs and calls. Also remove the `../../main/telegram_*.c` sources from the gcc line.
- **`test/host/test_agent.c`:**
  - Delete the Telegram queue setup, the helpers that read it, and the tests that assert Telegram delivery. These are the 4 tests that use `agent_test_process_message_for_chat`; remove their RUN entries too.
  - Change every `agent_test_set_queues(channel_q, telegram_q)` and `agent_test_set_queues(channel_q, NULL)` to `agent_test_set_queues(channel_q)`. Tests that asserted on the Telegram queue assert on `channel_q`.
  - Size response buffers with `CHANNEL_TX_BUF_SIZE` instead of `TELEGRAM_MAX_MSG_LEN`.
  - In `local_admin_commands_are_local_only`, use `agent_test_process_voice_message` to prove a non-channel source cannot run local admin commands.
- **`test/host/test_runtime_utils.c`:** delete the asserts that use `telegram_msg_t` and `TELEGRAM_*`.
- **`test/host/test_memory_keys.c`:** delete the asserts on the Telegram keys.
- **`test/host/mock_llm.c`:** nothing Telegram-specific. `llm_is_stub_mode` goes in Task 5.

- [ ] **Step 5: Prune the scripts and CI**

- **`scripts/provision.sh` and `scripts/provision-dev.sh`:** remove the Telegram token and chat-id flags, prompts, validation, NVS CSV lines and usage text.
- **`scripts/install.sh`:** remove the Telegram hint line.
- **`scripts/netdiag-summary.py`:** remove the Telegram op names from the summary.
- **`test/host/test_install_provision_scripts.py`:** delete the tests and asserts about Telegram flags and CSV lines. Keep the rest.
- **`.github/workflows/firmware-stack-guard.yml`:** remove the `main/telegram.c` guard entry.
- **`TODO.md`:** change "cada OTA por Telegram deja una copia del binario en sus servidores" to "el .bin viaja por la red en cada OTA".

- [ ] **Step 6: Build, test, check for leftovers**

```bash
source ~/esp/esp-idf/export.sh >/dev/null && idf.py build 2>&1 | grep -E 'warning:|error:|Project build complete'
./scripts/test.sh host; ./test/host/build/test_runner 2>/dev/null | grep -E 'FAIL|test\(s\) failed|All tests passed'
grep -rniE 'telegram|chat_id|tg_' main/ test/host/*.c test/host/*.h scripts/*.sh | grep -v '^Binary'
```

Expected:

- The build completes with no warnings in the touched files.
- The C runner fails only on `required_core_tools_exist`.
- The grep prints nothing.

Also edit the git-ignored `sdkconfig.defaults.esp32c6` and delete its `CONFIG_ZCLAW_TELEGRAM_ENABLE=n` line. Do not commit that file.

- [ ] **Step 7: Commit**

```bash
git add -A main scripts test .github TODO.md
git commit -m "refactor: remove Telegram support

Telegram was already disabled on the board. memory_export writes to the SD card,
OTA goes through the web page or the ota_update tool, and the LLM HTTP gate wait
is a constant (31 s) instead of the Telegram long-poll window.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Keep only the OpenAI-compatible LLM backend

**Files:**
- Modify:
  - `main/config.h`, `main/llm.c`, `main/llm.h`, `main/json_util.c`, `main/Kconfig.projbuild`
  - `test/host/mock_llm.c/.h`, `test/host/test_agent.c`, `test/host/test_json_util_integration.c`, `test/host/test_llm_runtime.c`
  - `scripts/provision.sh`, `scripts/provision-dev.sh`, `test/host/test_install_provision_scripts.py`
- Delete: `test/api/test_anthropic.py`, `test/api/test_openrouter.py`. Prune `test/api/provider_harness.py` and `scripts/test-api.sh` down to the OpenAI provider.

**Interfaces:**
- Produces:
  - `llm.h` without `llm_get_backend` or `llm_is_openai_format`;
  - `void mock_llm_set_model(const char *model);`, which replaces `mock_llm_set_backend`;
  - the mock default reply in OpenAI format: `{"choices":[{"message":{"role":"assistant","content":"mock ok"},"finish_reason":"stop"}]}`.

- [ ] **Step 1: Write the failing keyless-init test**

In `test/host/test_llm_runtime.c`, add the test below and register it in the file's runner:

```c
TEST(init_without_key_is_allowed)
{
    mock_memory_reset();
    ASSERT(llm_init() == ESP_OK);
    ASSERT(strcmp(llm_get_api_url(), LLM_API_URL_OPENAI) == 0);
    return 0;
}
```

Delete the tests `loads_anthropic_*`, `loads_openrouter_*`, `loads_ollama_*` and `unknown_backend_*`. Rewrite `reinit_without_key_clears_previous_api_key_state` with an unset key instead of `"ollama"`. Remove every `llm_get_backend()` assert.

Run `./scripts/test.sh host`. Expected: compile error or failure while the backend code still exists.

- [ ] **Step 2: Reduce llm.c and config.h to one backend**

- **`config.h`:**
  - Delete `llm_backend_t` and the Anthropic, OpenRouter and Ollama URL and model macros.
  - Keep `LLM_API_URL_OPENAI`, `LLM_DEFAULT_MODEL_OPENAI` and `LLM_FALLBACK_MODEL`.
- **`llm.c`:**
  - Delete `s_backend`, `llm_backend_requires_api_key`, `llm_backend_name`, the `llm_backend` NVS parsing, the `CONFIG_ZCLAW_CLAUDE_API_KEY` block, the Ollama warning, and the Anthropic and OpenRouter headers.
  - `llm_get_api_url()` returns the `llm_api_url` NVS override or `LLM_API_URL_OPENAI`. `llm_get_default_model()` returns `LLM_DEFAULT_MODEL_OPENAI`.
  - Send `Authorization: Bearer <key>` only when a key is set. A missing key is not an error: the proxy adds the real key.
  - Log line: `ESP_LOGI(TAG, "Model: %s", s_model);`.
- **`llm.h`:** remove the two backend declarations.
- **`Kconfig.projbuild`:** remove `ZCLAW_CLAUDE_API_KEY`.
- **NVS key `llm_backend`:** may stay in the sensitive list, but nothing reads it.

- [ ] **Step 3: One request format in json_util.c**

- Delete `build_anthropic_request` and `parse_anthropic_response` and the backend dispatch. `json_build_request` calls `build_openai_request`, and `json_parse_response` calls the OpenAI parser.
- `add_token_limit_field` always adds `max_completion_tokens` (`LLM_MAX_TOKENS`), which is what the board sends today.

- [ ] **Step 4: Convert the host mocks and tests**

- **`mock_llm.c/.h`:**
  - Replace `mock_llm_set_backend(...)` with `mock_llm_set_model(const char *model)`.
  - Delete the `llm_get_backend`/`llm_is_openai_format` stubs.
  - Set the default reply to the OpenAI JSON above.
- **`test_agent.c`:** convert every Anthropic-format mock reply to OpenAI format:
  - **text:** `{"choices":[{"message":{"role":"assistant","content":"<text>"},"finish_reason":"stop"}]}`;
  - **tool call:** `{"choices":[{"message":{"role":"assistant","content":null,"tool_calls":[{"id":"call_1","type":"function","function":{"name":"<tool>","arguments":"{...escaped...}"}}]},"finish_reason":"tool_calls"}]}`.
  - Keep each test's assertions on the request and reply text.
- **`test_json_util_integration.c`:** delete the Anthropic and OpenRouter build/parse tests and their RUN lines, and change `mock_llm_set_backend` calls to `mock_llm_set_model`.

- [ ] **Step 5: Prune provisioning**

- **`provision.sh` and `provision-dev.sh`:**
  - Remove `--backend` and the backend menus, the Anthropic/OpenRouter/Ollama key checks and URL handling.
  - Drop the `llm_backend` CSV line.
  - Keep `--api-key`, `--model` and `--api-url`.
- **`test_install_provision_scripts.py`:** delete the backend-specific tests and asserts.
- **`test/api/`:** delete the Anthropic and OpenRouter tests. Prune `provider_harness.py` and `scripts/test-api.sh` to the OpenAI provider.

- [ ] **Step 6: Build, test, grep**

```bash
idf.py build 2>&1 | grep -E 'warning:|error:|Project build complete'
./scripts/test.sh host; ./test/host/build/test_runner 2>/dev/null | grep -E 'FAIL|test\(s\) failed|All tests passed'
grep -rniE 'anthropic|openrouter|ollama|claude_api|llm_backend_t|LLM_BACKEND_' main/ test/host/*.c test/host/*.h scripts/*.sh
```

Expected:

- The build is clean.
- The only C failure is `required_core_tools_exist`, and `init_without_key_is_allowed` passes.
- The grep finds only the `NVS_KEY_LLM_BACKEND` definition and its sensitive-list entry, if kept.

The string "Claude" may still appear in user-facing text such as "(No response from Claude)". Change that one to `"(Sin respuesta del modelo)"`.

- [ ] **Step 7: Commit**

`refactor(llm): keep only the OpenAI-compatible backend`. The body says the board stores `llm_backend=openai` and that keyless setups still work.

---

### Task 3: Remove personas

**Files:**
- Delete: `main/tools_persona.c`
- Modify:
  - `main/CMakeLists.txt`, `main/builtin_tools.def`, `main/tools_handlers.h`, `main/nvs_keys.h`
  - `main/agent_prompt.c/.h`, `main/agent.c`, `main/config.h`
  - `test/host/test_agent.c`, `test/host/test_agent_prompt.c`

**Interfaces:**
- Produces: `const char *agent_build_system_prompt(char *buf, size_t buf_len);`, where the persona argument is gone and `AGENT_SYSTEM_PROMPT_BUF_SIZE` stays.

- [ ] **Step 1: Update the prompt test first**

Replace the body of `full_prompt_fits_agent_buffer_with_display` in `test/host/test_agent_prompt.c`:

```c
TEST(full_prompt_fits_agent_buffer_with_display)
{
    static char buf[AGENT_SYSTEM_PROMPT_BUF_SIZE];

    mock_display_reset();
    mock_display_set_running(true);
    const char *prompt = agent_build_system_prompt(buf, sizeof(buf));
    ASSERT(prompt == buf);
    ASSERT(strstr(prompt, "Device target is") != NULL);
    ASSERT(strstr(prompt, "Keep responses short") != NULL);
    ASSERT(strstr(prompt, "display_image") != NULL);
    ASSERT(strstr(prompt, "Persona") == NULL);
    mock_display_reset();
    return 0;
}
```

Run the host tests. Expected: compile error, because the signature still has the persona argument.

- [ ] **Step 2: Remove personas from the code**

- `git rm main/tools_persona.c`, and drop it from `main/CMakeLists.txt`.
- **`builtin_tools.def`:** delete the `#if 0` block with `set_persona`, `get_persona` and `reset_persona`.
- **`tools_handlers.h`:** delete the three persona handler declarations.
- **`nvs_keys.h`:** delete `NVS_KEY_PERSONA`. If `memory_keys.c` lists it, delete it there too.
- **`agent_prompt.h`:** delete `agent_persona_t`, `agent_persona_name` and `agent_parse_persona_name`, and change the build function to `(char *buf, size_t buf_len)`.
- **`agent_prompt.c`:**
  - Delete `persona_instruction` and the persona name and parse functions.
  - Remove the sentence `"Persona mode is '%s'. Persona affects wording only ... %s "` and its two arguments from the `snprintf`.
  - Change the overflow log to `"System prompt composition overflow, using base system prompt"`.
- **`agent.c`:**
  - Delete `s_persona`, `s_test_persona_value`, `persona_store_get`, `load_persona_from_store`, the persona tool hook and its calls.
  - Remove the persona line from `/help` and `/settings`.
  - Call `agent_build_system_prompt(s_system_prompt_buf, sizeof(s_system_prompt_buf))`.
- **`config.h`:** in `SYSTEM_PROMPT`, remove the word "personas," from the capability list. Keep everything about Fidel.
- **`test_agent.c`:** delete the persona tests and their RUN lines.

- [ ] **Step 3: Build, test, grep**

```bash
idf.py build 2>&1 | grep -E 'warning:|error:|Project build complete'
./scripts/test.sh host; ./test/host/build/test_runner 2>/dev/null | grep -E 'FAIL|test\(s\) failed|All tests passed'
grep -rni 'persona' main/ test/host/*.c test/host/*.h
```

Expected:

- The build is clean.
- The prompt test passes, and the only C failure is still `required_core_tools_exist`.
- The grep is empty.

The composed prompt is now shorter, but keep `AGENT_SYSTEM_PROMPT_BUF_SIZE` at 2816: the test proves it fits, and Task 11 makes it heap-allocated anyway.

- [ ] **Step 4: Commit**

`refactor(agent): remove personas; Fidel's voice lives in SYSTEM_PROMPT`.

---

### Task 4: Remove the I2C and DHT tools

**Files:**
- Delete: `main/tools_i2c.c`, `main/tools_dht.c`, `test/host/test_tools_i2c_policy.c`, `test/host/test_tools_dht.c`, `test/host/mock_i2c.c`, `test/host/driver/i2c.h`
- Modify:
  - `main/CMakeLists.txt`, `main/builtin_tools.def`, `main/tools_handlers.h`
  - `scripts/test.sh`, `test/host/test_runner.c`, `test/host/test_builtin_tools_registry.c`
  - `test/api/provider_harness.py`, which mentions i2c tools

- [ ] **Step 1: Fix the required-tools list (test first)**

In `test/host/test_builtin_tools_registry.c`, remove `"i2c_write"`, `"i2c_read"`, `"i2c_write_read"` and `"dht_read"` from `required[]`. The test fails today for those names, so this edit makes it pass with no code change. Then run the host tests. Expected: the C runner prints `All tests passed!`.

- [ ] **Step 2: Delete the dead code**

```bash
git rm -q main/tools_i2c.c main/tools_dht.c test/host/test_tools_i2c_policy.c test/host/test_tools_dht.c \
  test/host/mock_i2c.c test/host/driver/i2c.h
```

- **`main/CMakeLists.txt`:** drop the two sources.
- **`builtin_tools.def`:** delete the `#if 0` I2C/DHT block, including its comment.
- **`tools_handlers.h`:** delete the I2C and DHT handler declarations and the `TEST_BUILD` block that holds only DHT helpers.
- **`scripts/test.sh` and `test_runner.c`:** remove `test_tools_i2c_policy.c`, `test_tools_dht.c`, `mock_i2c.c`, `../../main/tools_i2c.c` and `../../main/tools_dht.c`, and their `_all()` externs and calls.
- **`test/api/provider_harness.py`:** drop the i2c tool references.

- [ ] **Step 3: Build, test, grep**

```bash
idf.py build 2>&1 | grep -E 'warning:|error:|Project build complete'
./scripts/test.sh host && ./test/host/build/test_runner 2>/dev/null | grep -E 'FAIL|All tests passed'
grep -rniE 'i2c|dht' main/ test/host/*.c test/host/*.h scripts/test.sh
```

Expected:

- The build is clean.
- `All tests passed!`, and `scripts/test.sh host` now reaches the Python suites.
- The grep is empty.

- [ ] **Step 4: Commit**

`refactor(tools): remove the I2C and DHT tools (no such hardware on the board)`.

---

### Task 5: Remove the emulator, the stub LLM and the UART channel

**Files:**
- Delete: `scripts/emulate.sh`, `scripts/exit-emulator.sh`, `scripts/qemu_live_llm_bridge.py`, `sdkconfig.qemu.defaults`, `sdkconfig.test`, `test/host/test_qemu_live_llm_bridge.py`
- Modify:
  - `main/Kconfig.projbuild`, `main/main.c`, `main/channel.c/.h`, `main/llm.c/.h`
  - `scripts/test.sh`, `test/host/test_llm_runtime.c`, `test/host/mock_llm.c`
  - `scripts/install.sh`, `scripts/bootstrap.sh`, `scripts/clean.sh`, `.gitignore`
  - `test/host/test_install_provision_scripts.py`

**Interfaces:**
- Produces: in `llm.c`, the transport is compiled only when `TEST_BUILD` is not defined. Under `TEST_BUILD`, `llm_request` returns a canned OpenAI-format reply, and `bool llm_test_has_api_key(void);` exists for the tests. `llm_is_stub_mode` no longer exists.

- [ ] **Step 1: Point the llm runtime test at TEST_BUILD (test first)**

- In `scripts/test.sh`, remove `-DCONFIG_ZCLAW_STUB_LLM=1 -DCONFIG_ZCLAW_EMULATOR_LIVE_LLM=0` from the `test_llm_runtime_runner` gcc line. `-DTEST_BUILD` stays.
- In `test/host/test_llm_runtime.c`:
  - change `stub_request_returns_response` to expect `"choices"` and `"gpio_write"` in the canned reply;
  - change `llm_stub_has_api_key_for_test()` calls to `llm_test_has_api_key()`.

Run the host tests. Expected: the llm runtime runner fails to compile until Step 2.

- [ ] **Step 2: Gate llm.c on TEST_BUILD**

In `main/llm.c`:

- Replace each `#if CONFIG_ZCLAW_STUB_LLM` / `#if !CONFIG_ZCLAW_STUB_LLM` with `#ifdef TEST_BUILD` / `#ifndef TEST_BUILD`.
- Delete the `CONFIG_ZCLAW_EMULATOR_LIVE_LLM` bridge branch, including the `channel_llm_bridge_exchange` call.
- Rename `llm_stub_has_api_key_for_test` to `llm_test_has_api_key`.
- Delete `llm_is_stub_mode`.
- The canned reply becomes:

```c
static const char *test_canned_reply(const char *request_json)
{
    if (strstr(request_json, "gpio")) {
        return "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":null,"
               "\"tool_calls\":[{\"id\":\"call_test\",\"type\":\"function\",\"function\":"
               "{\"name\":\"gpio_write\",\"arguments\":\"{\\\"pin\\\":2,\\\"state\\\":1}\"}}]},"
               "\"finish_reason\":\"tool_calls\"}]}";
    }
    return "{\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"test ok\"},"
           "\"finish_reason\":\"stop\"}]}";
}
```

In `main/llm.h`, delete `llm_is_stub_mode` and the bridge declaration. In `test/host/mock_llm.c`, delete `llm_is_stub_mode`.

- [ ] **Step 3: Remove the emulator and the UART channel**

- **`main/Kconfig.projbuild`:** delete `ZCLAW_STUB_LLM`, `ZCLAW_CHANNEL_UART`, `ZCLAW_EMULATOR_MODE` and `ZCLAW_EMULATOR_LIVE_LLM`.
- **`main/main.c`:** make the normal startup path unconditional and delete the emulator branch and its `#endif`.
- **`main/channel.c/.h`:** delete the UART implementation, the emulator line handling and the LLM bridge (`channel_llm_bridge_exchange`, `s_llm_bridge_payload`). The USB Serial/JTAG path is the only one.
- **Files:**

```bash
git rm -q scripts/emulate.sh scripts/exit-emulator.sh scripts/qemu_live_llm_bridge.py \
  sdkconfig.qemu.defaults sdkconfig.test test/host/test_qemu_live_llm_bridge.py
```

- **`scripts/test.sh`:**
  - Remove `test_qemu_live_llm_bridge.py` from the Python list.
  - Remove the `run_device_tests` function and its `device` and `all` callers that build with `sdkconfig.test`. `host` stays the default.
- **`scripts/install.sh`, `scripts/bootstrap.sh` and `scripts/clean.sh`:** remove the QEMU install options, packages and messages.
- **`.gitignore`:** remove the QEMU-only entries.
- **`test/host/test_install_provision_scripts.py`:** delete the QEMU tests and the `CHANNEL_UART` assert. Rework the two tests that use `--qemu` to check only the cJSON prerequisite.

- [ ] **Step 4: Build, test, grep**

```bash
idf.py build 2>&1 | grep -E 'warning:|error:|Project build complete'
./scripts/test.sh host && ./test/host/build/test_runner 2>/dev/null | grep -E 'FAIL|All tests passed'
grep -rniE 'qemu|emulator|STUB_LLM|CHANNEL_UART|llm_bridge|stub_mode' main/ test/host/*.c test/host/*.h scripts/ .gitignore
```

Expected:

- The build is clean.
- All C runners pass, the llm runtime runner included.
- The Python suites pass.
- The grep is empty.

- [ ] **Step 5: Commit**

`refactor: remove the QEMU emulator, the stub LLM and the UART channel`.

---

### Task 6: Remove the other board presets, the web relay and the benchmark

**Files:**
- Delete:
  - `sdkconfig.esp32-t-relay.defaults`, `sdkconfig.esp32s3-box-3.defaults`
  - `scripts/web_relay.py`, `scripts/web-relay.sh`, `scripts/bootstrap-web-relay.sh`, `scripts/requirements-web-relay.txt`
  - `scripts/benchmark.sh`, `scripts/benchmark_latency.py`
  - `test/host/test_web_relay.py`, `test/host/test_benchmark_latency.py`
- Modify:
  - `scripts/build.sh`, `scripts/flash.sh`, `scripts/flash-secure.sh`, `scripts/provision.sh`, `scripts/test.sh`
  - `main/config.h`, for the relay comment on `CHANNEL_TX_BUF_SIZE`
  - `test/host/test_install_provision_scripts.py`
  - `.github/workflows/firmware-target-matrix.yml`, `.github/workflows/release.yml`

- [ ] **Step 1: Delete the files**

```bash
git rm -q sdkconfig.esp32-t-relay.defaults sdkconfig.esp32s3-box-3.defaults \
  scripts/web_relay.py scripts/web-relay.sh scripts/bootstrap-web-relay.sh scripts/requirements-web-relay.txt \
  scripts/benchmark.sh scripts/benchmark_latency.py test/host/test_web_relay.py test/host/test_benchmark_latency.py
```

- [ ] **Step 2: Prune the references**

- **`scripts/build.sh`, `flash.sh` and `flash-secure.sh`:**
  - Remove the `--box-3` and `--t-relay` options, their preset merging and their usage text.
  - Keep the generic target checks. In `flash.sh`, only the `IDF_TARGET_OVERRIDE` preset branch goes.
- **`scripts/provision.sh`:** remove the "next step: web relay" text.
- **`scripts/test.sh`:** remove `test_benchmark_latency.py` and `test_web_relay.py` from the Python list.
- **`main/config.h`:** change the `CHANNEL_TX_BUF_SIZE` comment to `// Output response buffer for serial`.
- **`test/host/test_install_provision_scripts.py`:**
  - Delete the box-3 and t-relay preset tests.
  - Rework the esptool-fallback chip-detection test so it runs without `--box-3`, which keeps its coverage.
- **`.github/workflows/firmware-target-matrix.yml`:** reduce the matrix to esp32c6 and delete the box-3 job.
- **`.github/workflows/release.yml`:** delete the relay bootstrap checksum and the relay lines in the release notes.

- [ ] **Step 3: Test and grep**

```bash
./scripts/test.sh host && ./test/host/build/test_runner 2>/dev/null | grep -E 'FAIL|All tests passed'
grep -rniE 't-relay|box-3|box3|web_relay|web-relay|benchmark' scripts/ test/host/ .github/ main/
```

Expected: everything passes, and the grep is empty.

- [ ] **Step 4: Commit**

`chore: drop other board presets, the web relay and the latency benchmark`.

---

### Task 7: Remove the upstream docs site, refresh README and TODO

**Files:**
- Delete: `docs-site/`, `scripts/docs-site.sh`
- Modify: `README.md`, `TODO.md`, and `.github/workflows/*.yml` if any job builds `docs-site`

- [ ] **Step 1: Delete**

```bash
git rm -rq docs-site scripts/docs-site.sh
grep -rn 'docs-site' .github/ scripts/ README.md || true
```

Remove any hit from the grep.

- [ ] **Step 2: README**

- In `README.md`, delete the sections and lines about Telegram, the web relay, QEMU and the emulator, other boards, the Anthropic, OpenRouter and Ollama backends, personas, and the I2C and DHT tools.
- Add this section near the top:

```markdown
## Fidel fork

This branch runs one board: an ESP32-C6 with an ST7789 display, a microSD card and a MAX4466
microphone. It talks to an OpenAI-compatible LLM through a proxy, listens by voice (double clap)
and answers on the screen. Chat over USB serial; update over the web page with a PIN.

Current design documents live in `docs/superpowers/specs/`.
```

- [ ] **Step 3: Check and commit**

```bash
grep -niE 'telegram|relay|qemu|anthropic|openrouter|ollama|persona|i2c|dht' README.md
./scripts/test.sh host && ./test/host/build/test_runner 2>/dev/null | grep -E 'FAIL|All tests passed'
```

Expected: the grep is empty, or hits only words like "relay" in unrelated sense; review each hit.

Commit: `docs: drop the upstream docs site and describe the Fidel fork in the README`.

---

### Task 8: Ledger of the removals' RAM effect

**Files:** none.

- [ ] **Step 1: Measure**

Run the nm and `idf.py size` commands from Task 0, Step 2. Ledger the libmain `.bss` before and after. Expected: `s_send_msg` (4104) is gone, and no Telegram symbols remain.

---

### Task 9: Packed conversation history (`history.c`)

**Files:**
- Create: `main/history.h`, `main/history.c`, `test/host/test_history.c`
- Modify:
  - `main/json_util.h`, where `conversation_msg_t` fields become pointers
  - `main/agent.c`, `main/CMakeLists.txt`
  - `scripts/test.sh`, `test/host/test_runner.c`, `test/host/test_json_util_integration.c`

**Interfaces:**
- Consumes: `MAX_HISTORY_TURNS` (12) and `MAX_MESSAGE_LEN` (1024) from config.h.
- Produces:

```c
// json_util.h
typedef struct {
    const char *role;        // "user" or "assistant"
    const char *content;     // text, tool input JSON or tool result
    bool is_tool_use;
    bool is_tool_result;
    const char *tool_id;     // "" unless a tool message
    const char *tool_name;   // "" unless a tool_use
} conversation_msg_t;

// history.h
#define HISTORY_ARENA_BYTES  8192
#define HISTORY_MAX_RECORDS  (MAX_HISTORY_TURNS * 2)
typedef uint32_t history_mark_t;
void history_clear(void);
void history_add(const char *role, const char *content, bool is_tool_use, bool is_tool_result,
                 const char *tool_id, const char *tool_name);
history_mark_t history_mark(void);
int history_rollback(history_mark_t mark);
int history_view(conversation_msg_t *out, int max);
int history_count(void);
```

- [ ] **Step 1: Write the failing tests**

Create `test/host/test_history.c`:

```c
/*
 * Host tests for the packed conversation history.
 */

#include <stdio.h>
#include <string.h>

#include "history.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

static conversation_msg_t s_view[HISTORY_MAX_RECORDS];

TEST(keeps_order_and_fields)
{
    history_clear();
    history_add("user", "hola", false, false, NULL, NULL);
    history_add("assistant", "{\"pin\":2}", true, false, "call_1", "gpio_write");
    history_add("user", "ok", false, true, "call_1", NULL);
    int n = history_view(s_view, HISTORY_MAX_RECORDS);
    ASSERT(n == 3);
    ASSERT(strcmp(s_view[0].role, "user") == 0 && strcmp(s_view[0].content, "hola") == 0);
    ASSERT(strcmp(s_view[0].tool_id, "") == 0 && strcmp(s_view[0].tool_name, "") == 0);
    ASSERT(s_view[1].is_tool_use && strcmp(s_view[1].role, "assistant") == 0);
    ASSERT(strcmp(s_view[1].tool_id, "call_1") == 0 && strcmp(s_view[1].tool_name, "gpio_write") == 0);
    ASSERT(s_view[2].is_tool_result && strcmp(s_view[2].tool_id, "call_1") == 0);
    return 0;
}

TEST(caps_records_at_max)
{
    char text[16];
    history_clear();
    for (int i = 0; i < HISTORY_MAX_RECORDS + 5; i++) {
        snprintf(text, sizeof(text), "m%d", i);
        history_add("user", text, false, false, NULL, NULL);
    }
    int n = history_view(s_view, HISTORY_MAX_RECORDS);
    ASSERT(n == HISTORY_MAX_RECORDS);
    ASSERT(strcmp(s_view[0].content, "m5") == 0);
    snprintf(text, sizeof(text), "m%d", HISTORY_MAX_RECORDS + 4);
    ASSERT(strcmp(s_view[n - 1].content, text) == 0);
    return 0;
}

TEST(evicts_by_bytes_and_keeps_long_reply)
{
    static char big[MAX_MESSAGE_LEN + 200];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    history_clear();
    for (int i = 0; i < 12; i++) {
        history_add("assistant", big, false, false, NULL, NULL);  // ~1 KB each, arena is 8 KB
    }
    int n = history_view(s_view, HISTORY_MAX_RECORDS);
    ASSERT(n >= 6 && n < 12);
    ASSERT(strlen(s_view[n - 1].content) == MAX_MESSAGE_LEN - 1);  // truncated like before

    char reply[802];
    memset(reply, 'r', 801);
    reply[801] = '\0';
    history_add("assistant", reply, false, false, NULL, NULL);
    n = history_view(s_view, HISTORY_MAX_RECORDS);
    ASSERT(strlen(s_view[n - 1].content) == 801);
    return 0;
}

TEST(rollback_drops_the_turn)
{
    history_clear();
    history_add("user", "a", false, false, NULL, NULL);
    history_add("assistant", "b", false, false, NULL, NULL);
    history_mark_t mark = history_mark();
    history_add("user", "c", false, false, NULL, NULL);
    history_add("assistant", "{}", true, false, "call_9", "get_time");
    ASSERT(history_rollback(mark) == 2);
    ASSERT(history_count() == 2);
    ASSERT(history_rollback(mark) == 0);  // nothing left from that turn
    return 0;
}

TEST(rollback_after_eviction_drops_the_partial_turn)
{
    char text[16];
    history_clear();
    for (int i = 0; i < HISTORY_MAX_RECORDS; i++) {
        snprintf(text, sizeof(text), "old%d", i);
        history_add("user", text, false, false, NULL, NULL);
    }
    history_mark_t mark = history_mark();
    for (int i = 0; i < 6; i++) {  // a long tool round evicts old records and the turn start shifts
        snprintf(text, sizeof(text), "turn%d", i);
        history_add("user", text, false, false, NULL, NULL);
    }
    ASSERT(history_rollback(mark) == 6);
    int n = history_view(s_view, HISTORY_MAX_RECORDS);
    ASSERT(n == HISTORY_MAX_RECORDS - 6);
    for (int i = 0; i < n; i++) {
        ASSERT(strncmp(s_view[i].content, "old", 3) == 0);
    }
    return 0;
}

TEST(clear_empties)
{
    history_add("user", "x", false, false, NULL, NULL);
    history_clear();
    ASSERT(history_count() == 0);
    ASSERT(history_view(s_view, HISTORY_MAX_RECORDS) == 0);
    return 0;
}

int test_history_all(void)
{
    int failures = 0;

    printf("\nHistory Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(keeps_order_and_fields);
    RUN(caps_records_at_max);
    RUN(evicts_by_bytes_and_keeps_long_reply);
    RUN(rollback_drops_the_turn);
    RUN(rollback_after_eviction_drops_the_partial_turn);
    RUN(clear_empties);

#undef RUN
    return failures;
}
```

Register it:

- add `test_history.c` and `../../main/history.c` to the gcc line in `scripts/test.sh`;
- add `extern int test_history_all(void);` and `failures += test_history_all();` to `test_runner.c`.

Create `main/history.h` with the interface above, including `#include <stdbool.h>`, `<stdint.h>`, `"config.h"` and `"json_util.h"`. Change `conversation_msg_t` in `json_util.h` to the pointer form.

- [ ] **Step 2: Run the tests to see them fail**

Run `./scripts/test.sh host`. Expected: link errors for `history_*`.

- [ ] **Step 3: Implement `main/history.c`**

```c
#include "history.h"

#include <string.h>

// Records sit back to back in one arena: header, tool_id, tool_name, content,
// each string NUL-terminated, the record padded to 4 bytes. Adding a record
// drops the oldest ones until it fits.
#define FLAG_ASSISTANT   0x01
#define FLAG_TOOL_USE    0x02
#define FLAG_TOOL_RESULT 0x04
#define TOOL_ID_MAX      64
#define TOOL_NAME_MAX    32

typedef struct {
    uint32_t seq;
    uint16_t size;      // whole record, multiple of 4
    uint8_t flags;
    uint8_t id_len;     // bytes of tool_id including the NUL
    uint8_t name_len;   // bytes of tool_name including the NUL
    uint8_t pad[3];
} history_rec_t;

static uint8_t s_arena[HISTORY_ARENA_BYTES] __attribute__((aligned(4)));
static size_t s_used;
static int s_count;
static uint32_t s_next_seq = 1;

static history_rec_t *rec_at(size_t off)
{
    return (history_rec_t *)(s_arena + off);
}

static size_t capped_len(const char *s, size_t cap)
{
    size_t n = 0;
    if (!s) {
        return 0;
    }
    while (n + 1 < cap && s[n] != '\0') {
        n++;
    }
    return n;
}

static void drop_oldest(void)
{
    size_t first = rec_at(0)->size;
    memmove(s_arena, s_arena + first, s_used - first);
    s_used -= first;
    s_count--;
}

void history_clear(void)
{
    s_used = 0;
    s_count = 0;
}

void history_add(const char *role, const char *content, bool is_tool_use, bool is_tool_result,
                 const char *tool_id, const char *tool_name)
{
    size_t id_n = capped_len(tool_id, TOOL_ID_MAX);
    size_t name_n = capped_len(tool_name, TOOL_NAME_MAX);
    size_t text_n = capped_len(content, MAX_MESSAGE_LEN);
    size_t size = sizeof(history_rec_t) + id_n + 1 + name_n + 1 + text_n + 1;
    size = (size + 3u) & ~(size_t)3u;

    while (s_count > 0 && (s_count >= HISTORY_MAX_RECORDS || s_used + size > HISTORY_ARENA_BYTES)) {
        drop_oldest();
    }

    history_rec_t *rec = rec_at(s_used);
    memset(rec, 0, sizeof(*rec));
    rec->seq = s_next_seq++;
    rec->size = (uint16_t)size;
    rec->flags = (role && strcmp(role, "assistant") == 0 ? FLAG_ASSISTANT : 0) |
                 (is_tool_use ? FLAG_TOOL_USE : 0) | (is_tool_result ? FLAG_TOOL_RESULT : 0);
    rec->id_len = (uint8_t)(id_n + 1);
    rec->name_len = (uint8_t)(name_n + 1);

    char *p = (char *)(rec + 1);
    memcpy(p, tool_id ? tool_id : "", id_n);
    p[id_n] = '\0';
    p += id_n + 1;
    memcpy(p, tool_name ? tool_name : "", name_n);
    p[name_n] = '\0';
    p += name_n + 1;
    memcpy(p, content ? content : "", text_n);
    p[text_n] = '\0';

    s_used += size;
    s_count++;
}

history_mark_t history_mark(void)
{
    return s_next_seq;
}

int history_rollback(history_mark_t mark)
{
    size_t off = 0;
    for (int i = 0; i < s_count; i++) {
        history_rec_t *rec = rec_at(off);
        if (rec->seq >= mark) {
            int dropped = s_count - i;
            s_used = off;
            s_count = i;
            return dropped;
        }
        off += rec->size;
    }
    return 0;
}

int history_view(conversation_msg_t *out, int max)
{
    size_t off = 0;
    int n = 0;
    for (int i = 0; i < s_count && n < max; i++) {
        history_rec_t *rec = rec_at(off);
        const char *id = (const char *)(rec + 1);
        const char *name = id + rec->id_len;
        out[n].role = (rec->flags & FLAG_ASSISTANT) ? "assistant" : "user";
        out[n].is_tool_use = (rec->flags & FLAG_TOOL_USE) != 0;
        out[n].is_tool_result = (rec->flags & FLAG_TOOL_RESULT) != 0;
        out[n].tool_id = id;
        out[n].tool_name = name;
        out[n].content = name + rec->name_len;
        n++;
        off += rec->size;
    }
    return n;
}

int history_count(void)
{
    return s_count;
}
```

Add `"history.c"` to `main/CMakeLists.txt` next to `"agent.c"`.

- [ ] **Step 4: Switch agent.c to the module**

- Delete `s_history`, `s_history_len`, and the local `history_add` and `history_rollback_to`.
- Add `#include "history.h"` and `static conversation_msg_t s_history_view[HISTORY_MAX_RECORDS];`.
- Add the logging wrapper:

```c
static void history_rollback_to(history_mark_t mark, const char *reason)
{
    int dropped = history_rollback(mark);
    if (dropped > 0) {
        ESP_LOGW(TAG, "Rolling back conversation history (%d messages): %s",
                 dropped, reason ? reason : "unknown");
    }
}
```

- Replace `int history_turn_start = s_history_len;` with `history_mark_t history_turn_start = history_mark();`.
- Pass the view to `json_build_request` as `s_history_view, history_view(s_history_view, HISTORY_MAX_RECORDS),`.
- The existing `history_add(...)` call sites keep their arguments.
- In `agent_test_reset`, replace the history `memset` and `s_history_len = 0` with `history_clear();`.

- [ ] **Step 5: Fix the json_util integration test**

In `test/host/test_json_util_integration.c`, the test that builds `conversation_msg_t history[2]` assigns pointers instead of using `strncpy`:

```c
conversation_msg_t history[2] = {
    { .role = "user", .content = "hola", .tool_id = "", .tool_name = "" },
    { .role = "assistant", .content = "hey", .tool_id = "", .tool_name = "" },
};
```

Keep its expected JSON unchanged.

- [ ] **Step 6: Run everything**

```bash
./scripts/test.sh host && ./test/host/build/test_runner 2>/dev/null | grep -E 'History|FAIL|All tests passed'
idf.py build 2>&1 | grep -E 'warning:|error:|Project build complete'
```

Expected: the 6 history tests pass, `All tests passed!`, and the build is clean.

- [ ] **Step 7: Commit**

`refactor(agent): pack the conversation history into an 8 KB arena`. The body notes about 19 KB less static RAM, and that rollback by sequence now also works once the history is full.

---

### Task 10: LLM response buffer on the heap (`grow_buf`)

**Files:**
- Create: `main/grow_buf.h`, `main/grow_buf.c`, `test/host/test_grow_buf.c`
- Modify:
  - `main/config.h`, `main/llm.c/.h`, `main/agent.c`, `main/CMakeLists.txt`
  - `test/host/mock_llm.c`, `scripts/test.sh`, `test/host/test_runner.c`

**Interfaces:**
- Produces:

```c
// grow_buf.h
typedef struct {
    char *data;      // NUL-terminated
    size_t len;
    size_t cap;
    size_t max;      // hard cap, including the NUL
    bool truncated;
} grow_buf_t;
bool grow_buf_init(grow_buf_t *b, size_t initial, size_t max);
bool grow_buf_append(grow_buf_t *b, const char *src, size_t n);  // false once truncated
char *grow_buf_take(grow_buf_t *b);                              // caller frees; b is emptied
void grow_buf_free(grow_buf_t *b);

// llm.h
esp_err_t llm_request(const char *request_json, char **response_out);  // *response_out malloc'd on ESP_OK

// config.h
#define LLM_RESPONSE_INITIAL 4096
#define LLM_RESPONSE_MAX     16384
```

`LLM_RESPONSE_BUF_SIZE` is removed.

- [ ] **Step 1: Write the failing tests**

Create `test/host/test_grow_buf.c`:

```c
/*
 * Host tests for the growable text buffer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grow_buf.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(starts_empty_and_appends)
{
    grow_buf_t b;
    ASSERT(grow_buf_init(&b, 16, 64));
    ASSERT(b.len == 0 && b.data[0] == '\0');
    ASSERT(grow_buf_append(&b, "hola", 4));
    ASSERT(strcmp(b.data, "hola") == 0 && b.len == 4);
    grow_buf_free(&b);
    ASSERT(b.data == NULL);
    return 0;
}

TEST(grows_past_initial_capacity)
{
    static char chunk[1000];
    grow_buf_t b;
    memset(chunk, 'a', sizeof(chunk));
    ASSERT(grow_buf_init(&b, 4096, 16384));
    for (int i = 0; i < 10; i++) {
        ASSERT(grow_buf_append(&b, chunk, sizeof(chunk)));
    }
    ASSERT(b.len == 10000 && strlen(b.data) == 10000);
    ASSERT(b.cap >= 10001 && b.cap <= 16384);
    ASSERT(!b.truncated);
    grow_buf_free(&b);
    return 0;
}

TEST(refuses_beyond_max)
{
    static char chunk[100];
    grow_buf_t b;
    memset(chunk, 'z', sizeof(chunk));
    ASSERT(grow_buf_init(&b, 64, 256));
    ASSERT(grow_buf_append(&b, chunk, 100));
    ASSERT(grow_buf_append(&b, chunk, 100));
    ASSERT(!grow_buf_append(&b, chunk, 100));   // 300 > 255
    ASSERT(b.truncated);
    ASSERT(b.len == 255 && strlen(b.data) == 255);
    ASSERT(!grow_buf_append(&b, "x", 1));
    grow_buf_free(&b);
    return 0;
}

TEST(take_hands_over_the_string)
{
    grow_buf_t b;
    ASSERT(grow_buf_init(&b, 8, 64));
    ASSERT(grow_buf_append(&b, "respuesta", 9));
    char *s = grow_buf_take(&b);
    ASSERT(s && strcmp(s, "respuesta") == 0);
    ASSERT(b.data == NULL && b.len == 0);
    free(s);
    grow_buf_free(&b);  // safe after take
    return 0;
}

int test_grow_buf_all(void)
{
    int failures = 0;

    printf("\nGrow Buffer Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(starts_empty_and_appends);
    RUN(grows_past_initial_capacity);
    RUN(refuses_beyond_max);
    RUN(take_hands_over_the_string);

#undef RUN
    return failures;
}
```

Register it in `scripts/test.sh` (`test_grow_buf.c`, `../../main/grow_buf.c`) and `test_runner.c`. Create `main/grow_buf.h` with the interface above.

- [ ] **Step 2: Run them to see them fail**

Expected: link errors for `grow_buf_*`.

- [ ] **Step 3: Implement `main/grow_buf.c`**

```c
#include "grow_buf.h"

#include <stdlib.h>
#include <string.h>

bool grow_buf_init(grow_buf_t *b, size_t initial, size_t max)
{
    memset(b, 0, sizeof(*b));
    if (initial == 0 || max < initial) {
        return false;
    }
    b->data = malloc(initial);
    if (!b->data) {
        return false;
    }
    b->data[0] = '\0';
    b->cap = initial;
    b->max = max;
    return true;
}

bool grow_buf_append(grow_buf_t *b, const char *src, size_t n)
{
    if (!b->data || b->truncated) {
        return false;
    }
    size_t want = b->len + n + 1;
    if (want > b->cap) {
        size_t cap = b->cap;
        while (cap < want && cap < b->max) {
            cap = (cap * 2 > b->max) ? b->max : cap * 2;
        }
        if (cap != b->cap) {
            char *grown = realloc(b->data, cap);
            if (grown) {
                b->data = grown;
                b->cap = cap;
            }
        }
    }
    size_t room = b->cap - 1 - b->len;
    size_t copy = n < room ? n : room;
    memcpy(b->data + b->len, src, copy);
    b->len += copy;
    b->data[b->len] = '\0';
    if (copy < n) {
        b->truncated = true;
        return false;
    }
    return true;
}

char *grow_buf_take(grow_buf_t *b)
{
    char *s = b->data;
    memset(b, 0, sizeof(*b));
    return s;
}

void grow_buf_free(grow_buf_t *b)
{
    free(b->data);
    memset(b, 0, sizeof(*b));
}
```

Add `"grow_buf.c"` to `main/CMakeLists.txt`.

- [ ] **Step 4: Run the grow_buf tests**

Expected: 4 OK.

- [ ] **Step 5: Use it in llm.c, agent.c and the mock**

- **`config.h`:** replace `LLM_RESPONSE_BUF_SIZE` with `LLM_RESPONSE_INITIAL 4096` and `LLM_RESPONSE_MAX 16384`, each with a comment.
- **`llm.c`:**
  - `http_response_ctx_t` holds a `grow_buf_t buf`, and the ON_DATA handler calls `grow_buf_append(&ctx->buf, evt->data, evt->data_len)`. On the first `false`, log `"LLM response truncated at %d bytes"`.
  - `llm_request(const char *request_json, char **response_out)` sets `*response_out = NULL` and builds `ctx.buf` with `grow_buf_init(&ctx.buf, LLM_RESPONSE_INITIAL, LLM_RESPONSE_MAX)`. If that fails, it returns `ESP_ERR_NO_MEM`.
  - On a non-200 reply, it logs `ctx.buf.data` as the error body and frees it.
  - On `ctx.buf.truncated`, it frees and returns `ESP_ERR_NO_MEM`.
  - On success, `*response_out = grow_buf_take(&ctx.buf)`.
  - The 403/429 fallback recursion calls `llm_request(fallback_body, response_out)`.
  - The `TEST_BUILD` canned path returns `strdup(test_canned_reply(request_json))`.
- **`llm.h`:** the new signature, with the comment `// On ESP_OK *response_out is a malloc'd JSON string the caller frees.`
- **`agent.c`:**
  - Delete `s_response_buf`. Before the retry loop declare `char *response = NULL;`, and call `err = llm_request(request, &response);`.
  - After `json_parse_response(response, ...)` returns, successfully or not, call `free(response); response = NULL;` before any further work. The parser copies what it keeps.
  - In `agent_test_reset`, delete the `memset` of `s_response_buf`.
- **`test/host/mock_llm.c`:**
  - Set `MOCK_RESPONSE_MAX_LEN` to `LLM_RESPONSE_MAX`.
  - The mock `llm_request(const char *request_json, char **response_out)` returns `*response_out = strdup(...)` on `ESP_OK`, and `NULL` otherwise.
- **`test/host/test_llm_runtime.c`:** update `stub_request_returns_response` to the new signature and free the string.

- [ ] **Step 6: Run everything**

```bash
./scripts/test.sh host && ./test/host/build/test_runner 2>/dev/null | grep -E 'Grow|FAIL|All tests passed'
idf.py build 2>&1 | grep -E 'warning:|error:|Project build complete'
grep -rn 'LLM_RESPONSE_BUF_SIZE\|s_response_buf' main/ test/host/
```

Expected: all pass, the build is clean, and the grep is empty.

- [ ] **Step 7: Commit**

`refactor(llm): receive the reply into a heap buffer that grows from 4 KB to 16 KB`.

---

### Task 11: On-demand buffers for the prompt, web_search and /status

**Files:**
- Modify: `main/agent.c`, `main/tools_net.c`

**Interfaces:**
- Consumes: `agent_build_system_prompt(char *buf, size_t len)` from Task 3, and `AGENT_SYSTEM_PROMPT_BUF_SIZE`.

- [ ] **Step 1: System prompt buffer**

In `agent.c`, delete `static char s_system_prompt_buf[...]`. Around `json_build_request`:

```c
        char *prompt_buf = malloc(AGENT_SYSTEM_PROMPT_BUF_SIZE);
        // Without the buffer the base prompt still goes out (no pin policy line).
        const char *prompt = agent_build_system_prompt(prompt_buf,
                                                       prompt_buf ? AGENT_SYSTEM_PROMPT_BUF_SIZE : 0);
        char *request = json_build_request(prompt,
                                           s_history_view,
                                           history_view(s_history_view, HISTORY_MAX_RECORDS),
                                           NULL, tools, tool_count);
        free(prompt_buf);
```

This is safe because `agent_build_system_prompt(NULL, 0)` already returns `SYSTEM_PROMPT`, and cJSON copies the string.

- [ ] **Step 2: web_search HTML buffer**

In `tools_web_search_handler`, replace `static char html[WEB_SEARCH_HTML_MAX];` with:

```c
    char *html = malloc(WEB_SEARCH_HTML_MAX);
    if (!html) {
        http_gate_release();
        snprintf(result, result_len, "Error: out of memory for search");
        return false;
    }
```

Pass `html, WEB_SEARCH_HTML_MAX` to `net_http_request`. After `html_strip_into`, call `free(html)`, and also free it on each early return after the malloc. The gate is acquired before the malloc, so the out-of-memory path releases it, as shown.

- [ ] **Step 3: /status buffer**

In `agent.c`, the function-local `static char status_buf[1024]` becomes `static char status_buf[256]`. The `/status` text is ~150 B.

- [ ] **Step 4: Test and build**

```bash
./scripts/test.sh host && ./test/host/build/test_runner 2>/dev/null | grep -E 'FAIL|All tests passed'
idf.py build 2>&1 | grep -E 'warning:|error:|Project build complete'
```

Expected: all pass, and the build is clean.

- [ ] **Step 5: Commit**

`refactor: allocate the prompt and web_search buffers only while in use`.

---

### Task 12: read_web pages on the SD card (`web_page` slice + file cache)

**Files:**
- Create: `main/web_page.h`, `main/web_page.c`, `test/host/test_web_page.c`
- Modify: `main/tools_net.c`, `main/CMakeLists.txt`, `scripts/test.sh`, `test/host/test_runner.c`

**Interfaces:**
- Produces:

```c
// web_page.h
#define WEB_PAGE_SIZE 450
typedef struct {
    size_t start;      // byte offset of the page
    size_t len;        // bytes in the page (0 when past the end)
    int total_pages;   // >= 1
    int page;          // the page served, clamped to >= 1
    bool past_end;     // page > total_pages
} web_page_slice_t;
web_page_slice_t web_page_slice(size_t total_len, int page, size_t page_size);
```

- [ ] **Step 1: Write the failing tests**

Create `test/host/test_web_page.c`:

```c
/*
 * Host tests for read_web paging.
 */

#include <stdio.h>

#include "web_page.h"

#define TEST(name) static int test_##name(void)
#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        return 1; \
    } \
} while(0)

TEST(first_middle_and_last_page)
{
    web_page_slice_t s = web_page_slice(1000, 1, 450);
    ASSERT(s.total_pages == 3 && s.start == 0 && s.len == 450 && !s.past_end);
    s = web_page_slice(1000, 2, 450);
    ASSERT(s.start == 450 && s.len == 450);
    s = web_page_slice(1000, 3, 450);
    ASSERT(s.start == 900 && s.len == 100 && !s.past_end);
    return 0;
}

TEST(past_the_end)
{
    web_page_slice_t s = web_page_slice(1000, 4, 450);
    ASSERT(s.past_end && s.len == 0 && s.total_pages == 3);
    return 0;
}

TEST(empty_text_is_one_empty_page)
{
    web_page_slice_t s = web_page_slice(0, 1, 450);
    ASSERT(s.total_pages == 1 && s.len == 0 && !s.past_end);
    return 0;
}

TEST(page_below_one_is_page_one)
{
    web_page_slice_t s = web_page_slice(1000, 0, 450);
    ASSERT(s.page == 1 && s.start == 0 && s.len == 450);
    s = web_page_slice(1000, -3, 450);
    ASSERT(s.page == 1);
    return 0;
}

TEST(exact_multiple_has_no_extra_page)
{
    web_page_slice_t s = web_page_slice(900, 2, 450);
    ASSERT(s.total_pages == 2 && s.len == 450 && !s.past_end);
    return 0;
}

int test_web_page_all(void)
{
    int failures = 0;

    printf("\nWeb Page Tests:\n");

#define RUN(name) do { \
    printf("  " #name "... "); \
    if (test_##name() == 0) { printf("OK\n"); } else { failures++; } \
} while(0)

    RUN(first_middle_and_last_page);
    RUN(past_the_end);
    RUN(empty_text_is_one_empty_page);
    RUN(page_below_one_is_page_one);
    RUN(exact_multiple_has_no_extra_page);

#undef RUN
    return failures;
}
```

Register it (`test_web_page.c`, `../../main/web_page.c`, runner entries) and create `main/web_page.h`.

- [ ] **Step 2: Run to see them fail**

Expected: link errors.

- [ ] **Step 3: Implement `main/web_page.c`**

```c
#include "web_page.h"

web_page_slice_t web_page_slice(size_t total_len, int page, size_t page_size)
{
    web_page_slice_t s = {0};
    s.page = page < 1 ? 1 : page;
    s.total_pages = (int)((total_len + page_size - 1) / page_size);
    if (s.total_pages < 1) {
        s.total_pages = 1;
    }
    if (s.page > s.total_pages) {
        s.past_end = true;
        return s;
    }
    s.start = (size_t)(s.page - 1) * page_size;
    s.len = total_len - s.start < page_size ? total_len - s.start : page_size;
    return s;
}
```

Add `"web_page.c"` to `main/CMakeLists.txt`. Run the tests. Expected: 5 OK.

- [ ] **Step 4: File sink in tools_net.c**

Extend the response context and the event handler:

```c
typedef struct {
    char *buf;
    size_t len;
    size_t max;
    FILE *file;     // when set, the body goes to the file (up to max bytes)
} net_resp_ctx_t;

static esp_err_t net_http_event(esp_http_client_event_t *evt)
{
    net_resp_ctx_t *ctx = (net_resp_ctx_t *)evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA || !ctx || evt->data_len <= 0) {
        return ESP_OK;
    }
    if (ctx->file) {
        size_t space = ctx->max - ctx->len;
        size_t n = ((size_t)evt->data_len < space) ? (size_t)evt->data_len : space;
        if (n > 0 && fwrite(evt->data, 1, n, ctx->file) == n) {
            ctx->len += n;
        }
    } else if (ctx->buf) {
        size_t space = ctx->max - 1 - ctx->len;
        size_t n = ((size_t)evt->data_len < space) ? (size_t)evt->data_len : space;
        if (n > 0) {
            memcpy(ctx->buf + ctx->len, evt->data, n);
            ctx->len += n;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}
```

Factor the body of `net_http_request` into `static int net_http_perform(const char *url, esp_http_client_method_t method, const char *header_key, const char *header_value, const char *post_body, net_resp_ctx_t *ctx)`. Then:

```c
static int net_http_request(const char *url, esp_http_client_method_t method,
                            const char *header_key, const char *header_value,
                            const char *post_body, char *out, size_t out_max)
{
    net_resp_ctx_t ctx = { .buf = out, .len = 0, .max = out_max, .file = NULL };
    out[0] = '\0';
    return net_http_perform(url, method, header_key, header_value, post_body, &ctx);
}

// GET into an open file; *written gets the body bytes kept (at most max).
static int net_http_get_to_file(const char *url, const char *header_key, const char *header_value,
                                FILE *file, size_t max, size_t *written)
{
    net_resp_ctx_t ctx = { .buf = NULL, .len = 0, .max = max, .file = file };
    int status = net_http_perform(url, HTTP_METHOD_GET, header_key, header_value, NULL, &ctx);
    *written = ctx.len;
    return status;
}
```

- [ ] **Step 5: Rewrite `tools_read_web_handler`**

Delete `s_web_cache`, `s_web_cache_url` and `s_web_cache_len`, and the `READ_WEB_*` defines. Add `#include "sd_card.h"`, `#include "web_page.h"` and `#include <sys/stat.h>`, then:

```c
// read_web: fetch a page as clean text via the self-hosted reader on the VPS and
// hand it to the model in WEB_PAGE_SIZE pages. With an SD card the whole text
// (up to 64 KB) is kept in /sdcard/web, so paging costs one download; without
// one, every call downloads up to 4 KB into the heap and frees it.
#define READ_WEB_SD_MAX   (64 * 1024)
#define READ_WEB_RAM_MAX  4096
#define READ_WEB_DIR      SD_MOUNT_POINT "/web"
#define READ_WEB_TEXT     READ_WEB_DIR "/page.txt"
#define READ_WEB_URL      READ_WEB_DIR "/url.txt"

static void reader_url_for(const char *url, char *out, size_t out_len)
{
    snprintf(out, out_len, "https://bolita-cubana.duckdns.org/reader/%s", url);
}

static bool sd_cached_url_is(const char *url)
{
    char cached[256] = {0};
    FILE *f = fopen(READ_WEB_URL, "r");
    if (!f) {
        return false;
    }
    size_t n = fread(cached, 1, sizeof(cached) - 1, f);
    fclose(f);
    cached[n] = '\0';
    return strcmp(cached, url) == 0;
}

// Fetches into the SD cache. Returns the HTTP status (-1 on transport or file error).
static int sd_fetch(const char *url, size_t *text_len)
{
    char reader_url[256];
    reader_url_for(url, reader_url, sizeof(reader_url));
    mkdir(READ_WEB_DIR, 0755);  // EEXIST is fine
    remove(READ_WEB_URL);       // no stale URL if the download fails
    FILE *f = fopen(READ_WEB_TEXT, "w");
    if (!f) {
        return -1;
    }
    int status = net_http_get_to_file(reader_url, "User-Agent", "Mozilla/5.0 (zclaw-esp32)",
                                      f, READ_WEB_SD_MAX, text_len);
    fclose(f);
    if (status >= 200 && status < 300) {
        FILE *u = fopen(READ_WEB_URL, "w");
        if (u) {
            fputs(url, u);
            fclose(u);
        }
    }
    return status;
}

static size_t sd_text_len(void)
{
    struct stat st;
    return stat(READ_WEB_TEXT, &st) == 0 ? (size_t)st.st_size : 0;
}

static void format_page(const web_page_slice_t *s, const char *text, char *result, size_t result_len)
{
    if (s->page < s->total_pages) {
        snprintf(result, result_len, "[pag %d/%d, pide page=%d para seguir]\n%s",
                 s->page, s->total_pages, s->page + 1, text);
    } else {
        snprintf(result, result_len, "[pag %d/%d, fin]\n%s", s->page, s->total_pages, text);
    }
}

bool tools_read_web_handler(const cJSON *input, char *result, size_t result_len)
{
    const cJSON *url = cJSON_GetObjectItem(input, "url");
    const cJSON *page_json = cJSON_GetObjectItem(input, "page");
    int page = (page_json && cJSON_IsNumber(page_json)) ? page_json->valueint : 1;

    if (!url || !cJSON_IsString(url) ||
        (strncmp(url->valuestring, "http://", 7) != 0 &&
         strncmp(url->valuestring, "https://", 8) != 0)) {
        snprintf(result, result_len, "Error: 'url' required (http/https)");
        return false;
    }

    char chunk[WEB_PAGE_SIZE + 1];
    web_page_slice_t s;
    int status = 200;

    if (sd_card_begin()) {
        size_t len = 0;
        if (sd_cached_url_is(url->valuestring)) {
            len = sd_text_len();
        } else {
            if (!http_gate_acquire("read_web", pdMS_TO_TICKS(NET_TOOL_GATE_MS))) {
                sd_card_end();
                snprintf(result, result_len, "Error: HTTP busy, try again");
                return false;
            }
            status = sd_fetch(url->valuestring, &len);
            http_gate_release();
        }
        s = web_page_slice(len, page, WEB_PAGE_SIZE);
        chunk[0] = '\0';
        if (status >= 200 && status < 300 && !s.past_end && s.len > 0) {
            FILE *f = fopen(READ_WEB_TEXT, "r");
            size_t n = 0;
            if (f) {
                fseek(f, (long)s.start, SEEK_SET);
                n = fread(chunk, 1, s.len, f);
                fclose(f);
            }
            chunk[n] = '\0';
        }
        sd_card_end();
    } else {
        char *text = malloc(READ_WEB_RAM_MAX);
        if (!text) {
            snprintf(result, result_len, "Error: out of memory for read_web");
            return false;
        }
        char reader_url[256];
        reader_url_for(url->valuestring, reader_url, sizeof(reader_url));
        if (!http_gate_acquire("read_web", pdMS_TO_TICKS(NET_TOOL_GATE_MS))) {
            free(text);
            snprintf(result, result_len, "Error: HTTP busy, try again");
            return false;
        }
        status = net_http_request(reader_url, HTTP_METHOD_GET, "User-Agent",
                                  "Mozilla/5.0 (zclaw-esp32)", NULL, text, READ_WEB_RAM_MAX);
        http_gate_release();
        s = web_page_slice(status >= 200 && status < 300 ? strlen(text) : 0, page, WEB_PAGE_SIZE);
        memcpy(chunk, text + s.start, s.len);
        chunk[s.len] = '\0';
        free(text);
    }

    if (status < 0) {
        snprintf(result, result_len, "Error: reader request failed (network)");
        return false;
    }
    if (status < 200 || status >= 300) {
        snprintf(result, result_len, "Error: reader HTTP %d", status);
        return false;
    }
    if (s.past_end) {
        snprintf(result, result_len, "Fin del contenido (%d paginas en total)", s.total_pages);
        return true;
    }
    format_page(&s, chunk, result, result_len);
    return true;
}
```

Check that `sd_card.h` declares `SD_MOUNT_POINT` and `sd_card_begin/end` in builds with `CONFIG_ZCLAW_SD_ENABLE=n`, where `sd_card_begin` returns false. If the stub lacks them, add `bool sd_card_begin(void) { return false; }` and an empty `sd_card_end` to the `#else` branch of `sd_card.c`.

- [ ] **Step 6: Test and build**

```bash
./scripts/test.sh host && ./test/host/build/test_runner 2>/dev/null | grep -E 'Web Page|FAIL|All tests passed'
idf.py build 2>&1 | grep -E 'warning:|error:|Project build complete'
grep -n 's_web_cache\|READ_WEB_CACHE_MAX' main/tools_net.c
```

Expected: all pass, the build is clean, and the grep is empty.

- [ ] **Step 7: Commit**

`feat(read_web): keep the page on the SD card (up to 64 KB) instead of a 4 KB static cache`.

---

### Task 13: Cron fires copy actions only when something is due

**Files:**
- Modify: `main/cron.c`

- [ ] **Step 1: Replace the static pending array**

Delete `pending_cron_fire_t s_pending_fires[CRON_MAX_ENTRIES]` and its comment, and keep the `pending_cron_fire_t` typedef. In `check_entries`:

1. Under the mutex, first loop over the entries to fill `uint8_t due[CRON_MAX_ENTRIES]` with the indices that should fire, and count them in `due_count`. Use the same `should_fire` logic as today, unchanged, but do not modify the entries in this loop.
2. If `due_count == 0`, unlock and return.
3. Allocate `pending_cron_fire_t *pending = malloc(due_count * sizeof(*pending));`.
   - If it fails, log `ESP_LOGW(TAG, "Cron fire deferred: out of memory");`, unlock and return. The entries are untouched, so they fire on a later check.
4. For each due index, copy `id` and `action` into `pending[k]`. Then apply the existing one-shot clear or `last_run` update with `save_entry`, exactly as today.
5. Unlock, send the messages exactly as the current second loop does (`[CRON %d] %s`, `MSG_SOURCE_CRON`, 100 ms queue wait), then `free(pending)`.

The comment above the malloc:

```c
    // Copies are made only for entries that fire, on the heap: the cron task's
    // 4 KB stack cannot hold them and a static table would cost 4 KB forever.
```

- [ ] **Step 2: Build**

```bash
idf.py build 2>&1 | grep -E 'warning:|error:|Project build complete'
./scripts/test.sh host && ./test/host/build/test_runner 2>/dev/null | grep -E 'FAIL|All tests passed'
```

Expected: the build is clean and the tests pass. cron.c has no host tests, so behaviour is checked on the board in Task 15.

- [ ] **Step 3: Commit**

`refactor(cron): allocate fire copies only when entries are due`.

- [ ] **Step 4: Update the spec**

In the spec's section B5, replace the "entry index and generation counter" design with the one implemented here, and adjust §6's cron test line to "board check (cron.c has no host tests)". Commit: `docs: align the slimming spec with the cron change`.

---

### Task 14: TLS and FAT buffer settings

**Files:**
- Modify: `sdkconfig.defaults`, which is tracked. Also apply the settings to the local `sdkconfig` with `idf.py reconfigure`.

- [ ] **Step 1: Add the options**

Append to `sdkconfig.defaults`:

```
# RAM: TLS buffers only while data moves; FAT windows of 512 B (no flash FAT partition)
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y
CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y
CONFIG_MBEDTLS_DYNAMIC_FREE_CA_CERT=y
CONFIG_FATFS_SECTOR_512=y
```

`sdkconfig` is generated, so edit the same four symbols there too with `sed`, or delete them and run `idf.py reconfigure`. Do not lose the secrets in `sdkconfig`, and check them afterwards with `grep -c CONFIG_ZCLAW_STT_TOKEN sdkconfig`.

- [ ] **Step 2: Build and confirm**

```bash
idf.py reconfigure >/dev/null && idf.py build 2>&1 | grep -E 'warning:|error:|Project build complete'
grep -E 'CONFIG_MBEDTLS_DYNAMIC_BUFFER|CONFIG_FATFS_SECTOR_512' build/config/sdkconfig.h
```

Expected: the build completes, and both symbols are defined as 1.

- [ ] **Step 3: Commit**

`chore(config): dynamic mbedTLS buffers and 512 B FAT sectors to lower RAM peaks`.

These settings are verified on the board in Task 15. Revert them there if TLS or the SD card misbehaves.

---

### Task 15: Board verification (with the user)

**Files:** none planned. Record the results in the ledger.

The user watches the screen. Ask before flashing.

Serial helpers, in this session's scratchpad (recreate them if missing):

- `serial_cmd.py PORT SECONDS [LINE] [--grep RE] [--reset]` opens the port with DTR high and RTS low, so it does not reset the board.
- `serial_log.sh` keeps logging to `serial.log`; `serial_send.py PORT LINE` sends a command while the log runs.
- Stop the logger before flashing.

- [ ] **Step 1: Flash and boot.**

Ask: "¿Flasheo por USB el firmware adelgazado?" On a yes, run `idf.py -p <port> flash`.

Expected: `Voice ready`, `Connected:` and the `Ready! Free heap:` line. Ledger the value.

- [ ] **Step 2: Idle RAM.**

`/diag` about 20 s after boot. Ledger free/min/largest.

Target: free ≥ 200 KB. If it is below, ledger the nm top-10 and stop to discuss before tuning anything else.

- [ ] **Step 3: Feature checklist.**

Run the steps below in order, and ledger the heap minimum from `/diag` after each.

1. A serial question ("¿Qué hora es?").
2. Voice: a double clap and a question; then `/escucha` and a question.
3. A question that triggers `web_search`.
4. `read_web` of a long article: page 1, 2, and past the end (with the SD card).
5. Board check, no SD card: remove the card, run `read_web` page 2 of a new URL, then reinsert the card.
6. A dashboard request ("muéstrame el clima").
7. Cron: two one-shot entries due within the same minute. Both fire, in order, and both disappear from `cron_list`.
8. `memory_export`: a file appears under `/sdcard/backup`.
9. The web OTA page and `/panel`/`/tasks` open in the browser. The PIN check works: a wrong PIN is refused.
10. `sms_send` only if the user wants to spend a message. Otherwise skip it and say so.

Expected: every step works. After all of them, the heap minimum is ≥ 40 KB.

- [ ] **Step 4: TLS settings.**

If any HTTPS step failed only after Task 14, revert Task 14 (`git revert`), rebuild, flash and repeat the failed steps. Then ledger the outcome.

- [ ] **Step 5: Ledger and wrap-up.**

Record:

- the before and after numbers (libmain `.bss`, idle free, the heap minimum);
- the checklist results;
- anything skipped.

Push only if the user asks.
