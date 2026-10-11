#include "display.h"

#include "config.h"
#include "display_gfx.h"
#include "display_hw.h"
#include "esp_log.h"
#include "ota.h"
#include "sd_card.h"
#include "display_status.h"
#include "tools_handlers.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "display";

esp_err_t display_start(void)
{
#if CONFIG_ZCLAW_DISPLAY_ENABLE
    esp_err_t err = display_hw_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Display disabled: %s", esp_err_to_name(err));
        return err;
    }
    float warm = 0;
    (void)tools_chip_temp_read(&warm);  // install the sensor once
    // Bring-up self-test (replaced by the display task in Task 12).
    gfx_fill_rect(0, 0, DISPLAY_W, 40, GFX_RGB(255, 0, 0));
    gfx_fill_rect(0, 40, DISPLAY_W, 40, GFX_RGB(0, 255, 0));
    gfx_fill_rect(0, 80, DISPLAY_W, 40, GFX_RGB(0, 0, 255));
    gfx_text(8, 132, "Fidel", GFX_WHITE, GFX_BLACK, 3);
    gfx_text(8, 186, "se\xC3\xB1" "al \xC2\xBF" "ma\xC3\xB1" "ana?", GFX_AMBER, GFX_BLACK, 1);
    gfx_text(8, 210, ota_get_version(), GFX_GRAY, GFX_BLACK, 1);
    display_hw_wait_idle();
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (sd_card_service(esp_timer_get_time() / 1000)) {
        struct stat st;
        const char *path = SD_MOUNT_POINT "/face/neutral.r565";
        FILE *f = (stat(path, &st) == 0) ? fopen(path, "rb") : NULL;
        if (f) {
            int64_t t0 = esp_timer_get_time();
            esp_err_t ferr = gfx_draw_frame_file(f, (size_t)st.st_size, NULL);
            display_hw_wait_idle();
            fclose(f);
            ESP_LOGI(TAG, "neutral.r565: %s in %lu ms", esp_err_to_name(ferr),
                     (unsigned long)((esp_timer_get_time() - t0) / 1000));
        } else {
            ESP_LOGW(TAG, "%s not found", path);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(3000));
    display_status_draw(false);
    display_hw_wait_idle();
    return ESP_OK;
#else
    (void)TAG;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void display_notify_thinking(bool active) { (void)active; }
void display_notify_talking(size_t reply_chars) { (void)reply_chars; }
void display_notify_wifi(bool connected) { (void)connected; }
