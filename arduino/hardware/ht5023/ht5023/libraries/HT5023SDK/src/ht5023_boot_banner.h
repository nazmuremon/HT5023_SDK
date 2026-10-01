// ht5023_boot_banner.h — colored boot banner and runtime status printer
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ht5023_banner_putc_fn)(char c, void* ctx);

typedef struct {
  const char* chip_name;         // e.g. "HT5023"
  const char* core_name;         // e.g. "Cortex-M0"
  uint32_t flash_kib;            // e.g. 256
  uint32_t info_flash_kib;       // e.g. 1
  uint32_t sram_kib;             // e.g. 32
  uint32_t sysclk_hz_nominal;    // configured/assumed system clock
  uint8_t uart_ports;            // e.g. 6
  bool has_spi;
  bool has_i2c;
  bool has_rtc;
  bool has_lcd;
  bool has_dma;
} ht5023_banner_chip_info_t;

typedef struct {
  const char* app_name;          // e.g. "boot_banner"
  const char* version;           // e.g. "v1"
  const char* uart_path;         // e.g. "UART2 PC11/PC12 @ 9600"
  uint32_t uptime_ms;
  uint32_t frames;
  uint32_t rx_bytes;
  uint32_t resets_seen;
  bool ansi_enabled;
} ht5023_banner_runtime_t;

void ht5023_boot_banner_print(
    ht5023_banner_putc_fn putc_fn, void* ctx,
    const ht5023_banner_chip_info_t* chip,
    const ht5023_banner_runtime_t* rt);

void ht5023_boot_banner_print_runtime_line(
    ht5023_banner_putc_fn putc_fn, void* ctx,
    const ht5023_banner_runtime_t* rt);

#ifdef __cplusplus
}
#endif
