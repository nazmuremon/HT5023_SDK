// ht5023_spi.c — minimal HT5023 SPI master polling driver
#include "ht5023_spi.h"

#include <stddef.h>
#include <stdint.h>

#define HT5023_CMU_BASE               0x4000F000u
#define HT5023_CMU_WPREG_OFFSET       0x00u
#define HT5023_CMU_CLKCTRL0_OFFSET    0x2Cu
#define HT5023_WPREG_UNLOCK_KEY       0xA55Au

#define CLKCTRL0_SPI_EN_BIT           (1u << 2)

#define HT5023_GPIOC_BASE             0x40011200u
#define GPIO_IOCFG_OFFSET             0x00u
#define GPIO_AFCFG_OFFSET             0x04u

#define HT5023_SPI_BASE               0x4000B000u
#define SPICON_OFFSET                 0x00u
#define SPISTA_OFFSET                 0x04u
#define SPIDAT_OFFSET                 0x08u
#define SPISSN_OFFSET                 0x0Cu

#define SPICON_SSDIS_BIT              (1u << 7)
#define SPICON_SPR_SHIFT              4u
#define SPICON_CPHA_BIT               (1u << 3)
#define SPICON_CPOL_BIT               (1u << 2)
#define SPICON_MSTR_BIT               (1u << 1)
#define SPICON_SPI_EN_BIT             (1u << 0)

#define SPISTA_SPIF_BIT               (1u << 3)
#define SPISSN_SSN0_BIT               (1u << 0)

// Guard against an infinite poll if the SPI peripheral is mis-configured or
// the hardware is not clocked.  60 000 iterations is many times the worst-case
// latency at any supported SPR divider and 10 MHz system clock.
#define HT5023_SPI_POLL_TIMEOUT       60000u

static inline volatile uint16_t* reg16(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(base + off);
}

static void spi_enable_clock(void) {
  volatile uint16_t* wpreg = reg16(HT5023_CMU_BASE, HT5023_CMU_WPREG_OFFSET);
  volatile uint16_t* clk0 = reg16(HT5023_CMU_BASE, HT5023_CMU_CLKCTRL0_OFFSET);
  *wpreg = HT5023_WPREG_UNLOCK_KEY;
  *clk0 = (uint16_t)(*clk0 | (uint16_t)CLKCTRL0_SPI_EN_BIT);
  *wpreg = 0x0000u;
}

void ht5023_spi_pins_init_pc4_pc5_pc6_pc7(void) {
  const uint16_t pins = (uint16_t)((1u << 4) | (1u << 5) | (1u << 6) | (1u << 7));
  volatile uint16_t* iocfg = reg16(HT5023_GPIOC_BASE, GPIO_IOCFG_OFFSET);
  volatile uint16_t* afcfg = reg16(HT5023_GPIOC_BASE, GPIO_AFCFG_OFFSET);
  *iocfg |= pins;                 // muxed function
  *afcfg &= (uint16_t)~pins;      // first mux function => SPI_* on PC4..PC7
}

bool ht5023_spi_master_init(const ht5023_spi_master_cfg_t* cfg) {
  if (!cfg) return false;
  const uint8_t spr = (uint8_t)(cfg->spr & 0x7u);

  spi_enable_clock();
  ht5023_spi_pins_init_pc4_pc5_pc6_pc7();

  // Idle CS high
  *reg16(HT5023_SPI_BASE, SPISSN_OFFSET) = SPISSN_SSN0_BIT;

  uint16_t spicon = 0u;
  spicon |= SPICON_SPI_EN_BIT | SPICON_MSTR_BIT | SPICON_SSDIS_BIT;
  spicon |= (uint16_t)((uint16_t)spr << SPICON_SPR_SHIFT);
  if (cfg->cpol) spicon |= SPICON_CPOL_BIT;
  if (cfg->cpha) spicon |= SPICON_CPHA_BIT;
  *reg16(HT5023_SPI_BASE, SPICON_OFFSET) = spicon;

  return true;
}

void ht5023_spi_set_cs(bool high) {
  *reg16(HT5023_SPI_BASE, SPISSN_OFFSET) = high ? SPISSN_SSN0_BIT : 0u;
}

uint8_t ht5023_spi_transfer_byte(uint8_t tx) {
  volatile uint16_t* spsta = reg16(HT5023_SPI_BASE, SPISTA_OFFSET);
  volatile uint16_t* spdat = reg16(HT5023_SPI_BASE, SPIDAT_OFFSET);

  *spdat = tx;
  for (uint32_t i = 0u; i < HT5023_SPI_POLL_TIMEOUT; ++i) {
    if (((*spsta) & SPISTA_SPIF_BIT) != 0u) {
      (void)*spsta;                 // datasheet: reading SPISTA/SPIDAT clears flags
      return (uint8_t)(*spdat & 0xFFu);
    }
  }
  return 0x00u;  // timeout: SPI peripheral did not complete the transfer
}

bool ht5023_spi_wait_tx_complete(void) {
  volatile uint16_t* spsta = reg16(HT5023_SPI_BASE, SPISTA_OFFSET);
  volatile uint16_t* spdat = reg16(HT5023_SPI_BASE, SPIDAT_OFFSET);

  for (uint32_t i = 0u; i < HT5023_SPI_POLL_TIMEOUT; ++i) {
    if (((*spsta) & SPISTA_SPIF_BIT) != 0u) {
      (void)*spsta;
      (void)*spdat;
      return true;
    }
  }
  return false;
}

void ht5023_spi_transfer(const uint8_t* tx, uint8_t* rx, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    const uint8_t t = tx ? tx[i] : 0xFFu;
    const uint8_t r = ht5023_spi_transfer_byte(t);
    if (rx) rx[i] = r;
  }
}
