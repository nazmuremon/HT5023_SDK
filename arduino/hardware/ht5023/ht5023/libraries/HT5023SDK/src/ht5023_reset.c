// ht5023_reset.c — HT5023 reset status (RSTSTA) helper
#include "ht5023_reset.h"

#include <stdint.h>

#define HT5023_RST_BASE          0x4000F400u
#define HT5023_RSTSTA_OFFSET     0x30u

static inline volatile uint16_t* reg16(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(base + off);
}

uint16_t ht5023_reset_read_raw(void) {
  return *reg16(HT5023_RST_BASE, HT5023_RSTSTA_OFFSET);
}

ht5023_reset_status_t ht5023_reset_read(void) {
  const uint16_t r = ht5023_reset_read_raw();
  ht5023_reset_status_t s;
  s.raw = r;
  s.hold_wakeup       = (r & HT5023_RSTSTA_HOLD_FLAG_BIT) != 0u;
  s.sleep_wakeup      = (r & HT5023_RSTSTA_SLEEP_FLAG_BIT) != 0u;
  s.bor_reset         = (r & HT5023_RSTSTA_BORRST_BIT) != 0u;
  s.debug_reset       = (r & HT5023_RSTSTA_DEBUGRST_BIT) != 0u;
  s.soft_reset        = (r & HT5023_RSTSTA_SOFTRST_BIT) != 0u;
  s.ext_reset         = (r & HT5023_RSTSTA_EXTRST_BIT) != 0u;
  s.flashwr_lbor_reset= (r & HT5023_RSTSTA_FLASHWR_LBOR_BIT) != 0u;
  s.wakeup_reset      = (r & HT5023_RSTSTA_WAKEUPRST_BIT) != 0u;
  s.wdt_reset         = (r & HT5023_RSTSTA_WDTRST_BIT) != 0u;
  s.lbor_reset        = (r & HT5023_RSTSTA_LBORRST_BIT) != 0u;
  s.por_reset         = (r & HT5023_RSTSTA_PORRST_BIT) != 0u;
  return s;
}

void ht5023_reset_clear_flags(uint16_t clear_mask) {
  const uint16_t writable = (uint16_t)HT5023_RSTSTA_KNOWN_FLAGS_MASK;
  const uint16_t write_val = (uint16_t)(writable & (uint16_t)~clear_mask);
  *reg16(HT5023_RST_BASE, HT5023_RSTSTA_OFFSET) = write_val;
}

