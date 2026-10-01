#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool ht5023_systick_start(uint32_t cpu_hz, uint32_t tick_hz);
void ht5023_systick_stop(void);
uint32_t ht5023_systick_reload_value(uint32_t cpu_hz, uint32_t tick_hz);

#ifdef __cplusplus
}
#endif
