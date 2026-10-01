// examples/usb_echo/main.c — echo test for the USB-UART link. No options.
//
// Flash this when you want to know whether the USB serial link works. It talks
// on UART2 only -- no semihosting, no auto-detection, nothing to configure --
// so if the wiring and baud are right you WILL see output:
//
//   HT5023 usb_echo ready on UART2 (PC11=TX2, PC12=RX2) @ 115200 8N1
//   type anything and press Enter
//   > hello
//   echo: hello
//
// The LEDs toggle on every byte received, so traffic is visible even before a
// line is finished.
//
// Wiring: PC11 = TX2 -> adapter RX, PC12 = RX2 <- adapter TX, GND common.
//
// Build standalone for the J-Link:      make EXAMPLE=usb_echo
// Build for the UART bootloader:        make EXAMPLE=usb_echo BOOT_APP=1

#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/uart.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_off(void);
extern void led_toggle(void);

#define UART_ID HT5023_UART_ID_2
#define BAUD 115200u

// Fsys, the clock the UART divider counts: HT5023_FSYS_HZ from
// ht5023/core/system.h, measured at 9.196 MHz (the datasheet's 9.5 MHz is
// -2.65 % off at 115200). If text is garbled on a different board, measure
// its HRC and override HT5023_FSYS_HZ.
#ifndef USB_ECHO_SYSCLK_HZ
#define USB_ECHO_SYSCLK_HZ HT5023_FSYS_HZ
#endif

#define LINE_MAX 128u

static void put(const char* s) { ht5023_uart_write_str(UART_ID, s); }

static void put_line(const char* s) {
  put(s);
  put("\r\n");
}

int main(void) {
  static char line[LINE_MAX + 1u];
  uint32_t len = 0u;

  led_gpio_init();
  led_off();

  const ht5023_uart_config_t cfg = {
    .sysclk_hz = USB_ECHO_SYSCLK_HZ,
    .baud = BAUD,
    .enable_tx = true,
    .enable_rx = true,
  };
  ht5023_uart2_pins_init_pc11_pc12();
  if (!ht5023_uart_init(UART_ID, &cfg)) {
    // Nothing can be printed, so say it with the LEDs: fast permanent blink.
    while (1) {
      led_toggle();
      for (volatile uint32_t d = 0; d < 20000u; ++d) {
        __asm__ volatile("nop");
      }
    }
  }

  put_line("");
  put_line("HT5023 usb_echo ready on UART2 (PC11=TX2, PC12=RX2) @ 115200 8N1");
  put_line("type anything and press Enter");

  while (1) {
    uint8_t c;
    if (!ht5023_uart_try_read_byte(UART_ID, &c)) continue;

    led_toggle();

    if (c == '\r' || c == '\n') {
      if (len > 0u) {
        line[len] = '\0';
        put("echo: ");
        put_line(line);
        len = 0u;
      }
      continue;
    }

    if (c == 0x08u || c == 0x7Fu) {  // backspace
      if (len > 0u) --len;
      continue;
    }

    // Deliberately NOT echoing each character: the receiver holds a single byte
    // and sending one takes a full character time, during which the next byte
    // arrives and is lost -- typing "hello usb" came back as "helo sb". The line
    // is collected first, then answered.
    if (len < LINE_MAX) line[len++] = (char)c;
  }
}
