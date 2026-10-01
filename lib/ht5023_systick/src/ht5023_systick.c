// ht5023_systick.c — Cortex-M0 SysTick helper (core peripheral)
#include "ht5023_systick.h"

#include <stdint.h>

#define SYST_CSR   (*(volatile uint32_t*)0xE000E010u)
#define SYST_RVR   (*(volatile uint32_t*)0xE000E014u)
#define SYST_CVR   (*(volatile uint32_t*)0xE000E018u)

#define SYST_CSR_ENABLE      (1u << 0)
#define SYST_CSR_TICKINT     (1u << 1)
#define SYST_CSR_CLKSOURCE   (1u << 2)  // core clock

uint32_t ht5023_systick_reload_value(uint32_t cpu_hz, uint32_t tick_hz) {
  if (cpu_hz == 0u || tick_hz == 0u) return 0u;
  uint32_t ticks = cpu_hz / tick_hz;
  if (ticks == 0u) return 0u;
  uint32_t reload = ticks - 1u;
  if (reload > 0x00FFFFFFu) return 0u;
  return reload;
}

bool ht5023_systick_start(uint32_t cpu_hz, uint32_t tick_hz) {
  if (cpu_hz == 0u || tick_hz == 0u) return false;
  // tick_hz > cpu_hz means ticks < 1, which is not representable.
  if (tick_hz > cpu_hz) return false;

  uint32_t reload = ht5023_systick_reload_value(cpu_hz, tick_hz);
  // reload_value() returns 0 for two reasons:
  //   1. Invalid/overflow input  — already rejected above.
  //   2. cpu_hz == tick_hz  (ticks=1, reload=0) — this is valid and desired.
  // The only remaining bad case is overflow, also already caught above.

  SYST_CSR = 0u;
  SYST_RVR = reload;
  SYST_CVR = 0u;
  SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
  return true;
}

void ht5023_systick_stop(void) {
  SYST_CSR = 0u;
}
