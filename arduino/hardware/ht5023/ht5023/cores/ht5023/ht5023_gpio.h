#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HT5023_GPIO_MODE_INPUT = 0,
  HT5023_GPIO_MODE_OUTPUT = 1,
  HT5023_GPIO_MODE_INPUT_PULLUP = 2,
} ht5023_gpio_mode_t;

void ht5023_gpio_pin_mode(uint32_t port_base, uint8_t pin, ht5023_gpio_mode_t mode);
void ht5023_gpio_write(uint32_t port_base, uint8_t pin, bool high);
void ht5023_gpio_toggle(uint32_t port_base, uint8_t pin);
bool ht5023_gpio_read(uint32_t port_base, uint8_t pin);

#ifdef __cplusplus
}
#endif
