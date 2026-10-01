// ht5023_uart.c — minimal blocking UART driver for HT5023/HT502X
#include "ht5023_uart.h"

#include <stdint.h>

#define HT5023_CMU_BASE               0x4000F000u
#define HT5023_CMU_WPREG_OFFSET       0x00u
#define HT5023_CMU_CLKCTRL1_OFFSET    0x30u
#define HT5023_WPREG_UNLOCK_KEY       0xA55Au

#define HT5023_GPIOC_BASE             0x40011200u
#define HT5023_GPIO_IOCFG_OFFSET      0x00u
#define HT5023_GPIO_AFCFG_OFFSET      0x04u
#define HT5023_GPIO_PTUP_OFFSET       0x0Cu

#define HT5023_UART0_BASE             0x40005000u
#define HT5023_UART1_BASE             0x40006000u
#define HT5023_UART2_BASE             0x40007000u
#define HT5023_UART3_BASE             0x40008000u
#define HT5023_UART4_BASE             0x40009000u
#define HT5023_UART5_BASE             0x40000000u

#define HT5023_UART_MODESEL_OFFSET    0x00u
#define HT5023_UARTCON_OFFSET         0x04u
#define HT5023_SREL_OFFSET            0x0Cu
#define HT5023_SBUF_OFFSET            0x10u
#define HT5023_UARTSTA_OFFSET         0x14u

// UARTCON bits (datasheet chapter 10.6).
#define UARTCON_NEGFIX_BIT            (1u << 10)
#define UARTCON_UNEG_BIT              (1u << 9)
#define UARTCON_STOPSEL_BIT           (1u << 8)
#define UARTCON_LENSEL_BIT            (1u << 7)
#define UARTCON_PARITYSEL_SHIFT       5u
#define UARTCON_PARITYEN_BIT          (1u << 4)
#define UARTCON_RXIE_BIT              (1u << 3)
#define UARTCON_TXIE_BIT              (1u << 2)
#define UARTCON_RXEN_BIT              (1u << 1)
#define UARTCON_TXEN_BIT              (1u << 0)

// UARTSTA bits.
#define UARTSTA_PARITY_BIT            (1u << 2)
#define UARTSTA_RXIF_BIT              (1u << 1)
#define UARTSTA_TXIF_BIT              (1u << 0)

// CLKCTRL1 UART enables.
#define CLKCTRL1_UART3_EN_BIT         (1u << 7)
#define CLKCTRL1_UART2_EN_BIT         (1u << 6)
#define CLKCTRL1_UART1_EN_BIT         (1u << 5)
#define CLKCTRL1_UART0_EN_BIT         (1u << 4)
#define CLKCTRL1_UART5_EN_BIT         (1u << 9)
#define CLKCTRL1_UART4_EN_BIT         (1u << 8)

static inline volatile uint16_t* reg16(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(base + off);
}

static uint32_t uart_base_from_id(ht5023_uart_id_t id) {
  switch (id) {
    case HT5023_UART_ID_0: return HT5023_UART0_BASE;
    case HT5023_UART_ID_1: return HT5023_UART1_BASE;
    case HT5023_UART_ID_2: return HT5023_UART2_BASE;
    case HT5023_UART_ID_3: return HT5023_UART3_BASE;
    case HT5023_UART_ID_4: return HT5023_UART4_BASE;
    case HT5023_UART_ID_5: return HT5023_UART5_BASE;
    default: return 0u;
  }
}

static uint16_t uart_clkctrl1_mask_from_id(ht5023_uart_id_t id) {
  switch (id) {
    case HT5023_UART_ID_0: return (uint16_t)CLKCTRL1_UART0_EN_BIT;
    case HT5023_UART_ID_1: return (uint16_t)CLKCTRL1_UART1_EN_BIT;
    case HT5023_UART_ID_2: return (uint16_t)CLKCTRL1_UART2_EN_BIT;
    case HT5023_UART_ID_3: return (uint16_t)CLKCTRL1_UART3_EN_BIT;
    case HT5023_UART_ID_4: return (uint16_t)CLKCTRL1_UART4_EN_BIT;
    case HT5023_UART_ID_5: return (uint16_t)CLKCTRL1_UART5_EN_BIT;
    default: return 0u;
  }
}

static void cmu_write_protected_begin(void) {
  *reg16(HT5023_CMU_BASE, HT5023_CMU_WPREG_OFFSET) = HT5023_WPREG_UNLOCK_KEY;
}

static void cmu_write_protected_end(void) {
  *reg16(HT5023_CMU_BASE, HT5023_CMU_WPREG_OFFSET) = 0x0000u;
}

static void ht5023_uart_enable_clock(ht5023_uart_id_t id) {
  volatile uint16_t* clkctrl1 = reg16(HT5023_CMU_BASE, HT5023_CMU_CLKCTRL1_OFFSET);
  uint16_t mask = uart_clkctrl1_mask_from_id(id);
  if (mask == 0u) return;

  cmu_write_protected_begin();
  *clkctrl1 = (uint16_t)(*clkctrl1 | mask);
  cmu_write_protected_end();
}

static uint16_t calc_srel(uint32_t sysclk_hz, uint32_t baud, bool* ok) {
  if (ok) *ok = false;
  if (sysclk_hz == 0u || baud == 0u) return 0u;

  // Datasheet formula: baud = Fsys / (2 * (SREL + 1))
  uint32_t denom = 2u * baud;
  if (denom == 0u) return 0u;

  uint32_t q = (sysclk_hz + (denom / 2u)) / denom;  // rounded Fsys/(2*baud)
  if (q == 0u) return 0u;
  uint32_t srel = q - 1u;
  if (srel > 0xFFFFu) return 0u;

  if (ok) *ok = true;
  return (uint16_t)srel;
}

void ht5023_uart0_pins_init_pc2_pc3(void) {
  const uint16_t pins = (uint16_t)((1u << 2) | (1u << 3));  // PC2=RX0, PC3=TX0

  volatile uint16_t* iocfg = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_IOCFG_OFFSET);
  volatile uint16_t* afcfg = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_AFCFG_OFFSET);
  volatile uint16_t* ptup  = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_PTUP_OFFSET);

  // First multiplex function on PC2/PC3 is RX0/TX0 per datasheet pin table:
  // IOCFG=1 selects multiplexed function, AFCFG=0 selects the first function.
  *iocfg |= pins;
  *afcfg &= (uint16_t)~pins;

  // Enable pull-up on RX0 (PTUP bit=0 enables pull-up on digital inputs).
  *ptup &= (uint16_t)~(1u << 2);
}

void ht5023_uart1_pins_init_pc0_pc1(void) {
  const uint16_t pins = (uint16_t)((1u << 0) | (1u << 1));  // PC0=TX1, PC1=RX1

  volatile uint16_t* iocfg = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_IOCFG_OFFSET);
  volatile uint16_t* afcfg = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_AFCFG_OFFSET);
  volatile uint16_t* ptup  = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_PTUP_OFFSET);

  // Datasheet pin table: PC.0 = TX1 / SEG43, PC.1 = RX1 / SEG44 — UART is the
  // first multiplexed function, so IOCFG=1 selects the mux and AFCFG=0 picks
  // UART1 rather than the LCD segment.
  *iocfg |= pins;
  *afcfg &= (uint16_t)~pins;

  // Enable pull-up on RX1 (PTUP bit=0 enables pull-up on digital inputs).
  *ptup &= (uint16_t)~(1u << 1);
}

void ht5023_uart2_pins_init_pc11_pc12(void) {
  const uint16_t pins = (uint16_t)((1u << 11) | (1u << 12));  // PC11=TX2, PC12=RX2

  volatile uint16_t* iocfg = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_IOCFG_OFFSET);
  volatile uint16_t* ptup  = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_PTUP_OFFSET);

  // Datasheet pin table shows PC11/PC12 map to TX2/RX2 in both mux slots.
  *iocfg |= pins;

  // Enable pull-up on RX2 (PTUP bit=0 enables pull-up on digital inputs).
  *ptup &= (uint16_t)~(1u << 12);
}

void ht5023_uart5_pins_init_pc4_pc5(void) {
  const uint16_t pins = (uint16_t)((1u << 4) | (1u << 5));  // PC4=RX5, PC5=TX5

  volatile uint16_t* iocfg = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_IOCFG_OFFSET);
  volatile uint16_t* afcfg = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_AFCFG_OFFSET);
  volatile uint16_t* ptup  = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_PTUP_OFFSET);

  // Datasheet pin table: PC.4 = SPI_MOSI / RX5, PC.5 = SPI_MISO / TX5 — UART5
  // is the SECOND multiplexed function, so IOCFG=1 and AFCFG=1. This takes the
  // pins away from SPI; ht5023_spi_pins_init_pc4_pc5_pc6_pc7() clears AFCFG
  // again. UART5 is not routed to any other pad.
  *iocfg |= pins;
  *afcfg |= pins;

  // Enable pull-up on RX5 (PTUP bit=0 enables pull-up on digital inputs).
  *ptup &= (uint16_t)~(1u << 4);
}

bool ht5023_uart_init(ht5023_uart_id_t id, const ht5023_uart_config_t* cfg) {
  if (cfg == 0) return false;

  uint32_t base = uart_base_from_id(id);
  if (base == 0u) return false;

  bool ok = false;
  uint16_t srel = calc_srel(cfg->sysclk_hz, cfg->baud, &ok);
  if (!ok) return false;

  ht5023_uart_enable_clock(id);

  // UART mode (not 7816).
  *reg16(base, HT5023_UART_MODESEL_OFFSET) = 0x0000u;

  // Set baud divider before enabling TX/RX.
  *reg16(base, HT5023_SREL_OFFSET) = srel;

  uint16_t uartcon = 0u;
  // Defaults: positive logic, 8 data bits, 1 stop, no parity, no IRQs.
  uartcon &= (uint16_t)~(UARTCON_NEGFIX_BIT | UARTCON_UNEG_BIT | UARTCON_STOPSEL_BIT |
                         UARTCON_LENSEL_BIT | UARTCON_PARITYEN_BIT | UARTCON_RXIE_BIT |
                         UARTCON_TXIE_BIT);
  if (cfg->enable_rx) uartcon |= (uint16_t)UARTCON_RXEN_BIT;
  if (cfg->enable_tx) uartcon |= (uint16_t)UARTCON_TXEN_BIT;
  *reg16(base, HT5023_UARTCON_OFFSET) = uartcon;

  // Clear sticky UART flags (TXIF/RXIF/PARITY are cleared by writing 0).
  *reg16(base, HT5023_UARTSTA_OFFSET) = 0x0000u;
  return true;
}

static inline volatile uint16_t* uart_sbuf(ht5023_uart_id_t id) {
  return reg16(uart_base_from_id(id), HT5023_SBUF_OFFSET);
}

static inline volatile uint16_t* uart_sta(ht5023_uart_id_t id) {
  return reg16(uart_base_from_id(id), HT5023_UARTSTA_OFFSET);
}

static void uartsta_clear(ht5023_uart_id_t id, uint16_t clear_mask) {
  uint16_t v = (uint16_t)(UARTSTA_PARITY_BIT | UARTSTA_RXIF_BIT | UARTSTA_TXIF_BIT);
  v = (uint16_t)(v & (uint16_t)~clear_mask);
  *uart_sta(id) = v;
}

void ht5023_uart_write_byte(ht5023_uart_id_t id, uint8_t byte) {
  *uart_sbuf(id) = (uint16_t)byte;
  while (((*uart_sta(id)) & UARTSTA_TXIF_BIT) == 0u) { }
  uartsta_clear(id, (uint16_t)UARTSTA_TXIF_BIT);
}

void ht5023_uart_write(ht5023_uart_id_t id, const void* data, size_t len) {
  if (!data || len == 0u) return;
  const uint8_t* p = (const uint8_t*)data;
  for (size_t i = 0; i < len; ++i) {
    ht5023_uart_write_byte(id, p[i]);
  }
}

void ht5023_uart_write_str(ht5023_uart_id_t id, const char* s) {
  if (!s) return;
  while (*s) {
    ht5023_uart_write_byte(id, (uint8_t)*s++);
  }
}

bool ht5023_uart_rx_ready(ht5023_uart_id_t id) {
  return ((*uart_sta(id)) & UARTSTA_RXIF_BIT) != 0u;
}

bool ht5023_uart_try_read_byte(ht5023_uart_id_t id, uint8_t* out) {
  if (!out) return false;
  if (!ht5023_uart_rx_ready(id)) return false;

  *out = (uint8_t)(*uart_sbuf(id) & 0xFFu);
  uartsta_clear(id, (uint16_t)(UARTSTA_RXIF_BIT | UARTSTA_PARITY_BIT));
  return true;
}

uint8_t ht5023_uart_read_byte_blocking(ht5023_uart_id_t id) {
  uint8_t b = 0u;
  while (!ht5023_uart_try_read_byte(id, &b)) { }
  return b;
}
