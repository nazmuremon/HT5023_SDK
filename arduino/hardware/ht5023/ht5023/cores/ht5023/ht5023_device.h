#pragma once

#include <stdbool.h>
#include <stdint.h>

// Port bases run 0x100 apart, with one gap: 0x40011500 is the large-current
// port, not PF, so PF sits at 0x40011600. Reading 0x40011500 gives zeros and
// writes to it do not stick -- worth knowing before chasing a dead pin.
#define HT5023_GPIOA_BASE        0x40011000u
#define HT5023_GPIOB_BASE        0x40011100u
#define HT5023_GPIOC_BASE        0x40011200u
#define HT5023_GPIOD_BASE        0x40011300u
#define HT5023_GPIOE_BASE        0x40011400u
#define HT5023_GPIOF_BASE        0x40011600u

#define HT5023_GPIO_IOCFG_OFFSET 0x00u
#define HT5023_GPIO_AFCFG_OFFSET 0x04u
#define HT5023_GPIO_PTDIR_OFFSET 0x08u
#define HT5023_GPIO_PTUP_OFFSET  0x0Cu
#define HT5023_GPIO_PTDAT_OFFSET 0x10u
#define HT5023_GPIO_PTSET_OFFSET 0x14u
#define HT5023_GPIO_PTCLR_OFFSET 0x18u
#define HT5023_GPIO_PTTOG_OFFSET 0x1Cu
#define HT5023_GPIO_PTOD_OFFSET  0x20u

static inline volatile uint16_t* ht5023_reg16(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(uintptr_t)(base + off);
}

static inline volatile uint32_t* ht5023_reg32(uint32_t base, uint32_t off) {
  return (volatile uint32_t*)(uintptr_t)(base + off);
}
