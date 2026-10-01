// examples/wisun_get/main.c — ask the Wi-SUN module "wisun get wisun" and log
// whatever it answers.
//
// The MCU sends the command out of UART1 and prints every line that comes back
// to the Flasher's debug log over semihosting, so no second serial adapter is
// needed: set the log source to Semihosting and press Start Log.
//
//   > wisun get wisun
//   [wisun] wisun.network_name = "Wi-SUN Network"
//   [wisun] wisun.network_size = small
//   ...
//
// Wiring: PC0 = TX1 -> module RX, PC1 = RX1 <- module TX, GND common, 115200 8N1.
// Remove any PC0-PC1 loopback wire first, or the reply is just our own echo
// (which this example detects and points out).

#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/uart.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_off(void);
extern void led_toggle(void);

// ===========================================================================
// CONFIG
// ===========================================================================

// The command sent to the module. Silicon Labs' Wi-SUN CLI answers
// "wisun get wisun" with every setting in the wisun group.
#ifndef WISUN_CMD
#define WISUN_CMD "wisun get wisun "
#endif

// Line ending appended to the command. Some CLIs want "\r" only.
#ifndef WISUN_EOL
#define WISUN_EOL "\r\n"
#endif

// 1 -> UART1 PC0 (TX1) / PC1 (RX1)   2 -> UART2 PC11/PC12   0 -> UART0 PC3/PC2
#ifndef WISUN_PORT
#define WISUN_PORT 1
#endif

#ifndef WISUN_BAUD
#define WISUN_BAUD 115200u
#endif

// Fsys, the clock the UART divider counts: HT5023_FSYS_HZ from
// ht5023/core/system.h, measured at 9.196 MHz (the datasheet's 9.5 MHz is
// -2.65 % off at 115200). If text is garbled on a different board, measure
// its HRC and override HT5023_FSYS_HZ.
#ifndef WISUN_SYSCLK_HZ
#define WISUN_SYSCLK_HZ HT5023_FSYS_HZ
#endif

// Delay between repeats. The command is re-sent forever so you can watch it
// live; set to 0 to ask exactly once.
#ifndef WISUN_PERIOD_MS
#define WISUN_PERIOD_MS 5000u
#endif

// Where the log goes:
//   1 -> semihosting over the J-Link (no extra wiring)   <- default
//   2 -> UART2 on PC11/PC12, for an FTDI/CP210x/CH340 USB-UART on the host
//   3 -> both
#ifndef WISUN_LOG
#define WISUN_LOG 1
#endif

#ifndef WISUN_LOG_BAUD
#define WISUN_LOG_BAUD 115200u
#endif

#if ((WISUN_LOG) & 2) && (WISUN_PORT == 2)
#error "The log and the module cannot share UART2 -- move one of them."
#endif

// Prefix on every line that came from the module.
#ifndef WISUN_TAG
#define WISUN_TAG "[wisun] "
#endif

#ifndef WISUN_LINE_MAX
#define WISUN_LINE_MAX 160u
#endif

// Whole-reply capture buffer. The reply is stored here first and printed after,
// because printing mid-reception loses bytes (see WISUN_IDLE_SPINS below).
#ifndef WISUN_BUF_MAX
#define WISUN_BUF_MAX 2048u
#endif

// Receiving is a tight poll with no delays and no logging: the UART keeps only
// one byte, so anything slower drops most of a 115200 stream. These budgets are
// in poll iterations rather than milliseconds for that reason -- roughly
// 700 spins/ms on the default clock.
#ifndef WISUN_IDLE_SPINS
#define WISUN_IDLE_SPINS 250000u   // ~350 ms of silence ends the reply
#endif

#ifndef WISUN_MAX_SPINS
#define WISUN_MAX_SPINS 4000000u   // ~5 s overall ceiling
#endif

#ifndef WISUN_CYCLES_PER_MS
#define WISUN_CYCLES_PER_MS 450u
#endif

// ===========================================================================

#if WISUN_PORT == 1
#define UART_ID HT5023_UART_ID_1
#define UART_PINS_INIT() ht5023_uart1_pins_init_pc0_pc1()
#define UART_PIN_TEXT "UART1 PC0=TX1 PC1=RX1"
#elif WISUN_PORT == 2
#define UART_ID HT5023_UART_ID_2
#define UART_PINS_INIT() ht5023_uart2_pins_init_pc11_pc12()
#define UART_PIN_TEXT "UART2 PC11=TX2 PC12=RX2"
#elif WISUN_PORT == 0
#define UART_ID HT5023_UART_ID_0
#define UART_PINS_INIT() ht5023_uart0_pins_init_pc2_pc3()
#define UART_PIN_TEXT "UART0 PC3=TX0 PC2=RX0"
#else
#error "WISUN_PORT must be 0, 1 or 2."
#endif

// --- semihosting output (the logger) ---------------------------------------
#define SYS_WRITE0 0x04u

// Armed by the Flasher's monitor once execution reaches main(); a plain
// flash-and-run leaves it 0 so an unserviced `bkpt 0xAB` can never stall the
// core. Do not gate on DHCSR: that register is debugger-only and reads back
// unreliably from software on this part.
volatile int g_semihost_enable = 0;

#if (WISUN_LOG) & 1
static int semihost(int op, void* arg) {
  register int r0 __asm__("r0") = op;
  register void* r1 __asm__("r1") = arg;
  // r1 is in-out: the debugger may clobber it, and if the compiler assumes it
  // survives it will reuse it as a constant afterwards.
  __asm__ volatile("bkpt 0xAB" : "+r"(r0), "+r"(r1) : : "memory", "cc");
  return r0;
}
#endif

#define LOG_UART_ID HT5023_UART_ID_2

static void say(const char* s) {
  if (!s) return;
#if (WISUN_LOG) & 1
  if (g_semihost_enable != 0) (void)semihost(SYS_WRITE0, (void*)s);
#endif
#if (WISUN_LOG) & 2
  ht5023_uart_write_str(LOG_UART_ID, s);
#endif
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
  for (volatile uint32_t i = 0; i < ms * WISUN_CYCLES_PER_MS; ++i) {
    __asm__ volatile("nop");
  }
}

// --- reply handling --------------------------------------------------------
static uint8_t buf[WISUN_BUF_MAX];
static uint32_t buf_n;
static bool buf_full;

static void rx_flush(void) {
  uint8_t junk;
  while (ht5023_uart_try_read_byte(UART_ID, &junk)) {
  }
  buf_n = 0u;
  buf_full = false;
}

// Tight-polls the UART into `buf`. No delays, no printing: both would drop
// bytes. Returns when the line has been quiet for WISUN_IDLE_SPINS.
static uint32_t capture_reply(void) {
  uint32_t idle = 0u;

  for (uint32_t spins = 0u; spins < WISUN_MAX_SPINS; ++spins) {
    uint8_t c;
    if (ht5023_uart_try_read_byte(UART_ID, &c)) {
      if (buf_n < WISUN_BUF_MAX) {
        buf[buf_n++] = c;
      } else {
        buf_full = true;
      }
      idle = 0u;
      continue;
    }
    if (++idle > WISUN_IDLE_SPINS && buf_n > 0u) break;
  }
  return buf_n;
}

// Prints the captured reply as tagged lines, escaping unprintable bytes.
static void print_capture(void) {
  static const char hex[] = "0123456789ABCDEF";
  char line[WISUN_LINE_MAX + 1u];
  uint32_t n = 0u;

  for (uint32_t i = 0u; i < buf_n; ++i) {
    const uint8_t c = buf[i];

    if (c == '\n' || c == '\r') {
      if (n > 0u) {
        line[n] = '\0';
        say(WISUN_TAG);
        say_line(line);
        n = 0u;
      }
      continue;
    }

    if (c >= 32u && c < 127u) {
      line[n++] = (char)c;
    } else if (n + 4u < WISUN_LINE_MAX) {
      line[n++] = '<';
      line[n++] = hex[(c >> 4) & 0x0Fu];
      line[n++] = hex[c & 0x0Fu];
      line[n++] = '>';
    }

    if (n >= WISUN_LINE_MAX) {  // very long line: flush what we have
      line[n] = '\0';
      say(WISUN_TAG);
      say_line(line);
      n = 0u;
    }
  }

  if (n > 0u) {  // trailing text with no line ending
    line[n] = '\0';
    say(WISUN_TAG);
    say_line(line);
  }
}

int main(void) {
  led_gpio_init();
  led_off();

  const ht5023_uart_config_t cfg = {
    .sysclk_hz = WISUN_SYSCLK_HZ,
    .baud = WISUN_BAUD,
    .enable_tx = true,
    .enable_rx = true,
  };
  UART_PINS_INIT();
  const bool uart_ok = ht5023_uart_init(UART_ID, &cfg);

#if (WISUN_LOG) & 2
  {  // log channel: UART2 out to a USB-UART adapter
    const ht5023_uart_config_t log_cfg = {
      .sysclk_hz = WISUN_SYSCLK_HZ,
      .baud = WISUN_LOG_BAUD,
      .enable_tx = true,
      .enable_rx = false,
    };
    ht5023_uart2_pins_init_pc11_pc12();
    (void)ht5023_uart_init(LOG_UART_ID, &log_cfg);
  }
#endif

  delay_ms(200u);  // let the debugger arm semihosting before the first line
  say_line("");
  say("HT5023 -> Wi-SUN on ");
  say(UART_PIN_TEXT);
  say(" @ ");
  say_u32(WISUN_BAUD);
  say_line(" 8N1");
  if (!uart_ok) {
    say_line("ERROR: ht5023_uart_init() rejected this baud/sysclk pair -- nothing "
             "can be sent.");
    while (1) {
      led_toggle();
      delay_ms(120u);
    }
  }

  uint32_t round = 0u;
  while (1) {
    ++round;
    rx_flush();

    say("> ");
    say_line(WISUN_CMD);
    ht5023_uart_write_str(UART_ID, WISUN_CMD);
    ht5023_uart_write_str(UART_ID, WISUN_EOL);

    const uint32_t got = capture_reply();
    print_capture();
    if (buf_full) {
      say_line("(capture buffer full -- raise WISUN_BUF_MAX to see the rest)");
    }
    if (got == 0u) {
      say_line("(no reply -- check TX/RX orientation, common ground, that the "
               "module is powered, and its baud rate)");
    } else {
      say("(");
      say_u32(got);
      say_line(" bytes received)");
    }

    if (WISUN_PERIOD_MS == 0u) {
      say_line("Done (WISUN_PERIOD_MS is 0, asking once).");
      break;
    }
    delay_ms(WISUN_PERIOD_MS);
  }

  while (1) {
    led_toggle();
    delay_ms(500u);
  }
}
