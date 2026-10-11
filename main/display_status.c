#include "display_status.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "display_gfx.h"
#include "display_hw.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "llm.h"
#include "ota.h"
#include "status_format.h"
#include "telegram.h"
#include "tools_handlers.h"
#include "weather.h"

#define WEATHER_REFRESH_MS   (30LL * 60 * 1000)
#define WEATHER_RETRY_MS     (5LL * 60 * 1000)
#define WEATHER_GATE_WAIT_MS 40000
#define WEATHER_TASK_STACK   8192
#define WEATHER_TASK_PRIO    2
#define VALUE_X              (8 * 8)
#define ROW_H                20

static const char *TAG = "display_status";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_weather_valid = false;
static int s_weather_temp_c = 0;
static int s_weather_code = 0;
static volatile bool s_weather_busy = false;
static int64_t s_next_weather_ms = 0;

static void weather_task(void *arg)
{
    (void)arg;
    weather_current_t w;
    char err[96];

    // The poll task re-takes the HTTP gate immediately, so pause it and wait
    // for the gate here; only this task waits, nobody else is blocked.
    telegram_pause_polling();
    bool ok = weather_fetch_current(false, 0.0f, 0.0f, pdMS_TO_TICKS(WEATHER_GATE_WAIT_MS),
                                    &w, err, sizeof(err));
    telegram_resume_polling();

    int64_t now_ms = esp_timer_get_time() / 1000;
    taskENTER_CRITICAL(&s_lock);
    if (ok) {
        s_weather_valid = true;
        s_weather_temp_c = (int)(w.temp_c + (w.temp_c >= 0 ? 0.5f : -0.5f));
        s_weather_code = w.code;
        s_next_weather_ms = now_ms + WEATHER_REFRESH_MS;
    } else {
        s_next_weather_ms = now_ms + WEATHER_RETRY_MS;
    }
    taskEXIT_CRITICAL(&s_lock);

    if (!ok) {
        ESP_LOGW(TAG, "Weather refresh failed: %s", err);
    }
    s_weather_busy = false;
    vTaskDelete(NULL);
}

void display_status_service(int64_t now_ms, bool wifi_up)
{
    int64_t next;

    if (!wifi_up || s_weather_busy) {
        return;
    }
    taskENTER_CRITICAL(&s_lock);
    next = s_next_weather_ms;
    taskEXIT_CRITICAL(&s_lock);
    if (now_ms < next) {
        return;
    }

    s_weather_busy = true;
    if (xTaskCreate(weather_task, "weather", WEATHER_TASK_STACK, NULL, WEATHER_TASK_PRIO, NULL) != pdPASS) {
        s_weather_busy = false;
        taskENTER_CRITICAL(&s_lock);
        s_next_weather_ms = now_ms + WEATHER_RETRY_MS;
        taskEXIT_CRITICAL(&s_lock);
        ESP_LOGW(TAG, "Could not start weather task");
    }
}

static void draw_row(int y, const char *label, const char *value)
{
    gfx_text(4, y, label, GFX_AMBER, GFX_BLACK, 1);
    gfx_text(VALUE_X, y, value, GFX_WHITE, GFX_BLACK, 1);
}

static void draw_centered(int y, const char *text, gfx_color_t color, int scale)
{
    int w = gfx_text_width(text, scale);
    gfx_text(w < DISPLAY_W ? (DISPLAY_W - w) / 2 : 0, y, text, color, GFX_BLACK, scale);
}

void display_status_draw(bool wifi_up)
{
    char a[32];
    char b[48];
    char row[64];
    bool weather_valid;
    int weather_temp;
    int weather_code;

    gfx_fill_rect(0, 0, DISPLAY_W, DISPLAY_H, GFX_BLACK);

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    status_format_clock(&tm, a, sizeof(a));
    draw_centered(6, a, GFX_WHITE, 3);
    status_format_date(&tm, a, sizeof(a));
    draw_centered(58, a, GFX_GRAY, 1);

    int y = 86;
    taskENTER_CRITICAL(&s_lock);
    weather_valid = s_weather_valid;
    weather_temp = s_weather_temp_c;
    weather_code = s_weather_code;
    taskEXIT_CRITICAL(&s_lock);
    if (weather_valid) {
        snprintf(b, sizeof(b), "%dC %s", weather_temp, weather_code_desc(weather_code));
    } else {
        snprintf(b, sizeof(b), "--");
    }
    draw_row(y, "Clima", b);
    y += ROW_H;

    wifi_ap_record_t ap;
    memset(&ap, 0, sizeof(ap));
    if (wifi_up && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        status_truncate_utf8((const char *)ap.ssid, 22, b, sizeof(b));
        draw_row(y, "WiFi", b);
        y += ROW_H;
        snprintf(b, sizeof(b), "%d dBm", (int)ap.rssi);
        draw_row(y, "Se\xC3\xB1" "al", b);
    } else {
        draw_row(y, "WiFi", "sin conexi\xC3\xB3n");
        y += ROW_H;
        draw_row(y, "Se\xC3\xB1" "al", "--");
    }
    y += ROW_H;

    float celsius = 0;
    if (tools_chip_temp_read(&celsius)) {
        status_format_temp_c((int)(celsius * 10.0f), b, sizeof(b));
    } else {
        snprintf(b, sizeof(b), "--");
    }
    draw_row(y, "CPU", b);
    y += ROW_H;

    char free_heap[16];
    char min_heap[16];
    status_format_kib(esp_get_free_heap_size(), free_heap, sizeof(free_heap));
    status_format_kib(esp_get_minimum_free_heap_size(), min_heap, sizeof(min_heap));
    snprintf(b, sizeof(b), "%s (min %s)", free_heap, min_heap);
    draw_row(y, "Heap", b);
    y += ROW_H;

    status_format_uptime((uint64_t)(esp_timer_get_time() / 1000000), b, sizeof(b));
    draw_row(y, "Uptime", b);
    y += ROW_H;

    snprintf(row, sizeof(row), "v%s %s", ota_get_version(), status_model_short(llm_get_model()));
    status_truncate_utf8(row, 29, b, sizeof(b));
    gfx_text(4, y, b, GFX_GRAY, GFX_BLACK, 1);
}
