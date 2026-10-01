// ht5023_debug.h — UART2-backed debug console helpers
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ht5023_log.h"

#ifdef __cplusplus
extern "C" {
#endif

bool ht5023_debug_uart2_init(uint32_t sysclk_hz, uint32_t baud);
bool ht5023_debug_uart2_init_log(uint32_t sysclk_hz, uint32_t baud, ht5023_log_level_t level);
void ht5023_debug_uart2_write(const char* bytes, size_t len);
void ht5023_debug_uart2_write_str(const char* s);
void ht5023_debug_uart2_use_as_log_sink(void);

#ifdef __cplusplus
}
#endif
