#ifndef GPIO_POLICY_H
#define GPIO_POLICY_H

#include <stdbool.h>
#include <stddef.h>

bool gpio_policy_pin_is_allowed(int pin);
bool gpio_policy_pin_forbidden_hint(int pin, char *result, size_t result_len);
bool gpio_policy_runtime_input_pin_is_safe(int pin);

#define GPIO_POLICY_MAX_RESERVED 8

// Pins owned by enabled peripherals (display / SD SPI bus). Returns the count copied.
size_t gpio_policy_reserved_pins(int *out, size_t max);

// Pins the board itself uses (CONFIG_ZCLAW_BOARD_RESERVED_GPIOS), e.g. an RF switch.
bool gpio_policy_pin_is_board_reserved(int pin);

#ifdef TEST_BUILD
// Override the reserved list; count == 0 restores the Kconfig-derived list.
void gpio_policy_test_set_reserved(const int *pins, size_t count);
// Rebuild the reserved list with the given board CSV (plus enabled peripherals).
void gpio_policy_test_load_board_reserved(const char *csv);
bool gpio_policy_test_pin_is_allowed(int pin,
                                     const char *csv,
                                     int min_pin,
                                     int max_pin,
                                     bool block_esp32_flash_pins,
                                     bool require_valid_gpio);
bool gpio_policy_test_runtime_input_pin_is_safe(int pin,
                                                bool block_esp32_flash_pins,
                                                bool require_valid_gpio);
#endif

#endif  // GPIO_POLICY_H
