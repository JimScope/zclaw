/*
 * Host tests for agent retry logic and output queue fanout.
 */

#include <stdio.h>
#include <string.h>

#include "agent.h"
#include "config.h"
#include "local_admin.h"
#include "messages.h"
#include "mock_display.h"
#include "mock_freertos.h"
#include "mock_llm.h"
#include "mock_memory.h"
#include "mock_ratelimit.h"
#include "mock_tools.h"
#include "freertos/queue.h"

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

static int recv_channel_text(QueueHandle_t queue, char *out, size_t out_len)
{
    channel_output_msg_t msg;
    if (xQueueReceive(queue, &msg, 0) != pdTRUE) {
        return 0;
    }
    snprintf(out, out_len, "%s", msg.text);
    return 1;
}

static void reset_state(void)
{
    mock_freertos_reset();
    mock_llm_reset();
    mock_memory_reset();
    mock_ratelimit_reset();
    mock_tools_reset();
    mock_llm_set_backend(LLM_BACKEND_ANTHROPIC, "mock-anthropic");
    agent_test_reset();
}

TEST(retries_with_backoff_and_fanout)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];
    const char *success =
        "{\"content\":[{\"type\":\"text\",\"text\":\"retry succeeded\"}],\"stop_reason\":\"end_turn\"}";

    reset_state();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    ASSERT(mock_llm_push_result(ESP_FAIL, NULL));
    ASSERT(mock_llm_push_result(ESP_FAIL, NULL));
    ASSERT(mock_llm_push_result(ESP_OK, success));

    agent_test_process_message("hello");

    ASSERT(mock_llm_request_count() == 3);
    ASSERT(mock_freertos_delay_count() == 2);
    ASSERT(mock_freertos_delay_at(0) == pdMS_TO_TICKS(2000));
    ASSERT(mock_freertos_delay_at(1) == pdMS_TO_TICKS(4000));
    ASSERT(mock_ratelimit_record_count() == 1);

    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "retry succeeded");

    vQueueDelete(channel_q);
    return 0;
}

TEST(rate_limit_short_circuit)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();

    channel_q = xQueueCreate(2, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);
    mock_ratelimit_set_allow(false, "Rate limit hit");

    agent_test_process_message("hello");

    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_ratelimit_record_count() == 0);
    ASSERT(mock_freertos_delay_count() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "Rate limit hit");

    vQueueDelete(channel_q);
    return 0;
}

TEST(fails_after_max_retries_without_extra_sleep)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();

    channel_q = xQueueCreate(2, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    ASSERT(mock_llm_push_result(ESP_FAIL, NULL));
    ASSERT(mock_llm_push_result(ESP_FAIL, NULL));
    ASSERT(mock_llm_push_result(ESP_FAIL, NULL));

    agent_test_process_message("hello");

    ASSERT(mock_llm_request_count() == 3);
    ASSERT(mock_freertos_delay_count() == 2);
    ASSERT(mock_freertos_delay_at(0) == pdMS_TO_TICKS(2000));
    ASSERT(mock_freertos_delay_at(1) == pdMS_TO_TICKS(4000));
    ASSERT(mock_ratelimit_record_count() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "Error: Failed to contact LLM API after retries");

    vQueueDelete(channel_q);
    return 0;
}

TEST(failed_turn_does_not_pollute_followup_prompt)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];
    const char *success =
        "{\"content\":[{\"type\":\"text\",\"text\":\"fresh response\"}],\"stop_reason\":\"end_turn\"}";
    const char *last_request = NULL;

    reset_state();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    ASSERT(mock_llm_push_result(ESP_FAIL, NULL));
    ASSERT(mock_llm_push_result(ESP_FAIL, NULL));
    ASSERT(mock_llm_push_result(ESP_FAIL, NULL));
    ASSERT(mock_llm_push_result(ESP_OK, success));

    agent_test_process_message("is this really on a tiny board");
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "Error: Failed to contact LLM API after retries");

    agent_test_process_message("hello");
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "fresh response");

    ASSERT(mock_llm_request_count() == 4);
    ASSERT(mock_ratelimit_record_count() == 1);

    last_request = mock_llm_last_request_json();
    ASSERT(last_request != NULL);
    ASSERT(strstr(last_request, "is this really on a tiny board") == NULL);
    ASSERT(strstr(last_request, "hello") != NULL);

    vQueueDelete(channel_q);
    return 0;
}

TEST(channel_output_allows_long_response)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];
    char long_text[801];
    char response_json[1200];

    reset_state();

    memset(long_text, 'x', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';
    snprintf(response_json, sizeof(response_json),
             "{\"content\":[{\"type\":\"text\",\"text\":\"%s\"}],\"stop_reason\":\"end_turn\"}",
             long_text);

    channel_q = xQueueCreate(2, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    ASSERT(mock_llm_push_result(ESP_OK, response_json));
    agent_test_process_message("long output test");

    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strlen(text) == strlen(long_text));
    ASSERT(strcmp(text, long_text) == 0);

    vQueueDelete(channel_q);
    return 0;
}

TEST(start_command_bypasses_llm_and_debounces)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    agent_test_process_message("/start");

    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_ratelimit_record_count() == 0);

    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "zclaw online.") != NULL);

    // Immediate duplicate should be suppressed to stop burst spam.
    agent_test_process_message("/start");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 0);

    vQueueDelete(channel_q);
    return 0;
}

TEST(stop_and_resume_pause_message_processing)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];
    const char *success =
        "{\"content\":[{\"type\":\"text\",\"text\":\"normal response\"}],\"stop_reason\":\"end_turn\"}";

    reset_state();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    agent_test_process_message("/stop");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "zclaw paused.") != NULL);

    // While paused, regular messages are ignored and never hit the LLM.
    agent_test_process_message("hello");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 0);

    // /start should also be ignored while paused.
    agent_test_process_message("/start");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 0);

    agent_test_process_message("/resume");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "zclaw resumed.") != NULL);

    ASSERT(mock_llm_push_result(ESP_OK, success));
    agent_test_process_message("hello");
    ASSERT(mock_llm_request_count() == 1);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "normal response");

    vQueueDelete(channel_q);
    return 0;
}

TEST(panel_command_shows_status_without_talking)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();
    mock_display_reset();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    mock_display_set_running(true);
    agent_test_process_message("/panel");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_display_last_status_ms() == 30000);
    ASSERT(mock_display_talking_calls() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "Panel") != NULL);

    mock_display_set_running(false);
    agent_test_process_message("/panel");
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "no disponible") != NULL);

    vQueueDelete(channel_q);
    return 0;
}

TEST(dashboard_commands_request_remote_pages)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();
    mock_display_reset();
    mock_display_set_running(true);

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    agent_test_process_message("/clima");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(strcmp(mock_display_last_dashboard(), "clima") == 0);
    ASSERT(mock_display_last_dashboard_ms() == 30000);
    ASSERT(mock_display_talking_calls() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "Clima") != NULL);

    agent_test_process_message("/cripto");
    ASSERT(strcmp(mock_display_last_dashboard(), "cripto") == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "Cripto") != NULL);

    mock_display_set_running(false);
    agent_test_process_message("/clima");
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "no disponible") != NULL);

    vQueueDelete(channel_q);
    return 0;
}

TEST(help_and_settings_commands_bypass_llm)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    agent_test_process_message("/help");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "zclaw online.") != NULL);

    agent_test_process_message("/settings");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "Message intake: active") != NULL);

    // /settings should remain available while paused.
    agent_test_process_message("/stop");
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    agent_test_process_message("/settings");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "Message intake: paused") != NULL);

    vQueueDelete(channel_q);
    return 0;
}

TEST(diag_command_bypasses_llm_and_uses_tool)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    agent_test_process_message("/diag memory verbose");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_tools_execute_calls() == 1);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "mock tool executed");

    // /diag should remain available while paused.
    agent_test_process_message("/stop");
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    agent_test_process_message("/diag all");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_tools_execute_calls() == 2);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "mock tool executed");

    vQueueDelete(channel_q);
    return 0;
}

TEST(diag_command_rejects_invalid_args)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();

    channel_q = xQueueCreate(2, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    agent_test_process_message("/diag bananas");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_tools_execute_calls() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "unknown /diag argument") != NULL);

    vQueueDelete(channel_q);
    return 0;
}

TEST(gpio_command_bypasses_llm_and_uses_tool)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    agent_test_process_message("/gpio all");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_tools_execute_calls() == 1);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "mock tool executed");

    agent_test_process_message("/stop");
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    agent_test_process_message("/gpio 7");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_tools_execute_calls() == 2);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "mock tool executed");

    agent_test_process_message("/gpio 9 low");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_tools_execute_calls() == 3);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "mock tool executed");

    vQueueDelete(channel_q);
    return 0;
}

TEST(gpio_command_rejects_invalid_args)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();

    channel_q = xQueueCreate(2, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    agent_test_process_message("/gpio nope");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_tools_execute_calls() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "unknown /gpio argument") != NULL);

    agent_test_process_message("/gpio all extra");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_tools_execute_calls() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "/gpio all does not take extra arguments") != NULL);

    agent_test_process_message("/gpio 9 sideways");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_tools_execute_calls() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "unknown GPIO state") != NULL);

    vQueueDelete(channel_q);
    return 0;
}

TEST(local_admin_commands_are_local_only)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();

    channel_q = xQueueCreate(2, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    agent_test_process_voice_message("/wifi status");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_tools_execute_calls() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "only available on the USB serial console") != NULL);
    ASSERT(local_admin_test_last_action() == LOCAL_ADMIN_ACTION_NONE);

    vQueueDelete(channel_q);
    return 0;
}

TEST(local_admin_commands_bypass_llm_and_report_state)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);
    local_admin_test_set_wifi_status("WiFi status: provisioned=yes safe_mode=no driver=started link=connected ssid=Trident ip=10.0.0.24 rssi=-77 last_reason=none");
    local_admin_test_set_wifi_scan("WiFi scan: 2 APs visible (top 2)\n- Trident rssi=-77 auth=WPA2_PSK ch=6 (2437MHz)\n- Guest rssi=-88 auth=OPEN ch=1 (2412MHz)");
    local_admin_set_safe_mode(true);
    local_admin_set_device_configured(true);
    mock_memory_set_kv("boot_count", "3");

    agent_test_process_message("/wifi status");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_tools_execute_calls() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "ssid=Trident") != NULL);

    agent_test_process_message("/wifi scan");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_tools_execute_calls() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "WiFi scan: 2 APs visible") != NULL);

    agent_test_process_message("/bootcount");
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_tools_execute_calls() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "persisted=3") != NULL);
    ASSERT(strstr(text, "safe_mode=yes") != NULL);

    vQueueDelete(channel_q);
    return 0;
}

TEST(local_admin_reboot_and_factory_reset_commands)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);
    mock_memory_set_kv("wifi_ssid", "Trident");
    mock_memory_set_kv("api_key", "test-key");

    agent_test_process_message("/factory-reset");
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "Run /factory-reset confirm") != NULL);
    ASSERT(local_admin_test_last_action() == LOCAL_ADMIN_ACTION_NONE);
    ASSERT(mock_memory_count() == 2);

    agent_test_process_message("/factory-reset confirm");
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "Factory reset confirmed") != NULL);
    ASSERT(local_admin_test_last_action() == LOCAL_ADMIN_ACTION_FACTORY_RESET_REBOOT);
    ASSERT(mock_memory_count() == 0);

    agent_test_process_message("/reboot");
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "Rebooting...");
    ASSERT(local_admin_test_last_action() == LOCAL_ADMIN_ACTION_REBOOT);

    vQueueDelete(channel_q);
    return 0;
}

TEST(persona_phrases_route_through_llm)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];
    const char *reply_one =
        "{\"content\":[{\"type\":\"text\",\"text\":\"handled by llm\"}],\"stop_reason\":\"end_turn\"}";
    const char *reply_two =
        "{\"content\":[{\"type\":\"text\",\"text\":\"through llm again\"}],\"stop_reason\":\"end_turn\"}";
    const char *last_request = NULL;

    reset_state();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    ASSERT(mock_llm_push_result(ESP_OK, reply_one));
    agent_test_process_message("set persona witty");
    ASSERT(mock_llm_request_count() == 1);
    ASSERT(mock_tools_execute_calls() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "handled by llm");

    last_request = mock_llm_last_request_json();
    ASSERT(last_request != NULL);
    ASSERT(strstr(last_request, "set persona witty") != NULL);

    ASSERT(mock_llm_push_result(ESP_OK, reply_two));
    agent_test_process_message("show persona");
    ASSERT(mock_llm_request_count() == 2);
    ASSERT(mock_tools_execute_calls() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "through llm again");

    last_request = mock_llm_last_request_json();
    ASSERT(last_request != NULL);
    ASSERT(strstr(last_request, "show persona") != NULL);

    vQueueDelete(channel_q);
    return 0;
}

TEST(persona_can_change_via_llm_tool_call)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];
    const char *tool_call =
        "{\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_persona_1\","
        "\"name\":\"set_persona\",\"input\":{\"persona\":\"friendly\"}}],"
        "\"stop_reason\":\"tool_use\"}";
    const char *final_text =
        "{\"content\":[{\"type\":\"text\",\"text\":\"persona changed\"}],\"stop_reason\":\"end_turn\"}";
    const char *last_request = NULL;

    reset_state();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    ASSERT(mock_llm_push_result(ESP_OK, tool_call));
    ASSERT(mock_llm_push_result(ESP_OK, final_text));

    agent_test_process_message("please switch your personality to friendly");

    ASSERT(mock_llm_request_count() == 2);
    ASSERT(mock_tools_execute_calls() == 1);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "persona changed");

    last_request = mock_llm_last_request_json();
    ASSERT(last_request != NULL);
    ASSERT(strstr(last_request, "Device target is") != NULL);
    ASSERT(strstr(last_request, "Persona mode is 'friendly'") != NULL);

    vQueueDelete(channel_q);
    return 0;
}

TEST(cron_trigger_blocks_cron_set_tool_call)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];
    const char *tool_call =
        "{\"content\":[{\"type\":\"tool_use\",\"id\":\"toolu_cron_1\","
        "\"name\":\"cron_set\",\"input\":{\"type\":\"once\",\"delay_minutes\":1,"
        "\"action\":\"arcade_power state=1\"}}],\"stop_reason\":\"tool_use\"}";
    const char *final_text =
        "{\"content\":[{\"type\":\"text\",\"text\":\"running scheduled action now\"}],"
        "\"stop_reason\":\"end_turn\"}";
    const char *last_request = NULL;

    reset_state();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    ASSERT(mock_llm_push_result(ESP_OK, tool_call));
    ASSERT(mock_llm_push_result(ESP_OK, final_text));

    agent_test_process_message("[CRON 1] arcade_power state=1");

    ASSERT(mock_llm_request_count() == 2);
    ASSERT(mock_tools_execute_calls() == 0);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "running scheduled action now");

    last_request = mock_llm_last_request_json();
    ASSERT(last_request != NULL);
    ASSERT(strstr(last_request, "cron_set is not allowed during scheduled task execution") != NULL);

    vQueueDelete(channel_q);
    return 0;
}

TEST(repeated_non_command_is_suppressed)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];
    const char *success =
        "{\"content\":[{\"type\":\"text\",\"text\":\"hi there\"}],\"stop_reason\":\"end_turn\"}";

    reset_state();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    ASSERT(mock_llm_push_result(ESP_OK, success));
    agent_test_process_message("What can you do");
    ASSERT(mock_llm_request_count() == 1);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "hi there");

    // Immediate repeat should be dropped and not trigger another LLM call.
    agent_test_process_message("What can you do");
    ASSERT(mock_llm_request_count() == 1);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 0);

    vQueueDelete(channel_q);
    return 0;
}

TEST(repeated_non_command_not_suppressed_after_failure)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];
    const char *success =
        "{\"content\":[{\"type\":\"text\",\"text\":\"recovered\"}],\"stop_reason\":\"end_turn\"}";

    reset_state();

    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    ASSERT(mock_llm_push_result(ESP_FAIL, NULL));
    ASSERT(mock_llm_push_result(ESP_FAIL, NULL));
    ASSERT(mock_llm_push_result(ESP_FAIL, NULL));
    ASSERT(mock_llm_push_result(ESP_OK, success));

    agent_test_process_message("retry this");
    ASSERT(mock_llm_request_count() == 3);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "Error: Failed to contact LLM API after retries");
    ASSERT(mock_ratelimit_record_count() == 0);

    // The same message should be allowed immediately after a failed turn.
    agent_test_process_message("retry this");
    ASSERT(mock_llm_request_count() == 4);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT_STR_EQ(text, "recovered");
    ASSERT(mock_ratelimit_record_count() == 1);

    vQueueDelete(channel_q);
    return 0;
}

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
    agent_test_set_queues(channel_q);
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
    agent_test_set_queues(channel_q);
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
    agent_test_set_queues(channel_q);

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
    agent_test_set_queues(channel_q);

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

TEST(screen_hint_in_prompt_only_with_display)
{
    QueueHandle_t channel_q;
    const char *reply =
        "{\"content\":[{\"type\":\"text\",\"text\":\"ok\"}],\"stop_reason\":\"end_turn\"}";

    reset_state();
    mock_display_reset();
    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    mock_display_set_running(true);
    ASSERT(mock_llm_push_result(ESP_OK, reply));
    agent_test_process_message("hola");
    ASSERT(strstr(mock_llm_last_request_json(), "240x240 screen") != NULL);
    ASSERT(strstr(mock_llm_last_request_json(), "never use markdown") != NULL);
    ASSERT(strstr(mock_llm_last_request_json(), "call display_dashboard instead of answering with text") != NULL);

    mock_display_set_running(false);
    ASSERT(mock_llm_push_result(ESP_OK, reply));
    agent_test_process_message("adios");
    ASSERT(strstr(mock_llm_last_request_json(), "240x240 screen") == NULL);

    vQueueDelete(channel_q);
    mock_display_reset();
    return 0;
}

#include "mock_task_log.h"

TEST(cron_reply_goes_to_task_log)
{
    QueueHandle_t channel_q;
    const char *reply =
        "{\"content\":[{\"type\":\"text\",\"text\":\"Hora de la medicina.\"}],\"stop_reason\":\"end_turn\"}";

    reset_state();
    mock_task_log_reset();
    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);
    ASSERT(mock_llm_push_result(ESP_OK, reply));

    agent_test_process_message("[CRON 5] recuerdame la medicina");

    ASSERT(mock_task_log_calls() == 1);
    ASSERT_STR_EQ(mock_task_log_last_action(), "recuerdame la medicina");
    ASSERT_STR_EQ(mock_task_log_last_reply(), "Hora de la medicina.");

    vQueueDelete(channel_q);
    return 0;
}

TEST(serial_reply_is_not_logged)
{
    QueueHandle_t channel_q;
    const char *reply =
        "{\"content\":[{\"type\":\"text\",\"text\":\"Hola.\"}],\"stop_reason\":\"end_turn\"}";

    reset_state();
    mock_task_log_reset();
    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);
    ASSERT(mock_llm_push_result(ESP_OK, reply));

    agent_test_process_message("hola");

    ASSERT(mock_task_log_calls() == 0);

    vQueueDelete(channel_q);
    return 0;
}

#include "mock_voice.h"

TEST(voice_reply_shows_dijiste_header)
{
    QueueHandle_t channel_q;
    const char *reply =
        "{\"content\":[{\"type\":\"text\",\"text\":\"Son las tres.\"}],\"stop_reason\":\"end_turn\"}";

    reset_state();
    mock_display_reset();
    mock_display_set_running(true);
    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);
    ASSERT(mock_llm_push_result(ESP_OK, reply));

    agent_test_process_voice_message("que hora es");

    ASSERT(mock_display_answer_calls() == 1);
    ASSERT_STR_EQ(mock_display_last_answer_label(), "Dijiste:");
    ASSERT_STR_EQ(mock_display_last_answer_prompt(), "que hora es");
    ASSERT_STR_EQ(mock_display_last_answer_reply(), "Son las tres.");

    vQueueDelete(channel_q);
    mock_display_reset();
    return 0;
}

TEST(repeated_voice_message_is_answered_twice)
{
    QueueHandle_t channel_q;
    const char *reply =
        "{\"content\":[{\"type\":\"text\",\"text\":\"Hola.\"}],\"stop_reason\":\"end_turn\"}";

    reset_state();
    channel_q = xQueueCreate(4, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);
    ASSERT(mock_llm_push_result(ESP_OK, reply));
    ASSERT(mock_llm_push_result(ESP_OK, reply));

    agent_test_process_voice_message("hola fidel");
    agent_test_process_voice_message("hola fidel");

    ASSERT(mock_llm_request_count() == 2);

    vQueueDelete(channel_q);
    return 0;
}

TEST(escucha_and_mic_commands)
{
    QueueHandle_t channel_q;
    char text[CHANNEL_TX_BUF_SIZE];

    reset_state();
    mock_display_reset();
    mock_voice_reset();
    channel_q = xQueueCreate(8, sizeof(channel_output_msg_t));
    ASSERT(channel_q != NULL);
    agent_test_set_queues(channel_q);

    mock_voice_set_running(true);
    agent_test_process_message("/escucha");
    ASSERT(mock_voice_listen_calls() == 1);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "escucho") != NULL);
    agent_test_process_message("/mic");
    ASSERT(mock_voice_last_probe_ms() == 20000);
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "Micr") != NULL);
    ASSERT(mock_llm_request_count() == 0);
    ASSERT(mock_display_answer_calls() == 0);

    mock_voice_set_running(false);
    agent_test_process_message("/escucha");
    ASSERT(recv_channel_text(channel_q, text, sizeof(text)) == 1);
    ASSERT(strstr(text, "Voz no disponible") != NULL);

    vQueueDelete(channel_q);
    mock_voice_reset();
    return 0;
}

int test_agent_all(void)
{
    int failures = 0;

    printf("\nAgent Tests:\n");

    printf("  retries_with_backoff_and_fanout... ");
    if (test_retries_with_backoff_and_fanout() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  rate_limit_short_circuit... ");
    if (test_rate_limit_short_circuit() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  fails_after_max_retries_without_extra_sleep... ");
    if (test_fails_after_max_retries_without_extra_sleep() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  failed_turn_does_not_pollute_followup_prompt... ");
    if (test_failed_turn_does_not_pollute_followup_prompt() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  channel_output_allows_long_response... ");
    if (test_channel_output_allows_long_response() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  start_command_bypasses_llm_and_debounces... ");
    if (test_start_command_bypasses_llm_and_debounces() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  stop_and_resume_pause_message_processing... ");
    if (test_stop_and_resume_pause_message_processing() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  help_and_settings_commands_bypass_llm... ");
    if (test_help_and_settings_commands_bypass_llm() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  panel_command_shows_status_without_talking... ");
    if (test_panel_command_shows_status_without_talking() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  dashboard_commands_request_remote_pages... ");
    if (test_dashboard_commands_request_remote_pages() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  diag_command_bypasses_llm_and_uses_tool... ");
    if (test_diag_command_bypasses_llm_and_uses_tool() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  diag_command_rejects_invalid_args... ");
    if (test_diag_command_rejects_invalid_args() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  gpio_command_bypasses_llm_and_uses_tool... ");
    if (test_gpio_command_bypasses_llm_and_uses_tool() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  gpio_command_rejects_invalid_args... ");
    if (test_gpio_command_rejects_invalid_args() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  local_admin_commands_are_local_only... ");
    if (test_local_admin_commands_are_local_only() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  local_admin_commands_bypass_llm_and_report_state... ");
    if (test_local_admin_commands_bypass_llm_and_report_state() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  local_admin_reboot_and_factory_reset_commands... ");
    if (test_local_admin_reboot_and_factory_reset_commands() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  persona_phrases_route_through_llm... ");
    if (test_persona_phrases_route_through_llm() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  persona_can_change_via_llm_tool_call... ");
    if (test_persona_can_change_via_llm_tool_call() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  cron_trigger_blocks_cron_set_tool_call... ");
    if (test_cron_trigger_blocks_cron_set_tool_call() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  repeated_non_command_is_suppressed... ");
    if (test_repeated_non_command_is_suppressed() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  repeated_non_command_not_suppressed_after_failure... ");
    if (test_repeated_non_command_not_suppressed_after_failure() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

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

    printf("  screen_hint_in_prompt_only_with_display... ");
    if (test_screen_hint_in_prompt_only_with_display() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  cron_reply_goes_to_task_log... ");
    if (test_cron_reply_goes_to_task_log() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  serial_reply_is_not_logged... ");
    if (test_serial_reply_is_not_logged() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  voice_reply_shows_dijiste_header... ");
    if (test_voice_reply_shows_dijiste_header() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  repeated_voice_message_is_answered_twice... ");
    if (test_repeated_voice_message_is_answered_twice() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    printf("  escucha_and_mic_commands... ");
    if (test_escucha_and_mic_commands() == 0) {
        printf("OK\n");
    } else {
        failures++;
    }

    return failures;
}
