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

#define UARTCON_NEGFIX_BIT            (1u << 10)
#define UARTCON_UNEG_BIT              (1u << 9)
#define UARTCON_STOPSEL_BIT           (1u << 8)
#define UARTCON_LENSEL_BIT            (1u << 7)
#define UARTCON_PARITYEN_BIT          (1u << 4)
#define UARTCON_RXIE_BIT              (1u << 3)
#define UARTCON_TXIE_BIT              (1u << 2)
#define UARTCON_RXEN_BIT              (1u << 1)
#define UARTCON_TXEN_BIT              (1u << 0)

#define UARTSTA_PARITY_BIT            (1u << 2)
#define UARTSTA_RXIF_BIT              (1u << 1)
#define UARTSTA_TXIF_BIT              (1u << 0)

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

  uint32_t denom = 2u * baud;
  if (denom == 0u) return 0u;

  uint32_t q = (sysclk_hz + (denom / 2u)) / denom;
  if (q == 0u) return 0u;
  uint32_t srel = q - 1u;
  if (srel > 0xFFFFu) return 0u;

  if (ok) *ok = true;
  return (uint16_t)srel;
}

void ht5023_uart0_pins_init_pc2_pc3(void) {
  const uint16_t pins = (uint16_t)((1u << 2) | (1u << 3));

  volatile uint16_t* iocfg = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_IOCFG_OFFSET);
  volatile uint16_t* afcfg = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_AFCFG_OFFSET);
  volatile uint16_t* ptup = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_PTUP_OFFSET);

  *iocfg |= pins;
  *afcfg &= (uint16_t)~pins;
  *ptup &= (uint16_t)~(1u << 2);
}

void ht5023_uart2_pins_init_pc11_pc12(void) {
  const uint16_t pins = (uint16_t)((1u << 11) | (1u << 12));

  volatile uint16_t* iocfg = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_IOCFG_OFFSET);
  volatile uint16_t* ptup = reg16(HT5023_GPIOC_BASE, HT5023_GPIO_PTUP_OFFSET);

  *iocfg |= pins;
  *ptup &= (uint16_t)~(1u << 12);
}

bool ht5023_uart_init(ht5023_uart_id_t id, const ht5023_uart_config_t* cfg) {
  if (!cfg) return false;

  uint32_t base = uart_base_from_id(id);
  if (base == 0u) return false;

  bool ok = false;
  uint16_t srel = calc_srel(cfg->sysclk_hz, cfg->baud, &ok);
  if (!ok) return false;

  ht5023_uart_enable_clock(id);

  *reg16(base, HT5023_UART_MODESEL_OFFSET) = 0x0000u;
  *reg16(base, HT5023_SREL_OFFSET) = srel;

  uint16_t uartcon = 0u;
  uartcon &= (uint16_t)~(UARTCON_NEGFIX_BIT | UARTCON_UNEG_BIT | UARTCON_STOPSEL_BIT |
                         UARTCON_LENSEL_BIT | UARTCON_PARITYEN_BIT | UARTCON_RXIE_BIT |
                         UARTCON_TXIE_BIT);
  if (cfg->enable_rx) uartcon |= (uint16_t)UARTCON_RXEN_BIT;
  if (cfg->enable_tx) uartcon |= (uint16_t)UARTCON_TXEN_BIT;
  *reg16(base, HT5023_UARTCON_OFFSET) = uartcon;

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

bool ht5023_uart2_init(const ht5023_uart_config_t* cfg) {
  ht5023_uart2_pins_init_pc11_pc12();
  return ht5023_uart_init(HT5023_UART_ID_2, cfg);
}

void ht5023_uart_write_byte(ht5023_uart_id_t id, uint8_t byte) {
  *uart_sbuf(id) = (uint16_t)byte;
  while (((*uart_sta(id)) & UARTSTA_TXIF_BIT) == 0u) {
  }
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

void ht5023_uart2_write_byte(uint8_t byte) {
  ht5023_uart_write_byte(HT5023_UART_ID_2, byte);
}

void ht5023_uart2_write(const void* data, size_t len) {
  ht5023_uart_write(HT5023_UART_ID_2, data, len);
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
  while (!ht5023_uart_try_read_byte(id, &b)) {
  }
  return b;
}

bool ht5023_uart2_rx_ready(void) {
  return ht5023_uart_rx_ready(HT5023_UART_ID_2);
}

bool ht5023_uart2_try_read_byte(uint8_t* out) {
  return ht5023_uart_try_read_byte(HT5023_UART_ID_2, out);
}

uint8_t ht5023_uart2_read_byte_blocking(void) {
  return ht5023_uart_read_byte_blocking(HT5023_UART_ID_2);
}
