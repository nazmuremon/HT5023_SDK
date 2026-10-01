// ht5023_timer.c — minimal HT5023 TMR0..TMR3 polling API
#include "ht5023_timer.h"

#include <stdint.h>

#define HT5023_CMU_BASE               0x4000F000u
#define HT5023_CMU_WPREG_OFFSET       0x00u
#define HT5023_CMU_CLKCTRL1_OFFSET    0x30u
#define HT5023_WPREG_UNLOCK_KEY       0xA55Au

#define HT5023_TMR0_BASE              0x40001000u
#define HT5023_TMR1_BASE              0x40002000u
#define HT5023_TMR2_BASE              0x40003000u
#define HT5023_TMR3_BASE              0x40004000u

#define TMRCON_OFF                    0x00u
#define TMRCON_MODE_PERIODIC          (0x3u << 1)   // MODE[1:0] = 11
#define TMRCON_CNTEN                  (1u << 0)

#define TMRCON_OFFSET                 0x00u
#define TMRDIV_OFFSET                 0x04u
#define TMRPRD_OFFSET                 0x08u
#define TMRCNT_OFFSET                 0x10u
#define TMRIE_OFFSET                  0x18u
#define TMRIF_OFFSET                  0x1Cu

#define TMRIF_PRDIF                   (1u << 0)

#define CLKCTRL1_TMR3_EN_BIT          (1u << 3)
#define CLKCTRL1_TMR2_EN_BIT          (1u << 2)
#define CLKCTRL1_TMR1_EN_BIT          (1u << 1)
#define CLKCTRL1_TMR0_EN_BIT          (1u << 0)

static inline volatile uint16_t* reg16(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(base + off);
}

static uint32_t tmr_base(ht5023_timer_id_t id) {
  switch (id) {
    case HT5023_TMR0: return HT5023_TMR0_BASE;
    case HT5023_TMR1: return HT5023_TMR1_BASE;
    case HT5023_TMR2: return HT5023_TMR2_BASE;
    case HT5023_TMR3: return HT5023_TMR3_BASE;
    default: return 0u;
  }
}

static uint16_t tmr_clk_mask(ht5023_timer_id_t id) {
  switch (id) {
    case HT5023_TMR0: return (uint16_t)CLKCTRL1_TMR0_EN_BIT;
    case HT5023_TMR1: return (uint16_t)CLKCTRL1_TMR1_EN_BIT;
    case HT5023_TMR2: return (uint16_t)CLKCTRL1_TMR2_EN_BIT;
    case HT5023_TMR3: return (uint16_t)CLKCTRL1_TMR3_EN_BIT;
    default: return 0u;
  }
}

static void tmr_enable_clock(ht5023_timer_id_t id) {
  volatile uint16_t* wpreg = reg16(HT5023_CMU_BASE, HT5023_CMU_WPREG_OFFSET);
  volatile uint16_t* clk1  = reg16(HT5023_CMU_BASE, HT5023_CMU_CLKCTRL1_OFFSET);
  uint16_t mask = tmr_clk_mask(id);
  if (mask == 0u) return;

  *wpreg = HT5023_WPREG_UNLOCK_KEY;
  *clk1 = (uint16_t)(*clk1 | mask);
  *wpreg = 0x0000u;
}

static void tmrif_clear_mask(uint32_t base, uint16_t clear_mask) {
  // Flags clear by writing 0; write 1 to keep a flag unchanged.
  uint16_t v = 0x0007u;
  v = (uint16_t)(v & (uint16_t)~clear_mask);
  *reg16(base, TMRIF_OFFSET) = v;
}

bool ht5023_timer_periodic_start(ht5023_timer_id_t id, const ht5023_timer_periodic_cfg_t* cfg) {
  if (!cfg) return false;
  uint32_t base = tmr_base(id);
  if (base == 0u) return false;

  tmr_enable_clock(id);

  *reg16(base, TMRCON_OFFSET) = TMRCON_OFF;
  *reg16(base, TMRIE_OFFSET) = 0x0000u;  // polling mode
  *reg16(base, TMRDIV_OFFSET) = cfg->prescaler;
  *reg16(base, TMRPRD_OFFSET) = cfg->period;
  *reg16(base, TMRCNT_OFFSET) = 0x0000u;
  tmrif_clear_mask(base, 0x0007u);

  *reg16(base, TMRCON_OFFSET) = (uint16_t)(TMRCON_MODE_PERIODIC | TMRCON_CNTEN);
  return true;
}

void ht5023_timer_stop(ht5023_timer_id_t id) {
  uint32_t base = tmr_base(id);
  if (base == 0u) return;
  *reg16(base, TMRCON_OFFSET) = TMRCON_OFF;
}

bool ht5023_timer_period_elapsed(ht5023_timer_id_t id) {
  uint32_t base = tmr_base(id);
  if (base == 0u) return false;
  if (((*reg16(base, TMRIF_OFFSET)) & TMRIF_PRDIF) == 0u) return false;
  tmrif_clear_mask(base, (uint16_t)TMRIF_PRDIF);
  return true;
}

uint16_t ht5023_timer_get_count(ht5023_timer_id_t id) {
  uint32_t base = tmr_base(id);
  if (base == 0u) return 0u;
  return *reg16(base, TMRCNT_OFFSET);
}

