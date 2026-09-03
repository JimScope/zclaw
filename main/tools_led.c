#include "tools_handlers.h"
#include "tools_common.h"
#include "config.h"
#include "led_strip.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979f
#endif

#define LED_STRIP_GPIO      CONFIG_ZCLAW_LED_STRIP_GPIO
#define LED_STRIP_COUNT     1
#define LED_BLINK_MAX_TIMES 20
#define LED_BLINK_MAX_MS    2000
#define LED_BLINK_DEF_MS    300

static const char *TAG = "tools_led";
static led_strip_handle_t s_strip;

// --- "Thinking" animation: fluid AI-style gradient while a request runs ---
#define ANIM_FRAME_MS   25      // ~40 fps
#define ANIM_CYCLE_MS   2600    // full palette sweep duration
#define ANIM_BRIGHTNESS 0.55f

static SemaphoreHandle_t s_led_mutex;
static TaskHandle_t s_anim_task;
static volatile bool s_anim_active;
static volatile bool s_anim_hands_off;  // a led tool took over; leave LED alone

// Gemini-like palette, looped and balanced so warm tones get equal time
// (blue-heavy stops kept to a third of the cycle).
static const uint8_t ANIM_PALETTE[][3] = {
    {30,  100, 235},   // azul
    {130, 60,  220},   // violeta
    {225, 60,  150},   // magenta
    {245, 110, 60},    // coral
    {230, 90,  140},   // rosa
    {150, 70,  200},   // purpura
};
#define ANIM_STEPS   (int)(sizeof(ANIM_PALETTE) / sizeof(ANIM_PALETTE[0]))
#define ANIM_FADE_MS 400

static uint8_t s_cur_r, s_cur_g, s_cur_b;

static void led_lock(void)
{
    if (s_led_mutex) {
        xSemaphoreTake(s_led_mutex, portMAX_DELAY);
    }
}

static void led_unlock(void)
{
    if (s_led_mutex) {
        xSemaphoreGive(s_led_mutex);
    }
}

static bool led_strip_ensure(void);

static void led_anim_draw(uint8_t r, uint8_t g, uint8_t b)
{
    led_lock();
    if (led_strip_ensure()) {
        led_strip_set_pixel(s_strip, 0, r, g, b);
        led_strip_refresh(s_strip);
    }
    led_unlock();
    s_cur_r = r;
    s_cur_g = g;
    s_cur_b = b;
}

static void led_anim_fade_out(void)
{
    const int steps = ANIM_FADE_MS / ANIM_FRAME_MS;
    uint8_t r0 = s_cur_r, g0 = s_cur_g, b0 = s_cur_b;

    for (int i = steps - 1; i >= 0 && !s_anim_active && !s_anim_hands_off; i--) {
        float k = (1.0f - cosf(((float)i / steps) * (float)M_PI)) * 0.5f;
        led_anim_draw((uint8_t)(r0 * k), (uint8_t)(g0 * k), (uint8_t)(b0 * k));
        vTaskDelay(pdMS_TO_TICKS(ANIM_FRAME_MS));
    }
    if (!s_anim_active && !s_anim_hands_off) {
        led_lock();
        if (s_strip) {
            led_strip_clear(s_strip);
        }
        led_unlock();
        s_cur_r = s_cur_g = s_cur_b = 0;
    }
}

static void led_anim_task(void *arg)
{
    (void)arg;
    uint32_t t_ms = 0;
    bool was_drawing = false;

    for (;;) {
        if (!s_anim_active || s_anim_hands_off) {
            if (was_drawing && !s_anim_hands_off) {
                led_anim_fade_out();
            }
            was_drawing = false;
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            t_ms = 0;
            continue;
        }

        // Continuous interpolation along the looped palette with cosine
        // easing so transitions flow instead of stepping.
        float phase = ((float)(t_ms % ANIM_CYCLE_MS) / ANIM_CYCLE_MS) * ANIM_STEPS;
        int seg = (int)phase;
        float f = phase - (float)seg;
        f = (1.0f - cosf(f * (float)M_PI)) * 0.5f;

        // Soft fade-in over the first ANIM_FADE_MS.
        float ramp = 1.0f;
        if (t_ms < ANIM_FADE_MS) {
            ramp = (1.0f - cosf(((float)t_ms / ANIM_FADE_MS) * (float)M_PI)) * 0.5f;
        }

        const uint8_t *ca = ANIM_PALETTE[seg % ANIM_STEPS];
        const uint8_t *cb = ANIM_PALETTE[(seg + 1) % ANIM_STEPS];
        float scale = ANIM_BRIGHTNESS * ramp;
        led_anim_draw((uint8_t)((ca[0] + (cb[0] - ca[0]) * f) * scale),
                      (uint8_t)((ca[1] + (cb[1] - ca[1]) * f) * scale),
                      (uint8_t)((ca[2] + (cb[2] - ca[2]) * f) * scale));
        was_drawing = true;

        vTaskDelay(pdMS_TO_TICKS(ANIM_FRAME_MS));
        t_ms += ANIM_FRAME_MS;
    }
}

void led_thinking_start(void)
{
    if (!s_led_mutex) {
        s_led_mutex = xSemaphoreCreateMutex();
    }
    if (!s_anim_task &&
        xTaskCreate(led_anim_task, "led_anim", 2560, NULL, 2, &s_anim_task) != pdPASS) {
        ESP_LOGW(TAG, "led_anim task create failed");
        s_anim_task = NULL;
        return;
    }
    s_anim_hands_off = false;
    s_anim_active = true;
    if (s_anim_task) {
        xTaskNotifyGive(s_anim_task);
    }
}

void led_thinking_stop(void)
{
    // The animation task notices within one frame and fades out on its own
    // (unless a led tool took over, in which case the LED is left alone).
    s_anim_active = false;
}

// Called by led tools before touching the strip: pauses the animation for
// the remainder of the current request.
static void led_anim_hands_off(void)
{
    s_anim_hands_off = true;
    // Let any in-flight frame finish before the tool writes.
    vTaskDelay(pdMS_TO_TICKS(ANIM_FRAME_MS + 5));
}

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

    led_anim_hands_off();
    led_lock();
    if (r == 0 && g == 0 && b == 0) {
        led_strip_clear(s_strip);
        led_unlock();
        snprintf(result, result_len, "LED off");
        return true;
    }

    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
    led_unlock();
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

    led_anim_hands_off();
    led_lock();
    for (int i = 0; i < times; i++) {
        led_strip_set_pixel(s_strip, 0, r, g, b);
        led_strip_refresh(s_strip);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        led_strip_clear(s_strip);
        if (i < times - 1) {
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
    led_unlock();

    snprintf(result, result_len, "Blinked %d times rgb(%u,%u,%u)", times, r, g, b);
    return true;
}
