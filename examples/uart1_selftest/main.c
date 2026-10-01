// examples/uart1_selftest/main.c — UART1 loopback self-test (PC0 shorted to PC1)
//
// Short PC0 (TX1) to PC1 (RX1) with a wire, flash this, and open the Flasher's
// debug log with source = Semihosting. The target transmits on its own, reads
// back what returns, and reports the result — no typing needed:
//
//   UART1 loopback self-test @ 115200 8N1
//   [1] sent "AT"                    -> got "AT"                    PASS
//   [2] sent "HT5023 loopback 12345" -> got "HT5023 loopback 12345" PASS
//   byte sweep 0x00..0xFF: 256/256 returned correctly
//   RESULT: UART1 PASS -- TX1 and RX1 both work
//
// A FAIL with nothing received means the wire, the pin mux or the baud is wrong.
// Garbled bytes usually mean the clock assumption is off: measure this board's
// HRC and override HT5023_FSYS_HZ (see ht5023/core/system.h).

#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/uart.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_on(void);
extern void led_off(void);
extern void led_toggle(void);

#ifndef SELFTEST_BAUD
#define SELFTEST_BAUD 115200u
#endif

#ifndef SELFTEST_SYSCLK_HZ
#define SELFTEST_SYSCLK_HZ HT5023_FSYS_HZ
#endif

// 1 -> UART1 PC0/PC1 (default)   2 -> UART2 PC11/PC12   0 -> UART0 PC3/PC2
#ifndef SELFTEST_PORT
#define SELFTEST_PORT 1
#endif

// How long to wait for each echo (ms).
#ifndef SELFTEST_TIMEOUT_MS
#define SELFTEST_TIMEOUT_MS 200u
#endif

// Pin-pair short scanner. Off by default: floating inputs make it report
// false positives, so treat any result as a hint, not proof.
#ifndef SELFTEST_SCAN
#define SELFTEST_SCAN 0
#endif

#ifndef SELFTEST_CYCLES_PER_MS
#define SELFTEST_CYCLES_PER_MS 450u
#endif

#if SELFTEST_PORT == 1
#define UART_ID HT5023_UART_ID_1
#define UART_PINS_INIT() ht5023_uart1_pins_init_pc0_pc1()
#define UART_PIN_TEXT "UART1 PC0=TX1 PC1=RX1"
#elif SELFTEST_PORT == 2
#define UART_ID HT5023_UART_ID_2
#define UART_PINS_INIT() ht5023_uart2_pins_init_pc11_pc12()
#define UART_PIN_TEXT "UART2 PC11=TX2 PC12=RX2"
#else
#define UART_ID HT5023_UART_ID_0
#define UART_PINS_INIT() ht5023_uart0_pins_init_pc2_pc3()
#define UART_PIN_TEXT "UART0 PC3=TX0 PC2=RX0"
#endif

// --- semihosting output (results go to the debug log) ----------------------
#define SYS_WRITE0 0x04u

volatile int g_semihost_enable = 0;

static inline __attribute__((always_inline)) int semihost(int op, void* arg) {
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

static void say_port(uint32_t port) {
  static const char* const names[5] = {"A", "B", "C", "D", "E"};
  say(port < 5u ? names[port] : "?");
}

static void delay_ms(uint32_t ms) {
  for (volatile uint32_t i = 0; i < ms * SELFTEST_CYCLES_PER_MS; ++i) {
    __asm__ volatile("nop");
  }
}

// --- continuity check ------------------------------------------------------
// Before trusting the UART, prove the wire itself: drive PC0 as a plain GPIO
// output and read PC1 as an input. If the short is real, PC1 follows PC0.
#define GPIOC_BASE 0x40011200u
#define R_IOCFG (*(volatile uint16_t*)(GPIOC_BASE + 0x00u))
#define R_AFCFG (*(volatile uint16_t*)(GPIOC_BASE + 0x04u))
#define R_PTDIR (*(volatile uint16_t*)(GPIOC_BASE + 0x08u))
#define R_PTUP (*(volatile uint16_t*)(GPIOC_BASE + 0x0Cu))
#define R_PTDAT (*(volatile uint16_t*)(GPIOC_BASE + 0x10u))
#define R_PTSET (*(volatile uint16_t*)(GPIOC_BASE + 0x14u))
#define R_PTCLR (*(volatile uint16_t*)(GPIOC_BASE + 0x18u))
#define R_PTOD (*(volatile uint16_t*)(GPIOC_BASE + 0x20u))

#define TXPIN 0u
#define RXPIN 1u

static void say_hex16(uint16_t v) {
  static const char hex[] = "0123456789ABCDEF";
  char out[7] = {'0', 'x', hex[(v >> 12) & 0xF], hex[(v >> 8) & 0xF],
                 hex[(v >> 4) & 0xF], hex[v & 0xF], '\0'};
  say(out);
}

static bool pin_level(uint32_t pin) {
  return (R_PTDAT & (uint16_t)(1u << pin)) != 0u;
}

// Drives `drv` and samples `sense`. Reports both pads at both levels.
static bool direction_check(uint32_t drv, uint32_t sense, const char* label,
                            bool sense_pullup) {
  const uint16_t dm = (uint16_t)(1u << drv), sm = (uint16_t)(1u << sense);

  R_IOCFG &= (uint16_t)~(dm | sm);   // plain GPIO on both
  R_AFCFG &= (uint16_t)~(dm | sm);
  R_PTOD |= dm;                      // push-pull driver
  R_PTDIR |= dm;                     // driver = output
  R_PTDIR &= (uint16_t)~sm;          // sense = input
  if (sense_pullup) {
    R_PTUP &= (uint16_t)~sm;         // bit=0 enables the pull-up
  } else {
    R_PTUP |= sm;                    // no pull-up: a floating pin is undefined
  }

  R_PTCLR = dm;
  delay_ms(5u);
  const bool d_lo = pin_level(drv), s_lo = pin_level(sense);
  R_PTSET = dm;
  delay_ms(5u);
  const bool d_hi = pin_level(drv), s_hi = pin_level(sense);

  say(label);
  say(sense_pullup ? " (pull-up on) " : " (pull-up off)");
  say("  low: drv=");
  say(d_lo ? "1" : "0");
  say(" sense=");
  say(s_lo ? "1" : "0");
  say("   high: drv=");
  say(d_hi ? "1" : "0");
  say(" sense=");
  say(s_hi ? "1" : "0");
  const bool linked = (!d_lo) && d_hi && (!s_lo) && s_hi;
  say_line(linked ? "   -> LINKED" : "   -> not linked");

  R_PTDIR &= (uint16_t)~dm;          // release
  return linked;
}

// Proves PTDAT reports the physical pad and not the output latch: park the
// latch at 0, switch the pin to an input with its pull-up on, and read. A pad
// read follows the pull-up to 1; a latch read stays stuck at the stale 0.
static bool ptdat_reads_pad(uint32_t pin) {
  const uint16_t m = (uint16_t)(1u << pin);
  R_IOCFG &= (uint16_t)~m;
  R_AFCFG &= (uint16_t)~m;
  R_PTOD |= m;
  R_PTDIR |= m;        // output
  R_PTCLR = m;         // latch := 0
  delay_ms(2u);
  R_PTDIR &= (uint16_t)~m;  // input
  R_PTUP &= (uint16_t)~m;   // pull-up on (bit=0)
  delay_ms(5u);
  const bool reads_high = pin_level(pin);
  say("PTDAT sanity on PC");
  say_u32(pin);
  say(": latch parked at 0, input+pull-up reads ");
  say(reads_high ? "1" : "0");
  say_line(reads_high ? "  -> PTDAT reflects the pad (test is valid)"
                      : "  -> PTDAT returned the stale latch (test INVALID)");
  return reads_high;
}

static bool continuity_check(void) {
  const bool valid0 = ptdat_reads_pad(TXPIN);
  const bool valid1 = ptdat_reads_pad(RXPIN);
  if (!valid0 || !valid1) {
    say_line("Cannot trust the continuity result on this pin -- PTDAT is not "
             "reporting the pad level.");
  }

  say("GPIOC before: IOCFG=");
  say_hex16(R_IOCFG);
  say(" AFCFG=");
  say_hex16(R_AFCFG);
  say(" PTDIR=");
  say_hex16(R_PTDIR);
  say(" PTUP=");
  say_hex16(R_PTUP);
  say(" PTOD=");
  say_hex16(R_PTOD);
  say(" PTDAT=");
  say_hex16(R_PTDAT);
  say_line("");

  bool linked = false;
  linked |= direction_check(TXPIN, RXPIN, "PC0 drives -> PC1 senses", true);
  linked |= direction_check(RXPIN, TXPIN, "PC1 drives -> PC0 senses", true);
  linked |= direction_check(TXPIN, RXPIN, "PC0 drives -> PC1 senses", false);
  linked |= direction_check(RXPIN, TXPIN, "PC1 drives -> PC0 senses", false);

  say("GPIOC after:  IOCFG=");
  say_hex16(R_IOCFG);
  say(" AFCFG=");
  say_hex16(R_AFCFG);
  say(" PTDIR=");
  say_hex16(R_PTDIR);
  say_line("");
  say_line(linked ? "continuity: PASS (the pins are connected)"
                  : "continuity: FAIL (no direction showed a connection)");
  return linked;
}

// --- find which pins are actually shorted -----------------------------------
// Drives each candidate pin low OPEN-DRAIN (sink only, so it cannot fight a
// device driving high) and watches every other pin. Any pin that follows is
// wired to it. SWD (PB13/PB15) and the LEDs (PE3/PE4) are left alone.
#define PORTS 5u

static volatile uint16_t* preg(uint32_t port, uint32_t off) {
  return (volatile uint16_t*)(0x40011000u + port * 0x100u + off);
}

static uint16_t skip_mask(uint32_t port) {
  if (port == 1u) return (uint16_t)((1u << 13) | (1u << 15));  // SWDIO / SWCLK
  if (port == 4u) return (uint16_t)LED_PINS_MASK;              // LEDs
  return 0u;
}

static void scan_shorts(void) {
  uint16_t base[PORTS];

  say_line("");
  say_line("Scanning every pin pair to find the short (open-drain probe)...");

  for (uint32_t p = 0u; p < PORTS; ++p) {
    const uint16_t keep = skip_mask(p);
    *preg(p, 0x00u) &= (uint16_t)~(uint16_t)~keep;   // IOCFG: GPIO except skipped
    *preg(p, 0x00u) &= (uint16_t)keep ? *preg(p, 0x00u) : 0u;
    *preg(p, 0x04u) &= (uint16_t)keep;               // AFCFG
    *preg(p, 0x08u) &= (uint16_t)keep;               // PTDIR: inputs
    *preg(p, 0x0Cu) &= (uint16_t)keep;               // PTUP: pull-ups on (bit=0)
  }
  delay_ms(5u);
  for (uint32_t p = 0u; p < PORTS; ++p) base[p] = *preg(p, 0x10u);

  say("baseline levels: ");
  for (uint32_t p = 0u; p < PORTS; ++p) {
    say("P");
    say_u32(p);
    say("=");
    say_hex16(base[p]);
    say(" ");
  }
  say_line("");

  uint32_t found = 0u;
  for (uint32_t dp = 0u; dp < PORTS; ++dp) {
    for (uint32_t db = 0u; db < 16u; ++db) {
      const uint16_t dm = (uint16_t)(1u << db);
      if (skip_mask(dp) & dm) continue;
      if ((base[dp] & dm) == 0u) continue;  // already low: cannot tell

      *preg(dp, 0x20u) &= (uint16_t)~dm;    // PTOD=0 -> open drain
      *preg(dp, 0x08u) |= dm;               // output
      *preg(dp, 0x18u) = dm;                // drive low
      delay_ms(2u);

      for (uint32_t sp = 0u; sp < PORTS; ++sp) {
        const uint16_t now = *preg(sp, 0x10u);
        uint16_t fell = (uint16_t)(base[sp] & (uint16_t)~now);
        if (sp == dp) fell = (uint16_t)(fell & (uint16_t)~dm);
        fell = (uint16_t)(fell & (uint16_t)~skip_mask(sp));
        for (uint32_t sb = 0u; sb < 16u; ++sb) {
          if (fell & (uint16_t)(1u << sb)) {
            say("  SHORT: P");
            say_port(dp);
            say_u32(db);
            say(" <-> P");
            say_port(sp);
            say_u32(sb);
            say_line("");
            ++found;
          }
        }
      }

      *preg(dp, 0x08u) &= (uint16_t)~dm;    // release
      *preg(dp, 0x20u) |= dm;
      delay_ms(1u);
    }
  }

  if (found == 0u) {
    say_line("  no connected pin pair found anywhere on ports A-E.");
  }
}

// --- loopback plumbing -----------------------------------------------------
static void rx_flush(void) {
  uint8_t junk;
  while (ht5023_uart_try_read_byte(UART_ID, &junk)) {
  }
}

// Waits up to SELFTEST_TIMEOUT_MS for one byte. Returns false on timeout.
static bool rx_byte(uint8_t* out) {
  for (uint32_t ms = 0u; ms < SELFTEST_TIMEOUT_MS; ++ms) {
    for (uint32_t spin = 0u; spin < 60u; ++spin) {
      if (ht5023_uart_try_read_byte(UART_ID, out)) return true;
    }
    delay_ms(1u);
  }
  return false;
}

// Echoes `text` one byte at a time. The receiver holds a single byte, so a
// loopback has to read each character back before sending the next one --
// blasting the whole string first would leave only the last byte in the buffer.
static bool check_string(uint32_t index, const char* text) {
  char got[64];
  uint32_t n = 0u;
  bool same = true;

  rx_flush();
  for (const char* p = text; *p; ++p) {
    uint8_t c;
    ht5023_uart_write_byte(UART_ID, (uint8_t)*p);
    if (!rx_byte(&c)) {
      same = false;
      break;
    }
    if (n < sizeof(got) - 1u) got[n++] = (char)c;
    if ((char)c != *p) same = false;
  }
  got[n] = '\0';

  say("[");
  say_u32(index);
  say("] sent \"");
  say(text);
  say("\" -> got \"");
  say(got);
  say(same ? "\"  PASS" : "\"  FAIL");
  say_line("");
  return same;
}

// Sends every byte value and counts how many return unchanged.
static uint32_t check_byte_sweep(void) {
  uint32_t good = 0u;
  rx_flush();
  for (uint32_t v = 0u; v < 256u; ++v) {
    uint8_t back;
    ht5023_uart_write_byte(UART_ID, (uint8_t)v);
    if (rx_byte(&back) && back == (uint8_t)v) ++good;
  }
  return good;
}

int main(void) {
  led_gpio_init();
  led_off();

  const ht5023_uart_config_t cfg = {
    .sysclk_hz = SELFTEST_SYSCLK_HZ,
    .baud = SELFTEST_BAUD,
    .enable_tx = true,
    .enable_rx = true,
  };
  // Give the debugger a moment to arm semihosting before the first report.
  delay_ms(200u);

  say_line("");
  say_line("--- wire check before the UART test ---");
  const bool wired = continuity_check();

#if SELFTEST_SCAN
  if (!wired) scan_shorts();
#else
  (void)scan_shorts;
#endif

  UART_PINS_INIT();
  const bool init_ok = ht5023_uart_init(UART_ID, &cfg);

  say("UART1 loopback self-test: ");
  say(UART_PIN_TEXT);
  say(" @ ");
  say_u32(SELFTEST_BAUD);
  say_line(" 8N1");
  if (!init_ok) {
    say_line("RESULT: FAIL -- ht5023_uart_init() rejected this baud/sysclk pair.");
    while (1) {
      led_toggle();
      delay_ms(100u);
    }
  }

  uint32_t passed = 0u, total = 0u;
  ++total;
  if (check_string(total, "AT")) ++passed;
  ++total;
  if (check_string(total, "HT5023 loopback 12345")) ++passed;
  ++total;
  if (check_string(total, "The quick brown fox 0123456789")) ++passed;

  const uint32_t sweep = check_byte_sweep();
  say("byte sweep 0x00..0xFF: ");
  say_u32(sweep);
  say_line("/256 returned correctly");

  const bool all_ok = (passed == total) && (sweep == 256u);
  if (!all_ok && !wired) {
    say_line("NOTE: the continuity check failed too, so PC0 and PC1 are not "
             "actually connected -- fix the short before suspecting the UART.");
  }
  say("RESULT: ");
  if (all_ok) {
    say_line("UART1 PASS -- TX1 and RX1 both work");
  } else {
    say("UART1 FAIL -- ");
    say_u32(passed);
    say("/");
    say_u32(total);
    say_line(" strings echoed. Nothing back at all means the wire, pin mux or "
             "baud is wrong; garbled bytes usually mean the sysclk assumption "
             "is off (measure the HRC and set HT5023_FSYS_HZ).");
  }

  // Steady LEDs on pass, blinking on failure.
  while (1) {
    if (all_ok) {
      led_on();
      delay_ms(500u);
    } else {
      led_toggle();
      delay_ms(120u);
    }
  }
}
