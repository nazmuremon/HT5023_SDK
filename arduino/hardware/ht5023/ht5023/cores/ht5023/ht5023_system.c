#include "ht5023_system.h"

#include "ht5023_device.h"

// F_CPU is Fsys (see Arduino.h). CMSIS SystemCoreClock is the CPU clock, Fcpu,
// which SYSCLK_DIV divides down from it -- by 2 out of reset.
#define HT5023_CMU_BASE_ADDR        0x4000F000u
#define HT5023_CMU_SYSCLKDIV_OFFSET 0x1Cu

uint32_t SystemCoreClock = (uint32_t)(F_CPU) / 2u;

#define HT5023_CTRLBYFLASH_BASE     0x4000C000u
#define HT5023_CTRLBYFLASH_OFFSET   0x100u
#define HT5023_LRCCOMMAND_OFFSET    0x104u

#define HT5023_CTRLBYFLASH_LRC_CTRL (1u << 1)

#ifndef HT5023_DISABLE_WDT_BY_DEFAULT
#define HT5023_DISABLE_WDT_BY_DEFAULT 1
#endif

static void ht5023_disable_lrc_and_wdt_default(void) {
  volatile uint16_t* ctrlbyflash = ht5023_reg16(HT5023_CTRLBYFLASH_BASE, HT5023_CTRLBYFLASH_OFFSET);
  volatile uint16_t* lrccommand = ht5023_reg16(HT5023_CTRLBYFLASH_BASE, HT5023_LRCCOMMAND_OFFSET);

  *ctrlbyflash = (uint16_t)(*ctrlbyflash & (uint16_t)~HT5023_CTRLBYFLASH_LRC_CTRL);
  *lrccommand = 0x5555u;
  *lrccommand = 0xAAAAu;
}

void SystemCoreClockUpdate(void) {
  const uint16_t div = (uint16_t)(*ht5023_reg16(HT5023_CMU_BASE_ADDR, HT5023_CMU_SYSCLKDIV_OFFSET) & 7u);
  SystemCoreClock = (uint32_t)(F_CPU) >> div;
}

// A sketch that needs the 32 kHz LRC -- the LCD controller runs from it, and so
// does the watchdog -- opts out of the shutdown below by defining
//
//   extern "C" const bool ht5023_keep_lrc = true;
//
// It has to be decided here rather than in setup(): the shutdown is a latch,
// and once it has run nothing short of a reset restarts the LRC. The build
// flag -DHT5023_DISABLE_WDT_BY_DEFAULT=0 does the same for a whole build, but
// the Flasher cannot pass build flags. Weak, so a sketch that does not define
// it links and behaves exactly as before. A sketch that does must feed the
// watchdog (WDTCLR, 0xAA in the HIGH byte), which then runs: 4.16 s default.
extern const bool ht5023_keep_lrc __attribute__((weak));

void SystemInit(void) {
#if HT5023_DISABLE_WDT_BY_DEFAULT
  if (&ht5023_keep_lrc == 0 || !ht5023_keep_lrc) {
    ht5023_disable_lrc_and_wdt_default();
  }
#endif
  SystemCoreClockUpdate();
}
