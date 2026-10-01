// examples/uart_at/main.c — receive data over UART and answer AT commands
//
// Everything the target receives is reported back on the same UART, so the
// Flasher's DEBUG LOG (source = Serial) shows the incoming data, and its input
// box sends AT commands to the target:
//
//   > AT              -> OK
//   > AT+INFO         -> +INFO: HT5023 uart_at, UART2 @ 9600 8N1
//   > AT+ECHO=0       -> OK           (stop echoing typed characters back)
//   > AT+BLINK        -> OK           (LEDs blink twice)
//   > hello           -> RX[5]: "hello"  +  ERROR
//
// Default wiring: PC11 = TX2 -> adapter RX, PC12 = RX2 <- adapter TX, GND
// common, 9600 8N1. Every parameter is a #define in the CONFIG block below.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/uart.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_on(void);
extern void led_off(void);
extern void led_toggle(void);

// ===========================================================================
// CONFIG — edit these
// ===========================================================================

// Which UART block, and which pin pair to route it to.
//   1 -> UART1 on PC0  (TX1) / PC1  (RX1)
//   2 -> UART2 on PC11 (TX2) / PC12 (RX2)   <- default: the USB-UART adapter
//   0 -> UART0 on PC3  (TX0) / PC2  (RX0)
#ifndef UART_AT_PORT
#define UART_AT_PORT 2
#endif

// Line speed. The divider is Fsys / (2 * (SREL + 1)), so high rates lose
// resolution; at the measured Fsys, 115200 comes out -0.22 % slow.
#ifndef UART_AT_BAUD
#define UART_AT_BAUD 115200u
#endif

// Frame format: 8 data bits, no parity, 1 stop bit. Stated here so the wire
// format is explicit, and checked below — ht5023_uart_init() always configures
// 8N1. For anything else use UART_AT_UARTCON_SET / _CLR further down.
#ifndef UART_AT_DATA_BITS
#define UART_AT_DATA_BITS 8
#endif

#ifndef UART_AT_PARITY
#define UART_AT_PARITY 'N'
#endif

#ifndef UART_AT_STOP_BITS
#define UART_AT_STOP_BITS 1
#endif

// (checked below, once the UARTCON overrides are known)

// Fsys, the clock the UART divider counts: HT5023_FSYS_HZ from
// ht5023/core/system.h, measured at 9.196 MHz (the datasheet's 9.5 MHz is
// -2.65 % off at 115200). If text is garbled on a different board, measure
// its HRC and override HT5023_FSYS_HZ.
#ifndef UART_AT_SYSCLK_HZ
#define UART_AT_SYSCLK_HZ HT5023_FSYS_HZ
#endif

#ifndef UART_AT_ENABLE_TX
#define UART_AT_ENABLE_TX true
#endif

#ifndef UART_AT_ENABLE_RX
#define UART_AT_ENABLE_RX true
#endif

// Echo every received character straight back (what a terminal expects).
// Runtime-switchable with AT+ECHO=0 / AT+ECHO=1.
// OFF by default: this UART holds one byte, and sending the echo takes a full
// character time during which the next incoming byte is lost, so echoing mangles
// anything typed at speed. Enable only if your terminal cannot echo locally.
#ifndef UART_AT_ECHO
#define UART_AT_ECHO 0
#endif

// Terminator the target appends to its own replies.
#ifndef UART_AT_EOL
#define UART_AT_EOL "\r\n"
#endif

// Longest command line accepted (bytes). Longer lines report an overflow.
#ifndef UART_AT_LINE_MAX
#define UART_AT_LINE_MAX 128u
#endif

// Report each received line as RX[len]: "text" before acting on it.
#ifndef UART_AT_SHOW_RX
#define UART_AT_SHOW_RX 1
#endif

// Also dump the raw bytes of each line as hex (useful for binary traffic).
#ifndef UART_AT_SHOW_HEX
#define UART_AT_SHOW_HEX 0
#endif

// Toggle the LEDs on every received byte, as a wiring sanity check.
#ifndef UART_AT_LED_ON_RX
#define UART_AT_LED_ON_RX 1
#endif

// --- Frame format ----------------------------------------------------------
// ht5023_uart_init() configures 8 data bits, 1 stop bit, no parity, positive
// logic. The hardware supports more through UARTCON, but this SDK does not
// name those encodings, so they are exposed here as raw bits: put the value
// your datasheet's UART chapter calls for into UART_AT_UARTCON_SET (OR-ed in)
// and UART_AT_UARTCON_CLR (cleared afterwards). Leave both 0 for 8N1.
//
// Bit positions the driver already defines (UARTCON, offset 0x04):
//   bit 10 NEGFIX   bit 9 UNEG      inverted / fixed-polarity signalling
//   bit  8 STOPSEL  stop-bit select
//   bit  7 LENSEL   data-length select
//   bits 6:5 PARITYSEL   bit 4 PARITYEN   parity mode / enable
// The encodings for these are NOT verified here — check HT5023.pdf before use.
#ifndef UART_AT_UARTCON_SET
#define UART_AT_UARTCON_SET 0x0000u
#endif

#ifndef UART_AT_UARTCON_CLR
#define UART_AT_UARTCON_CLR 0x0000u
#endif

// The frame defines above must match what the driver actually programs, unless
// you are overriding UARTCON yourself.
#if ((UART_AT_DATA_BITS != 8) || (UART_AT_STOP_BITS != 1) || (UART_AT_PARITY != 'N')) \
    && (UART_AT_UARTCON_SET == 0u) && (UART_AT_UARTCON_CLR == 0u)
#error "ht5023_uart_init() configures 8N1 only -- set UART_AT_UARTCON_SET/_CLR for other frames."
#endif

// ===========================================================================

#if UART_AT_PORT == 1
#define UART_AT_ID HT5023_UART_ID_1
#define UART_AT_PINS_INIT() ht5023_uart1_pins_init_pc0_pc1()
#define UART_AT_PIN_TEXT "UART1 PC0=TX1 PC1=RX1"
#define UART_AT_BASE 0x40006000u  // UART1
#elif UART_AT_PORT == 2
#define UART_AT_ID HT5023_UART_ID_2
#define UART_AT_PINS_INIT() ht5023_uart2_pins_init_pc11_pc12()
#define UART_AT_PIN_TEXT "UART2 PC11=TX2 PC12=RX2"
#define UART_AT_BASE 0x40007000u
#elif UART_AT_PORT == 0
#define UART_AT_ID HT5023_UART_ID_0
#define UART_AT_PINS_INIT() ht5023_uart0_pins_init_pc2_pc3()
#define UART_AT_PIN_TEXT "UART0 PC3=TX0 PC2=RX0"
#define UART_AT_BASE 0x40005000u
#else
#error "UART_AT_PORT must be 0, 1 or 2."
#endif

#define UARTCON_OFFSET 0x04u

static bool echo_on = (UART_AT_ECHO != 0);

// ---------------------------------------------------------------------------
// Output helpers
// ---------------------------------------------------------------------------
static void put(const char* s) { ht5023_uart_write_str(UART_AT_ID, s); }

static void put_line(const char* s) {
  put(s);
  put(UART_AT_EOL);
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

#if UART_AT_SHOW_HEX
static void put_hex8(uint8_t v) {
  static const char hex[] = "0123456789ABCDEF";
  char out[3] = {hex[(v >> 4) & 0x0Fu], hex[v & 0x0Fu], '\0'};
  put(out);
}
#endif

// ---------------------------------------------------------------------------
// Tiny string helpers (case-insensitive, no libc)
// ---------------------------------------------------------------------------
static char upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

static bool eq_ci(const char* a, const char* b) {
  while (*a && *b) {
    if (upper(*a) != upper(*b)) return false;
    ++a;
    ++b;
  }
  return *a == *b;
}

static bool starts_ci(const char* s, const char* prefix) {
  while (*prefix) {
    if (upper(*s) != upper(*prefix)) return false;
    ++s;
    ++prefix;
  }
  return true;
}

static void delay_ms(uint32_t ms) {
  for (volatile uint32_t i = 0; i < ms * 450u; ++i) {
    __asm__ volatile("nop");
  }
}

static void blink_twice(void) {
  for (int i = 0; i < 2; ++i) {
    led_on();
    delay_ms(120);
    led_off();
    delay_ms(120);
  }
}

// ---------------------------------------------------------------------------
// AT command handling
// ---------------------------------------------------------------------------
static void print_config(void) {
  put("+INFO: HT5023 uart_at, ");
  put(UART_AT_PIN_TEXT);
  put(" @ ");
  put_u32(UART_AT_BAUD);
  {
    const char frame[5] = {' ', (char)('0' + UART_AT_DATA_BITS), (char)UART_AT_PARITY,
                           (char)('0' + UART_AT_STOP_BITS), '\0'};
    put(frame);
  }
  put(", sysclk ");
  put_u32(UART_AT_SYSCLK_HZ);
  put(" Hz, echo ");
  put(echo_on ? "on" : "off");
  put_line("");
}

static void handle_line(const char* line, uint32_t len) {
#if UART_AT_SHOW_RX
  put("RX[");
  put_u32(len);
  put("]: \"");
  put(line);
  put_line("\"");
#else
  (void)len;
#endif

#if UART_AT_SHOW_HEX
  put("HEX:");
  for (uint32_t i = 0u; i < len; ++i) {
    put(" ");
    put_hex8((uint8_t)line[i]);
  }
  put_line("");
#endif

  if (line[0] == '\0') return;  // bare Enter

  if (eq_ci(line, "AT")) {
    put_line("OK");
  } else if (eq_ci(line, "AT+INFO")) {
    print_config();
    put_line("OK");
  } else if (eq_ci(line, "AT+HELP")) {
    put_line("+HELP: AT, AT+INFO, AT+HELP, AT+ECHO=0|1, AT+BAUD?, AT+LED=0|1, AT+BLINK");
    put_line("OK");
  } else if (eq_ci(line, "AT+BAUD?")) {
    put("+BAUD: ");
    put_u32(UART_AT_BAUD);
    put_line("");
    put_line("OK");
  } else if (starts_ci(line, "AT+ECHO=")) {
    const char v = line[8];
    if (v == '0' || v == '1') {
      echo_on = (v == '1');
      put_line("OK");
    } else {
      put_line("ERROR");
    }
  } else if (starts_ci(line, "AT+LED=")) {
    const char v = line[7];
    if (v == '0') {
      led_off();
      put_line("OK");
    } else if (v == '1') {
      led_on();
      put_line("OK");
    } else {
      put_line("ERROR");
    }
  } else if (eq_ci(line, "AT+BLINK")) {
    blink_twice();
    put_line("OK");
  } else {
    put_line("ERROR");
  }
}

// ---------------------------------------------------------------------------
int main(void) {
  static char line[UART_AT_LINE_MAX + 1u];
  uint32_t len = 0u;
  bool overflow = false;

  led_gpio_init();
  led_off();

  const ht5023_uart_config_t cfg = {
    .sysclk_hz = UART_AT_SYSCLK_HZ,
    .baud = UART_AT_BAUD,
    .enable_tx = UART_AT_ENABLE_TX,
    .enable_rx = UART_AT_ENABLE_RX,
  };
  UART_AT_PINS_INIT();
  if (!ht5023_uart_init(UART_AT_ID, &cfg)) {
    // Divider out of range for this sysclk/baud pair: the LEDs stay on so the
    // failure is visible even with no working serial link.
    led_on();
    while (1) {
    }
  }

  // Optional frame-format overrides (see the CONFIG block).
#if (UART_AT_UARTCON_SET != 0u) || (UART_AT_UARTCON_CLR != 0u)
  {
    volatile uint16_t* uartcon = (volatile uint16_t*)(UART_AT_BASE + UARTCON_OFFSET);
    uint16_t v = *uartcon;
    v |= (uint16_t)(UART_AT_UARTCON_SET);
    v &= (uint16_t)~((uint16_t)(UART_AT_UARTCON_CLR));
    *uartcon = v;
  }
#endif

  put_line("");
  put_line("HT5023 uart_at ready -- type AT commands, AT+HELP lists them.");
  print_config();

  while (1) {
    uint8_t c;
    if (!ht5023_uart_try_read_byte(UART_AT_ID, &c)) continue;

#if UART_AT_LED_ON_RX
    led_toggle();
#endif
    if (echo_on) ht5023_uart_write_byte(UART_AT_ID, c);

    if (c == '\r' || c == '\n') {
      if (echo_on && c == '\r') put(UART_AT_EOL);
      if (overflow) {
        put_line("ERROR: line too long");
      } else if (len > 0u || UART_AT_SHOW_RX) {
        line[len] = '\0';
        handle_line(line, len);
      }
      len = 0u;
      overflow = false;
      continue;
    }

    if (c == 0x08u || c == 0x7Fu) {  // backspace / delete
      if (len > 0u) --len;
      continue;
    }

    if (len < UART_AT_LINE_MAX) {
      line[len++] = (char)c;
    } else {
      overflow = true;
    }
  }
}
