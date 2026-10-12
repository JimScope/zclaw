#include "agent.h"
#include "agent_commands.h"
#include "agent_prompt.h"
#include "config.h"
#include "history.h"
#include "local_admin.h"
#include "llm.h"
#include "tools.h"
#include "user_tools.h"
#include "json_util.h"
#include "messages.h"
#include "ratelimit.h"
#include "memory.h"
#include "nvs_keys.h"
#include "display.h"
#include "task_log.h"
#include "voice.h"

#define PANEL_DISPLAY_MS 30000
#define MIC_PROBE_MS 20000
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "ota.h"
#include "tools_handlers.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>

static const char *TAG = "agent";

// Queues
static QueueHandle_t s_input_queue;
static QueueHandle_t s_channel_output_queue;
static int64_t s_last_start_response_us = 0;
static int64_t s_last_non_command_response_us = 0;
static char s_last_non_command_text[CHANNEL_RX_BUF_SIZE] = {0};
// Screen header of the reply being produced: who asked and what. Set at the
// start of process_message; points into the message being processed.
static const char *s_reply_label = "";
static const char *s_reply_prompt = "";
static bool s_reply_is_task = false;  // cron turn: replies go to the task log
static volatile bool s_busy = false;  // agent task inside process_message

bool agent_is_busy(void)
{
    return s_busy;
}
static bool s_messages_paused = false;
static char s_system_prompt_buf[AGENT_SYSTEM_PROMPT_BUF_SIZE];

// Conversation history (rolling message buffer)
static conversation_msg_t s_history_view[HISTORY_MAX_RECORDS];

// Buffers (static to avoid stack overflow)
static char s_response_buf[LLM_RESPONSE_BUF_SIZE];
static char s_tool_result_buf[TOOL_RESULT_BUF_SIZE];

typedef struct {
    int64_t started_us;
    uint64_t llm_us_total;
    uint64_t tool_us_total;
    int llm_calls;
    int tool_calls;
    int rounds;
} request_metrics_t;

static uint64_t elapsed_us_since(int64_t started_us)
{
    int64_t now_us = esp_timer_get_time();
    if (now_us <= started_us) {
        return 0;
    }
    return (uint64_t)(now_us - started_us);
}

static uint32_t us_to_ms_u32(uint64_t duration_us)
{
    uint64_t duration_ms = duration_us / 1000ULL;
    if (duration_ms > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)duration_ms;
}

static void metrics_log_request(const request_metrics_t *metrics, const char *outcome)
{
    if (!metrics) {
        return;
    }

    ESP_LOGI(TAG,
             "METRIC request outcome=%s total_ms=%" PRIu32 " llm_ms=%" PRIu32
             " tool_ms=%" PRIu32 " rounds=%d llm_calls=%d tool_calls=%d",
             outcome ? outcome : "unknown",
             us_to_ms_u32(elapsed_us_since(metrics->started_us)),
             us_to_ms_u32(metrics->llm_us_total),
             us_to_ms_u32(metrics->tool_us_total),
             metrics->rounds,
             metrics->llm_calls,
             metrics->tool_calls);
}

static void history_rollback_to(history_mark_t mark, const char *reason)
{
    int dropped = history_rollback(mark);
    if (dropped > 0) {
        ESP_LOGW(TAG, "Rolling back conversation history (%d messages): %s",
                 dropped, reason ? reason : "unknown");
    }
}

static void queue_channel_response(const char *text)
{
    if (!s_channel_output_queue) {
        return;
    }

    channel_output_msg_t msg;
    strncpy(msg.text, text, CHANNEL_TX_BUF_SIZE - 1);
    msg.text[CHANNEL_TX_BUF_SIZE - 1] = '\0';

    if (xQueueSend(s_channel_output_queue, &msg, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send response to channel queue");
    }
}

static void send_response(const char *text)
{
    (void)display_show_answer(s_reply_label, s_reply_prompt, text ? text : "");
    if (s_reply_is_task) {
        task_log_append(s_reply_prompt, text ? text : "");
    }
    queue_channel_response(text);
}

static void handle_diag_command(const char *user_message, request_metrics_t *metrics)
{
    char error[120] = {0};
    cJSON *tool_input = cJSON_CreateObject();
    bool ok;
    int64_t started_us;

    if (!tool_input) {
        send_response("Error: diagnostics unavailable (allocation failed)");
        metrics_log_request(metrics, "diag_no_mem");
        return;
    }

    if (!agent_parse_diag_command_args(user_message, tool_input, error, sizeof(error))) {
        send_response(error);
        cJSON_Delete(tool_input);
        metrics_log_request(metrics, "diag_invalid_args");
        return;
    }

    s_tool_result_buf[0] = '\0';
    started_us = esp_timer_get_time();
    ok = tools_execute("get_diagnostics", tool_input, s_tool_result_buf, sizeof(s_tool_result_buf));
    metrics->tool_us_total += elapsed_us_since(started_us);
    metrics->tool_calls++;
    cJSON_Delete(tool_input);

    if (!ok) {
        if (s_tool_result_buf[0] == '\0') {
            snprintf(s_tool_result_buf, sizeof(s_tool_result_buf), "Error: diagnostics failed");
        }
        send_response(s_tool_result_buf);
        metrics_log_request(metrics, "diag_failed");
        return;
    }

    send_response(s_tool_result_buf);
    metrics_log_request(metrics, "diag_handled");
}

static void handle_gpio_command(const char *user_message, request_metrics_t *metrics)
{
    char error[120] = {0};
    cJSON *tool_input = cJSON_CreateObject();
    const char *tool_name = NULL;
    bool ok;
    int64_t started_us;

    if (!tool_input) {
        send_response("Error: GPIO read unavailable (allocation failed)");
        metrics_log_request(metrics, "gpio_no_mem");
        return;
    }

    if (!agent_parse_gpio_command_args(user_message, &tool_name, tool_input, error, sizeof(error))) {
        send_response(error);
        cJSON_Delete(tool_input);
        metrics_log_request(metrics, "gpio_invalid_args");
        return;
    }

    s_tool_result_buf[0] = '\0';
    started_us = esp_timer_get_time();
    ok = tools_execute(tool_name, tool_input, s_tool_result_buf, sizeof(s_tool_result_buf));
    metrics->tool_us_total += elapsed_us_since(started_us);
    metrics->tool_calls++;
    cJSON_Delete(tool_input);

    if (!ok) {
        if (s_tool_result_buf[0] == '\0') {
            snprintf(s_tool_result_buf, sizeof(s_tool_result_buf), "Error: GPIO read failed");
        }
        send_response(s_tool_result_buf);
        metrics_log_request(metrics, "gpio_failed");
        return;
    }

    send_response(s_tool_result_buf);
    metrics_log_request(metrics, "gpio_handled");
}

static bool is_local_admin_command(const char *user_message)
{
    return agent_is_command(user_message, "gpio") ||
           agent_is_command(user_message, "diag") ||
           local_admin_is_command(user_message);
}

static void handle_local_admin_command(const char *user_message,
                                       message_source_t source,
                                       request_metrics_t *metrics)
{
    char response[CHANNEL_TX_BUF_SIZE];
    local_admin_action_t action = LOCAL_ADMIN_ACTION_NONE;

    if (source != MSG_SOURCE_CHANNEL) {
        send_response("Error: local admin commands are only available on the USB serial console.");
        metrics_log_request(metrics, "local_admin_remote_denied");
        return;
    }

    if (agent_is_command(user_message, "diag")) {
        handle_diag_command(user_message, metrics);
        return;
    }

    if (agent_is_command(user_message, "gpio")) {
        handle_gpio_command(user_message, metrics);
        return;
    }

    if (!local_admin_handle_command(user_message, response, sizeof(response), &action)) {
        send_response(response);
        metrics_log_request(metrics, "local_admin_invalid");
        return;
    }

    send_response(response);
    metrics_log_request(metrics, "local_admin_handled");
    local_admin_perform_action(action);
}

static void handle_start_command(void)
{
    static const char *START_HELP_TEXT =
        "zclaw online.\n\n"
        "Talk to me in normal language. You do not need command syntax.\n\n"
        "Examples:\n"
        "- what are all GPIO states\n"
        "- turn GPIO 5 on\n"
        "- remind me daily at 8:15 to water plants\n"
        "- remember that GPIO 4 controls the arcade machine\n"
        "- create a tool called arcade_on that turns GPIO 4 on\n"
        "- turn the arcade on in 10 minutes\n"
        "\n"
        "Chat commands:\n"
        "- /help (show this message)\n"
        "- /settings (show status)\n"
        "- /status, /temp (device status, chip temperature)\n"
        "- /panel, /clima, /cripto (dashboards on the display for 30 s)\n"
        "- /stop (pause intake)\n"
        "- /resume (resume)\n"
        "\n"
        "USB local admin commands:\n"
        "- /gpio [all|pin|pin high|pin low]\n"
        "- /diag [scope] [verbose]\n"
        "- /reboot\n"
        "- /wifi [status|scan]\n"
        "- /bootcount\n"
        "- /factory-reset confirm";
    send_response(START_HELP_TEXT);
}

static void handle_settings_command(void)
{
    char settings_text[384];
    snprintf(settings_text, sizeof(settings_text),
             "zclaw settings:\n"
             "- Message intake: %s\n"
             "- Chat commands: /start, /help, /settings, /stop, /resume\n"
             "- USB local admin: /gpio, /diag, /reboot, /wifi, /bootcount, /factory-reset\n"
             "- /gpio supports reads and writes (e.g. /gpio 9 low)\n"
             "- Device settings are global (e.g., timezone <name>)",
             s_messages_paused ? "paused" : "active");
    send_response(settings_text);
}

// Process a single user message
static void process_message(const char *user_message, message_source_t source)
{
    ESP_LOGI(TAG, "Processing: %s", user_message);
    history_mark_t history_turn_start = history_mark();
    bool is_non_command_message = !agent_is_slash_command(user_message);
    bool is_cron_trigger = agent_is_cron_trigger_message(user_message);
    s_reply_label = is_cron_trigger ? "Tarea:"
                  : source == MSG_SOURCE_VOICE ? "Dijiste:" : "T\xC3\xBA:";
    s_reply_prompt = is_cron_trigger ? agent_cron_trigger_body(user_message) : user_message;
    s_reply_is_task = is_cron_trigger;
    request_metrics_t metrics = {
        .started_us = esp_timer_get_time(),
        .llm_us_total = 0,
        .tool_us_total = 0,
        .llm_calls = 0,
        .tool_calls = 0,
        .rounds = 0,
    };

    if (agent_is_command(user_message, "resume")) {
        if (!s_messages_paused) {
            send_response("zclaw is already active.");
            metrics_log_request(&metrics, "resume_noop");
            return;
        }
        s_messages_paused = false;
        send_response("zclaw resumed. Send /start for command help.");
        metrics_log_request(&metrics, "resumed");
        return;
    }

    if (agent_is_command(user_message, "settings")) {
        handle_settings_command();
        metrics_log_request(&metrics, "settings_handled");
        return;
    }

    if (is_local_admin_command(user_message)) {
        handle_local_admin_command(user_message, source, &metrics);
        return;
    }

    if (s_messages_paused) {
        ESP_LOGD(TAG, "Paused mode: ignoring message");
        metrics_log_request(&metrics, "paused_drop");
        return;
    }

    if (agent_is_command(user_message, "help")) {
        handle_start_command();
        metrics_log_request(&metrics, "help_handled");
        return;
    }

    if (agent_is_command(user_message, "status")) {
        // Static: keeps 1KB off the task stack. NOTE: firmware uses newlib
        // nano printf (no %lld support) - stick to %lu/%d here.
        static char status_buf[CHANNEL_TX_BUF_SIZE];
        unsigned long up_min = (unsigned long)(esp_timer_get_time() / 60000000LL);
        wifi_ap_record_t ap = {0};
        int rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
        snprintf(status_buf, sizeof(status_buf),
                 "Uptime: %lu min\nHeap libre: %lu (min %lu)\nWiFi RSSI: %d dBm\n"
                 "Modelo: %s\nFirmware: %s",
                 up_min,
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size(),
                 rssi, llm_get_model(), ota_get_version());
        send_response(status_buf);
        metrics_log_request(&metrics, "status_handled");
        return;
    }

    if (agent_is_command(user_message, "escucha")) {
        // No answer page: the listening face must stay on screen.
        const char *reply = voice_request_listen() ? "Te escucho." : "Voz no disponible u ocupada.";
        queue_channel_response(reply);
        metrics_log_request(&metrics, "escucha_handled");
        return;
    }

    if (agent_is_command(user_message, "mic")) {
        const char *reply = voice_request_probe(MIC_PROBE_MS)
                                ? "Micr\xC3\xB3" "fono en pantalla 20 s (y por serie)."
                                : "Voz no disponible.";
        queue_channel_response(reply);
        metrics_log_request(&metrics, "mic_handled");
        return;
    }

    if (agent_is_command(user_message, "panel")) {
        // Ack without display_notify_talking(): the talking face would cover the panel.
        const char *reply = display_request_status(PANEL_DISPLAY_MS)
                                ? "Panel en pantalla 30 s."
                                : "Pantalla no disponible.";
        queue_channel_response(reply);
        metrics_log_request(&metrics, "panel_handled");
        return;
    }

    if (agent_is_command(user_message, "clima") || agent_is_command(user_message, "cripto")) {
        bool weather = agent_is_command(user_message, "clima");
        const char *reply;
        if (display_request_dashboard(weather ? "clima" : "cripto", PANEL_DISPLAY_MS)) {
            reply = weather ? "Clima en pantalla 30 s." : "Cripto en pantalla 30 s.";
        } else {
            reply = "Pantalla no disponible.";
        }
        // Same as /panel: no talking animation over the dashboard.
        queue_channel_response(reply);
        metrics_log_request(&metrics, weather ? "clima_handled" : "cripto_handled");
        return;
    }

    if (agent_is_command(user_message, "temp")) {
        char temp_buf[128];
        tools_chip_temp_handler(NULL, temp_buf, sizeof(temp_buf));
        send_response(temp_buf);
        metrics_log_request(&metrics, "temp_handled");
        return;
    }

    if (agent_is_command(user_message, "stop")) {
        s_messages_paused = true;
        send_response("zclaw paused. I will ignore new messages until /resume.");
        metrics_log_request(&metrics, "paused");
        return;
    }

    if (agent_is_command(user_message, "start")) {
        int64_t now_us = esp_timer_get_time();
        uint32_t since_last_start_ms = 0;
        if (s_last_start_response_us > 0 && now_us > s_last_start_response_us) {
            since_last_start_ms = (uint32_t)((now_us - s_last_start_response_us) / 1000ULL);
        }

        if (s_last_start_response_us > 0 && since_last_start_ms < START_COMMAND_COOLDOWN_MS) {
            ESP_LOGW(TAG, "Suppressing repeated /start (%" PRIu32 "ms since last response)",
                     since_last_start_ms);
            metrics_log_request(&metrics, "start_suppressed");
            return;
        }

        s_last_start_response_us = now_us;
        handle_start_command();
        metrics_log_request(&metrics, "start_handled");
        return;
    }

    if (is_non_command_message && source != MSG_SOURCE_VOICE) {
        int64_t now_us = esp_timer_get_time();
        uint32_t since_last_ms = 0;

        if (s_last_non_command_response_us > 0 && now_us > s_last_non_command_response_us) {
            since_last_ms = (uint32_t)((now_us - s_last_non_command_response_us) / 1000ULL);
        }

        if (s_last_non_command_text[0] != '\0' &&
            strcmp(user_message, s_last_non_command_text) == 0 &&
            s_last_non_command_response_us > 0 &&
            since_last_ms < MESSAGE_REPLAY_COOLDOWN_MS) {
            ESP_LOGW(TAG, "Suppressing repeated message replay (%" PRIu32 "ms since last response)",
                     since_last_ms);
            metrics_log_request(&metrics, "replay_suppressed");
            return;
        }
    }

    // Get tools
    int tool_count;
    const tool_def_t *tools = tools_get_all(&tool_count);

    led_thinking_start();
    display_notify_thinking(true);

    // Add user message to history
    history_add("user", user_message, false, false, NULL, NULL);

    int rounds = 0;
    bool done = false;

    while (!done && rounds < MAX_TOOL_ROUNDS) {
        rounds++;
        metrics.rounds = rounds;

        // Build request JSON (user message already in history)
        char *request = json_build_request(
            agent_build_system_prompt(s_system_prompt_buf, sizeof(s_system_prompt_buf)),
            s_history_view,
            history_view(s_history_view, HISTORY_MAX_RECORDS),
            NULL,  // User message already in history
            tools,
            tool_count
        );

        if (!request) {
            ESP_LOGE(TAG, "Failed to build request JSON");
            history_rollback_to(history_turn_start, "request build failed");
            send_response("Error: Failed to build request");
            metrics_log_request(&metrics, "request_build_error");
            return;
        }

        ESP_LOGI(TAG, "Request: %d bytes", (int)strlen(request));

        // Check rate limit before making request
        char rate_reason[128];
        if (!ratelimit_check(rate_reason, sizeof(rate_reason))) {
            free(request);
            history_rollback_to(history_turn_start, "rate limited");
            send_response(rate_reason);
            metrics_log_request(&metrics, "rate_limited");
            return;
        }

        // Send to LLM with retry
        esp_err_t err = ESP_FAIL;
        int retry_delay_ms = LLM_RETRY_BASE_MS;
        int64_t retry_window_started_us = esp_timer_get_time();

        for (int retry = 0; retry < LLM_MAX_RETRIES; retry++) {
            uint32_t retry_elapsed_ms = us_to_ms_u32(elapsed_us_since(retry_window_started_us));
            if (retry > 0 && retry_elapsed_ms >= LLM_RETRY_BUDGET_MS) {
                ESP_LOGW(TAG,
                         "LLM retry budget exhausted before attempt %d/%d (%" PRIu32 "ms/%dms)",
                         retry + 1, LLM_MAX_RETRIES, retry_elapsed_ms, LLM_RETRY_BUDGET_MS);
                break;
            }

            int64_t llm_started_us = esp_timer_get_time();
            err = llm_request(request, s_response_buf, sizeof(s_response_buf));
            metrics.llm_us_total += elapsed_us_since(llm_started_us);
            metrics.llm_calls++;
            if (err == ESP_OK) {
                break;
            }

            if (retry == LLM_MAX_RETRIES - 1) {
                break;
            }

            retry_elapsed_ms = us_to_ms_u32(elapsed_us_since(retry_window_started_us));
            if (retry_elapsed_ms >= LLM_RETRY_BUDGET_MS) {
                ESP_LOGW(TAG,
                         "LLM retry budget exhausted after attempt %d/%d (%" PRIu32 "ms/%dms)",
                         retry + 1, LLM_MAX_RETRIES, retry_elapsed_ms, LLM_RETRY_BUDGET_MS);
                break;
            }

            uint32_t remaining_budget_ms = (uint32_t)(LLM_RETRY_BUDGET_MS - retry_elapsed_ms);
            int delay_ms = retry_delay_ms;
            if ((uint32_t)delay_ms > remaining_budget_ms) {
                delay_ms = (int)remaining_budget_ms;
            }

            if (delay_ms <= 0) {
                ESP_LOGW(TAG,
                         "LLM retry budget left no delay before next attempt (%" PRIu32 "ms/%dms)",
                         retry_elapsed_ms, LLM_RETRY_BUDGET_MS);
                break;
            }

            ESP_LOGW(TAG,
                     "LLM request failed (attempt %d/%d), retrying in %dms (budget %" PRIu32 "/%dms)",
                     retry + 1, LLM_MAX_RETRIES, delay_ms, retry_elapsed_ms, LLM_RETRY_BUDGET_MS);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));

            // Exponential backoff
            retry_delay_ms *= 2;
            if (retry_delay_ms > LLM_RETRY_MAX_MS) {
                retry_delay_ms = LLM_RETRY_MAX_MS;
            }
        }

        free(request);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LLM request failed after %d retries", LLM_MAX_RETRIES);
            history_rollback_to(history_turn_start, "llm request failed");
            send_response("Error: Failed to contact LLM API after retries");
            metrics_log_request(&metrics, "llm_error");
            return;
        }

        // Record successful request for rate limiting
        ratelimit_record_request();

        // Parse response
        char text_out[MAX_MESSAGE_LEN] = {0};
        char tool_name[32] = {0};
        char tool_id[64] = {0};
        cJSON *tool_input = NULL;

        if (!json_parse_response(s_response_buf, text_out, sizeof(text_out),
                                  tool_name, sizeof(tool_name),
                                  tool_id, sizeof(tool_id),
                                  &tool_input)) {
            ESP_LOGE(TAG, "Failed to parse response");
            history_rollback_to(history_turn_start, "llm response parse failed");
            send_response("Error: Failed to parse LLM response");
            json_free_parsed_response();
            metrics_log_request(&metrics, "parse_error");
            return;
        }

        // Check if it's a tool use
        if (tool_name[0] != '\0' && tool_input) {
            ESP_LOGI(TAG, "Tool call: %s (round %d)", tool_name, rounds);

            // Store the tool_input as JSON string for history
            char *input_str = cJSON_PrintUnformatted(tool_input);

            // Add tool_use to history
            history_add("assistant", input_str ? input_str : "{}",
                        true, false, tool_id, tool_name);
            free(input_str);

            // Check if it's a user-defined tool
            const user_tool_t *user_tool = user_tools_find(tool_name);
            metrics.tool_calls++;
            if (user_tool) {
                // User tool: return the action as "instruction" for Claude to execute
                snprintf(s_tool_result_buf, sizeof(s_tool_result_buf),
                         "Execute this action now: %s", user_tool->action);
                ESP_LOGI(TAG, "User tool '%s' action: %s", tool_name, user_tool->action);
            } else if (is_cron_trigger && strcmp(tool_name, "cron_set") == 0) {
                snprintf(s_tool_result_buf, sizeof(s_tool_result_buf),
                         "Error: cron_set is not allowed during scheduled task execution. "
                         "Execute the scheduled action now instead of creating a new schedule.");
                ESP_LOGW(TAG, "Blocked cron_set during cron-triggered turn");
            } else {
                // Built-in tool: execute directly
                int64_t tool_started_us = esp_timer_get_time();
                (void)tools_execute(tool_name, tool_input,
                                  s_tool_result_buf, sizeof(s_tool_result_buf));
                metrics.tool_us_total += elapsed_us_since(tool_started_us);

                ESP_LOGI(TAG, "Tool result: %s", s_tool_result_buf);
            }

            // Add tool_result to history
            history_add("user", s_tool_result_buf, false, true, tool_id, NULL);

            json_free_parsed_response();
            // Continue loop to let Claude see the result
        } else {
            // Text response - we're done
            if (text_out[0] != '\0') {
                history_add("assistant", text_out, false, false, NULL, NULL);
                send_response(text_out);
            } else {
                history_add("assistant", "(Sin respuesta del modelo)", false, false, NULL, NULL);
                send_response("(Sin respuesta del modelo)");
            }
            json_free_parsed_response();
            done = true;
        }
    }

    if (!done) {
        ESP_LOGW(TAG, "Max tool rounds reached");
        history_add("assistant", "(Reached max tool iterations)", false, false, NULL, NULL);
        send_response("(Reached max tool iterations)");
        metrics_log_request(&metrics, "max_rounds");
        return;
    }

    if (is_non_command_message) {
        strncpy(s_last_non_command_text, user_message, sizeof(s_last_non_command_text) - 1);
        s_last_non_command_text[sizeof(s_last_non_command_text) - 1] = '\0';
        s_last_non_command_response_us = esp_timer_get_time();
    }

    metrics_log_request(&metrics, "success");
}

#ifdef TEST_BUILD
void agent_test_reset(void)
{
    history_clear();
    memset(s_response_buf, 0, sizeof(s_response_buf));
    memset(s_tool_result_buf, 0, sizeof(s_tool_result_buf));
    s_channel_output_queue = NULL;
    s_last_start_response_us = 0;
    s_last_non_command_response_us = 0;
    memset(s_last_non_command_text, 0, sizeof(s_last_non_command_text));
    s_messages_paused = false;
    local_admin_test_reset();
}

void agent_test_set_queues(QueueHandle_t channel_output_queue)
{
    s_channel_output_queue = channel_output_queue;
}

void agent_test_process_message(const char *user_message)
{
    process_message(user_message, MSG_SOURCE_CHANNEL);
}

void agent_test_process_voice_message(const char *user_message)
{
    process_message(user_message, MSG_SOURCE_VOICE);
}
#endif

// Agent task
static void agent_task(void *arg)
{
    (void)arg;
    channel_msg_t msg;

    ESP_LOGI(TAG, "Agent task started");

    while (1) {
        if (xQueueReceive(s_input_queue, &msg, portMAX_DELAY) == pdTRUE) {
            s_busy = true;
            process_message(msg.text, msg.source);
            // Single stop point: covers every exit path of process_message.
            led_thinking_stop();
            display_notify_thinking(false);
            s_reply_prompt = "";
            s_reply_is_task = false;
            s_busy = false;
        }
    }
}

esp_err_t agent_start(QueueHandle_t input_queue, QueueHandle_t channel_output_queue)
{
    if (!input_queue || !channel_output_queue) {
        ESP_LOGE(TAG, "Invalid queues for agent startup");
        return ESP_ERR_INVALID_ARG;
    }

    s_input_queue = input_queue;
    s_channel_output_queue = channel_output_queue;

    if (xTaskCreate(agent_task, "agent", AGENT_TASK_STACK_SIZE, NULL,
                    AGENT_TASK_PRIORITY, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create agent task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Agent started");
    return ESP_OK;
}
