// examples/uart_ansi/main.c — ANSI color loader animation over UART2 (PC11/PC12)
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
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

static void uputs(const char* s) {
  ht5023_uart_write_str(HT5023_UART_ID_2, s);
}

static void uputc(char c) {
  ht5023_uart_write_byte(HT5023_UART_ID_2, (uint8_t)c);
}

static void ansi_set_fg_idx(uint8_t idx) {
  static const char* seqs[] = {
    "\x1b[31m", "\x1b[33m", "\x1b[32m",
    "\x1b[36m", "\x1b[34m", "\x1b[35m"
  };
  uputs(seqs[idx % 6u]);
}

static void uprint_u32(uint32_t v) {
  char buf[10];
  uint8_t n = 0;
  if (v == 0u) {
    uputc('0');
    return;
  }
  while (v != 0u && n < sizeof(buf)) {
    buf[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  while (n) {
    uputc(buf[--n]);
  }
}

static void draw_loader(uint32_t t) {
  static const char spinner[] = { '|', '/', '-', '\\' };
  const uint8_t width = 30;
  const uint8_t phase = (uint8_t)(t % (width * 2u));
  const uint8_t fill = (phase < width) ? phase : (uint8_t)((width * 2u) - phase);
  const uint8_t pulse = (uint8_t)(t % 6u);

  uputs("\x1b[H");

  uputs("\x1b[2K\x1b[1;37mHT5023 COLOR LOADER \x1b[0m");
  ansi_set_fg_idx((uint8_t)(t + 1u));
  uputc(spinner[t & 3u]);
  uputs("\r\n");

  uputs("\x1b[2K[");
  for (uint8_t i = 0; i < width; ++i) {
    if (i < fill) {
      ansi_set_fg_idx((uint8_t)(i + t));
      uputc('#');
    } else if (i == fill && fill < width) {
      uputs("\x1b[97m");
      uputc('>');
    } else {
      uputs("\x1b[90m");
      uputc('.');
    }
  }
  uputs("\x1b[0m]\r\n");

  uputs("\x1b[2K");
  ansi_set_fg_idx((uint8_t)(t + 2u));
  uputs("loading ");
  uputs("\x1b[1;36m");
  uprint_u32((uint32_t)fill * 100u / width);
  uputs("%\x1b[0m  ");
  uputs("\x1b[90m(");
  uputs("UART2 PC11/PC12");
  uputs(")\x1b[0m\r\n");

  uputs("\x1b[2K");
  for (uint8_t i = 0; i < width; ++i) {
    if (((i + pulse) % 6u) == 0u) {
      ansi_set_fg_idx((uint8_t)(t + i));
      uputc('*');
    } else {
      uputs("\x1b[90m");
      uputc('-');
    }
  }
  uputs("\x1b[0m\r\n");

  uputs("\x1b[2K");
  ansi_set_fg_idx((uint8_t)(t + 3u));
  uputs("Press keys to inject serial bytes -> ");
  uputs("\x1b[0m");
  while (ht5023_uart_rx_ready(HT5023_UART_ID_2)) {
    uint8_t c = ht5023_uart_read_byte_blocking(HT5023_UART_ID_2);
    ansi_set_fg_idx((uint8_t)(c + t));
    if (c >= 32u && c <= 126u) uputc((char)c);
    else uputc('.');
    uputs("\x1b[0m");
  }
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

  // Clear screen, home, hide cursor.
  uputs("\x1b[2J\x1b[H\x1b[?25l");
  uputs("\x1b[1;37mHT5023 UART2 ANSI loader demo\x1b[0m\r\n");
  uputs("Open terminal as 9600 8N1, ANSI enabled.\r\n\r\n");

  for (uint32_t t = 0;; ++t) {
    draw_loader(t);
    if ((t & 7u) == 0u) {
      led_toggle();
    }
    delay_cycles(70000);
  }
}
