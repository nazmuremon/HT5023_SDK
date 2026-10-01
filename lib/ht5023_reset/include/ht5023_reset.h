// ht5023_reset.h — HT5023 reset status (RSTSTA) helper
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// RSTSTA bits (datasheet, base 0x4000F400 + 0x30)
#define HT5023_RSTSTA_HOLD_FLAG_BIT        (1u << 15)
#define HT5023_RSTSTA_SLEEP_FLAG_BIT       (1u << 14)
#define HT5023_RSTSTA_BORRST_BIT           (1u << 8)
#define HT5023_RSTSTA_DEBUGRST_BIT         (1u << 7)
#define HT5023_RSTSTA_SOFTRST_BIT          (1u << 6)
#define HT5023_RSTSTA_EXTRST_BIT           (1u << 5)
#define HT5023_RSTSTA_FLASHWR_LBOR_BIT     (1u << 4)  // datasheet naming is inconsistent
#define HT5023_RSTSTA_WAKEUPRST_BIT        (1u << 3)
#define HT5023_RSTSTA_WDTRST_BIT           (1u << 2)
#define HT5023_RSTSTA_LBORRST_BIT          (1u << 1)
#define HT5023_RSTSTA_PORRST_BIT           (1u << 0)

#define HT5023_RSTSTA_KNOWN_FLAGS_MASK ( \
  HT5023_RSTSTA_HOLD_FLAG_BIT    | HT5023_RSTSTA_SLEEP_FLAG_BIT | \
  HT5023_RSTSTA_BORRST_BIT       | HT5023_RSTSTA_DEBUGRST_BIT   | \
  HT5023_RSTSTA_SOFTRST_BIT      | HT5023_RSTSTA_EXTRST_BIT     | \
  HT5023_RSTSTA_FLASHWR_LBOR_BIT | HT5023_RSTSTA_WAKEUPRST_BIT  | \
  HT5023_RSTSTA_WDTRST_BIT       | HT5023_RSTSTA_LBORRST_BIT    | \
  HT5023_RSTSTA_PORRST_BIT)

typedef struct {
  uint16_t raw;
  bool hold_wakeup;
  bool sleep_wakeup;
  bool bor_reset;
  bool debug_reset;
  bool soft_reset;
  bool ext_reset;
  bool flashwr_lbor_reset;
  bool wakeup_reset;
  bool wdt_reset;
  bool lbor_reset;
  bool por_reset;
} ht5023_reset_status_t;

uint16_t ht5023_reset_read_raw(void);
ht5023_reset_status_t ht5023_reset_read(void);

// Flags are cleared by writing 0 (write 1 preserves). `clear_mask` uses RSTSTA bit positions.
void ht5023_reset_clear_flags(uint16_t clear_mask);

#ifdef __cplusplus
}
#endif
