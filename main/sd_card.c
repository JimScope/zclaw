#include "sd_card.h"

#include "board_spi.h"
#include "config.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "sd_access.h"
#include "sdmmc_cmd.h"

#if CONFIG_ZCLAW_SD_ENABLE

static const char *TAG = "sd_card";
static sdmmc_card_t *s_card = NULL;
static int64_t s_next_try_ms = 0;
static sd_access_t s_access;  // users and failure state, under s_mux
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static bool try_mount(void)
{
    if (board_spi_init() != ESP_OK) {
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SPI_HOST;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = BOARD_SPI_HOST;
    slot.gpio_cs = CONFIG_ZCLAW_SD_CS_GPIO;

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        ESP_LOGW(TAG, "SD mount failed: %s (retry in %d s)", esp_err_to_name(err), SD_RETRY_MS / 1000);
        return false;
    }
    // nano printf: no %llu, so print MB as unsigned long.
    ESP_LOGI(TAG, "SD mounted at %s (%s, %lu MB)", SD_MOUNT_POINT, s_card->cid.name,
             (unsigned long)(((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) >> 20));
    return true;
}

bool sd_card_service(int64_t now_ms)
{
    taskENTER_CRITICAL(&s_mux);
    bool unmount = sd_access_take_unmount(&s_access);
    taskEXIT_CRITICAL(&s_mux);
    if (unmount && s_card) {
        ESP_LOGW(TAG, "SD I/O error: unmounting");
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        s_card = NULL;
    }
    if (s_card || now_ms < s_next_try_ms) {
        return false;
    }
    s_next_try_ms = now_ms + SD_RETRY_MS;
    if (!try_mount()) {
        return false;
    }
    taskENTER_CRITICAL(&s_mux);
    sd_access_mounted(&s_access);
    taskEXIT_CRITICAL(&s_mux);
    return true;
}

bool sd_card_is_mounted(void)
{
    taskENTER_CRITICAL(&s_mux);
    bool usable = sd_access_usable(&s_access);
    taskEXIT_CRITICAL(&s_mux);
    return usable;
}

bool sd_card_begin(void)
{
    taskENTER_CRITICAL(&s_mux);
    bool ok = sd_access_begin(&s_access);
    taskEXIT_CRITICAL(&s_mux);
    return ok;
}

void sd_card_end(void)
{
    taskENTER_CRITICAL(&s_mux);
    sd_access_end(&s_access);
    taskEXIT_CRITICAL(&s_mux);
}

void sd_card_mark_failed(int64_t now_ms)
{
    taskENTER_CRITICAL(&s_mux);
    sd_access_fail(&s_access);
    taskEXIT_CRITICAL(&s_mux);
    s_next_try_ms = now_ms + SD_RETRY_MS;
}

#else  // !CONFIG_ZCLAW_SD_ENABLE

bool sd_card_service(int64_t now_ms) { (void)now_ms; return false; }
bool sd_card_is_mounted(void) { return false; }
bool sd_card_begin(void) { return false; }
void sd_card_end(void) {}
void sd_card_mark_failed(int64_t now_ms) { (void)now_ms; }

#endif
