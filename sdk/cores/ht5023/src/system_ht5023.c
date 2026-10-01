// system_ht5023.c — basic SystemInit (keep default clock, disable WDT by default)
#include <stdint.h>

#include "system_ht5023.h"

/* CMSIS SystemCoreClock is the CPU clock, Fcpu -- not Fsys, which is what the
   UART dividers use. See the clock notes in ht5023/core/system.h. */
uint32_t SystemCoreClock = HT5023_FCPU_HZ;

#define HT5023_CMU_BASE                0x4000F000u
#define HT5023_CMU_SYSCLKDIV_OFFSET    0x1Cu

#define HT5023_CTRLBYFLASH_BASE        0x4000C000u
#define HT5023_CTRLBYFLASH_OFFSET      0x100u
#define HT5023_LRCCOMMAND_OFFSET       0x104u

// Datasheet CTRLByFlash.LRC_CTRL (bit 1): 1=enable LRC, 0=disable LRC.
// WDT depends on JTAGWDTEN and LRC; with JTAGWDTEN high, shutting down LRC disables WDT.
#define HT5023_CTRLBYFLASH_LRC_CTRL    (1u << 1)

#ifndef HT5023_DISABLE_WDT_BY_DEFAULT
#define HT5023_DISABLE_WDT_BY_DEFAULT 1
#endif

static inline volatile uint16_t* reg16(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(uintptr_t)(base + off);
}

static void ht5023_disable_lrc_and_wdt_default(void) {
  volatile uint16_t* ctrlbyflash = reg16(HT5023_CTRLBYFLASH_BASE, HT5023_CTRLBYFLASH_OFFSET);
  volatile uint16_t* lrccommand  = reg16(HT5023_CTRLBYFLASH_BASE, HT5023_LRCCOMMAND_OFFSET);

  // Clear LRC_CTRL, then issue the required LRCCOMMAND sequence (datasheet, CTRLByFlash/LRCCOMMAND).
  *ctrlbyflash = (uint16_t)(*ctrlbyflash & (uint16_t)~HT5023_CTRLBYFLASH_LRC_CTRL);
  *lrccommand = 0x5555u;
  *lrccommand = 0xAAAAu;
}

uint32_t ht5023_fcpu_hz(void) {
  const uint16_t div = (uint16_t)(*reg16(HT5023_CMU_BASE, HT5023_CMU_SYSCLKDIV_OFFSET) & 7u);
  return HT5023_FSYS_HZ >> div;
}

void SystemCoreClockUpdate(void) {
  /* The PLL is not modelled; the HRC is assumed to be the source. */
  SystemCoreClock = ht5023_fcpu_hz();
}

// Overridden by the board-support package; the bootloader links none.
__attribute__((weak)) void ht5023_board_init(void) {
}

void SystemInit(void) {
#if HT5023_DISABLE_WDT_BY_DEFAULT
  ht5023_disable_lrc_and_wdt_default();
#endif
  // Leave default clocks. If needed, user can enable PLL later.
  SystemCoreClockUpdate();
  ht5023_board_init();
}
