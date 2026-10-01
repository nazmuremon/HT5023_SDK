// ht5023_systick.h — Cortex-M0 SysTick helper (core peripheral)
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Start SysTick with the processor core clock at tick_hz interrupts per second.
// Returns false if the parameters are invalid (zero, or tick_hz > cpu_hz,
// or the resulting reload value exceeds the 24-bit SysTick counter range).
// The caller must define SysTick_Handler() to handle tick interrupts.
bool ht5023_systick_start(uint32_t cpu_hz, uint32_t tick_hz);

void ht5023_systick_stop(void);

// Compute the SysTick RELOAD register value for the given rate.
// Returns 0 on error (invalid inputs or overflow) or when ticks == 1 (reload = 0 is valid).
uint32_t ht5023_systick_reload_value(uint32_t cpu_hz, uint32_t tick_hz);

#ifdef __cplusplus
}
#endif
