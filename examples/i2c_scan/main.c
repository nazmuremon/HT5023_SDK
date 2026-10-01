// examples/i2c_scan/main.c — I2C 7-bit address scanner over UART2 logs
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/i2c.h"
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
static void uhex8(uint8_t v) {
  static const char h[] = "0123456789ABCDEF";
  uputc(h[(v >> 4) & 0xF]); uputc(h[v & 0xF]);
}

int main(void) {
  const ht5023_uart_config_t uart_cfg = {
    .sysclk_hz = UART_EXAMPLE_SYSCLK_HZ, .baud = UART_EXAMPLE_BAUD,
    .enable_tx = true, .enable_rx = true
  };
  const ht5023_i2c_master_cfg_t i2c_cfg = {
    .sysclk_hz = UART_EXAMPLE_SYSCLK_HZ,
    .bus_hz = 100000u,
  };

  uint32_t pass = 0;

  led_gpio_init();
  ht5023_uart2_pins_init_pc11_pc12();
  (void)ht5023_uart_init(HT5023_UART_ID_2, &uart_cfg);
  (void)ht5023_i2c_master_init(&i2c_cfg);

  uputs("\r\nHT5023 I2C scanner demo\r\n");
  uputs("I2C pins (HT5023 pin table): PC13=SCL, PC14=SDA\r\n");
  uputs("Use external pull-ups for reliable bus operation.\r\n\r\n");

  for (;;) {
    uint8_t found = 0;
    uputs("scan pass "); uhex8((uint8_t)pass++); uputs(": ");
    for (uint8_t a = 0x08u; a < 0x78u; ++a) {
      if (ht5023_i2c_probe_7bit(a)) {
        uputs("0x"); uhex8(a); uputc(' ');
        found++;
        led_toggle();
      }
      delay_cycles(2000);
    }
    if (found == 0u) uputs("(none)");
    uputs("\r\n");
    delay_cycles(300000);
  }
}

