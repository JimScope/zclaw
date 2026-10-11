#include "display_status.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "display_gfx.h"
#include "display_hw.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "llm.h"
#include "ota.h"
#include "status_format.h"
#include "tools_handlers.h"

#define VALUE_X              (8 * 8)
#define ROW_H                20

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

    gfx_fill_rect(0, 0, DISPLAY_W, DISPLAY_H, GFX_BLACK);

    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    status_format_clock(&tm, a, sizeof(a));
    draw_centered(6, a, GFX_WHITE, 3);
    status_format_date(&tm, a, sizeof(a));
    draw_centered(58, a, GFX_GRAY, 1);

    int y = 86;
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
