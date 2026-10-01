// examples/timer_poll/main.c — TMR0 periodic polling demo with UART2 output
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/timer.h"
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

  // 10 Hz: tick = Fsys / ((prescaler+1) * (period+1)) = Fsys / (Fsys/1000 * 100).
  // Derived from HT5023_FSYS_HZ; the old fixed 9499 assumed 9.5 MHz and gave 9.68 Hz.
  const ht5023_timer_periodic_cfg_t tmr_cfg = {
    .prescaler = (uint16_t)(UART_EXAMPLE_SYSCLK_HZ / 1000u - 1u),
    .period = 99u,
  };

  uint32_t ticks = 0;

  led_gpio_init();
  ht5023_uart2_pins_init_pc11_pc12();
  (void)ht5023_uart_init(HT5023_UART_ID_2, &uart_cfg);
  (void)ht5023_timer_periodic_start(HT5023_TMR0, &tmr_cfg);

  uputs("\r\nHT5023 TMR0 polling demo (UART2 PC11/PC12)\r\n");
  uputs("Polling PRDIF and printing a tick every period.\r\n\r\n");

  for (;;) {
    if (!ht5023_timer_period_elapsed(HT5023_TMR0)) {
      continue;
    }

    ticks++;
    led_toggle();

    uputs("tick=");
    uprint_u32(ticks);
    uputs(" tmr0_cnt=");
    uprint_u32(ht5023_timer_get_count(HT5023_TMR0));
    uputs("\r\n");
  }
}

