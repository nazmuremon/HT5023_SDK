// examples/random_sentence/main.c — prints a new random sentence every second
//
// Builds sentences by picking words out of small tables with an xorshift32 PRNG
// (seeded from the free-running SysTick counter), so the debug log fills with
// readable, always-different lines. The LEDs toggle once per sentence.
//
// Output goes to BOTH debug channels, so the Flasher's real-time debug log shows
// it whichever source is selected:
//
//   1. UART2  — PC11 = TX2, PC12 = RX2, 9600 8N1 (USB-TTL adapter -> "Serial")
//   2. ARM semihosting over the J-Link SWD cable (no wiring -> "Semihosting"),
//      active only while the Flasher's monitor is attached (see below).

#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/debug.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_toggle(void);

#ifndef SENTENCE_EXAMPLE_SYSCLK_HZ
#define SENTENCE_EXAMPLE_SYSCLK_HZ HT5023_FSYS_HZ
#endif

#ifndef SENTENCE_EXAMPLE_BAUD
#define SENTENCE_EXAMPLE_BAUD 9600u
#endif

// Milliseconds between sentences.
#ifndef SENTENCE_PERIOD_MS
#define SENTENCE_PERIOD_MS 1000u
#endif

// Busy-loop iterations per millisecond (matches blinky: 45000 ~= 100 ms on the
// default clock).
#ifndef SENTENCE_CYCLES_PER_MS
#define SENTENCE_CYCLES_PER_MS 450u
#endif

#define COUNT(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

// ---------------------------------------------------------------------------
// Debug output: UART2 always, semihosting only when the monitor is attached
// ---------------------------------------------------------------------------
#define SYS_WRITE0 0x04u  // ARM semihosting: r1 -> NUL-terminated string

// A `bkpt 0xAB` with no debugger servicing it halts (or faults) the core, so
// semihosting stays OFF unless a debugger explicitly turns it on. The Flasher's
// monitor sets this to 1 after reaching main(); a plain flash-and-run leaves it
// at 0 and the example simply logs over UART2 only.
//
// Do NOT gate on DHCSR.C_DEBUGEN (0xE000EDF0) as a "debugger attached" test:
// the ARMv6-M Debug Control Block is debugger-only, and on this part the core
// reads it back as 0 while the debugger sees 0x00030003 -- so that check is
// always false and semihosting never fires.
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

static bool semihost_ready(void) {
  return g_semihost_enable != 0;
}

static void dbg_puts(const char* s) {
  if (!s) return;
  ht5023_debug_uart2_write_str(s);
  if (semihost_ready()) {
    (void)semihost(SYS_WRITE0, (void*)s);
  }
}

// ---------------------------------------------------------------------------
// SysTick (seed source only) + xorshift32 PRNG
// ---------------------------------------------------------------------------
#define SYST_CSR (*(volatile uint32_t*)0xE000E010u)
#define SYST_RVR (*(volatile uint32_t*)0xE000E014u)
#define SYST_CVR (*(volatile uint32_t*)0xE000E018u)

static uint32_t rng_state = 0x12345678u;

static uint32_t rng_next(void) {
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_state = x;
  return x;
}

// Uniform enough for picking a word out of a short table.
static uint32_t rng_below(uint32_t limit) {
  return (limit == 0u) ? 0u : (rng_next() % limit);
}

// ---------------------------------------------------------------------------
// Word tables
// ---------------------------------------------------------------------------
static const char* const ADJECTIVES[] = {
  "sleepy", "stubborn", "tiny", "restless", "polite", "overclocked",
  "curious", "unflappable", "caffeinated", "quiet", "rusty", "eager",
};

static const char* const NOUNS[] = {
  "watchdog", "flash controller", "ring buffer", "SPI bus", "interrupt",
  "boot loader", "UART driver", "timer", "GPIO pin", "stack pointer",
  "oscillator", "linker script",
};

static const char* const VERBS[] = {
  "nudges", "argues with", "reboots", "measures", "ignores", "salutes",
  "debounces", "counts", "serenades", "polls", "outsmarts", "tickles",
};

static const char* const ADVERBS[] = {
  "at 9600 baud", "before the coffee runs out", "without dropping a byte",
  "in half a clock cycle", "on the rising edge", "for no clear reason",
  "twice, just to be sure", "while the LEDs blink",
};

static const char* const MOODS[] = {
  "Meanwhile", "Somehow", "As expected", "Against all odds", "Every reset",
  "In the datasheet",
};

// ---------------------------------------------------------------------------
// Sentence assembly (no printf: keeps the image tiny)
// ---------------------------------------------------------------------------
static char* append(char* p, const char* s) {
  while (*s) *p++ = *s++;
  return p;
}

static char* append_u32(char* p, uint32_t v) {
  char tmp[10];
  int n = 0;
  if (v == 0u) {
    *p++ = '0';
    return p;
  }
  while (v) {
    tmp[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  while (n) *p++ = tmp[--n];
  return p;
}

#define PICK(table) ((table)[rng_below(COUNT(table))])

// Fills `out` with one sentence. Longest template stays well under 128 chars.
static void make_sentence(char* out) {
  char* p = out;

  switch (rng_below(4u)) {
    case 0:
      p = append(p, "the ");
      p = append(p, PICK(ADJECTIVES));
      p = append(p, " ");
      p = append(p, PICK(NOUNS));
      p = append(p, " ");
      p = append(p, PICK(VERBS));
      p = append(p, " the ");
      p = append(p, PICK(ADJECTIVES));
      p = append(p, " ");
      p = append(p, PICK(NOUNS));
      p = append(p, ".");
      break;
    case 1:
      p = append(p, PICK(MOODS));
      p = append(p, ", the ");
      p = append(p, PICK(NOUNS));
      p = append(p, " ");
      p = append(p, PICK(VERBS));
      p = append(p, " the ");
      p = append(p, PICK(NOUNS));
      p = append(p, " ");
      p = append(p, PICK(ADVERBS));
      p = append(p, ".");
      break;
    case 2:
      p = append(p, "a ");
      p = append(p, PICK(ADJECTIVES));
      p = append(p, " ");
      p = append(p, PICK(NOUNS));
      p = append(p, " ");
      p = append(p, PICK(VERBS));
      p = append(p, " everything ");
      p = append(p, PICK(ADVERBS));
      p = append(p, ".");
      break;
    default:
      p = append(p, "the ");
      p = append(p, PICK(NOUNS));
      p = append(p, " ");
      p = append(p, PICK(VERBS));
      p = append(p, " ");
      p = append(p, PICK(ADVERBS));
      p = append(p, ", says the ");
      p = append(p, PICK(ADJECTIVES));
      p = append(p, " ");
      p = append(p, PICK(NOUNS));
      p = append(p, ".");
      break;
  }

  *p = '\0';
  if (out[0] >= 'a' && out[0] <= 'z') {
    out[0] = (char)(out[0] - ('a' - 'A'));
  }
}

static void delay_ms(uint32_t ms) {
  for (volatile uint32_t i = 0; i < ms * SENTENCE_CYCLES_PER_MS; ++i) {
    __asm__ volatile("nop");
  }
}

// ---------------------------------------------------------------------------
int main(void) {
  led_gpio_init();
  (void)ht5023_debug_uart2_init(SENTENCE_EXAMPLE_SYSCLK_HZ, SENTENCE_EXAMPLE_BAUD);

  // Free-run SysTick purely so its current value can seed the PRNG: the exact
  // moment the target leaves reset varies, so every run reads differently.
  SYST_RVR = 0x00FFFFFFu;
  SYST_CVR = 0u;
  SYST_CSR = 0x5u;  // ENABLE | CLKSOURCE=core

  rng_state = SYST_CVR ^ 0xA5A5F00Du;
  if (rng_state == 0u) rng_state = 0xDEADBEEFu;

  dbg_puts("\r\nHT5023 random sentences -- a new one every second.\r\n");

  for (uint32_t n = 1u;; ++n) {
    char line[160];
    char* p = line;

    p = append(p, "[");
    p = append_u32(p, n);
    p = append(p, "] ");
    make_sentence(p);
    dbg_puts(line);
    dbg_puts("\r\n");

    led_toggle();
    delay_ms(SENTENCE_PERIOD_MS);
  }
}
