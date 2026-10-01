// examples/reset_info/main.c — print reset flags from RSTSTA over UART2
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/reset.h"
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
  for (volatile uint32_t i = 0; i < cycles; ++i) __asm__ volatile ("nop");
}

static void uputc(char c) { ht5023_uart_write_byte(HT5023_UART_ID_2, (uint8_t)c); }
static void uputs(const char* s) { ht5023_uart_write_str(HT5023_UART_ID_2, s); }

static void uhex16(uint16_t v) {
  static const char h[] = "0123456789ABCDEF";
  for (int i = 12; i >= 0; i -= 4) uputc(h[(v >> i) & 0xF]);
}

static void print_flag(const char* name, bool v) {
  uputs(name);
  uputs("=");
  uputs(v ? "1" : "0");
  uputs("  ");
}

static void print_reset_status(void) {
  ht5023_reset_status_t s = ht5023_reset_read();
  uputs("\r\nRSTSTA raw=0x");
  uhex16(s.raw);
  uputs("\r\n");
  print_flag("POR", s.por_reset);
  print_flag("LBOR", s.lbor_reset);
  print_flag("WDT", s.wdt_reset);
  print_flag("WAKE", s.wakeup_reset);
  uputs("\r\n");
  print_flag("EXT", s.ext_reset);
  print_flag("SOFT", s.soft_reset);
  print_flag("DBG", s.debug_reset);
  print_flag("BOR", s.bor_reset);
  uputs("\r\n");
  print_flag("SLEEPWK", s.sleep_wakeup);
  print_flag("HOLDWK", s.hold_wakeup);
  print_flag("FLASHWR/LBOR", s.flashwr_lbor_reset);
  uputs("\r\n");
}

int main(void) {
  const ht5023_uart_config_t uart_cfg = {
    .sysclk_hz = UART_EXAMPLE_SYSCLK_HZ,
    .baud = UART_EXAMPLE_BAUD,
    .enable_tx = true,
    .enable_rx = true,
  };

  led_gpio_init();
  ht5023_uart2_pins_init_pc11_pc12();
  (void)ht5023_uart_init(HT5023_UART_ID_2, &uart_cfg);

  uputs("\r\nHT5023 reset info demo (UART2 PC11/PC12)\r\n");
  uputs("Printing reset flags and then clearing known flags once.\r\n");
  print_reset_status();

  ht5023_reset_clear_flags((uint16_t)HT5023_RSTSTA_KNOWN_FLAGS_MASK);
  uputs("After clear:\r\n");
  print_reset_status();

  for (;;) {
    led_toggle();
    delay_cycles(400000);
  }
}

