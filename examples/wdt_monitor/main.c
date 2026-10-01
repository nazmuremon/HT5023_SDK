// examples/wdt_monitor/main.c — watchdog monitor/feed demo over UART2
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/uart.h"
#include "ht5023/wdt.h"
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

static void uputc(char c) {
  ht5023_uart_write_byte(HT5023_UART_ID_2, (uint8_t)c);
}

static void uputs(const char* s) {
  ht5023_uart_write_str(HT5023_UART_ID_2, s);
}

static void uprint_u32(uint32_t v) {
  char buf[10];
  uint8_t n = 0;
  if (v == 0u) { uputc('0'); return; }
  while (v && n < sizeof(buf)) {
    buf[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  while (n) uputc(buf[--n]);
}

int main(void) {
  const ht5023_uart_config_t uart_cfg = {
    .sysclk_hz = UART_EXAMPLE_SYSCLK_HZ,
    .baud = UART_EXAMPLE_BAUD,
    .enable_tx = true,
    .enable_rx = true,
  };

  const uint8_t setv = ht5023_wdt_set_from_timeout_ms(1024u);
  uint32_t loops = 0;

  led_gpio_init();
  ht5023_uart2_pins_init_pc11_pc12();
  (void)ht5023_uart_init(HT5023_UART_ID_2, &uart_cfg);

  uputs("\r\nHT5023 WDT monitor (UART2 PC11/PC12)\r\n");
  uputs("Configuring watchdog timeout to ~");
  uprint_u32(ht5023_wdt_timeout_ms_from_set(setv));
  uputs(" ms, feeding in loop.\r\n");
  uputs("If JTAGWDTEN pin is low, WDT may be disabled by hardware strap.\r\n\r\n");

  ht5023_wdt_configure_and_feed(setv);

  for (;;) {
    ht5023_wdt_feed();

    uputs("wdt set=");
    uprint_u32(ht5023_wdt_get_set_value());
    uputs(" cnt=");
    uprint_u32(ht5023_wdt_get_counter());
    uputs(" loops=");
    uprint_u32(loops++);
    uputs("\r\n");

    led_toggle();
    delay_cycles(220000);
  }
}

