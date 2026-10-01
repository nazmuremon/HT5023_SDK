// ht5023_wdt.c — HT5023 watchdog helper
#include "ht5023_wdt.h"

#include <stdint.h>

#define HT5023_WDT_BASE          0x40010000u
#define HT5023_WDTCLR_OFFSET     0x04u
#define HT5023_WDTCNT_OFFSET     0x08u
#define HT5023_WDT_FEED_KEY      0xAAu

static inline volatile uint16_t* reg16(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(base + off);
}

uint8_t ht5023_wdt_set_from_timeout_ms(uint32_t timeout_ms) {
  if (timeout_ms <= 64u) return 0u;
  uint32_t q = (timeout_ms + 32u) / 64u;  // rounded
  if (q == 0u) q = 1u;
  if (q > 256u) q = 256u;
  return (uint8_t)(q - 1u);
}

uint32_t ht5023_wdt_timeout_ms_from_set(uint8_t set_value) {
  return 64u * (uint32_t)(1u + set_value);
}

void ht5023_wdt_configure_and_feed(uint8_t set_value) {
  *reg16(HT5023_WDT_BASE, HT5023_WDTCLR_OFFSET) =
      (uint16_t)(((uint16_t)HT5023_WDT_FEED_KEY << 8) | set_value);
}

uint8_t ht5023_wdt_get_set_value(void) {
  return (uint8_t)(*reg16(HT5023_WDT_BASE, HT5023_WDTCLR_OFFSET) & 0x00FFu);
}

void ht5023_wdt_feed(void) {
  ht5023_wdt_configure_and_feed(ht5023_wdt_get_set_value());
}

uint16_t ht5023_wdt_get_counter(void) {
  return *reg16(HT5023_WDT_BASE, HT5023_WDTCNT_OFFSET);
}

