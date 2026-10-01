// board_buzzer.c — keep the PA7 buzzer quiet from reset, with one boot beep.
//
// On the energy-meter PCB, PA7 drives the buzzer through R84 (1k) into an
// AO3400A gate held down by R85 (100k). Out of reset every GPIOA pin is an
// input with its ~88k pull-up ENABLED (PTUP resets to 0x0000, and 0 means
// pull-up on), so the gate sits near 3.3 V * 100k / 189k = 1.75 V -- above the
// MOSFET's threshold -- and the (active) buzzer sounds for as long as nothing
// drives PA7. Every example that never touches PA7 therefore buzzed nonstop.
//
// ht5023_board_init() runs at the end of SystemInit(), before main(): it gives
// one short beep so a reset is audible, then leaves PA7 a push-pull output
// driven low with its pull-up disabled. As an output it is also skipped by
// code that only reconfigures GPIO inputs (button_press's pin scan). An
// example that wants the buzzer just drives PA7 as usual.
//
// The pull-up is back on while the chip is held in reset (for example during
// a J-Link flash), which no firmware can prevent; fitting 10k for R85 keeps
// the gate below threshold even then.
#include <stdint.h>

#include "ht5023/core/device.h"
#include "ht5023/core/system.h"

// Length of the boot beep; 0 disables it (PA7 is still silenced).
#ifndef HT5023_BOOT_BEEP_MS
#define HT5023_BOOT_BEEP_MS 80u
#endif

#define BUZZER_GPIO_BASE   0x40011000u  // GPIOA
#define BUZZER_PIN_MASK    ((uint16_t)(1u << 7))

#define GPIO_IOCFG_OFFSET  0x00u
#define GPIO_AFCFG_OFFSET  0x04u
#define GPIO_PTDIR_OFFSET  0x08u
#define GPIO_PTUP_OFFSET   0x0Cu
#define GPIO_PTSET_OFFSET  0x14u
#define GPIO_PTCLR_OFFSET  0x18u
#define GPIO_PTOD_OFFSET   0x20u

#define SYSTICK_BASE       0xE000E000u
#define SYST_CSR_OFFSET    0x10u
#define SYST_RVR_OFFSET    0x14u
#define SYST_CVR_OFFSET    0x18u
#define SYST_CSR_ENABLE    (1u << 0)
#define SYST_CSR_CLKSOURCE (1u << 2)
#define SYST_CSR_COUNTFLAG (1u << 16)

// Busy-waits on SysTick, polled with its interrupt off, and stops it again
// afterwards: nothing has started it yet, and whatever the example does with
// it later starts from the same stopped state it would have out of reset.
static void boot_delay_ms(uint32_t ms) {
  volatile uint32_t* csr = ht5023_reg32(SYSTICK_BASE, SYST_CSR_OFFSET);

  *csr = 0u;
  *ht5023_reg32(SYSTICK_BASE, SYST_RVR_OFFSET) = ht5023_fcpu_hz() / 1000u - 1u;
  *ht5023_reg32(SYSTICK_BASE, SYST_CVR_OFFSET) = 0u;
  *csr = SYST_CSR_CLKSOURCE | SYST_CSR_ENABLE;

  for (uint32_t i = 0u; i < ms; ++i) {
    while ((*csr & SYST_CSR_COUNTFLAG) == 0u) {
    }
  }
  *csr = 0u;
}

void ht5023_board_init(void) {
  const uint16_t mask = BUZZER_PIN_MASK;

  // Low before switching to output, so PA7 never drives high by accident.
  *ht5023_reg16(BUZZER_GPIO_BASE, GPIO_PTCLR_OFFSET) = mask;
  *ht5023_reg16(BUZZER_GPIO_BASE, GPIO_IOCFG_OFFSET) &= (uint16_t)~mask;
  *ht5023_reg16(BUZZER_GPIO_BASE, GPIO_AFCFG_OFFSET) &= (uint16_t)~mask;
  // PTUP bit = 1 disables the pull-up, in case PA7 is made an input later.
  *ht5023_reg16(BUZZER_GPIO_BASE, GPIO_PTUP_OFFSET) |= mask;
  // PTOD bit = 1 is push-pull; PTDIR bit = 1 is output.
  *ht5023_reg16(BUZZER_GPIO_BASE, GPIO_PTOD_OFFSET) |= mask;
  *ht5023_reg16(BUZZER_GPIO_BASE, GPIO_PTDIR_OFFSET) |= mask;

#if HT5023_BOOT_BEEP_MS > 0
  *ht5023_reg16(BUZZER_GPIO_BASE, GPIO_PTSET_OFFSET) = mask;
  boot_delay_ms(HT5023_BOOT_BEEP_MS);
  *ht5023_reg16(BUZZER_GPIO_BASE, GPIO_PTCLR_OFFSET) = mask;
#endif
}
