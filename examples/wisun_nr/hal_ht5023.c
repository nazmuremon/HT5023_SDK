// hal_ht5023.c — wisun_hal.h for the Holtek HT5023 (Cortex-M0).
//
// The SDK's UART driver is polled and the UART holds a single byte, which drops
// most of a 115200 burst the moment the main loop does anything else. So this
// layer adds interrupt-driven receive and transmit on top of it:
//
//   UART5 (IRQ 14)  E51 module   RX ring WISUN_UART_RX_RING, TX ring WISUN_UART_TX_RING
//   UART2 (IRQ 11)  log/console  TX ring WISUN_LOG_TX_RING,  RX ring WISUN_CONSOLE_RX_RING
//
// (defaults; WISUN_MODULE_UART / WISUN_LOG_UART in wisun_config.h pick them)
//
// IRQ numbers are from HT502X User Manual 8.1 (UART0-5 = 9-14). The rings are
// single-producer/single-consumer with the ISR owning one index and the main
// loop the other, so neither side needs to mask interrupts to move data.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ht5023/core/system.h"
#include "ht5023/systick.h"
#include "ht5023/uart.h"
#include "wisun_config.h"
#include "wisun_hal.h"

// ---------------------------------------------------------------- registers --
#define UART0_BASE      0x40005000u
#define UART_SPACING    0x1000u        // UART1 0x40006000, UART2 0x40007000, ...
#define UARTCON_OFF     0x04u
#define SBUF_OFF        0x10u
#define UARTSTA_OFF     0x14u
#define UARTCON_RXIE    (1u << 3)
#define UARTCON_TXIE    (1u << 2)
#define UARTSTA_PARITY  (1u << 2)
#define UARTSTA_RXIF    (1u << 1)
#define UARTSTA_TXIF    (1u << 0)
#define UART_IRQ_FIRST  9u             // UART0; manual 8.1

#define NVIC_ISER       (*(volatile uint32_t*)0xE000E100u)

#define GPIO_IOCFG      0x00u
#define GPIO_PTDIR      0x08u
#define GPIO_PTSET      0x14u
#define GPIO_PTCLR      0x18u
#define GPIO_PTOD       0x20u

static inline volatile uint16_t* reg16(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(base + off);
}

static uint32_t uart_base(uint8_t id) {
  // UART5 sits at 0x40000000, out of sequence with 0-4.
  return (id == 5u) ? 0x40000000u : UART0_BASE + (uint32_t)id * UART_SPACING;
}

// ------------------------------------------------------ interrupt masking --
__attribute__((naked, noinline)) static uint32_t irq_save(void) {
  __asm__("mrs r0, primask\n"
          "cpsid i\n"
          "bx lr\n");
}

__attribute__((naked, noinline)) static void irq_restore(uint32_t primask) {
  (void)primask;
  __asm__("msr primask, r0\n"
          "bx lr\n");
}

// ---------------------------------------------------------- SPSC ring buffer --
// Capacity must be a power of two. head is written only by the producer and
// tail only by the consumer; one slot stays empty to tell full from empty.
typedef struct {
  uint8_t* data;
  uint16_t mask;
  volatile uint16_t head;
  volatile uint16_t tail;
} ring_t;

static inline bool ring_put(ring_t* r, uint8_t b) {
  uint16_t next = (uint16_t)((r->head + 1u) & r->mask);
  if (next == r->tail) return false;
  r->data[r->head] = b;
  r->head = next;
  return true;
}

static inline bool ring_get(ring_t* r, uint8_t* b) {
  if (r->tail == r->head) return false;
  *b = r->data[r->tail];
  r->tail = (uint16_t)((r->tail + 1u) & r->mask);
  return true;
}

static inline uint16_t ring_free(const ring_t* r) {
  return (uint16_t)((r->tail - r->head - 1u) & r->mask);
}

// -------------------------------------------------------------- UART ports --
typedef struct {
  uint8_t id;
  ring_t rx;
  ring_t tx;
  volatile bool tx_active;      // a byte is on the wire; the ISR feeds the rest
  volatile uint32_t rx_overflows;
} port_t;

static uint8_t s_mod_rx_buf[WISUN_UART_RX_RING];
static uint8_t s_mod_tx_buf[WISUN_UART_TX_RING];
static uint8_t s_log_tx_buf[WISUN_LOG_TX_RING];
static uint8_t s_con_rx_buf[WISUN_CONSOLE_RX_RING];

static port_t s_mod = {
  .id = WISUN_MODULE_UART,
  .rx = { s_mod_rx_buf, WISUN_UART_RX_RING - 1u, 0, 0 },
  .tx = { s_mod_tx_buf, WISUN_UART_TX_RING - 1u, 0, 0 },
};
static port_t s_log = {
  .id = WISUN_LOG_UART,
  .rx = { s_con_rx_buf, WISUN_CONSOLE_RX_RING - 1u, 0, 0 },
  .tx = { s_log_tx_buf, WISUN_LOG_TX_RING - 1u, 0, 0 },
};

static uint32_t s_log_dropped;

_Static_assert((WISUN_UART_RX_RING & (WISUN_UART_RX_RING - 1u)) == 0, "ring sizes must be powers of two");
_Static_assert((WISUN_UART_TX_RING & (WISUN_UART_TX_RING - 1u)) == 0, "ring sizes must be powers of two");
_Static_assert((WISUN_LOG_TX_RING & (WISUN_LOG_TX_RING - 1u)) == 0, "ring sizes must be powers of two");
_Static_assert((WISUN_CONSOLE_RX_RING & (WISUN_CONSOLE_RX_RING - 1u)) == 0, "ring sizes must be powers of two");
_Static_assert(WISUN_MODULE_UART != WISUN_LOG_UART, "the module and the log need separate UARTs");

// UARTSTA flags are cleared by writing 0; a 1 leaves a flag alone. So write 1
// everywhere except the flags being acknowledged.
static inline void sta_clear(uint32_t base, uint16_t flags) {
  *reg16(base, UARTSTA_OFF) =
      (uint16_t)((UARTSTA_PARITY | UARTSTA_RXIF | UARTSTA_TXIF) & (uint16_t)~flags);
}

static void port_isr(port_t* p) {
  const uint32_t base = uart_base(p->id);
  const uint16_t sta = *reg16(base, UARTSTA_OFF);

  if (sta & UARTSTA_RXIF) {
    const uint8_t b = (uint8_t)(*reg16(base, SBUF_OFF) & 0xFFu);
    sta_clear(base, (uint16_t)(UARTSTA_RXIF | UARTSTA_PARITY));
    if (!ring_put(&p->rx, b)) p->rx_overflows++;
  }

  if (sta & UARTSTA_TXIF) {
    sta_clear(base, UARTSTA_TXIF);
    uint8_t b;
    if (ring_get(&p->tx, &b)) {
      p->tx_active = true;         // port_kick must not load SBUF behind us
      *reg16(base, SBUF_OFF) = b;
    } else {
      p->tx_active = false;
    }
  }
}

// Starts the transmitter if it is idle. The ISR carries on from there.
static void port_kick(port_t* p) {
  const uint32_t primask = irq_save();
  if (!p->tx_active) {
    uint8_t b;
    if (ring_get(&p->tx, &b)) {
      p->tx_active = true;
      *reg16(uart_base(p->id), SBUF_OFF) = b;
    }
  }
  irq_restore(primask);
}

static bool port_init(port_t* p, uint32_t baud) {
  switch (p->id) {
    case 0: ht5023_uart0_pins_init_pc2_pc3(); break;
    case 1: ht5023_uart1_pins_init_pc0_pc1(); break;
    case 2: ht5023_uart2_pins_init_pc11_pc12(); break;
    case 5: ht5023_uart5_pins_init_pc4_pc5(); break;
    default: return false;        // add the pin helper before using another UART
  }
  const ht5023_uart_config_t cfg = {
    .sysclk_hz = WISUN_SYSCLK_HZ, .baud = baud, .enable_tx = true, .enable_rx = true,
  };
  if (!ht5023_uart_init((ht5023_uart_id_t)p->id, &cfg)) return false;

  const uint32_t base = uart_base(p->id);
  sta_clear(base, (uint16_t)(UARTSTA_PARITY | UARTSTA_RXIF | UARTSTA_TXIF));
  *reg16(base, UARTCON_OFF) = (uint16_t)(*reg16(base, UARTCON_OFF) | UARTCON_RXIE | UARTCON_TXIE);
  NVIC_ISER = 1u << (UART_IRQ_FIRST + p->id);
  return true;
}

// Which port each vector serves is fixed by the config, so one handler per
// possible UART forwards to the right port and the rest stay weak defaults.
#define PORT_FOR(uart) ((WISUN_MODULE_UART == (uart)) ? &s_mod : \
                        (WISUN_LOG_UART == (uart)) ? &s_log : (port_t*)0)

#if WISUN_MODULE_UART == 0 || WISUN_LOG_UART == 0
void UART0_IRQHandler(void) { port_isr(PORT_FOR(0)); }
#endif
#if WISUN_MODULE_UART == 1 || WISUN_LOG_UART == 1
void UART1_IRQHandler(void) { port_isr(PORT_FOR(1)); }
#endif
#if WISUN_MODULE_UART == 2 || WISUN_LOG_UART == 2
void UART2_IRQHandler(void) { port_isr(PORT_FOR(2)); }
#endif
#if WISUN_MODULE_UART == 5 || WISUN_LOG_UART == 5
void UART5_IRQHandler(void) { port_isr(PORT_FOR(5)); }
#endif

// ------------------------------------------------------------------- time --
// Manual 3.2: the UARTs are clocked from Fsys, but the CPU -- and so SysTick --
// from Fcpu = Fsys >> SYSCLK_DIV[2:0]. SYSCLK_DIV resets to 001, i.e. Fcpu is
// HALF of Fsys by default. Programming SysTick with Fsys therefore makes every
// "millisecond" last two (measured on the board: 0.48x wall-clock speed). Read
// the divider instead of assuming it, so this stays right if anything
// reconfigures the clock.
#define CMU_SYSCLKDIV_OFF  0x1Cu
#define CMU_BASE_ADDR      0x4000F000u

static uint32_t cpu_clock_hz(void) {
  const uint16_t div = (uint16_t)(*reg16(CMU_BASE_ADDR, CMU_SYSCLKDIV_OFF) & 7u);
  return WISUN_SYSCLK_HZ >> div;
}

static volatile uint32_t s_ms;

void SysTick_Handler(void) { s_ms++; }

uint32_t hal_time_ms(void) { return s_ms; }

// ------------------------------------------------------------------ reset --
#if WISUN_RESET_ENABLED
static const uint32_t k_gpio_base[6] = {
  0x40011000u, 0x40011100u, 0x40011200u, 0x40011300u, 0x40011400u,
  0x40011600u,                     // port F is at 0x600, not 0x500
};
static bool s_reset_active;
static uint32_t s_reset_started;

static void reset_drive(bool active) {
  const uint32_t base = k_gpio_base[WISUN_RESET_PORT];
  const uint16_t mask = (uint16_t)(1u << WISUN_RESET_PIN);
  const bool high = WISUN_RESET_ACTIVE_LOW ? !active : active;
  *reg16(base, high ? GPIO_PTSET : GPIO_PTCLR) = mask;
}
#endif

bool hal_module_reset_available(void) { return WISUN_RESET_ENABLED != 0; }

void hal_module_reset_pulse(void) {
#if WISUN_RESET_ENABLED
  reset_drive(true);
  s_reset_active = true;
  s_reset_started = hal_time_ms();
#endif
}

void hal_poll(void) {
#if WISUN_RESET_ENABLED
  if (s_reset_active && (uint32_t)(hal_time_ms() - s_reset_started) >= WISUN_RESET_PULSE_MS) {
    reset_drive(false);
    s_reset_active = false;
  }
#endif
}

// -------------------------------------------------------------- interface --
void hal_init(void) {
  (void)ht5023_systick_start(cpu_clock_hz(), 1000u);
  (void)port_init(&s_log, WISUN_LOG_BAUD);

#if WISUN_RESET_ENABLED
  const uint32_t base = k_gpio_base[WISUN_RESET_PORT];
  const uint16_t mask = (uint16_t)(1u << WISUN_RESET_PIN);
  reset_drive(false);                                  // inactive before it is an output
  *reg16(base, GPIO_IOCFG) = (uint16_t)(*reg16(base, GPIO_IOCFG) & (uint16_t)~mask);
  *reg16(base, GPIO_PTOD) = (uint16_t)(*reg16(base, GPIO_PTOD) | mask);   // push-pull
  *reg16(base, GPIO_PTDIR) = (uint16_t)(*reg16(base, GPIO_PTDIR) | mask); // output
#endif
}

bool hal_uart_init(uint32_t baud) { return port_init(&s_mod, baud); }

bool hal_uart_write(const uint8_t* bytes, size_t len) {
  if (len > ring_free(&s_mod.tx)) return false;
  for (size_t i = 0; i < len; i++) (void)ring_put(&s_mod.tx, bytes[i]);
  port_kick(&s_mod);
  return true;
}

size_t hal_uart_read(uint8_t* buf, size_t max) {
  size_t n = 0;
  while (n < max && ring_get(&s_mod.rx, &buf[n])) n++;
  return n;
}

uint32_t hal_uart_rx_overflows(void) { return s_mod.rx_overflows; }

void hal_log(const char* text) {
  size_t len = strlen(text);
  if (len > ring_free(&s_log.tx)) {
    s_log_dropped++;
    // Keep what fits of a marker so a gap in the log is visible, not silent.
    static const char k_gap[] = "\r\n[NR] ... log overflow ...\r\n";
    if (ring_free(&s_log.tx) >= sizeof(k_gap) - 1u) {
      for (const char* c = k_gap; *c; c++) (void)ring_put(&s_log.tx, (uint8_t)*c);
    }
  } else {
    for (size_t i = 0; i < len; i++) (void)ring_put(&s_log.tx, (uint8_t)text[i]);
  }
  port_kick(&s_log);
}

bool hal_console_read_line(char* buf, size_t max) {
  static char line[WISUN_CONSOLE_LINE_MAX];
  static size_t used;
  uint8_t b;
  while (ring_get(&s_log.rx, &b)) {
    if (b == '\r' || b == '\n') {
      if (used == 0u) continue;            // CRLF, or an empty Enter
      size_t n = (used < max - 1u) ? used : max - 1u;
      memcpy(buf, line, n);
      buf[n] = '\0';
      used = 0;
      return true;
    }
    if (b == 0x08 || b == 0x7F) {          // backspace
      if (used) used--;
      continue;
    }
    if (used < sizeof(line) - 1u) line[used++] = (char)b;
  }
  return false;
}
