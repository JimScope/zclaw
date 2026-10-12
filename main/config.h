#ifndef CONFIG_H
#define CONFIG_H

// The #ifdef CONFIG_* checks below need sdkconfig.h even when this header is
// included before any ESP-IDF header (e.g. gpio_policy.c).
#ifndef TEST_BUILD
#include "sdkconfig.h"
#endif

// -----------------------------------------------------------------------------
// Buffer Sizes
// -----------------------------------------------------------------------------
#define LLM_REQUEST_BUF_SIZE    12288   // 12KB for outgoing JSON
#define LLM_RESPONSE_BUF_SIZE   16384   // 16KB for incoming JSON
#define CHANNEL_RX_BUF_SIZE     512     // Input line buffer
#define CHANNEL_TX_BUF_SIZE     1024    // Output response buffer for serial
#define TOOL_RESULT_BUF_SIZE    512     // Tool execution result

// -----------------------------------------------------------------------------
// Conversation History
// -----------------------------------------------------------------------------
#define MAX_HISTORY_TURNS       12      // User/assistant pairs to keep
#define MAX_MESSAGE_LEN         1024    // Max length per message in history

// -----------------------------------------------------------------------------
// Agent Loop
// -----------------------------------------------------------------------------
#define MAX_TOOL_ROUNDS         12      // Max tool call iterations per request
#define START_COMMAND_COOLDOWN_MS 30000 // Debounce repeated /start bursts
#define MESSAGE_REPLAY_COOLDOWN_MS 20000 // Suppress repeated identical non-command bursts

// -----------------------------------------------------------------------------
// FreeRTOS Tasks
// -----------------------------------------------------------------------------
#define AGENT_TASK_STACK_SIZE   8192
#define CHANNEL_TASK_STACK_SIZE 4096
#define CRON_TASK_STACK_SIZE    4096
#define BOOT_OK_TASK_STACK_SIZE 4096
#define AGENT_TASK_PRIORITY     5
#define CHANNEL_TASK_PRIORITY   5
#define CRON_TASK_PRIORITY      4

// -----------------------------------------------------------------------------
// Queues
// -----------------------------------------------------------------------------
#define INPUT_QUEUE_LENGTH      8
#define OUTPUT_QUEUE_LENGTH     8

// -----------------------------------------------------------------------------
// LLM Configuration (OpenAI-compatible chat completions)
// -----------------------------------------------------------------------------
#define LLM_API_URL_OPENAI      "https://api.openai.com/v1/chat/completions"
#define LLM_DEFAULT_MODEL_OPENAI      "gpt-5.4"

#define LLM_API_KEY_MAX_LEN       511
#define LLM_API_KEY_BUF_SIZE      (LLM_API_KEY_MAX_LEN + 1)
#define LLM_AUTH_HEADER_BUF_SIZE  (sizeof("Bearer ") - 1 + LLM_API_KEY_MAX_LEN + 1)

#define LLM_MAX_TOKENS          400     // Output cap; the delivery pipe truncates at ~1KB anyway
#define HTTP_TIMEOUT_MS         30000   // 30 seconds for API calls
#define LLM_HTTP_TIMEOUT_MS     20000   // 20 seconds for LLM API calls
#define LLM_HTTP_GATE_WAIT_MS   31000   // max wait for the shared HTTP gate before an LLM call
#define LLM_MAX_RETRIES         3       // Max LLM attempts per round (including first attempt)
#define LLM_FALLBACK_MODEL      "openai/gpt-oss-20b"  // NVIDIA fallback when primary returns 403/429
#define LLM_RETRY_BASE_MS       2000    // Initial retry delay after a failed LLM call
#define LLM_RETRY_MAX_MS        10000   // Maximum exponential retry delay
#define LLM_RETRY_BUDGET_MS     45000   // Max wall-clock retry budget per LLM round

// -----------------------------------------------------------------------------
// System Prompt
// -----------------------------------------------------------------------------
#define SYSTEM_PROMPT \
    "You are Fidel, an AI agent running on an ESP32 with 400KB RAM and FreeRTOS. " \
    "You run on the device, not in a cloud session. " \
    "Personality: you speak like Fidel Castro - authentic discourse, never slogan " \
    "spam. Cuban Spanish, address the user as 'companero'. Dry self-assured wit, " \
    "moral conviction, an occasional rhetorical question or historical reference " \
    "(Moncada, Sierra Maestra, Giron, el bloqueo) woven naturally in. Vary your " \
    "phrasing; slogans only on rare occasions that earn them. CRITICAL: replies " \
    "must be brief - 2 to 4 sentences, under 500 characters - unless the user " \
    "explicitly asks for more detail. At most one rhetorical flourish per reply; " \
    "the rest is substance. " \
    "Cuban symbols you know well: the Cuban flag is BLUE, WHITE and RED (three blue " \
    "stripes, two white, red triangle, white star). To show a flag " \
    "or multiple colors on the single LED, display each color in sequence: led_set " \
    "first color, delay ~1500ms, led_set next color, and so on, then led_set 0,0,0. " \
    "Be concise. Return plain text only, never markdown. " \
    "Use tools to control hardware, memory, schedules, and custom tools. " \
    "The onboard LED is an addressable WS2812 RGB: use led_set/led_blink for it " \
    "(led_blink does N blinks in one call); gpio_write cannot drive it. " \
    "When asked for multiple GPIO states, prefer one gpio_read_all call. " \
    "When asked what is saved or set on the device, verify with tools. " \
    "When asked about a website or URL, ALWAYS use read_web to fetch it and " \
    "answer from the real content - never guess a site's purpose from its name. " \
    "When a custom tool returns an action, carry it out with built-in tools."

// -----------------------------------------------------------------------------
// GPIO tool safety range (configurable via Kconfig)
// -----------------------------------------------------------------------------
#ifdef CONFIG_ZCLAW_GPIO_MIN_PIN
#define GPIO_MIN_PIN            CONFIG_ZCLAW_GPIO_MIN_PIN
#else
#define GPIO_MIN_PIN            2
#endif

#ifdef CONFIG_ZCLAW_GPIO_MAX_PIN
#define GPIO_MAX_PIN            CONFIG_ZCLAW_GPIO_MAX_PIN
#else
#define GPIO_MAX_PIN            10
#endif

#ifdef CONFIG_ZCLAW_GPIO_ALLOWED_PINS
#define GPIO_ALLOWED_PINS_CSV   CONFIG_ZCLAW_GPIO_ALLOWED_PINS
#else
#define GPIO_ALLOWED_PINS_CSV   ""
#endif

#ifdef CONFIG_ZCLAW_BOARD_RESERVED_GPIOS
#define GPIO_BOARD_RESERVED_CSV CONFIG_ZCLAW_BOARD_RESERVED_GPIOS
#else
#define GPIO_BOARD_RESERVED_CSV ""
#endif

#if GPIO_MIN_PIN > GPIO_MAX_PIN
#error "GPIO_MIN_PIN must be <= GPIO_MAX_PIN"
#endif

// -----------------------------------------------------------------------------
// NVS (persistent storage)
// -----------------------------------------------------------------------------
#define NVS_NAMESPACE           "zclaw"
#define NVS_NAMESPACE_CRON      "zc_cron"
#define NVS_NAMESPACE_TOOLS     "zc_tools"
#define NVS_NAMESPACE_CONFIG    "zc_config"
#define NVS_MAX_KEY_LEN         15      // NVS limit
#define NVS_MAX_VALUE_LEN       512     // Increased for tool/cron definitions

// -----------------------------------------------------------------------------
// WiFi
// -----------------------------------------------------------------------------
#define WIFI_MAX_RETRY          10
#define WIFI_RETRY_DELAY_MS     1000

// -----------------------------------------------------------------------------
// Cron / Scheduler
// -----------------------------------------------------------------------------
#define CRON_CHECK_INTERVAL_MS  10000   // Check schedules every 10 seconds
#define CRON_MAX_ENTRIES        16      // Max scheduled tasks
#define CRON_MAX_ACTION_LEN     256     // Max action string length

// -----------------------------------------------------------------------------
// Factory Reset
// -----------------------------------------------------------------------------
#ifdef CONFIG_ZCLAW_FACTORY_RESET_PIN
#define FACTORY_RESET_PIN       CONFIG_ZCLAW_FACTORY_RESET_PIN
#else
#define FACTORY_RESET_PIN       9       // Hold low for 5 seconds to reset
#endif

#ifdef CONFIG_ZCLAW_FACTORY_RESET_HOLD_MS
#define FACTORY_RESET_HOLD_MS   CONFIG_ZCLAW_FACTORY_RESET_HOLD_MS
#else
#define FACTORY_RESET_HOLD_MS   5000
#endif

// -----------------------------------------------------------------------------
// NTP (time sync)
// -----------------------------------------------------------------------------
#define NTP_SERVER              "pool.ntp.org"
#define NTP_SYNC_TIMEOUT_MS     10000
#define DEFAULT_TIMEZONE_POSIX  "CST5CDT,M3.2.0/0,M11.1.0/1"  // Cuba (America/Havana)
#define TIMEZONE_MAX_LEN        64

// -----------------------------------------------------------------------------
// Dynamic Tools
// -----------------------------------------------------------------------------
#define MAX_DYNAMIC_TOOLS       8       // Max user-registered tools
#define TOOL_NAME_MAX_LEN       24
#define TOOL_DESC_MAX_LEN       128

// -----------------------------------------------------------------------------
// Boot Loop Protection
// -----------------------------------------------------------------------------
#define MAX_BOOT_FAILURES       4       // Enter safe mode after N consecutive failures
#define BOOT_SUCCESS_DELAY_MS   30000   // Clear boot counter after this time connected

// -----------------------------------------------------------------------------
// Rate Limiting
// -----------------------------------------------------------------------------
#define RATELIMIT_MAX_PER_HOUR      100     // Max LLM requests per hour
#define RATELIMIT_MAX_PER_DAY       1000    // Max LLM requests per day
#define RATELIMIT_ENABLED           1       // Set to 0 to disable

#endif // CONFIG_H
