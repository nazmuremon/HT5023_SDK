// examples/button_press/main.c — user button -> serial log + double LED blink
//
// Press the user button and the debug console prints "Button pressed", then the
// user LEDs (PE3 + PE4) blink twice.
//
// Debug output goes to TWO channels so it shows up in the Flasher's real-time
// debug log whichever way you are wired:
//
//   1. UART2  — PC11 = TX2, PC12 = RX2, 9600 8N1 (USB-TTL adapter -> "Serial" source)
//   2. ARM semihosting over the J-Link SWD cable (no extra wiring -> "Semihosting"
//      source). Only active while the Flasher's monitor is attached: see the
//      g_semihost_enable note below.
//
// The button pin is NOT fixed by the board support package — set it here to
// match your board. Defaults: PE5, active-low, internal pull-up enabled.

#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/debug.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_on(void);
extern void led_off(void);

// ---------------------------------------------------------------------------
// Board wiring — edit to match your button
// ---------------------------------------------------------------------------
// Port bases: PA 0x40011000, PB 0x40011100, PC 0x40011200, PD 0x40011300,
//             PE 0x40011400.  (LEDs live on PE3/PE4.)
#ifndef BUTTON_PORT_BASE
#define BUTTON_PORT_BASE 0x40011400u  // GPIOE
#endif

#ifndef BUTTON_PIN
#define BUTTON_PIN 5u  // PE5
#endif

// 1 = button shorts the pin to GND when pressed (needs the pull-up).
// 0 = button drives the pin high when pressed (external pull-down expected).
#ifndef BUTTON_ACTIVE_LOW
#define BUTTON_ACTIVE_LOW 1
#endif

// Enable the internal pull-up on the button pin (required for active-low).
#ifndef BUTTON_PULLUP
#define BUTTON_PULLUP BUTTON_ACTIVE_LOW
#endif

// Pin finder: reports any other GPIO input that changes level, so pressing the
// button tells you exactly which pin it is wired to. Set to 0 once BUTTON_PIN
// below matches your board.
#ifndef BUTTON_SCAN
#define BUTTON_SCAN 1
#endif

#ifndef BUTTON_EXAMPLE_SYSCLK_HZ
#define BUTTON_EXAMPLE_SYSCLK_HZ HT5023_FSYS_HZ
#endif

#ifndef BUTTON_EXAMPLE_BAUD
#define BUTTON_EXAMPLE_BAUD 9600u
#endif

// Busy-loop iterations per millisecond (matches the blinky example's timing:
// 45000 iterations ~= 100 ms on the default clock).
#ifndef BUTTON_CYCLES_PER_MS
#define BUTTON_CYCLES_PER_MS 450u
#endif

#define BUTTON_MASK ((uint16_t)(1u << (BUTTON_PIN)))

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

// Writes one line to every debug channel that is available right now.
static void dbg_puts(const char* s) {
  if (!s) return;
  ht5023_debug_uart2_write_str(s);
  if (semihost_ready()) {
    (void)semihost(SYS_WRITE0, (void*)s);
  }
}

// Tiny unsigned -> decimal (avoids pulling in newlib printf).
static void dbg_put_u32(uint32_t v) {
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
  dbg_puts(buf);
}

// ---------------------------------------------------------------------------
// Timing + button
// ---------------------------------------------------------------------------
static void delay_ms(uint32_t ms) {
  for (volatile uint32_t i = 0; i < ms * BUTTON_CYCLES_PER_MS; ++i) {
    __asm__ volatile("nop");
  }
}

static inline volatile uint16_t* btn_reg(uint32_t off) {
  return (volatile uint16_t*)(BUTTON_PORT_BASE + off);
}

static void button_init(void) {
  // Plain GPIO (IOCFG bit=0), input (PTDIR bit=0). Read-modify-write so the
  // LED bits on the same port keep their configuration.
  *btn_reg(GPIO_IOCFG_OFFSET) &= (uint16_t)~BUTTON_MASK;
  *btn_reg(GPIO_AFCFG_OFFSET) &= (uint16_t)~BUTTON_MASK;
  *btn_reg(GPIO_PTDIR_OFFSET) &= (uint16_t)~BUTTON_MASK;

  // PTUP bit=0 ENABLES the pull-up on a digital input (inverted polarity).
#if BUTTON_PULLUP
  *btn_reg(GPIO_PTUP_OFFSET) &= (uint16_t)~BUTTON_MASK;
#else
  *btn_reg(GPIO_PTUP_OFFSET) |= BUTTON_MASK;
#endif
}

static bool button_is_down(void) {
  const bool high = ((*btn_reg(GPIO_PTDAT_OFFSET)) & BUTTON_MASK) != 0u;
#if BUTTON_ACTIVE_LOW
  return !high;
#else
  return high;
#endif
}

// Samples until the level has been stable for ~16 ms, so contact bounce cannot
// register as several presses.
static bool button_read_debounced(bool previous) {
  bool level = button_is_down();
  if (level == previous) return level;

  for (int stable = 0; stable < 8; ++stable) {
    delay_ms(2);
    if (button_is_down() != level) {
      stable = -1;  // bounced: restart the stability window
      level = button_is_down();
    }
  }
  return level;
}

// ---------------------------------------------------------------------------
// Pin finder
// ---------------------------------------------------------------------------
#if BUTTON_SCAN
#define SCAN_PORTS 5u
#define GPIO_PORT_STRIDE 0x100u
#define GPIO_PORT_BASE0 0x40011000u

static uint16_t scan_mask[SCAN_PORTS];
static uint16_t scan_prev[SCAN_PORTS];

static const char* const SCAN_BASES[SCAN_PORTS] = {
  "0x40011000", "0x40011100", "0x40011200", "0x40011300", "0x40011400",
};

static inline volatile uint16_t* scan_reg(uint32_t port, uint32_t off) {
  return (volatile uint16_t*)(GPIO_PORT_BASE0 + port * GPIO_PORT_STRIDE + off);
}

// Pins that must not be disturbed or reported: the SWD debug link (PB13/PB15,
// which toggles nonstop while the monitor is attached), UART2 (PC11/PC12), the
// LEDs (PE3/PE4) and the button pin this example already watches.
static uint16_t scan_excluded(uint32_t port) {
  uint16_t mask = 0u;
  if (port == 1u) mask |= (uint16_t)((1u << 13) | (1u << 15));
  if (port == 2u) mask |= (uint16_t)((1u << 11) | (1u << 12));
  if (port == 4u) mask |= LED_PINS_MASK;
  if ((GPIO_PORT_BASE0 + port * GPIO_PORT_STRIDE) == BUTTON_PORT_BASE) {
    mask |= BUTTON_MASK;
  }
  return mask;
}

static void scan_init(void) {
  for (uint32_t i = 0u; i < SCAN_PORTS; ++i) {
    // Only plain GPIO inputs: IOCFG bit=0 means GPIO, PTDIR bit=0 means input.
    const uint16_t gpio_in = (uint16_t)(~*scan_reg(i, GPIO_IOCFG_OFFSET) &
                                        ~*scan_reg(i, GPIO_PTDIR_OFFSET));
    scan_mask[i] = (uint16_t)(gpio_in & ~scan_excluded(i));
    // Pull-ups on (PTUP bit=0) so unconnected pins read a steady high instead
    // of floating and reporting phantom changes.
    *scan_reg(i, GPIO_PTUP_OFFSET) &= (uint16_t)~scan_mask[i];
    scan_prev[i] = (uint16_t)(*scan_reg(i, GPIO_PTDAT_OFFSET) & scan_mask[i]);
  }
}

static void scan_report(uint32_t port, uint32_t bit, bool now_high) {
  dbg_puts("PIN CHANGE: P");
  dbg_puts((const char[]){(char)('A' + port), '\0'});
  dbg_put_u32(bit);
  dbg_puts(now_high ? " low -> high" : " high -> low");
  dbg_puts("   <- set BUTTON_PORT_BASE ");
  dbg_puts(SCAN_BASES[port]);
  dbg_puts("u, BUTTON_PIN ");
  dbg_put_u32(bit);
  dbg_puts("u, BUTTON_ACTIVE_LOW ");
  dbg_puts(now_high ? "0" : "1");
  dbg_puts("\r\n");
}

// Reports level changes, confirming each one after ~30 ms so contact bounce and
// noise do not spam the log.
static void scan_poll(void) {
  for (uint32_t i = 0u; i < SCAN_PORTS; ++i) {
    if (scan_mask[i] == 0u) continue;
    uint16_t now = (uint16_t)(*scan_reg(i, GPIO_PTDAT_OFFSET) & scan_mask[i]);
    if (now == scan_prev[i]) continue;

    delay_ms(30);
    now = (uint16_t)(*scan_reg(i, GPIO_PTDAT_OFFSET) & scan_mask[i]);
    const uint16_t changed = (uint16_t)(now ^ scan_prev[i]);
    if (changed == 0u) continue;

    for (uint32_t bit = 0u; bit < 16u; ++bit) {
      if (changed & (uint16_t)(1u << bit)) {
        scan_report(i, bit, (now & (uint16_t)(1u << bit)) != 0u);
      }
    }
    scan_prev[i] = now;
  }
}
#endif  // BUTTON_SCAN

static void led_blink_twice(void) {
  for (int i = 0; i < 2; ++i) {
    led_on();
    delay_ms(120);
    led_off();
    delay_ms(120);
  }
}

// ---------------------------------------------------------------------------
int main(void) {
  led_gpio_init();
  led_off();
  button_init();
  (void)ht5023_debug_uart2_init(BUTTON_EXAMPLE_SYSCLK_HZ, BUTTON_EXAMPLE_BAUD);

  dbg_puts("\r\nHT5023 button demo ready.\r\n");
  dbg_puts("Press the user button -> this console logs it and the LEDs blink twice.\r\n");
#if BUTTON_SCAN
  scan_init();
  dbg_puts("Pin finder is on: if nothing happens when you press, watch for a "
           "PIN CHANGE line\r\n  telling you which pin it is (set BUTTON_SCAN 0 "
           "once BUTTON_PIN matches).\r\n");
#endif

  uint32_t presses = 0;
  bool pressed = button_read_debounced(false);
  if (pressed) {
    dbg_puts("Note: button reads as held at boot -- check BUTTON_PIN / BUTTON_ACTIVE_LOW.\r\n");
  }

  while (1) {
    const bool now = button_read_debounced(pressed);

    if (now && !pressed) {  // rising edge of "pressed"
      ++presses;
      dbg_puts("Button pressed (count=");
      dbg_put_u32(presses);
      dbg_puts(")\r\n");
      led_blink_twice();
    } else if (!now && pressed) {
      dbg_puts("Button released\r\n");
    }

#if BUTTON_SCAN
    scan_poll();
#endif

    pressed = now;
    delay_ms(5);
  }
}
