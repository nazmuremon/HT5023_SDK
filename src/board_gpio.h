// board_gpio.h — HT5023 GPIOE LED pin mapping (PE3 + PE4)
#pragma once
#include <stdint.h>

// HT502X User Manual, GPIO chapter (7.3/7.4):
// PE port base = 0x40011400, GPIO register offsets as below.
#define GPIOE_BASE_ADDR        0x40011400u
#define GPIO_IOCFG_OFFSET      0x00u
#define GPIO_AFCFG_OFFSET      0x04u
#define GPIO_PTDIR_OFFSET      0x08u
#define GPIO_PTUP_OFFSET       0x0Cu
#define GPIO_PTDAT_OFFSET      0x10u
#define GPIO_PTSET_OFFSET      0x14u
#define GPIO_PTCLR_OFFSET      0x18u
#define GPIO_PTTOG_OFFSET      0x1Cu
#define GPIO_PTOD_OFFSET       0x20u

// LEDs are connected to PE3 and PE4.
#define LED_PINS_MASK ((uint16_t)((1u << 3) | (1u << 4)))

// Set to 1 if LEDs are wired active-low.
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0
#endif

static inline volatile uint16_t* reg16(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(base + off);
}
