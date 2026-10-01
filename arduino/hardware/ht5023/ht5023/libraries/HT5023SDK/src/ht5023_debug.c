// ht5023_debug.c — UART2-backed debug console helpers
#include "ht5023_debug.h"

#include "ht5023_uart.h"

static void uart2_log_sink(const char* bytes, size_t len, void* user) {
  (void)user;
  ht5023_uart_write(HT5023_UART_ID_2, bytes, len);
}

bool ht5023_debug_uart2_init(uint32_t sysclk_hz, uint32_t baud) {
  const ht5023_uart_config_t cfg = {
    .sysclk_hz = sysclk_hz,
    .baud = baud,
    .enable_tx = true,
    .enable_rx = true,
  };
  ht5023_uart2_pins_init_pc11_pc12();
  return ht5023_uart_init(HT5023_UART_ID_2, &cfg);
}

bool ht5023_debug_uart2_init_log(uint32_t sysclk_hz, uint32_t baud, ht5023_log_level_t level) {
  if (!ht5023_debug_uart2_init(sysclk_hz, baud)) return false;
  ht5023_log_set_level(level);
  ht5023_debug_uart2_use_as_log_sink();
  return true;
}

void ht5023_debug_uart2_write(const char* bytes, size_t len) {
  if (!bytes || len == 0u) return;
  ht5023_uart_write(HT5023_UART_ID_2, bytes, len);
}

void ht5023_debug_uart2_write_str(const char* s) {
  if (!s) return;
  ht5023_uart_write_str(HT5023_UART_ID_2, s);
}

void ht5023_debug_uart2_use_as_log_sink(void) {
  ht5023_log_set_sink(uart2_log_sink, 0);
}
