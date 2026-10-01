#include "ht5023_gpio.h"

#include "ht5023_device.h"

void ht5023_gpio_pin_mode(uint32_t port_base, uint8_t pin, ht5023_gpio_mode_t mode) {
  const uint16_t mask = (uint16_t)(1u << pin);
  volatile uint16_t* iocfg = ht5023_reg16(port_base, HT5023_GPIO_IOCFG_OFFSET);
  volatile uint16_t* afcfg = ht5023_reg16(port_base, HT5023_GPIO_AFCFG_OFFSET);
  volatile uint16_t* dir = ht5023_reg16(port_base, HT5023_GPIO_PTDIR_OFFSET);
  volatile uint16_t* ptup = ht5023_reg16(port_base, HT5023_GPIO_PTUP_OFFSET);
  volatile uint16_t* ptod = ht5023_reg16(port_base, HT5023_GPIO_PTOD_OFFSET);

  *iocfg &= (uint16_t)~mask;
  *afcfg &= (uint16_t)~mask;

  switch (mode) {
    case HT5023_GPIO_MODE_OUTPUT:
      *ptod |= mask;
      *dir |= mask;
      break;
    case HT5023_GPIO_MODE_INPUT_PULLUP:
      *dir &= (uint16_t)~mask;
      *ptup &= (uint16_t)~mask;
      break;
    case HT5023_GPIO_MODE_INPUT:
    default:
      *dir &= (uint16_t)~mask;
      *ptup |= mask;
      break;
  }
}

void ht5023_gpio_write(uint32_t port_base, uint8_t pin, bool high) {
  const uint16_t mask = (uint16_t)(1u << pin);
  if (high) {
    *ht5023_reg16(port_base, HT5023_GPIO_PTSET_OFFSET) = mask;
  } else {
    *ht5023_reg16(port_base, HT5023_GPIO_PTCLR_OFFSET) = mask;
  }
}

void ht5023_gpio_toggle(uint32_t port_base, uint8_t pin) {
  *ht5023_reg16(port_base, HT5023_GPIO_PTTOG_OFFSET) = (uint16_t)(1u << pin);
}

bool ht5023_gpio_read(uint32_t port_base, uint8_t pin) {
  return ((*ht5023_reg16(port_base, HT5023_GPIO_PTDAT_OFFSET)) & (uint16_t)(1u << pin)) != 0u;
}
