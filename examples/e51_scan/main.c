// examples/e51_scan/main.c — find out whether, how and at what baud a UART
// device answers on PC4/PC5, with no log port and no assumptions.
//
// Runs once after reset, then listens forever. Results live in `e51_scan` in
// RAM; read them over SWD (target keeps running) and decode with
//   python3 examples/e51_scan/decode.py
//
//   1. Hardware loopback: UART5 PC5->PC4 and UART1 PC0->PC1, all 256 byte
//      values plus text, at several bauds. Needs a jumper on the pair.
//   2. Pin probe: drive PC4/PC5 briefly, release with the pull-up off, and
//      time the return -- tells floating pins from ones something holds.
//   3. Sniff: "wisun get wisun" out of PC5 at 115200 while every input pad on
//      ports A-F is watched, to find where a device's TXD really lands.
//   4. Normal wiring (PC5 -> device RXD, PC4 <- TXD, hardware UART5 TX) and
//      swapped wiring (PC4 -> RXD bit-banged, PC5 <- TXD), 9600..921600 baud.
//      Pins are sampled as GPIO and every edge is timestamped against SysTick
//      run as a free-running CPU-cycle counter. The swapped pass drives PC4,
//      so it is skipped when the normal pass already got an answer.
//   5. Listen on every input pad forever; the first pad to move is recorded
//      at full speed for 1.5 s (catches a reset banner).
//
// Limitations: steps 3-4 start listening only after the (blocking) send
// returns, so they see a device's reply but not a jumper's echo -- step 1 is
// the loopback test. At Fcpu 4.6 MHz each recorded edge costs about as long
// as one bit at 115200, so dense traffic at that rate loses 1-2 bit pulses and
// does not decode; edge detection and rates up to ~38400 are reliable.
//
// Nothing here touches PB13/PB15 (SWD) or PC11/PC12 (the log UART).

#include <stdbool.h>
#include <stdint.h>

#include "ht5023/core/device.h"
#include "ht5023/core/system.h"
#include "ht5023/uart.h"

#define GPIOC_BASE   0x40011200u
#define GPIO_IOCFG   0x00u
#define GPIO_AFCFG   0x04u
#define GPIO_PTDIR   0x08u
#define GPIO_PTUP    0x0Cu
#define GPIO_PTDAT   0x10u
#define GPIO_PTSET   0x14u
#define GPIO_PTCLR   0x18u
#define GPIO_PTOD    0x20u

#define PIN4         (1u << 4)
#define PIN5         (1u << 5)

#define SYST_CSR     (*(volatile uint32_t*)0xE000E010u)
#define SYST_RVR     (*(volatile uint32_t*)0xE000E014u)
#define SYST_CVR     (*(volatile uint32_t*)0xE000E018u)

#define EDGE_CAP     512u
#define CAPTURE_MS   300u
#define ANSWER_EDGES 8u              // fewer edges than this is not a reply

enum { METHOD_UART5 = 0, METHOD_BITBANG = 1, METHOD_LISTEN = 2 };

// One edge record: bit 15 = line level after the record, bits 14..0 = CPU
// cycles since the previous record (saturated). A record whose level equals
// the previous one marks a gap between captures, not an edge.
typedef struct {
  uint8_t tx_pin;
  uint8_t rx_pin;
  uint8_t method;
  uint8_t start_level;
  uint32_t baud;
  uint32_t edges;                  // every edge, including those past the buffer
  uint32_t n;                      // records stored
  uint32_t loop_x16;               // capture loop period, CPU cycles x16
  uint32_t prev;                   // internal: time of the last record
  uint16_t buf[EDGE_CAP];
} trial_t;

static const uint32_t HW_BAUDS[] = { 115200u, 9600u, 19200u, 38400u, 57600u, 230400u, 460800u, 921600u };
static const uint32_t BB_BAUDS[] = { 115200u, 9600u, 19200u, 38400u, 57600u, 230400u };

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define N_TRIALS (COUNT(HW_BAUDS) + COUNT(BB_BAUDS))

// Connectivity probe: drive the pin to `drive` for 0.2 ms, release it with the
// pull-up off, and time how long it takes to leave that level. An external
// driver or pull (a powered TXD idles high) moves it within microseconds; a
// floating pin keeps its charge for milliseconds.
typedef struct {
  uint8_t pin;
  uint8_t drive;
  uint8_t first_level;             // read right after release
  uint8_t final_level;             // after PROBE_MS with the pull-up still off
  uint32_t change_cycles;          // cycles to the first change; 0xFFFFFFFF = none
  uint8_t pulled_up_level;         // with the pull-up back on
  uint8_t pad[3];
} pinprobe_t;

#define PROBE_MS     100u

// UART5 hardware loopback result for one baud: all 256 byte values, then text.
typedef struct {
  uint32_t baud;
  uint16_t sent;
  uint16_t received;
  uint16_t matched;
  uint16_t first_bad_index;        // 0xFFFF if none
  uint8_t first_bad_sent;
  uint8_t first_bad_got;
  uint8_t text_len;
  uint8_t uart;                    // 5 = PC5->PC4, 1 = PC0->PC1
  char text[40];                   // what came back for LOOP_TEXT
} loop_t;

#define LOOP_TEXT "UART loopback test ok\r\n"
#define N_LOOPS   5u

typedef struct {
  uint32_t magic;                  // 0xE51C0DE5 once initialised
  uint32_t fcpu_hz;
  uint32_t phase;                  // 1 normal, 2 swapped, 3 listening
  uint32_t trial;                  // trial in progress
  uint32_t swapped_skipped;        // 1 if the normal pass got an answer
  uint32_t listen_ms;
  uint32_t n_trials;
  uint32_t edge_cap;
  trial_t trials[N_TRIALS];
  trial_t listen_pc4;
  trial_t listen_pc5;
  pinprobe_t probes[4];            // PC4 low, PC4 high, PC5 low, PC5 high
  // Where does the device's TXD land? "wisun get wisun" goes out on PC5 at
  // 115200 while every input pad on ports A-F is watched for edges.
  uint16_t sniff_edges[6][16];     // [port A..F][bit], saturating
  uint8_t sniff_port;              // busiest pad, 0xFF if none
  uint8_t sniff_pin;
  uint8_t sniff_pad[2];
  trial_t sniff;                   // that pad captured while "\r\n" is sent
  // Listen phase: every input pad watched forever (catches a reset banner on
  // whichever pad the device's TXD reaches). The first pad to move is then
  // recorded at full speed for 1.5 s.
  uint16_t listen_edges[6][16];
  uint8_t hit_port;                // 0xFF until something moves
  uint8_t hit_pin;
  uint8_t hit_pad[2];
  trial_t hit;
  // Hardware UART loopbacks, run before everything else: UART5 (PC5 jumpered
  // to PC4) at 9600/57600/115200, then UART1 (PC0 jumpered to PC1) at 9600/115200.
  loop_t loops[N_LOOPS];
} scan_t;

static const uint32_t PORT_BASE[6] = {
  0x40011000u, 0x40011100u, 0x40011200u, 0x40011300u, 0x40011400u, 0x40011600u,
};
// Pads that exist, minus SWD (PB13, PB15), our TX (PC5) and the log UART
// (PC11, PC12).
static const uint16_t SNIFF_MASK[6] = {
  0x3FFFu, (uint16_t)(0xFFFFu & ~((1u << 13) | (1u << 15))),
  (uint16_t)(0x7FFFu & ~((1u << 5) | (1u << 11) | (1u << 12))), 0xFFFFu, 0x03FFu, 0x00FFu,
};

__attribute__((used)) scan_t e51_scan;

// ------------------------------------------------------------------- time --
static uint32_t s_now;
static uint32_t s_last_val;

// SysTick as a 24-bit down-counter of CPU cycles with no interrupt. Must be
// called at least every 2^24 cycles (~3.6 s at 4.6 MHz) to see every wrap.
static uint32_t tick_now(void) {
  const uint32_t v = SYST_CVR & 0xFFFFFFu;
  s_now += (s_last_val - v) & 0xFFFFFFu;
  s_last_val = v;
  return s_now;
}

static void wait_ms(uint32_t ms) {
  const uint32_t start = tick_now();
  const uint32_t n = ms * (e51_scan.fcpu_hz / 1000u);
  while (tick_now() - start < n) {
  }
}

// ------------------------------------------------------------------- pins --
static volatile uint16_t* gpc(uint32_t off) { return ht5023_reg16(GPIOC_BASE, off); }

static void pin_input(uint16_t m) {
  *gpc(GPIO_PTDIR) = (uint16_t)(*gpc(GPIO_PTDIR) & (uint16_t)~m);
  *gpc(GPIO_PTUP) = (uint16_t)(*gpc(GPIO_PTUP) & (uint16_t)~m);    // 0 = pull-up on
  *gpc(GPIO_IOCFG) = (uint16_t)(*gpc(GPIO_IOCFG) & (uint16_t)~m);  // GPIO
}

static void pin_output_idle_high(uint16_t m) {
  *gpc(GPIO_PTSET) = m;
  *gpc(GPIO_PTOD) = (uint16_t)(*gpc(GPIO_PTOD) | m);               // push-pull
  *gpc(GPIO_IOCFG) = (uint16_t)(*gpc(GPIO_IOCFG) & (uint16_t)~m);
  *gpc(GPIO_PTDIR) = (uint16_t)(*gpc(GPIO_PTDIR) | m);
}

static void pin_tx5(void) {                                        // PC5 = TX5
  *gpc(GPIO_AFCFG) = (uint16_t)(*gpc(GPIO_AFCFG) | PIN5);
  *gpc(GPIO_IOCFG) = (uint16_t)(*gpc(GPIO_IOCFG) | PIN5);
}

// ----------------------------------------------------------------- record --
static void record(trial_t* t, bool level, uint32_t when) {
  uint32_t d = when - t->prev;
  t->prev = when;
  if (d > 0x7FFFu) d = 0x7FFFu;
  if (t->n < EDGE_CAP) t->buf[t->n++] = (uint16_t)((level ? 0x8000u : 0u) | d);
}

__attribute__((optimize("O2")))
static void capture_port(trial_t* t, uint32_t base, uint16_t mask, uint32_t ms) {
  volatile uint16_t* const ptdat = ht5023_reg16(base, GPIO_PTDAT);
  const uint32_t window = ms * (e51_scan.fcpu_hz / 1000u);
  uint16_t level = (uint16_t)(*ptdat & mask);
  record(t, level != 0u, tick_now());          // gap marker: level unchanged
  const uint32_t start = t->prev;
  uint32_t spins = 0;
  for (;;) {
    // Tight inner loop; the clock is only consulted every 256 samples.
    uint32_t k = 256u;
    uint16_t now_level;
    do {
      now_level = (uint16_t)(*ptdat & mask);
    } while (now_level == level && --k);
    spins += 256u - k;
    if (now_level != level) {
      level = now_level;
      t->edges++;
      record(t, level != 0u, tick_now());
    } else if (tick_now() - start >= window) {
      break;
    }
  }
  if (t->loop_x16 == 0u && spins) t->loop_x16 = (tick_now() - start) * 16u / spins;
}

static void capture(trial_t* t, uint16_t mask, uint32_t ms) { capture_port(t, GPIOC_BASE, mask, ms); }

__attribute__((optimize("O2")))
static void sniff_all_ports(uint32_t ms) {
  uint16_t last[6];
  for (uint32_t p = 0; p < 6u; p++) last[p] = (uint16_t)(*ht5023_reg16(PORT_BASE[p], GPIO_PTDAT) & SNIFF_MASK[p]);
  const uint32_t start = tick_now();
  const uint32_t window = ms * (e51_scan.fcpu_hz / 1000u);
  for (;;) {
    for (uint32_t k = 0; k < 64u; k++) {
      for (uint32_t p = 0; p < 6u; p++) {
        const uint16_t v = (uint16_t)(*ht5023_reg16(PORT_BASE[p], GPIO_PTDAT) & SNIFF_MASK[p]);
        uint16_t diff = (uint16_t)(v ^ last[p]);
        if (diff) {
          last[p] = v;
          for (uint32_t b = 0; diff; b++, diff >>= 1) {
            if ((diff & 1u) && e51_scan.sniff_edges[p][b] != 0xFFFFu) e51_scan.sniff_edges[p][b]++;
          }
        }
      }
    }
    if (tick_now() - start >= window) break;
  }
}

static void sniff(void) {
  const ht5023_uart_config_t cfg = {
    .sysclk_hz = HT5023_FSYS_HZ, .baud = 115200u, .enable_tx = true, .enable_rx = false,
  };
  (void)ht5023_uart_init(HT5023_UART_ID_5, &cfg);
  pin_tx5();
  wait_ms(20u);
  ht5023_uart_write(HT5023_UART_ID_5, "\r\n", 2u);
  wait_ms(200u);
  ht5023_uart_write(HT5023_UART_ID_5, "wisun get wisun\r\n", 17u);
  sniff_all_ports(1500u);

  e51_scan.sniff_port = 0xFFu;
  uint16_t best = ANSWER_EDGES - 1u;
  for (uint8_t p = 0; p < 6u; p++) {
    for (uint8_t b = 0; b < 16u; b++) {
      if (e51_scan.sniff_edges[p][b] > best) {
        best = e51_scan.sniff_edges[p][b];
        e51_scan.sniff_port = p;
        e51_scan.sniff_pin = b;
      }
    }
  }
  if (e51_scan.sniff_port != 0xFFu) {
    // Record that pad properly so the reply can be decoded.
    const uint32_t base = PORT_BASE[e51_scan.sniff_port];
    const uint16_t m = (uint16_t)(1u << e51_scan.sniff_pin);
    trial_t* t = &e51_scan.sniff;
    wait_ms(500u);
    t->tx_pin = 5u;
    t->rx_pin = e51_scan.sniff_pin;
    t->method = METHOD_UART5;
    t->baud = 115200u;
    t->prev = tick_now();
    t->start_level = (uint8_t)((*ht5023_reg16(base, GPIO_PTDAT) & m) != 0u);
    ht5023_uart_write(HT5023_UART_ID_5, "\r\n", 2u);
    capture_port(t, base, m, 400u);
  }
  pin_input(PIN5);
}

static void probe_pin(pinprobe_t* p, uint8_t pin, bool drive) {
  const uint16_t m = (uint16_t)(1u << pin);
  volatile uint16_t* const ptdat = gpc(GPIO_PTDAT);
  p->pin = pin;
  p->drive = drive;

  *gpc(drive ? GPIO_PTSET : GPIO_PTCLR) = m;
  *gpc(GPIO_PTOD) = (uint16_t)(*gpc(GPIO_PTOD) | m);
  *gpc(GPIO_IOCFG) = (uint16_t)(*gpc(GPIO_IOCFG) & (uint16_t)~m);
  *gpc(GPIO_PTUP) = (uint16_t)(*gpc(GPIO_PTUP) | m);               // 1 = pull-up off
  *gpc(GPIO_PTDIR) = (uint16_t)(*gpc(GPIO_PTDIR) | m);             // drive
  const uint32_t t_drive = tick_now();
  while (tick_now() - t_drive < e51_scan.fcpu_hz / 5000u) {         // 0.2 ms
  }

  *gpc(GPIO_PTDIR) = (uint16_t)(*gpc(GPIO_PTDIR) & (uint16_t)~m);  // release, high-Z
  const uint32_t t0 = tick_now();
  const uint16_t first = (uint16_t)(*ptdat & m);
  p->first_level = first != 0u;
  p->change_cycles = 0xFFFFFFFFu;
  const uint32_t window = PROBE_MS * (e51_scan.fcpu_hz / 1000u);
  uint32_t now;
  while ((now = tick_now()) - t0 < window) {
    if (p->change_cycles == 0xFFFFFFFFu && (uint16_t)(*ptdat & m) != first) p->change_cycles = now - t0;
  }
  p->final_level = (*ptdat & m) != 0u;

  *gpc(GPIO_PTUP) = (uint16_t)(*gpc(GPIO_PTUP) & (uint16_t)~m);    // pull-up back on
  wait_ms(5u);
  p->pulled_up_level = (*ptdat & m) != 0u;
}

// ------------------------------------------------------------------ send --
static void bitbang_send(uint16_t pin, uint32_t baud, const char* s) {
  volatile uint16_t* const set = gpc(GPIO_PTSET);
  volatile uint16_t* const clr = gpc(GPIO_PTCLR);
  const uint32_t fcpu = e51_scan.fcpu_hz;
  // Bit period in 1/256 CPU cycles, without 64-bit division.
  const uint32_t t256 = ((fcpu / baud) << 8) + (((fcpu % baud) << 8) / baud);
  const uint32_t v0 = SYST_CVR & 0xFFFFFFu;
  uint32_t k = 0;
  for (; *s; s++) {
    const uint16_t frame = (uint16_t)(((uint16_t)(uint8_t)*s << 1) | 0x200u);  // start 0, stop 1
    for (uint32_t b = 0; b < 10u; b++, k++) {
      const uint32_t due = (k * t256) >> 8;
      while (((v0 - SYST_CVR) & 0xFFFFFFu) < due) {
      }
      if ((frame >> b) & 1u) *set = pin;
      else *clr = pin;
    }
  }
  const uint32_t due = (k * t256) >> 8;       // let the last stop bit finish
  while (((v0 - SYST_CVR) & 0xFFFFFFu) < due) {
  }
  (void)tick_now();
}

static void begin_trial(trial_t* t, uint8_t tx, uint8_t rx, uint8_t method, uint32_t baud) {
  t->tx_pin = tx;
  t->rx_pin = rx;
  t->method = method;
  t->baud = baud;
  t->edges = t->n = t->loop_x16 = 0u;
  t->prev = tick_now();
  t->start_level = (uint8_t)((*gpc(GPIO_PTDAT) & (rx == 4u ? PIN4 : PIN5)) != 0u);
}

static void refresh(uint16_t last[6], const uint16_t mask[6]) {
  for (uint32_t p = 0; p < 6u; p++) last[p] = (uint16_t)(*ht5023_reg16(PORT_BASE[p], GPIO_PTDAT) & mask[p]);
}

__attribute__((optimize("O2")))
static void listen_all(void) {
  uint16_t mask[6];
  for (uint32_t p = 0; p < 6u; p++) mask[p] = SNIFF_MASK[p];
  mask[2] = (uint16_t)(mask[2] | PIN5);          // PC5 is an input again
  uint16_t last[6];
  refresh(last, mask);
  e51_scan.hit_port = 0xFFu;
  const uint32_t listen_start = tick_now();
  for (;;) {
    for (uint32_t k = 0; k < 64u; k++) {
      for (uint32_t p = 0; p < 6u; p++) {
        const uint16_t v = (uint16_t)(*ht5023_reg16(PORT_BASE[p], GPIO_PTDAT) & mask[p]);
        uint16_t diff = (uint16_t)(v ^ last[p]);
        if (!diff) continue;
        last[p] = v;
        uint32_t first_bit = 16u;
        for (uint32_t b = 0; b < 16u; b++) {
          if ((diff >> b) & 1u) {
            if (first_bit == 16u) first_bit = b;
            if (e51_scan.listen_edges[p][b] != 0xFFFFu) e51_scan.listen_edges[p][b]++;
          }
        }
        if (e51_scan.hit_port == 0xFFu) {
          // First activity anywhere: record that pad properly.
          const uint16_t m = (uint16_t)(1u << first_bit);
          const bool level = (v & m) != 0u;
          trial_t* t = &e51_scan.hit;
          e51_scan.hit_port = (uint8_t)p;
          e51_scan.hit_pin = (uint8_t)first_bit;
          t->method = METHOD_LISTEN;
          t->rx_pin = (uint8_t)first_bit;
          t->start_level = (uint8_t)!level;
          t->prev = tick_now();
          t->edges = 1u;
          record(t, level, t->prev);
          capture_port(t, PORT_BASE[p], m, 1500u);
          const uint32_t more = t->edges - 1u;
          e51_scan.listen_edges[p][first_bit] =
              (uint16_t)((e51_scan.listen_edges[p][first_bit] + more > 0xFFFFu)
                             ? 0xFFFFu : e51_scan.listen_edges[p][first_bit] + more);
          refresh(last, mask);
        }
      }
    }
    e51_scan.listen_ms = (tick_now() - listen_start) / (e51_scan.fcpu_hz / 1000u);
  }
}

static volatile uint16_t* reg16_uartcon(ht5023_uart_id_t id) {
  return ht5023_reg16(id == HT5023_UART_ID_5 ? 0x40000000u : 0x40006000u, 0x04u);
}

// Sends one byte and waits (bounded) for it to come back. -1 if nothing did.
static int loop_byte(ht5023_uart_id_t id, uint8_t b, uint32_t baud) {
  ht5023_uart_write_byte(id, b);                 // returns after the stop bit
  const uint32_t start = tick_now();
  const uint32_t limit = e51_scan.fcpu_hz / baud * 30u;   // three frames
  uint8_t got;
  do {
    if (ht5023_uart_try_read_byte(id, &got)) return got;
  } while (tick_now() - start < limit);
  return -1;
}

static void uart_loopback(loop_t* r, uint8_t uart, uint32_t baud) {
  const ht5023_uart_id_t id = uart == 5u ? HT5023_UART_ID_5 : HT5023_UART_ID_1;
  const ht5023_uart_config_t cfg = {
    .sysclk_hz = HT5023_FSYS_HZ, .baud = baud, .enable_tx = true, .enable_rx = true,
  };
  if (uart == 5u) ht5023_uart5_pins_init_pc4_pc5();
  else ht5023_uart1_pins_init_pc0_pc1();
  (void)ht5023_uart_init(id, &cfg);
  wait_ms(20u);
  uint8_t junk;
  while (ht5023_uart_try_read_byte(id, &junk)) {
  }
  r->uart = uart;

  r->baud = baud;
  r->first_bad_index = 0xFFFFu;
  const char* text = LOOP_TEXT;
  const uint16_t total = (uint16_t)(256u + (sizeof(LOOP_TEXT) - 1u));
  for (uint16_t i = 0; i < total; i++) {
    const bool is_text = i >= 256u;
    const uint8_t b = is_text ? (uint8_t)text[i - 256u] : (uint8_t)i;
    const int got = loop_byte(id, b, baud);
    r->sent++;
    if (got >= 0) r->received++;
    if (got == (int)b) {
      r->matched++;
    } else if (r->first_bad_index == 0xFFFFu) {
      r->first_bad_index = i;
      r->first_bad_sent = b;
      r->first_bad_got = (uint8_t)(got < 0 ? 0u : (unsigned)got);
    }
    if (is_text && got >= 0 && r->text_len < sizeof(r->text)) r->text[r->text_len++] = (char)got;
  }
  *reg16_uartcon(id) = 0u;                       // UART off before the pins go back to GPIO
  const uint16_t pins = uart == 5u ? (uint16_t)(PIN4 | PIN5) : (uint16_t)0x3u;
  *gpc(GPIO_AFCFG) = (uint16_t)(*gpc(GPIO_AFCFG) & (uint16_t)~pins);
  pin_input(pins);
}

// ------------------------------------------------------------------ main --
int main(void) {
  e51_scan.fcpu_hz = ht5023_fcpu_hz();
  e51_scan.n_trials = (uint32_t)N_TRIALS;
  e51_scan.edge_cap = EDGE_CAP;

  SYST_CSR = 0u;
  SYST_RVR = 0xFFFFFFu;
  SYST_CVR = 0u;
  SYST_CSR = (1u << 2) | 1u;                   // CPU clock, enabled, no interrupt
  s_last_val = SYST_CVR & 0xFFFFFFu;

  pin_input(PIN4 | PIN5);
  e51_scan.magic = 0xE51C0DE5u;
  wait_ms(300u);

  e51_scan.phase = 5u;
  uart_loopback(&e51_scan.loops[0], 5u, 9600u);
  uart_loopback(&e51_scan.loops[1], 5u, 57600u);
  uart_loopback(&e51_scan.loops[2], 5u, 115200u);
  uart_loopback(&e51_scan.loops[3], 1u, 9600u);
  uart_loopback(&e51_scan.loops[4], 1u, 115200u);
  wait_ms(50u);

  // Is anything attached to each pin, and is it powered?
  probe_pin(&e51_scan.probes[0], 4u, false);
  probe_pin(&e51_scan.probes[1], 4u, true);
  probe_pin(&e51_scan.probes[2], 5u, false);
  probe_pin(&e51_scan.probes[3], 5u, true);
  pin_input(PIN4 | PIN5);
  wait_ms(50u);

  e51_scan.phase = 4u;
  sniff();

  // Normal wiring: PC5 = TX5 (hardware), listen on PC4.
  e51_scan.phase = 1u;
  bool answered = false;
  uint32_t ti = 0;
  for (uint32_t i = 0; i < COUNT(HW_BAUDS); i++, ti++) {
    trial_t* t = &e51_scan.trials[ti];
    e51_scan.trial = ti;
    const ht5023_uart_config_t cfg = {
      .sysclk_hz = HT5023_FSYS_HZ, .baud = HW_BAUDS[i], .enable_tx = true, .enable_rx = false,
    };
    pin_input(PIN4);
    (void)ht5023_uart_init(HT5023_UART_ID_5, &cfg);
    pin_tx5();
    begin_trial(t, 5u, 4u, METHOD_UART5, HW_BAUDS[i]);
    for (int k = 0; k < 2; k++) {
      ht5023_uart_write(HT5023_UART_ID_5, "\r\n", 2u);
      capture(t, PIN4, CAPTURE_MS);
    }
    if (t->edges >= ANSWER_EDGES) answered = true;
  }
  pin_input(PIN5);                             // release TX5

  // Swapped wiring: bit-bang TX on PC4, listen on PC5.
  e51_scan.phase = 2u;
  if (answered) {
    e51_scan.swapped_skipped = 1u;
  } else {
    for (uint32_t i = 0; i < COUNT(BB_BAUDS); i++, ti++) {
      trial_t* t = &e51_scan.trials[ti];
      e51_scan.trial = ti;
      pin_input(PIN5);
      pin_output_idle_high(PIN4);
      wait_ms(20u);                            // idle high before the first start bit
      begin_trial(t, 4u, 5u, METHOD_BITBANG, BB_BAUDS[i]);
      for (int k = 0; k < 2; k++) {
        bitbang_send(PIN4, BB_BAUDS[i], "\r\n");
        capture(t, PIN5, CAPTURE_MS);
      }
    }
    pin_input(PIN4);                           // stop driving PC4
  }

  // Listen on every input pad forever.
  e51_scan.phase = 3u;
  listen_all();
}
