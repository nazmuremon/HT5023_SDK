// gpio_lowlevel.c — minimal helpers to init/toggle LED
#include "board_gpio.h"

static inline volatile uint16_t* REG_IOCFG(void){ return reg16(GPIOE_BASE_ADDR, GPIO_IOCFG_OFFSET); }
static inline volatile uint16_t* REG_AFCFG(void){ return reg16(GPIOE_BASE_ADDR, GPIO_AFCFG_OFFSET); }
static inline volatile uint16_t* REG_DIR(void){ return reg16(GPIOE_BASE_ADDR, GPIO_PTDIR_OFFSET); }
static inline volatile uint16_t* REG_SET(void){ return reg16(GPIOE_BASE_ADDR, GPIO_PTSET_OFFSET); }
static inline volatile uint16_t* REG_CLR(void){ return reg16(GPIOE_BASE_ADDR, GPIO_PTCLR_OFFSET); }
static inline volatile uint16_t* REG_TOG(void){ return reg16(GPIOE_BASE_ADDR, GPIO_PTTOG_OFFSET); }
static inline volatile uint16_t* REG_OD(void){ return reg16(GPIOE_BASE_ADDR, GPIO_PTOD_OFFSET); }

void led_gpio_init(void) {
  // Force PE3/PE4 to GPIO (IOCFG bit=0 means GPIO). AFCFG is don't-care for GPIO,
  // but cleared here to leave the port in a known state.
  *REG_IOCFG() &= (uint16_t)~LED_PINS_MASK;
  *REG_AFCFG() &= (uint16_t)~LED_PINS_MASK;

  // Push-pull output: PTOD bit=1 disables open-drain (enables PMOS).
  *REG_OD() |= LED_PINS_MASK;

  // Configure as outputs.
  *REG_DIR() |= LED_PINS_MASK;

  // Drive LEDs to the default OFF state.
#if LED_ACTIVE_LOW
  *REG_SET() = LED_PINS_MASK;
#else
  *REG_CLR() = LED_PINS_MASK;
#endif
}

void led_on(void) {
#if LED_ACTIVE_LOW
  *REG_CLR() = LED_PINS_MASK;
#else
  *REG_SET() = LED_PINS_MASK;
#endif
}

void led_off(void) {
#if LED_ACTIVE_LOW
  *REG_SET() = LED_PINS_MASK;
#else
  *REG_CLR() = LED_PINS_MASK;
#endif
}

void led_toggle(void) {
  // PTTOG flips only bits written as 1 when configured as GPIO outputs.
  *REG_TOG() = LED_PINS_MASK;
}
