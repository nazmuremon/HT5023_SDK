// examples/boot_banner/main.c — boot banner + runtime stats over UART2 (PC11/PC12)
#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/boot_banner.h"
#include "ht5023/uart.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_toggle(void);

#ifndef UART_EXAMPLE_SYSCLK_HZ
#define UART_EXAMPLE_SYSCLK_HZ HT5023_FSYS_HZ
#endif

#ifndef UART_EXAMPLE_BAUD
#define UART_EXAMPLE_BAUD 9600u
#endif

static void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; ++i) {
    __asm__ volatile ("nop");
  }
}

static void uart2_banner_putc(char c, void* ctx) {
  (void)ctx;
  ht5023_uart_write_byte(HT5023_UART_ID_2, (uint8_t)c);
}

static void uart2_puts(const char* s) {
  ht5023_uart_write_str(HT5023_UART_ID_2, s);
}

int main(void) {
  const ht5023_uart_config_t uart_cfg = {
    .sysclk_hz = UART_EXAMPLE_SYSCLK_HZ,
    .baud = UART_EXAMPLE_BAUD,
    .enable_tx = true,
    .enable_rx = true,
  };

  ht5023_banner_chip_info_t chip = {
    .chip_name = "HT5023",
    .core_name = "ARM Cortex-M0",
    .flash_kib = 256u,
    .info_flash_kib = 1u,
    .sram_kib = 32u,
    .sysclk_hz_nominal = UART_EXAMPLE_SYSCLK_HZ,
    .uart_ports = 6u,
    .has_spi = true,
    .has_i2c = true,
    .has_rtc = true,
    .has_lcd = true,
    .has_dma = true,
  };

  ht5023_banner_runtime_t rt = {
    .app_name = "boot_banner",
    .version = "v1",
    .uart_path = "UART2 PC11/PC12 @ 9600",
    .uptime_ms = 0u,
    .frames = 0u,
    .rx_bytes = 0u,
    .resets_seen = 1u,
    .ansi_enabled = true,
  };

  led_gpio_init();
  ht5023_uart2_pins_init_pc11_pc12();
  (void)ht5023_uart_init(HT5023_UART_ID_2, &uart_cfg);

  ht5023_boot_banner_print(uart2_banner_putc, 0, &chip, &rt);
  uart2_puts("\r\nType any bytes to increment RX counter. LED toggles on activity.\r\n\r\n");

  for (;;) {
    uint8_t b;
    bool had_rx = false;
    while (ht5023_uart_try_read_byte(HT5023_UART_ID_2, &b)) {
      rt.rx_bytes++;
      had_rx = true;
    }

    if (had_rx || ((rt.frames & 7u) == 0u)) {
      led_toggle();
    }

    ht5023_boot_banner_print_runtime_line(uart2_banner_putc, 0, &rt);

    delay_cycles(80000);
    rt.frames++;
    rt.uptime_ms += 20u;  // coarse estimate for display only

    if ((rt.frames % 150u) == 0u) {
      // Refresh the full banner periodically in case terminal state gets messy.
      ht5023_boot_banner_print(uart2_banner_putc, 0, &chip, &rt);
      uart2_puts("\r\nType any bytes to increment RX counter. LED toggles on activity.\r\n\r\n");
    }
  }
}

