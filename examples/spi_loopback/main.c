// examples/spi_loopback/main.c — SPI master loopback demo (PC4..PC7) + UART2 logs
#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/spi.h"
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
  uputc(h[(v >> 4) & 0xF]);
  uputc(h[v & 0xF]);
}

int main(void) {
  const ht5023_uart_config_t uart_cfg = {
    .sysclk_hz = UART_EXAMPLE_SYSCLK_HZ, .baud = UART_EXAMPLE_BAUD,
    .enable_tx = true, .enable_rx = true
  };
  const ht5023_spi_master_cfg_t spi_cfg = {
    .spr = 3u,   // Fsys/16
    .cpol = false,
    .cpha = false,
  };

  uint8_t pattern = 0x3Cu;
  uint32_t n = 0;

  led_gpio_init();
  ht5023_uart2_pins_init_pc11_pc12();
  (void)ht5023_uart_init(HT5023_UART_ID_2, &uart_cfg);
  (void)ht5023_spi_master_init(&spi_cfg);

  uputs("\r\nHT5023 SPI loopback demo\r\n");
  uputs("SPI pins: PC4=MOSI, PC5=MISO, PC6=CLK, PC7=CS\r\n");
  uputs("For test, jumper PC4 <-> PC5 (MOSI to MISO).\r\n\r\n");

  for (;;) {
    ht5023_spi_set_cs(false);
    uint8_t rx = ht5023_spi_transfer_byte(pattern);
    ht5023_spi_set_cs(true);

    uputs("tx=0x"); uhex8(pattern);
    uputs(" rx=0x"); uhex8(rx);
    if (rx == pattern) uputs(" OK");
    else uputs(" MISMATCH");
    uputs(" n=");
    {
      char tmp[10]; uint8_t m = 0; uint32_t v = n++;
      if (v == 0u) uputc('0');
      else { while (v && m < sizeof(tmp)) { tmp[m++] = (char)('0' + (v % 10u)); v /= 10u; } while (m) uputc(tmp[--m]); }
    }
    uputs("\r\n");

    led_toggle();
    pattern = (uint8_t)(pattern + 0x11u);
    delay_cycles(220000);
  }
}

