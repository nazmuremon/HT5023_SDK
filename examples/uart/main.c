// examples/uart/main.c — UART2 echo + heartbeat on HT5023 (PC11/PC12)
#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/uart.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_toggle(void);

static void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; ++i) {
    __asm__ volatile ("nop");
  }
}

#ifndef UART_EXAMPLE_SYSCLK_HZ
#define UART_EXAMPLE_SYSCLK_HZ HT5023_FSYS_HZ
#endif

#ifndef UART_EXAMPLE_BAUD
#define UART_EXAMPLE_BAUD 9600u
#endif

static void uart_write_hex8(uint8_t v) {
  static const char hex[] = "0123456789ABCDEF";
  char out[2];
  out[0] = hex[(v >> 4) & 0x0F];
  out[1] = hex[v & 0x0F];
  ht5023_uart_write(HT5023_UART_ID_2, out, 2);
}

int main(void) {
  // UART_EXAMPLE_SYSCLK_HZ defaults to HT5023_FSYS_HZ, the measured HRC -- see
  // ht5023/core/system.h before changing it.
  const ht5023_uart_config_t uart_cfg = {
    .sysclk_hz = UART_EXAMPLE_SYSCLK_HZ,
    .baud = UART_EXAMPLE_BAUD,
    .enable_tx = true,
    .enable_rx = true,
  };

  led_gpio_init();
  ht5023_uart2_pins_init_pc11_pc12();
  (void)ht5023_uart_init(HT5023_UART_ID_2, &uart_cfg);

  ht5023_uart_write_str(HT5023_UART_ID_2, "\r\nHT5023 UART2 ready (PC11=TX2, PC12=RX2)\r\n");
  ht5023_uart_write_str(HT5023_UART_ID_2, "Type characters for echo. LED toggles on RX.\r\n");

  uint32_t heartbeat = 0;
  while (1) {
    uint8_t c;
    if (ht5023_uart_try_read_byte(HT5023_UART_ID_2, &c)) {
      led_toggle();
      ht5023_uart_write_str(HT5023_UART_ID_2, "RX: 0x");
      uart_write_hex8(c);
      ht5023_uart_write_str(HT5023_UART_ID_2, " '");
      if (c >= 32u && c <= 126u) {
        ht5023_uart_write_byte(HT5023_UART_ID_2, c);
      } else {
        ht5023_uart_write_byte(HT5023_UART_ID_2, '.');
      }
      ht5023_uart_write_str(HT5023_UART_ID_2, "'\r\n");
    }

    delay_cycles(50000);
    if (++heartbeat >= 20u) {
      heartbeat = 0u;
      led_toggle();
      ht5023_uart_write_str(HT5023_UART_ID_2, "tick\r\n");
    }
  }
}
