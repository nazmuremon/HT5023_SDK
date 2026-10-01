// examples/uart_bridge/main.c — type commands at a device on UART1 (PC0/PC1)
//
// You type in the Flasher's send box; the MCU forwards the line to whatever
// module is wired to UART1 and prints the whole reply back:
//
//   > wisun get wisun
//   [dev] wisun.network_name = "cyberwisun"
//   [dev] wisun.mac_address = 34:10:f4:ff:fe:a8:7a:1d
//   [dev] >
//   (1481 bytes)
//
// Set the Flasher's log source to Semihosting and press Start Log; typing goes
// through the J-Link, so no serial adapter is needed for the host side.
//
// With no debugger attached the console falls back to UART2 (PC11 = TX2 ->
// adapter RX, PC12 = RX2 <- adapter TX) at BRIDGE_HOST_BAUD, so the same build
// works over a USB-UART adapter once the J-Link is unplugged. Flashing still
// needs the J-Link: this part has no UART bootloader, only SWD.
//
// Wiring: PC0 = TX1 -> module RX, PC1 = RX1 <- module TX, GND common. TX and RX
// cross over; straight through, the module never hears the command.
//
// Two timing facts drive the design of this example:
//
//   * Reading a typed character costs a semihosting trap (~10 ms), so the host
//     side is polled in a tight loop with nothing between the reads. Spacing
//     those reads out loses keystrokes.
//   * The UART holds a single byte with no interrupt or DMA, and one log write
//     halts the core for milliseconds. So a reply is captured into RAM by a
//     tight poll that never logs, and printed only once the module goes quiet.
//     Printing while receiving drops most of a 115200 reply.

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

// Which UART the external device is on:
//   1 -> UART1 on PC0 (TX1) / PC1 (RX1)     <- default
//   2 -> UART2 on PC11 (TX2) / PC12 (RX2)
//   0 -> UART0 on PC3 (TX0) / PC2 (RX0)
#ifndef BRIDGE_DEV_PORT
#define BRIDGE_DEV_PORT 1
#endif

#ifndef BRIDGE_DEV_BAUD
#define BRIDGE_DEV_BAUD 115200u
#endif

// How you talk to the MCU:
//   1 -> semihosting over the J-Link (no extra wiring)   <- default
//   2 -> UART2 on PC11/PC12 through a USB-serial adapter
#ifndef BRIDGE_HOST
#define BRIDGE_HOST 1
#endif

#ifndef BRIDGE_HOST_BAUD
#define BRIDGE_HOST_BAUD 115200u
#endif

// Fsys, the clock the UART divider counts: HT5023_FSYS_HZ from
// ht5023/core/system.h, measured at 9.196 MHz (the datasheet's 9.5 MHz is
// -2.65 % off at 115200). If text is garbled on a different board, measure
// its HRC and override HT5023_FSYS_HZ.
#ifndef BRIDGE_SYSCLK_HZ
#define BRIDGE_SYSCLK_HZ HT5023_FSYS_HZ
#endif

// Frame format. ht5023_uart_init() programs 8N1 only.
#ifndef BRIDGE_DATA_BITS
#define BRIDGE_DATA_BITS 8
#endif
#ifndef BRIDGE_PARITY
#define BRIDGE_PARITY 'N'
#endif
#ifndef BRIDGE_STOP_BITS
#define BRIDGE_STOP_BITS 1
#endif
#if (BRIDGE_DATA_BITS != 8) || (BRIDGE_STOP_BITS != 1) || (BRIDGE_PARITY != 'N')
#error "ht5023_uart_init() configures 8N1 only -- use the raw UARTCON bits for other frames."
#endif

// Line ending appended to what you type. "\r\n" suits most CLIs and AT modems;
// some devices want "\r" only.
#ifndef BRIDGE_DEV_EOL
#define BRIDGE_DEV_EOL "\r\n"
#endif

// Bytes of one reply held in RAM before printing. The Wi-SUN CLI answers
// "wisun get wisun" with ~1.5 kB, so keep this comfortably above your longest
// expected frame; the example says so when it fills up.
#ifndef BRIDGE_BURST_MAX
#define BRIDGE_BURST_MAX 3072u
#endif

// Poll iterations to wait for the FIRST byte of a reply after sending a command.
// Counted in spins because the capture loop must not delay or log; roughly
// 700 spins/ms on the default clock, so the default is about half a second of patience.
#ifndef BRIDGE_FIRST_WAIT_SPINS
#define BRIDGE_FIRST_WAIT_SPINS 400000u
#endif

// Spins of silence that mark the end of a reply (~50 ms by default). Raise it if
// a device pauses mid-frame and the log splits one reply into two.
#ifndef BRIDGE_IDLE_SPINS
#define BRIDGE_IDLE_SPINS 40000u
#endif

// Peek at the device once per this many idle host polls, so unsolicited output
// still shows up. 0 disables it: the device is then read only after a command,
// which is the most reliable setting for typing.
#ifndef BRIDGE_DEVICE_PEEK_EVERY
#define BRIDGE_DEVICE_PEEK_EVERY 0u
#endif

// Milliseconds to wait after sending, before listening. Normally 0; give it a
// few ms if a device needs to turn its line around.
#ifndef BRIDGE_TURNAROUND_MS
#define BRIDGE_TURNAROUND_MS 0u
#endif

// Tag device lines so they cannot be confused with MCU messages.
#ifndef BRIDGE_TAG
#define BRIDGE_TAG "[dev] "
#endif

// Render bytes that are not printable ASCII as <XX>.
#ifndef BRIDGE_SHOW_NONPRINTABLE
#define BRIDGE_SHOW_NONPRINTABLE 1
#endif

// Echo the command back into the log as "> ..." when you press Enter.
#ifndef BRIDGE_ECHO_COMMAND
#define BRIDGE_ECHO_COMMAND 1
#endif

// Report how many bytes each reply contained.
#ifndef BRIDGE_SHOW_COUNT
#define BRIDGE_SHOW_COUNT 1
#endif

#ifndef BRIDGE_LINE_MAX
#define BRIDGE_LINE_MAX 200u
#endif

#ifndef BRIDGE_CYCLES_PER_MS
#define BRIDGE_CYCLES_PER_MS 450u
#endif

// ===========================================================================

#if BRIDGE_DEV_PORT == 1
#define DEV_ID HT5023_UART_ID_1
#define DEV_PINS_INIT() ht5023_uart1_pins_init_pc0_pc1()
#define DEV_PIN_TEXT "UART1 PC0=TX1 PC1=RX1"
#elif BRIDGE_DEV_PORT == 2
#define DEV_ID HT5023_UART_ID_2
#define DEV_PINS_INIT() ht5023_uart2_pins_init_pc11_pc12()
#define DEV_PIN_TEXT "UART2 PC11=TX2 PC12=RX2"
#elif BRIDGE_DEV_PORT == 0
#define DEV_ID HT5023_UART_ID_0
#define DEV_PINS_INIT() ht5023_uart0_pins_init_pc2_pc3()
#define DEV_PIN_TEXT "UART0 PC3=TX0 PC2=RX0"
#else
#error "BRIDGE_DEV_PORT must be 0, 1 or 2."
#endif

#if (BRIDGE_HOST == 2) && (BRIDGE_DEV_PORT == 2)
#error "The host and the device cannot share UART2 -- move one of them."
#endif

#define HOST_ID HT5023_UART_ID_2

// --- ARM semihosting (host link when BRIDGE_HOST == 1) ---------------------
#define SYS_WRITE0 0x04u
#define SYS_READC 0x07u

// Armed by the Flasher's monitor at main(); a plain flash-and-run leaves it 0 so
// an unserviced `bkpt 0xAB` can never stall the core. Never gate on DHCSR: that
// register is debugger-only and reads back unreliably from software here.
volatile int g_semihost_enable = 0;

static int semihost(int op, void* arg) {
  register int r0 __asm__("r0") = op;
  register void* r1 __asm__("r1") = arg;
  // r1 is in-out: the debugger may clobber it, and if the compiler assumes it
  // survives it will reuse it as a constant afterwards.
  __asm__ volatile("bkpt 0xAB" : "+r"(r0), "+r"(r1) : : "memory", "cc");
  return r0;
}

static bool host_is_semihost(void) {
#if BRIDGE_HOST == 1
  return g_semihost_enable != 0;
#else
  return false;
#endif
}

// --- host I/O --------------------------------------------------------------
static void host_put(const char* s) {
  if (!s) return;
  if (host_is_semihost()) {
    (void)semihost(SYS_WRITE0, (void*)s);
  } else {
    ht5023_uart_write_str(HOST_ID, s);
  }
}

static void host_put_line(const char* s) {
  host_put(s);
  host_put("\r\n");
}

static void host_put_u32(uint32_t v) {
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
  host_put(buf);
}

// One typed character, or false when nothing has been typed.
static bool host_get(uint8_t* out) {
  if (host_is_semihost()) {
    const int c = semihost(SYS_READC, 0);
    if (c < 0) return false;
    *out = (uint8_t)c;
    return true;
  }
  return ht5023_uart_try_read_byte(HOST_ID, out);
}

static void delay_ms(uint32_t ms) {
  for (volatile uint32_t i = 0; i < ms * BRIDGE_CYCLES_PER_MS; ++i) {
    __asm__ volatile("nop");
  }
}

// --- device I/O ------------------------------------------------------------
static uint8_t burst[BRIDGE_BURST_MAX];

// Sends one byte and immediately drains anything the device echoes back into
// `burst`. Draining as we go matters: the receiver holds a single byte, so
// sending the whole line first loses every echoed character but the last.
static uint32_t send_byte_capturing(uint8_t b, uint32_t n) {
  uint8_t c;
  ht5023_uart_write_byte(DEV_ID, b);
  while (ht5023_uart_try_read_byte(DEV_ID, &c)) {
    if (n < BRIDGE_BURST_MAX) burst[n++] = c;
  }
  return n;
}

// Sends the command plus its line ending, returning how many echoed bytes were
// captured on the way.
static uint32_t send_to_device(const char* text) {
  uint32_t n = 0u;
  for (const char* p = text; *p; ++p) {
    n = send_byte_capturing((uint8_t)*p, n);
  }
  for (const char* p = BRIDGE_DEV_EOL; *p; ++p) {
    n = send_byte_capturing((uint8_t)*p, n);
  }
  return n;
}

// Captures one reply into `burst`, waiting up to `first_wait` spins for it to
// start. Nothing is logged in here: a log write would halt the core long enough
// to lose the rest of the frame. Returns the byte count.
static uint32_t capture_burst(uint32_t first_wait, uint32_t n) {
  uint8_t c;

  for (uint32_t waited = 0u; waited <= first_wait; ++waited) {
    if (ht5023_uart_try_read_byte(DEV_ID, &c)) {
      if (n < BRIDGE_BURST_MAX) burst[n++] = c;
      break;
    }
  }
  if (n == 0u) return 0u;

  for (uint32_t idle = 0u; idle <= BRIDGE_IDLE_SPINS; ++idle) {
    if (ht5023_uart_try_read_byte(DEV_ID, &c)) {
      if (n < BRIDGE_BURST_MAX) burst[n++] = c;
      idle = 0u;
    }
  }
  return n;
}

// Prints a captured reply as tagged lines, escaping anything unprintable.
static void print_burst(uint32_t n) {
  static const char hex[] = "0123456789ABCDEF";
  char line[BRIDGE_LINE_MAX + 1u];
  uint32_t len = 0u;

  for (uint32_t i = 0u; i < n; ++i) {
    const uint8_t b = burst[i];

    if (b == '\n' || b == '\r') {
      if (len > 0u) {
        line[len] = '\0';
        host_put(BRIDGE_TAG);
        host_put_line(line);
        len = 0u;
      }
      continue;
    }

    if (b >= 32u && b < 127u) {
      line[len++] = (char)b;
    } else if (len + 4u < BRIDGE_LINE_MAX) {
#if BRIDGE_SHOW_NONPRINTABLE
      line[len++] = '<';
      line[len++] = hex[(b >> 4) & 0x0Fu];
      line[len++] = hex[b & 0x0Fu];
      line[len++] = '>';
#endif
    }

    if (len >= BRIDGE_LINE_MAX) {
      line[len] = '\0';
      host_put(BRIDGE_TAG);
      host_put_line(line);
      len = 0u;
    }
  }

  if (len > 0u) {
    line[len] = '\0';
    host_put(BRIDGE_TAG);
    host_put_line(line);
  }
}

static void report(uint32_t n) {
  if (n == 0u) {
    host_put_line("(no reply -- check baud, TX/RX orientation, ground, and "
                  "whether the device wants a bare CR)");
    return;
  }
  print_burst(n);
  if (n >= BRIDGE_BURST_MAX) {
    host_put_line("(reply filled BRIDGE_BURST_MAX -- raise it to capture more)");
  }
#if BRIDGE_SHOW_COUNT
  host_put("(");
  host_put_u32(n);
  host_put_line(" bytes)");
#endif
}

// ---------------------------------------------------------------------------
int main(void) {
  static char line[BRIDGE_LINE_MAX + 1u];
  uint32_t len = 0u;

  led_gpio_init();
  led_off();

  const ht5023_uart_config_t dev_cfg = {
    .sysclk_hz = BRIDGE_SYSCLK_HZ,
    .baud = BRIDGE_DEV_BAUD,
    .enable_tx = true,
    .enable_rx = true,
  };
  DEV_PINS_INIT();
  const bool dev_ok = ht5023_uart_init(DEV_ID, &dev_cfg);

  // UART2 is brought up even in semihosting builds: host_put()/host_get() fall
  // back to it whenever a debugger has not armed semihosting, so unplugging the
  // J-Link moves the console to a USB-UART adapter on PC11/PC12 instead of
  // hanging on an unclocked peripheral.
#if BRIDGE_DEV_PORT != 2
  {
    const ht5023_uart_config_t host_cfg = {
      .sysclk_hz = BRIDGE_SYSCLK_HZ,
      .baud = BRIDGE_HOST_BAUD,
      .enable_tx = true,
      .enable_rx = true,
    };
    ht5023_uart2_pins_init_pc11_pc12();
    (void)ht5023_uart_init(HOST_ID, &host_cfg);
  }
#endif

  delay_ms(200u);  // let the debugger arm semihosting before the banner
  host_put_line("");
  host_put("HT5023 uart_bridge: device on ");
  host_put(DEV_PIN_TEXT);
  host_put(" @ ");
  host_put_u32(BRIDGE_DEV_BAUD);
  {
    const char frame[5] = {' ', (char)('0' + BRIDGE_DATA_BITS), (char)BRIDGE_PARITY,
                           (char)('0' + BRIDGE_STOP_BITS), '\0'};
    host_put(frame);
  }
  host_put_line(".");
  if (!dev_ok) {
    host_put_line("WARNING: the device UART rejected that baud/sysclk pair -- "
                  "nothing will be sent.");
  }
  host_put_line("Type a command and press Enter; the reply appears as "
                BRIDGE_TAG "lines.");

#if BRIDGE_DEVICE_PEEK_EVERY
  uint32_t idle_polls = 0u;
#endif

  while (1) {
    uint8_t c;

    // Host side: read back to back. A semihosting read costs milliseconds, and
    // putting other work between reads loses keystrokes.
    if (host_get(&c)) {
#if BRIDGE_DEVICE_PEEK_EVERY
      idle_polls = 0u;
#endif

      if (c == '\r' || c == '\n') {
        if (len > 0u) {
          line[len] = '\0';
#if BRIDGE_ECHO_COMMAND
          host_put("> ");
          host_put_line(line);
#endif
          const uint32_t echoed = send_to_device(line);
          len = 0u;
#if BRIDGE_TURNAROUND_MS
          delay_ms(BRIDGE_TURNAROUND_MS);
#endif
          led_toggle();
          report(capture_burst(BRIDGE_FIRST_WAIT_SPINS, echoed));
        }
      } else if (c == 0x08u || c == 0x7Fu) {  // backspace / delete
        if (len > 0u) --len;
      } else if (len < BRIDGE_LINE_MAX) {
        line[len++] = (char)c;
      }
      continue;
    }

#if BRIDGE_DEVICE_PEEK_EVERY
    // Optional: look for unsolicited output while idle. Off by default because
    // anything between semihosting reads makes typing less reliable.
    if (++idle_polls >= BRIDGE_DEVICE_PEEK_EVERY) {
      idle_polls = 0u;
      const uint32_t n = capture_burst(0u, 0u);
      if (n > 0u) report(n);
    }
#endif
  }
}
