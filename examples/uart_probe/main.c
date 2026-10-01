// examples/uart_probe/main.c — find out what is attached to the UART
//
// Sweeps common baud rates, listens for a boot banner, then pokes the module
// with the command sets Wi-SUN and AT modules usually answer to, printing
// anything that comes back. Results go to the Flasher's debug log over
// semihosting (source = Semihosting), so no second serial adapter is needed.
//
// Wiring: PC0 = TX1 -> module RX, PC1 = RX1 <- module TX, GND common.
//
// IMPORTANT: remove any loopback wire between PC0 and PC1 first. With it in
// place the target hears its own transmission; the probe detects that case and
// says so rather than reporting a phantom module.

#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/uart.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_off(void);
extern void led_toggle(void);

// 1 -> UART1 PC0/PC1 (default)   2 -> UART2 PC11/PC12   0 -> UART0 PC3/PC2
#ifndef PROBE_PORT
#define PROBE_PORT 1
#endif

#ifndef PROBE_SYSCLK_HZ
#define PROBE_SYSCLK_HZ HT5023_FSYS_HZ
#endif

// Milliseconds to wait for a reply after each poke.
#ifndef PROBE_REPLY_MS
#define PROBE_REPLY_MS 400u
#endif

// Milliseconds to listen for an unprompted boot banner at each baud.
#ifndef PROBE_LISTEN_MS
#define PROBE_LISTEN_MS 600u
#endif

#ifndef PROBE_CYCLES_PER_MS
#define PROBE_CYCLES_PER_MS 450u
#endif

#define CAP 192u

#if PROBE_PORT == 1
#define UART_ID HT5023_UART_ID_1
#define UART_PINS_INIT() ht5023_uart1_pins_init_pc0_pc1()
#define UART_PIN_TEXT "UART1 PC0=TX1 PC1=RX1"
#elif PROBE_PORT == 2
#define UART_ID HT5023_UART_ID_2
#define UART_PINS_INIT() ht5023_uart2_pins_init_pc11_pc12()
#define UART_PIN_TEXT "UART2 PC11=TX2 PC12=RX2"
#else
#define UART_ID HT5023_UART_ID_0
#define UART_PINS_INIT() ht5023_uart0_pins_init_pc2_pc3()
#define UART_PIN_TEXT "UART0 PC3=TX0 PC2=RX0"
#endif

// --- semihosting output ----------------------------------------------------
#define SYS_WRITE0 0x04u

volatile int g_semihost_enable = 0;

static int semihost(int op, void* arg) {
  register int r0 __asm__("r0") = op;
  register void* r1 __asm__("r1") = arg;
  __asm__ volatile("bkpt 0xAB" : "+r"(r0), "+r"(r1) : : "memory", "cc");
  return r0;
}

static void say(const char* s) {
  if (g_semihost_enable != 0) (void)semihost(SYS_WRITE0, (void*)s);
}

static void say_line(const char* s) {
  say(s);
  say("\r\n");
}

static void say_u32(uint32_t v) {
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
  say(buf);
}

static void delay_ms(uint32_t ms) {
  for (volatile uint32_t i = 0; i < ms * PROBE_CYCLES_PER_MS; ++i) {
    __asm__ volatile("nop");
  }
}

// --- capture ---------------------------------------------------------------
static uint8_t rx[CAP];
static uint32_t rx_n;

static void rx_reset(void) {
  uint8_t junk;
  while (ht5023_uart_try_read_byte(UART_ID, &junk)) {
  }
  rx_n = 0u;
}

// Collects for `ms`, stopping early once nothing new has arrived for a while.
static void collect(uint32_t ms) {
  uint32_t quiet = 0u;
  for (uint32_t t = 0u; t < ms; ++t) {
    bool got = false;
    for (uint32_t spin = 0u; spin < 60u; ++spin) {
      uint8_t c;
      if (ht5023_uart_try_read_byte(UART_ID, &c)) {
        got = true;
        led_toggle();
        if (rx_n < CAP) rx[rx_n++] = c;
      }
    }
    quiet = got ? 0u : (quiet + 1u);
    if (rx_n > 0u && quiet > 60u) break;  // reply finished
    delay_ms(1u);
  }
}

// Prints the capture as text, escaping anything unprintable.
static void show_capture(void) {
  char line[CAP * 4u + 1u];
  uint32_t n = 0u;
  static const char hex[] = "0123456789ABCDEF";
  for (uint32_t i = 0u; i < rx_n && n + 6u < sizeof(line); ++i) {
    const uint8_t c = rx[i];
    if (c == '\r') {
      line[n++] = '<';
      line[n++] = 'C';
      line[n++] = 'R';
      line[n++] = '>';
    } else if (c == '\n') {
      line[n++] = '<';
      line[n++] = 'L';
      line[n++] = 'F';
      line[n++] = '>';
    } else if (c >= 32u && c < 127u) {
      line[n++] = (char)c;
    } else {
      line[n++] = '<';
      line[n++] = hex[(c >> 4) & 0xFu];
      line[n++] = hex[c & 0xFu];
      line[n++] = '>';
    }
  }
  line[n] = '\0';
  say("      ");
  say_u32(rx_n);
  say(" bytes: ");
  say_line(line);
}

// True when the capture is exactly the bytes we just sent (loopback wire on).
static bool is_echo_of(const char* sent) {
  uint32_t i = 0u;
  for (const char* p = sent; *p; ++p, ++i) {
    if (i >= rx_n || rx[i] != (uint8_t)*p) return false;
  }
  return i == rx_n && i > 0u;
}

static const uint32_t BAUDS[] = {115200u, 9600u, 57600u, 38400u, 19200u, 4800u};

static const char* const POKES[] = {
  "\r\n",             // bare newline: many CLIs answer with a prompt
  "AT\r\n",           // generic AT
  "SKVER\r\n",        // ROHM BP35xx Wi-SUN
  "SKINFO\r\n",       // ROHM BP35xx Wi-SUN
  "wisun help\r\n",   // Silicon Labs Wi-SUN CLI
  "?\r\n",            // assorted CLIs
};

#define COUNT(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

int main(void) {
  led_gpio_init();
  led_off();
  UART_PINS_INIT();

  delay_ms(200u);
  say_line("");
  say("UART probe on ");
  say(UART_PIN_TEXT);
  say_line(" -- looking for whatever is attached.");
  say_line("(remove any PC0-PC1 loopback wire first, or every reply is our own echo)");

  uint32_t answers = 0u;
  uint32_t echoes = 0u;

  for (uint32_t b = 0u; b < COUNT(BAUDS); ++b) {
    const ht5023_uart_config_t cfg = {
      .sysclk_hz = PROBE_SYSCLK_HZ,
      .baud = BAUDS[b],
      .enable_tx = true,
      .enable_rx = true,
    };
    say("");
    say("--- ");
    say_u32(BAUDS[b]);
    say_line(" baud 8N1 ---");
    if (!ht5023_uart_init(UART_ID, &cfg)) {
      say_line("   (divider out of range for this clock, skipped)");
      continue;
    }

    rx_reset();
    collect(PROBE_LISTEN_MS);
    if (rx_n > 0u) {
      say_line("   unprompted output:");
      show_capture();
      ++answers;
    }

    for (uint32_t k = 0u; k < COUNT(POKES); ++k) {
      rx_reset();
      ht5023_uart_write_str(UART_ID, POKES[k]);
      collect(PROBE_REPLY_MS);
      if (rx_n == 0u) continue;

      say("   sent ");
      say(POKES[k][0] == '\r' ? "<CR><LF>" : POKES[k]);
      if (is_echo_of(POKES[k])) {
        say_line(" -> exactly what we sent (loopback wire still fitted?)");
        ++echoes;
      } else {
        say_line(" -> reply:");
        ++answers;
      }
      show_capture();
    }
  }

  say_line("");
  if (answers > 0u) {
    say("RESULT: something answered on ");
    say(UART_PIN_TEXT);
    say_line(". Use the baud that produced readable text.");
  } else if (echoes > 0u) {
    say_line("RESULT: only our own transmission came back -- that is the loopback "
             "wire, not a module. Remove it and rerun.");
  } else {
    say_line("RESULT: nothing replied at any baud. Check TX/RX orientation "
             "(PC0 -> module RX, PC1 <- module TX), a common ground, that the "
             "module is powered and out of reset, and its documented baud.");
  }

  while (1) {
    led_toggle();
    delay_ms(400u);
  }
}
