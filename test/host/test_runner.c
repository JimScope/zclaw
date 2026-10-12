/*
 * Host-based test runner for zclaw
 * Runs on development machine without hardware
 */

#include <stdio.h>
#include <stdlib.h>

// Test declarations
extern int test_json_all(void);
extern int test_tools_parse_all(void);
extern int test_json_util_integration_all(void);
extern int test_runtime_utils_all(void);
extern int test_memory_keys_all(void);
extern int test_agent_all(void);
extern int test_tools_gpio_policy_all(void);
extern int test_builtin_tools_registry_all(void);
extern int test_tools_system_diag_all(void);
extern int test_llm_auth_all(void);
extern int test_wifi_credentials_all(void);
extern int test_text_latin1_all(void);
extern int test_frame_format_all(void);
extern int test_face_state_all(void);
extern int test_page_rotation_all(void);
extern int test_status_format_all(void);
extern int test_dash_url_all(void);
extern int test_jpeg_strip_all(void);
extern int test_dash_state_all(void);
extern int test_text_layout_all(void);
extern int test_agent_commands_all(void);
extern int test_tools_dashboard_all(void);
extern int test_backup_name_all(void);
extern int test_web_ota_policy_all(void);
extern int test_task_log_format_all(void);
extern int test_sd_access_all(void);
extern int test_voice_audio_all(void);
extern int test_voice_detect_all(void);
extern int test_stt_reply_all(void);
extern int test_agent_prompt_all(void);
extern int test_voice_buf_all(void);

int main(int argc, char *argv[])
{
    int failures = 0;
    (void)argc;
    (void)argv;

    printf("zclaw Host Tests\n");
    printf("===================\n\n");

    failures += test_json_all();
    failures += test_tools_parse_all();
    failures += test_json_util_integration_all();
    failures += test_runtime_utils_all();
    failures += test_memory_keys_all();
    failures += test_agent_all();
    failures += test_tools_gpio_policy_all();
    failures += test_builtin_tools_registry_all();
    failures += test_tools_system_diag_all();
    failures += test_llm_auth_all();
    failures += test_wifi_credentials_all();
    failures += test_text_latin1_all();
    failures += test_frame_format_all();
    failures += test_face_state_all();
    failures += test_page_rotation_all();
    failures += test_status_format_all();
    failures += test_dash_url_all();
    failures += test_jpeg_strip_all();
    failures += test_dash_state_all();
    failures += test_text_layout_all();
    failures += test_agent_commands_all();
    failures += test_tools_dashboard_all();
    failures += test_backup_name_all();
    failures += test_web_ota_policy_all();
    failures += test_task_log_format_all();
    failures += test_sd_access_all();
    failures += test_voice_audio_all();
    failures += test_voice_detect_all();
    failures += test_stt_reply_all();
    failures += test_agent_prompt_all();
    failures += test_voice_buf_all();

    printf("\n===================\n");
    if (failures == 0) {
        printf("All tests passed!\n");
        return 0;
    } else {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
}
