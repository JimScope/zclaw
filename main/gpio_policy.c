#include "gpio_policy.h"

#include "config.h"
#include "driver/gpio.h"

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#ifndef GPIO_IS_VALID_GPIO
#define GPIO_IS_VALID_GPIO(pin) ((pin) >= 0)
#endif

static bool pin_in_allowlist(int pin, const char *csv)
{
    const char *cursor;

    if (!csv || csv[0] == '\0') {
        return false;
    }

    cursor = csv;
    while (*cursor != '\0') {
        char *endptr = NULL;
        long value;

        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        value = strtol(cursor, &endptr, 10);
        if (endptr == cursor) {
            while (*cursor != '\0' && *cursor != ',') {
                cursor++;
            }
            continue;
        }

        if ((int)value == pin) {
            return true;
        }
        cursor = endptr;
    }

    return false;
}

static int s_reserved[GPIO_POLICY_MAX_RESERVED];
static size_t s_reserved_count;
static bool s_reserved_ready;

static void reserved_add(int pin)
{
    if (pin < 0) {
        return;
    }
    for (size_t i = 0; i < s_reserved_count; i++) {
        if (s_reserved[i] == pin) {
            return;
        }
    }
    if (s_reserved_count < GPIO_POLICY_MAX_RESERVED) {
        s_reserved[s_reserved_count++] = pin;
    }
}

static void reserved_load_from_config(void)
{
    s_reserved_count = 0;
#if !(CONFIG_ZCLAW_DISPLAY_ENABLE || CONFIG_ZCLAW_SD_ENABLE)
    (void)reserved_add;  // only the TEST_BUILD hook adds pins in this configuration
#endif
#if CONFIG_ZCLAW_DISPLAY_ENABLE || CONFIG_ZCLAW_SD_ENABLE
    reserved_add(CONFIG_ZCLAW_SPI_SCLK_GPIO);
    reserved_add(CONFIG_ZCLAW_SPI_MOSI_GPIO);
#endif
#if CONFIG_ZCLAW_DISPLAY_ENABLE
    reserved_add(CONFIG_ZCLAW_DISPLAY_CS_GPIO);
    reserved_add(CONFIG_ZCLAW_DISPLAY_DC_GPIO);
    reserved_add(CONFIG_ZCLAW_DISPLAY_RST_GPIO);
    reserved_add(CONFIG_ZCLAW_DISPLAY_BL_GPIO);
#endif
#if CONFIG_ZCLAW_SD_ENABLE
    reserved_add(CONFIG_ZCLAW_SPI_MISO_GPIO);
    reserved_add(CONFIG_ZCLAW_SD_CS_GPIO);
#endif
    s_reserved_ready = true;
}

static bool pin_is_reserved(int pin)
{
    if (!s_reserved_ready) {
        reserved_load_from_config();
    }
    for (size_t i = 0; i < s_reserved_count; i++) {
        if (s_reserved[i] == pin) {
            return true;
        }
    }
    return false;
}

size_t gpio_policy_reserved_pins(int *out, size_t max)
{
    size_t n;

    if (!s_reserved_ready) {
        reserved_load_from_config();
    }
    n = s_reserved_count < max ? s_reserved_count : max;
    for (size_t i = 0; out && i < n; i++) {
        out[i] = s_reserved[i];
    }
    return out ? n : 0;
}

static bool pin_is_allowed_impl(int pin,
                                const char *allowlist_csv,
                                int min_pin,
                                int max_pin,
                                bool block_esp32_flash_pins,
                                bool require_valid_gpio)
{
    bool in_policy;

    if (pin < 0) {
        return false;
    }

    if (pin_is_reserved(pin)) {
        return false;
    }

    if (block_esp32_flash_pins && pin >= 6 && pin <= 11) {
        return false;
    }

    if (allowlist_csv && allowlist_csv[0] != '\0') {
        in_policy = pin_in_allowlist(pin, allowlist_csv);
    } else {
        in_policy = pin >= min_pin && pin <= max_pin;
    }

    if (!in_policy) {
        return false;
    }

    if (require_valid_gpio) {
        return GPIO_IS_VALID_GPIO((gpio_num_t)pin);
    }

    return true;
}

bool gpio_policy_pin_is_allowed(int pin)
{
#if defined(CONFIG_IDF_TARGET_ESP32)
    return pin_is_allowed_impl(pin, GPIO_ALLOWED_PINS_CSV, GPIO_MIN_PIN, GPIO_MAX_PIN, true, true);
#else
    return pin_is_allowed_impl(pin, GPIO_ALLOWED_PINS_CSV, GPIO_MIN_PIN, GPIO_MAX_PIN, false, true);
#endif
}

bool gpio_policy_runtime_input_pin_is_safe(int pin)
{
#if defined(CONFIG_IDF_TARGET_ESP32)
    return pin_is_allowed_impl(pin, NULL, 0, INT_MAX, true, true);
#else
    return pin_is_allowed_impl(pin, NULL, 0, INT_MAX, false, true);
#endif
}

bool gpio_policy_pin_forbidden_hint(int pin, char *result, size_t result_len)
{
    if (pin >= 0 && pin_is_reserved(pin)) {
        snprintf(result, result_len,
                 "Error: pin %d is reserved for the display/SD SPI bus; choose a different pin",
                 pin);
        return true;
    }
#if defined(CONFIG_IDF_TARGET_ESP32)
    if (pin >= 6 && pin <= 11) {
        snprintf(result, result_len,
                 "Error: pin %d is reserved for ESP32 flash/PSRAM (GPIO6-11); choose a different pin",
                 pin);
        return true;
    }
#endif
    return false;
}

#ifdef TEST_BUILD
bool gpio_policy_test_pin_is_allowed(int pin,
                                     const char *csv,
                                     int min_pin,
                                     int max_pin,
                                     bool block_esp32_flash_pins,
                                     bool require_valid_gpio)
{
    return pin_is_allowed_impl(pin, csv, min_pin, max_pin, block_esp32_flash_pins, require_valid_gpio);
}

bool gpio_policy_test_runtime_input_pin_is_safe(int pin,
                                                bool block_esp32_flash_pins,
                                                bool require_valid_gpio)
{
    return pin_is_allowed_impl(pin, NULL, 0, INT_MAX, block_esp32_flash_pins, require_valid_gpio);
}

void gpio_policy_test_set_reserved(const int *pins, size_t count)
{
    if (!pins || count == 0) {
        reserved_load_from_config();
        return;
    }
    s_reserved_count = 0;
    for (size_t i = 0; i < count; i++) {
        reserved_add(pins[i]);
    }
    s_reserved_ready = true;
}
#endif
