// ht5023_timer.h — HT5023 TMR0..TMR3 polling API
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HT5023_TMR0 = 0,
  HT5023_TMR1 = 1,
  HT5023_TMR2 = 2,
  HT5023_TMR3 = 3,
} ht5023_timer_id_t;

typedef struct {
  uint16_t prescaler;   // TMRDIV, divides input by (prescaler + 1)
  uint16_t period;      // TMRPRD
} ht5023_timer_periodic_cfg_t;

bool ht5023_timer_periodic_start(ht5023_timer_id_t id, const ht5023_timer_periodic_cfg_t* cfg);
void ht5023_timer_stop(ht5023_timer_id_t id);

// Returns true and clears PRDIF when the period has elapsed; false if not yet.
bool ht5023_timer_period_elapsed(ht5023_timer_id_t id);
uint16_t ht5023_timer_get_count(ht5023_timer_id_t id);

#ifdef __cplusplus
}
#endif
