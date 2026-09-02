#include "tools_handlers.h"
#include "tools_common.h"
#include "driver/temperature_sensor.h"
#include "esp_log.h"

#include <stdio.h>

static const char *TAG = "tools_tsens";
static temperature_sensor_handle_t s_tsens;

bool tools_chip_temp_handler(const cJSON *input, char *result, size_t result_len)
{
    (void)input;

    if (!s_tsens) {
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
        esp_err_t err = temperature_sensor_install(&cfg, &s_tsens);
        if (err == ESP_OK) {
            err = temperature_sensor_enable(s_tsens);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "tsens init failed: %s", esp_err_to_name(err));
            s_tsens = NULL;
            snprintf(result, result_len, "Error: temperature sensor init failed");
            return false;
        }
    }

    float celsius = 0;
    if (temperature_sensor_get_celsius(s_tsens, &celsius) != ESP_OK) {
        snprintf(result, result_len, "Error: temperature read failed");
        return false;
    }

    snprintf(result, result_len,
             "Chip temperature: %.1f C (die runs ~10-20 C above ambient)", celsius);
    return true;
}
