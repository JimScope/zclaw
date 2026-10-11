#include "display.h"

#include "config.h"
#include "display_gfx.h"
#include "display_hw.h"
#include "esp_log.h"
#include "ota.h"

static const char *TAG = "display";

esp_err_t display_start(void)
{
#if CONFIG_ZCLAW_DISPLAY_ENABLE
    esp_err_t err = display_hw_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Display disabled: %s", esp_err_to_name(err));
        return err;
    }
    // Bring-up self-test (replaced by the display task in Task 12).
    gfx_fill_rect(0, 0, DISPLAY_W, 40, GFX_RGB(255, 0, 0));
    gfx_fill_rect(0, 40, DISPLAY_W, 40, GFX_RGB(0, 255, 0));
    gfx_fill_rect(0, 80, DISPLAY_W, 40, GFX_RGB(0, 0, 255));
    gfx_text(8, 132, "Fidel", GFX_WHITE, GFX_BLACK, 3);
    gfx_text(8, 186, "se\xC3\xB1" "al \xC2\xBF" "ma\xC3\xB1" "ana?", GFX_AMBER, GFX_BLACK, 1);
    gfx_text(8, 210, ota_get_version(), GFX_GRAY, GFX_BLACK, 1);
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
