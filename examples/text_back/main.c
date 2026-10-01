// examples/text_back/main.c — whatever you type, the MCU sends it back twice
//
// The simplest round-trip test for the Flasher's send box. Type a line, press
// Enter, and the target replies with the same text twice.
//
//   > hello
//   hello
//   hello
//
// It works over either link:
//
//   * Semihosting (J-Link cable only, no extra wiring) — used automatically when
//     the Flasher's monitor is attached. Set the log source to Semihosting.
//   * UART2 (PC11 = TX2 -> adapter RX, PC12 = RX2 <- adapter TX, GND common,
//     9600 8N1) — used when no debugger armed semihosting.
//
// Semihosting input blocks: while the target is waiting for a character it sits
// in the debugger, so the LEDs pause between keystrokes. That is expected.

#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/uart.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_off(void);
extern void led_toggle(void);

// ===========================================================================
// CONFIG — edit these
// ===========================================================================

// Which link carries the conversation:
//   3 = auto    semihosting when the Flasher's monitor armed it, else UART
//   2 = semihosting only (J-Link)
//   1 = UART only
#ifndef TEXT_BACK_LINK
#define TEXT_BACK_LINK 3
#endif

// Which UART pins the link uses:
//   1 -> UART1 on PC0 (TX1) / PC1 (RX1)
//   2 -> UART2 on PC11 (TX2) / PC12 (RX2)   <- default: the USB-UART adapter
//   0 -> UART0 on PC3 (TX0) / PC2 (RX0)
#ifndef TEXT_BACK_PORT
#define TEXT_BACK_PORT 2
#endif

#ifndef TEXT_BACK_BAUD
#define TEXT_BACK_BAUD 115200u
#endif

// Frame format: 8 data bits, no parity, 1 stop bit. Stated here so the wire
// format is explicit, and checked below — ht5023_uart_init() always configures
// 8N1, so setting these to anything else would silently lie. The hardware can do
// other frames through UARTCON, but this SDK does not name those encodings; see
// UART_AT_UARTCON_SET / UART_AT_UARTCON_CLR in the uart_at example.
#ifndef TEXT_BACK_DATA_BITS
#define TEXT_BACK_DATA_BITS 8
#endif

#ifndef TEXT_BACK_PARITY
#define TEXT_BACK_PARITY 'N'
#endif

#ifndef TEXT_BACK_STOP_BITS
#define TEXT_BACK_STOP_BITS 1
#endif

#if (TEXT_BACK_DATA_BITS != 8) || (TEXT_BACK_STOP_BITS != 1) || (TEXT_BACK_PARITY != 'N')
#error "ht5023_uart_init() configures 8N1 only -- use the raw UARTCON bits for other frames."
#endif

// Fsys, the clock the UART divider counts: HT5023_FSYS_HZ from
// ht5023/core/system.h, measured at 9.196 MHz (the datasheet's 9.5 MHz is
// -2.65 % off at 115200). If text is garbled on a different board, measure
// its HRC and override HT5023_FSYS_HZ.
#ifndef TEXT_BACK_SYSCLK_HZ
#define TEXT_BACK_SYSCLK_HZ HT5023_FSYS_HZ
#endif

// How many times each line is sent back. "Twice" is the point of this example,
// but any count works.
#ifndef TEXT_BACK_REPEAT
#define TEXT_BACK_REPEAT 2u
#endif

// Echo characters as they arrive. Off by default so the reply is exactly two
// copies and nothing else — turn it on if your terminal does not echo locally.
#ifndef TEXT_BACK_ECHO
#define TEXT_BACK_ECHO 0
#endif

// Terminator appended to each reply line.
#ifndef TEXT_BACK_EOL
#define TEXT_BACK_EOL "\r\n"
#endif

// Number each copy: "1: hello" / "2: hello" instead of plain copies.
#ifndef TEXT_BACK_NUMBERED
#define TEXT_BACK_NUMBERED 0
#endif

// Longest line accepted; anything longer is reported instead of echoed.
#ifndef TEXT_BACK_LINE_MAX
#define TEXT_BACK_LINE_MAX 128u
#endif

// Toggle the LEDs on every received byte, as a wiring sanity check.
#ifndef TEXT_BACK_LED
#define TEXT_BACK_LED 1
#endif

// ===========================================================================

#if TEXT_BACK_PORT == 1
#define TEXT_BACK_ID HT5023_UART_ID_1
#define TEXT_BACK_PINS_INIT() ht5023_uart1_pins_init_pc0_pc1()
#define TEXT_BACK_PIN_TEXT "UART1 PC0=TX1 PC1=RX1"
#elif TEXT_BACK_PORT == 2
#define TEXT_BACK_ID HT5023_UART_ID_2
#define TEXT_BACK_PINS_INIT() ht5023_uart2_pins_init_pc11_pc12()
#define TEXT_BACK_PIN_TEXT "UART2 PC11=TX2 PC12=RX2"
#elif TEXT_BACK_PORT == 0
#define TEXT_BACK_ID HT5023_UART_ID_0
#define TEXT_BACK_PINS_INIT() ht5023_uart0_pins_init_pc2_pc3()
#define TEXT_BACK_PIN_TEXT "UART0 PC3=TX0 PC2=RX0"
#else
#error "TEXT_BACK_PORT must be 0, 1 or 2."
#endif

// --- ARM semihosting -------------------------------------------------------
#define SYS_WRITE0 0x04u  // r1 -> NUL-terminated string
#define SYS_READC 0x07u   // returns one character read from the debugger

// A `bkpt 0xAB` with no debugger servicing it stops the core, so semihosting
// stays off until a debugger opts in. The Flasher's monitor sets this at main().
// Never gate this on DHCSR.C_DEBUGEN: that register is debugger-only and reads
// back unreliably from software on this part.
volatile int g_semihost_enable = 0;

static int semihost(int op, void* arg) {
  register int r0 __asm__("r0") = op;
  register void* r1 __asm__("r1") = arg;
  // r1 must be in-out: the debugger does not guarantee it survives the call, and
  // if the compiler assumes it does it will reuse it as a constant afterwards
  // (e.g. comparing the result against a stale r1 instead of 0).
  __asm__ volatile("bkpt 0xAB" : "+r"(r0), "+r"(r1) : : "memory", "cc");
  return r0;
}

static bool use_semihost(void) {
#if TEXT_BACK_LINK == 2
  return true;
#elif TEXT_BACK_LINK == 1
  return false;
#else
  return g_semihost_enable != 0;
#endif
}

// --- link I/O --------------------------------------------------------------
static void put(const char* s) {
  if (use_semihost()) {
    (void)semihost(SYS_WRITE0, (void*)s);
  } else {
    ht5023_uart_write_str(TEXT_BACK_ID, s);
  }
}

static void put_line(const char* s) {
  put(s);
  put(TEXT_BACK_EOL);
}

static void put_char(uint8_t c) {
  if (use_semihost()) {
    const char s[2] = {(char)c, '\0'};
    (void)semihost(SYS_WRITE0, (void*)s);
  } else {
    ht5023_uart_write_byte(TEXT_BACK_ID, c);
  }
}

// Fetches one byte. Semihosting reads block until you type; UART polling does
// not, so the caller just spins.
static bool get_byte(uint8_t* out) {
  if (use_semihost()) {
    const int c = semihost(SYS_READC, 0);
    if (c < 0) return false;
    *out = (uint8_t)c;
    return true;
  }
  return ht5023_uart_try_read_byte(TEXT_BACK_ID, out);
}

static void put_u32(uint32_t v) {
  char buf[11];
  int n = 0;
  if (v == 0u) {
    buf[n++] = '0';
  } else {
    char tmp[10];
    int t = 0;
    while (v) {
      tmp[t++] = (char)('0' + (v % 10u));
      v /= 10u;
    }
    while (t) buf[n++] = tmp[--t];
  }
  buf[n] = '\0';
  put(buf);
}

// Sends one received line back TEXT_BACK_REPEAT times.
static void reply(const char* text) {
  for (uint32_t i = 1u; i <= TEXT_BACK_REPEAT; ++i) {
#if TEXT_BACK_NUMBERED
    put_u32(i);
    put(": ");
#else
    (void)i;
#endif
    put_line(text);
  }
}

int main(void) {
  static char line[TEXT_BACK_LINE_MAX + 1u];
  uint32_t len = 0u;
  bool overflow = false;

  led_gpio_init();
  led_off();

  const ht5023_uart_config_t cfg = {
    .sysclk_hz = TEXT_BACK_SYSCLK_HZ,
    .baud = TEXT_BACK_BAUD,
    .enable_tx = true,
    .enable_rx = true,
  };
  TEXT_BACK_PINS_INIT();
  if (!ht5023_uart_init(TEXT_BACK_ID, &cfg) && !use_semihost()) {
    // Baud/sysclk pair out of divider range and no debug link to complain on:
    // blink forever so the failure is visible.
    while (1) {
      led_toggle();
      for (volatile uint32_t d = 0; d < 45000u; ++d) {
        __asm__ volatile("nop");
      }
    }
  }

  put_line("");
  put("HT5023 text_back ready on ");
  if (use_semihost()) {
    put_line("semihosting (J-Link).");
  } else {
    const char frame[4] = {(char)('0' + TEXT_BACK_DATA_BITS), (char)TEXT_BACK_PARITY,
                           (char)('0' + TEXT_BACK_STOP_BITS), '\0'};
    put(TEXT_BACK_PIN_TEXT);
    put(" @ ");
    put_u32(TEXT_BACK_BAUD);
    put(" ");
    put(frame);
    put_line(".");
  }
  put("Type a line and press Enter -- it comes back ");
  put_u32(TEXT_BACK_REPEAT);
  put_line(" times.");

  while (1) {
    uint8_t c;
    if (!get_byte(&c)) continue;

#if TEXT_BACK_LED
    led_toggle();
#endif
#if TEXT_BACK_ECHO
    put_char(c);
#else
    (void)put_char;
#endif

    if (c == '\r' || c == '\n') {
      if (overflow) {
        put_line("(line too long)");
      } else if (len > 0u) {
        line[len] = '\0';
        reply(line);
      }
      len = 0u;
      overflow = false;
      continue;
    }

    if (c == 0x08u || c == 0x7Fu) {  // backspace / delete
      if (len > 0u) --len;
      continue;
    }

    if (len < TEXT_BACK_LINE_MAX) {
      line[len++] = (char)c;
    } else {
      overflow = true;
    }
  }
}
