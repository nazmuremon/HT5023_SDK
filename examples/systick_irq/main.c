// examples/systick_irq/main.c — SysTick interrupt demo + UART2 runtime output
#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/systick.h"
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

// Datasheet SYSCLKDIV default is /2 for Fcpu, while UART baud uses Fsys.
#ifndef CPU_EXAMPLE_HZ
#define CPU_EXAMPLE_HZ (UART_EXAMPLE_SYSCLK_HZ / 2u)
#endif

volatile uint32_t g_systick_ticks = 0;

void SysTick_Handler(void) {
  g_systick_ticks++;
}

static void uputc(char c) { ht5023_uart_write_byte(HT5023_UART_ID_2, (uint8_t)c); }
static void uputs(const char* s) { ht5023_uart_write_str(HT5023_UART_ID_2, s); }
static void uprint_u32(uint32_t v) {
  char buf[10]; uint8_t n = 0;
  if (v == 0u) { uputc('0'); return; }
  while (v && n < sizeof(buf)) { buf[n++] = (char)('0' + (v % 10u)); v /= 10u; }
  while (n) uputc(buf[--n]);
}

int main(void) {
  const ht5023_uart_config_t uart_cfg = {
    .sysclk_hz = UART_EXAMPLE_SYSCLK_HZ, .baud = UART_EXAMPLE_BAUD,
    .enable_tx = true, .enable_rx = true
  };
  uint32_t last_print = 0;
  uint32_t last_led = 0;

  led_gpio_init();
  ht5023_uart2_pins_init_pc11_pc12();
  (void)ht5023_uart_init(HT5023_UART_ID_2, &uart_cfg);

  if (!ht5023_systick_start(CPU_EXAMPLE_HZ, 1000u)) {
    uputs("\r\nSysTick start failed (reload out of range)\r\n");
    while (1) { }
  }

  uputs("\r\nHT5023 SysTick IRQ demo (UART2 PC11/PC12)\r\n");
  uputs("SysTick running at 1 kHz interrupt rate.\r\n\r\n");

  for (;;) {
    uint32_t now = g_systick_ticks;
    if ((uint32_t)(now - last_led) >= 250u) {
      last_led = now;
      led_toggle();
    }
    if ((uint32_t)(now - last_print) >= 1000u) {
      last_print = now;
      uputs("ticks=");
      uprint_u32(now);
      uputs(" (~ms)\r\n");
    }
  }
}

