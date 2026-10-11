#include "board_spi.h"

#include "config.h"
#include "driver/gpio.h"
#include "gpio_policy.h"
#include "esp_log.h"

static const char *TAG = "board_spi";
static bool s_ready = false;

#if CONFIG_ZCLAW_DISPLAY_ENABLE || CONFIG_ZCLAW_SD_ENABLE
static bool pin_free(int gpio, const char *what)
{
    if (gpio_policy_pin_is_board_reserved(gpio)) {
        ESP_LOGE(TAG, "%s GPIO %d is reserved by the board (CONFIG_ZCLAW_BOARD_RESERVED_GPIOS)",
                 what, gpio);
        return false;
    }
    return true;
}

static bool bus_pins_free(void)
{
    bool ok = pin_free(CONFIG_ZCLAW_SPI_SCLK_GPIO, "SPI SCLK") &
              pin_free(CONFIG_ZCLAW_SPI_MOSI_GPIO, "SPI MOSI");
#if CONFIG_ZCLAW_DISPLAY_ENABLE
    ok &= pin_free(CONFIG_ZCLAW_DISPLAY_CS_GPIO, "Display CS") &
          pin_free(CONFIG_ZCLAW_DISPLAY_DC_GPIO, "Display DC") &
          pin_free(CONFIG_ZCLAW_DISPLAY_RST_GPIO, "Display RST") &
          pin_free(CONFIG_ZCLAW_DISPLAY_BL_GPIO, "Display backlight");
#endif
#if CONFIG_ZCLAW_SD_ENABLE
    ok &= pin_free(CONFIG_ZCLAW_SPI_MISO_GPIO, "SPI MISO") &
          pin_free(CONFIG_ZCLAW_SD_CS_GPIO, "SD CS");
#endif
    return ok;
}

static void deselect(int gpio)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(gpio, 1);
}
#endif

esp_err_t board_spi_init(void)
{
#if CONFIG_ZCLAW_DISPLAY_ENABLE || CONFIG_ZCLAW_SD_ENABLE
    if (s_ready) {
        return ESP_OK;
    }
    if (!bus_pins_free()) {
        return ESP_ERR_INVALID_ARG;
    }

    // Keep every chip select high before the bus clocks anything, so the SD
    // card never sees display traffic during init and vice versa.
#if CONFIG_ZCLAW_DISPLAY_ENABLE
    deselect(CONFIG_ZCLAW_DISPLAY_CS_GPIO);
#endif
#if CONFIG_ZCLAW_SD_ENABLE
    deselect(CONFIG_ZCLAW_SD_CS_GPIO);
#endif

    spi_bus_config_t cfg = {
        .sclk_io_num = CONFIG_ZCLAW_SPI_SCLK_GPIO,
        .mosi_io_num = CONFIG_ZCLAW_SPI_MOSI_GPIO,
#if CONFIG_ZCLAW_SD_ENABLE
        .miso_io_num = CONFIG_ZCLAW_SPI_MISO_GPIO,
#else
        .miso_io_num = -1,
#endif
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOARD_SPI_MAX_TRANSFER,
    };
    esp_err_t err = spi_bus_initialize(BOARD_SPI_HOST, &cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }
    s_ready = true;
    ESP_LOGI(TAG, "SPI2 ready (SCLK %d, MOSI %d)", CONFIG_ZCLAW_SPI_SCLK_GPIO,
             CONFIG_ZCLAW_SPI_MOSI_GPIO);
    return ESP_OK;
#else
    (void)TAG;
    (void)s_ready;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
