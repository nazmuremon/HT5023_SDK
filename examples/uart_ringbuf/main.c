// examples/uart_ringbuf/main.c — UART2 RX ring buffer demo (PC11/PC12)
#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/ringbuf.h"
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

static void uprint_u32(uint32_t v) {
  char buf[10];
  uint8_t n = 0;
  if (v == 0u) { uputc('0'); return; }
  while (v && n < sizeof(buf)) { buf[n++] = (char)('0' + (v % 10u)); v /= 10u; }
  while (n) uputc(buf[--n]);
}

int main(void) {
  const ht5023_uart_config_t uart_cfg = {
    .sysclk_hz = UART_EXAMPLE_SYSCLK_HZ,
    .baud = UART_EXAMPLE_BAUD,
    .enable_tx = true,
    .enable_rx = true,
  };

  uint8_t storage[64];
  ht5023_ringbuf_t rb;
  uint32_t rx_total = 0;
  uint32_t dropped = 0;
  uint32_t loops = 0;

  ht5023_ringbuf_init(&rb, storage, (uint16_t)sizeof(storage));

  led_gpio_init();
  ht5023_uart2_pins_init_pc11_pc12();
  (void)ht5023_uart_init(HT5023_UART_ID_2, &uart_cfg);

  uputs("\r\nHT5023 UART2 ring buffer demo (PC11/PC12)\r\n");
  uputs("Type data. It is buffered and echoed in chunks.\r\n\r\n");

  for (;;) {
    uint8_t b;
    bool had_rx = false;

    while (ht5023_uart_try_read_byte(HT5023_UART_ID_2, &b)) {
      had_rx = true;
      rx_total++;
      if (!ht5023_ringbuf_push(&rb, b)) {
        dropped++;
      }
    }

    if (had_rx) led_toggle();

    // Drain up to 16 bytes per loop to show decoupled buffering.
    uint8_t drained = 0;
    if (!ht5023_ringbuf_is_empty(&rb)) {
      uputs("[drain ");
      uprint_u32(ht5023_ringbuf_count(&rb));
      uputs("] ");
      while (drained < 16u && ht5023_ringbuf_pop(&rb, &b)) {
        if (b >= 32u && b <= 126u) uputc((char)b);
        else if (b == '\r' || b == '\n') uputc((char)b);
        else uputc('.');
        drained++;
      }
      uputs("\r\n");
    }

    if ((loops++ & 31u) == 0u) {
      uputs("stats: rx=");
      uprint_u32(rx_total);
      uputs(" buffered=");
      uprint_u32(ht5023_ringbuf_count(&rb));
      uputs("/");
      uprint_u32(ht5023_ringbuf_capacity(&rb));
      uputs(" dropped=");
      uprint_u32(dropped);
      uputs("\r\n");
    }

    delay_cycles(30000);
  }
}

