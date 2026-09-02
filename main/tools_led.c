#include "tools_handlers.h"
#include "tools_common.h"
#include "config.h"
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

#define LED_STRIP_GPIO      CONFIG_ZCLAW_LED_STRIP_GPIO
#define LED_STRIP_COUNT     1
#define LED_BLINK_MAX_TIMES 20
#define LED_BLINK_MAX_MS    2000
#define LED_BLINK_DEF_MS    300

static const char *TAG = "tools_led";
static led_strip_handle_t s_strip;

static bool led_strip_ensure(void)
{
    if (s_strip) {
        return true;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed: %s", esp_err_to_name(err));
        s_strip = NULL;
        return false;
    }
    return true;
}

static int json_int_or(const cJSON *input, const char *key, int fallback)
{
    const cJSON *item = cJSON_GetObjectItem(input, key);
    if (!item || !cJSON_IsNumber(item)) {
        return fallback;
    }
    return item->valueint;
}

static bool clamp_u8(int value, uint8_t *out)
{
    if (value < 0 || value > 255) {
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

bool tools_led_set_handler(const cJSON *input, char *result, size_t result_len)
{
    uint8_t r, g, b;

    if (!clamp_u8(json_int_or(input, "r", 0), &r) ||
        !clamp_u8(json_int_or(input, "g", 0), &g) ||
        !clamp_u8(json_int_or(input, "b", 0), &b)) {
        snprintf(result, result_len, "Error: r/g/b must be 0-255");
        return false;
    }

    if (!led_strip_ensure()) {
        snprintf(result, result_len, "Error: LED strip init failed (GPIO %d)", LED_STRIP_GPIO);
        return false;
    }

    if (r == 0 && g == 0 && b == 0) {
        led_strip_clear(s_strip);
        snprintf(result, result_len, "LED off");
        return true;
    }

    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
    snprintf(result, result_len, "LED set to rgb(%u,%u,%u)", r, g, b);
    return true;
}

bool tools_led_blink_handler(const cJSON *input, char *result, size_t result_len)
{
    int times = json_int_or(input, "times", 0);
    int on_ms = json_int_or(input, "on_ms", LED_BLINK_DEF_MS);
    int off_ms = json_int_or(input, "off_ms", LED_BLINK_DEF_MS);
    uint8_t r, g, b;

    if (times < 1 || times > LED_BLINK_MAX_TIMES) {
        snprintf(result, result_len, "Error: 'times' required (1-%d)", LED_BLINK_MAX_TIMES);
        return false;
    }
    if (on_ms < 10 || on_ms > LED_BLINK_MAX_MS || off_ms < 10 || off_ms > LED_BLINK_MAX_MS) {
        snprintf(result, result_len, "Error: on_ms/off_ms must be 10-%d", LED_BLINK_MAX_MS);
        return false;
    }
    if (!clamp_u8(json_int_or(input, "r", 128), &r) ||
        !clamp_u8(json_int_or(input, "g", 128), &g) ||
        !clamp_u8(json_int_or(input, "b", 128), &b)) {
        snprintf(result, result_len, "Error: r/g/b must be 0-255");
        return false;
    }

    if (!led_strip_ensure()) {
        snprintf(result, result_len, "Error: LED strip init failed (GPIO %d)", LED_STRIP_GPIO);
        return false;
    }

    for (int i = 0; i < times; i++) {
        led_strip_set_pixel(s_strip, 0, r, g, b);
        led_strip_refresh(s_strip);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        led_strip_clear(s_strip);
        if (i < times - 1) {
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }

    snprintf(result, result_len, "Blinked %d times rgb(%u,%u,%u)", times, r, g, b);
    return true;
}
