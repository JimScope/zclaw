#include "display_hw.h"

#include <string.h>

#include "board_spi.h"
#include "config.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#if CONFIG_ZCLAW_DISPLAY_ENABLE

#define DISPLAY_PCLK_HZ   (40 * 1000 * 1000)
#define BACKLIGHT_TIMER   LEDC_TIMER_0
#define BACKLIGHT_CHANNEL LEDC_CHANNEL_0
// 80 MHz / 1024 steps: far above the audio band. At 5 kHz the PWM ripple on 3V3
// reached the microphone as a 5 kHz tone 20 dB over the room noise.
#define BACKLIGHT_PWM_HZ  78125

static const char *TAG = "display_hw";
static esp_lcd_panel_io_handle_t s_io = NULL;
static esp_lcd_panel_handle_t s_panel = NULL;
static uint16_t *s_buf[2] = {NULL, NULL};
static int s_next_buf = 0;
static SemaphoreHandle_t s_free = NULL;
static bool s_ready = false;
static uint8_t s_brightness = 0;

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata,
                          void *ctx)
{
    (void)io;
    (void)edata;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_free, &woken);
    return woken == pdTRUE;
}

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BACKLIGHT_TIMER,
        .freq_hz = BACKLIGHT_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Backlight PWM at %d Hz failed: %s", BACKLIGHT_PWM_HZ, esp_err_to_name(err));
    }

    ledc_channel_config_t channel = {
        .gpio_num = CONFIG_ZCLAW_DISPLAY_BL_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BACKLIGHT_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&channel);
    ledc_fade_func_install(0);
}

void display_hw_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_brightness = percent;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_CHANNEL, (1023u * percent) / 100u);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_CHANNEL);
}

void display_hw_fade(uint8_t percent, uint32_t ms)
{
    if (percent > 100) {
        percent = 100;
    }
    if (ms == 0) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_CHANNEL, (1023u * percent) / 100u);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, BACKLIGHT_CHANNEL);
        return;
    }
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, BACKLIGHT_CHANNEL, (1023u * percent) / 100u, (int)ms);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, BACKLIGHT_CHANNEL, LEDC_FADE_WAIT_DONE);
}

uint8_t display_hw_brightness(void)
{
    return s_brightness;
}

static void release_resources(void)
{
    if (s_panel) {
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_io) {
        esp_lcd_panel_io_del(s_io);
        s_io = NULL;
    }
    for (int i = 0; i < 2; i++) {
        heap_caps_free(s_buf[i]);
        s_buf[i] = NULL;
    }
    if (s_free) {
        vSemaphoreDelete(s_free);
        s_free = NULL;
    }
}

esp_err_t display_hw_init(void)
{
    esp_err_t err;

    if (s_ready) {
        return ESP_OK;
    }
    err = board_spi_init();
    if (err != ESP_OK) {
        return err;
    }

    s_free = xSemaphoreCreateCounting(2, 2);
    s_buf[0] = heap_caps_malloc(DISPLAY_STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
    s_buf[1] = heap_caps_malloc(DISPLAY_STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!s_free || !s_buf[0] || !s_buf[1]) {
        ESP_LOGE(TAG, "Out of memory for display buffers");
        release_resources();
        return ESP_ERR_NO_MEM;
    }

    backlight_init();

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = CONFIG_ZCLAW_DISPLAY_CS_GPIO,
        .dc_gpio_num = CONFIG_ZCLAW_DISPLAY_DC_GPIO,
        .spi_mode = 0,
        .pclk_hz = DISPLAY_PCLK_HZ,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_color_done,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_SPI_HOST, &io_cfg, &s_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel IO init failed: %s", esp_err_to_name(err));
        release_resources();
        return err;
    }

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = CONFIG_ZCLAW_DISPLAY_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel);
    if (err == ESP_OK) {
        err = esp_lcd_panel_reset(s_panel);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_init(s_panel);
    }
    if (err == ESP_OK) {
#if CONFIG_ZCLAW_DISPLAY_INVERT_COLORS
        err = esp_lcd_panel_invert_color(s_panel, true);
#else
        err = esp_lcd_panel_invert_color(s_panel, false);
#endif
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_set_gap(s_panel, CONFIG_ZCLAW_DISPLAY_X_GAP, CONFIG_ZCLAW_DISPLAY_Y_GAP);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_on_off(s_panel, true);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ST7789 init failed: %s", esp_err_to_name(err));
        release_resources();
        return err;
    }

    s_ready = true;
    for (int y = 0; y < DISPLAY_H; y += DISPLAY_STRIP_ROWS) {
        uint16_t *buf = display_hw_acquire_buffer();
        memset(buf, 0, DISPLAY_STRIP_PIXELS * sizeof(uint16_t));
        display_hw_blit(0, y, DISPLAY_W, DISPLAY_STRIP_ROWS, buf);
    }
    display_hw_wait_idle();
    display_hw_set_brightness(CONFIG_ZCLAW_DISPLAY_BRIGHTNESS);
    ESP_LOGI(TAG, "ST7789 ready (CS %d, DC %d, RST %d, BL %d)", CONFIG_ZCLAW_DISPLAY_CS_GPIO,
             CONFIG_ZCLAW_DISPLAY_DC_GPIO, CONFIG_ZCLAW_DISPLAY_RST_GPIO,
             CONFIG_ZCLAW_DISPLAY_BL_GPIO);
    return ESP_OK;
}

bool display_hw_ready(void)
{
    return s_ready;
}

uint16_t *display_hw_acquire_buffer(void)
{
    if (!s_ready) {
        return NULL;
    }
    xSemaphoreTake(s_free, portMAX_DELAY);
    uint16_t *buf = s_buf[s_next_buf];
    s_next_buf ^= 1;
    return buf;
}

void display_hw_release_buffer(uint16_t *buf)
{
    if (!s_ready || !buf) {
        return;
    }
    // The unused buffer becomes the next one handed out, preserving the
    // completion order the round-robin relies on.
    s_next_buf = (buf == s_buf[0]) ? 0 : 1;
    xSemaphoreGive(s_free);
}

esp_err_t display_hw_blit(int x, int y, int w, int h, uint16_t *buf)
{
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, buf);
    if (err != ESP_OK) {
        xSemaphoreGive(s_free);  // no completion callback will fire
    }
    return err;
}

void display_hw_wait_idle(void)
{
    if (!s_ready) {
        return;
    }
    xSemaphoreTake(s_free, portMAX_DELAY);
    xSemaphoreTake(s_free, portMAX_DELAY);
    xSemaphoreGive(s_free);
    xSemaphoreGive(s_free);
}

#else  // !CONFIG_ZCLAW_DISPLAY_ENABLE

esp_err_t display_hw_init(void) { return ESP_ERR_NOT_SUPPORTED; }
bool display_hw_ready(void) { return false; }
uint16_t *display_hw_acquire_buffer(void) { return NULL; }
void display_hw_release_buffer(uint16_t *buf) { (void)buf; }
esp_err_t display_hw_blit(int x, int y, int w, int h, uint16_t *buf)
{
    (void)x; (void)y; (void)w; (void)h; (void)buf;
    return ESP_ERR_NOT_SUPPORTED;
}
void display_hw_wait_idle(void) {}
void display_hw_set_brightness(uint8_t percent) { (void)percent; }
void display_hw_fade(uint8_t percent, uint32_t ms) { (void)percent; (void)ms; }
uint8_t display_hw_brightness(void) { return 0; }

#endif
