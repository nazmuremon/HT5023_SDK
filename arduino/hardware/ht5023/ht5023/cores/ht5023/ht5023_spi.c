#include "ht5023_spi.h"

#include "ht5023_device.h"

#define HT5023_CMU_BASE            0x4000F000u
#define HT5023_CMU_WPREG_OFFSET    0x00u
#define HT5023_CMU_CLKCTRL0_OFFSET 0x2Cu
#define HT5023_WPREG_UNLOCK_KEY    0xA55Au

#define CLKCTRL0_SPI_EN_BIT        (1u << 2)

#define HT5023_SPI_BASE            0x4000B000u
#define SPICON_OFFSET              0x00u
#define SPISTA_OFFSET              0x04u
#define SPIDAT_OFFSET              0x08u
#define SPISSN_OFFSET              0x0Cu

#define SPICON_SSDIS_BIT           (1u << 7)
#define SPICON_SPR_SHIFT           4u
#define SPICON_CPHA_BIT            (1u << 3)
#define SPICON_CPOL_BIT            (1u << 2)
#define SPICON_MSTR_BIT            (1u << 1)
#define SPICON_SPI_EN_BIT          (1u << 0)

#define SPISTA_SPIF_BIT            (1u << 3)
#define SPISSN_SSN0_BIT            (1u << 0)

#define HT5023_SPI_POLL_TIMEOUT    60000u

static bool g_spi_active = false;

static void spi_enable_clock(void) {
  volatile uint16_t* wpreg = ht5023_reg16(HT5023_CMU_BASE, HT5023_CMU_WPREG_OFFSET);
  volatile uint16_t* clk0 = ht5023_reg16(HT5023_CMU_BASE, HT5023_CMU_CLKCTRL0_OFFSET);

  *wpreg = HT5023_WPREG_UNLOCK_KEY;
  *clk0 = (uint16_t)(*clk0 | (uint16_t)CLKCTRL0_SPI_EN_BIT);
  *wpreg = 0x0000u;
}

void ht5023_spi_pins_init_pc4_pc5_pc6_pc7(void) {
  const uint16_t pins = (uint16_t)((1u << 4) | (1u << 5) | (1u << 6) | (1u << 7));
  volatile uint16_t* iocfg = ht5023_reg16(HT5023_GPIOC_BASE, HT5023_GPIO_IOCFG_OFFSET);
  volatile uint16_t* afcfg = ht5023_reg16(HT5023_GPIOC_BASE, HT5023_GPIO_AFCFG_OFFSET);

  *iocfg |= pins;
  *afcfg &= (uint16_t)~pins;
}

bool ht5023_spi_master_init(const ht5023_spi_master_cfg_t* cfg) {
  if (!cfg) return false;

  const uint8_t spr = (uint8_t)(cfg->spr & 0x7u);

  spi_enable_clock();
  ht5023_spi_pins_init_pc4_pc5_pc6_pc7();

  *ht5023_reg16(HT5023_SPI_BASE, SPISSN_OFFSET) = SPISSN_SSN0_BIT;

  uint16_t spicon = (uint16_t)(SPICON_SPI_EN_BIT | SPICON_MSTR_BIT | SPICON_SSDIS_BIT);
  spicon |= (uint16_t)((uint16_t)spr << SPICON_SPR_SHIFT);
  if (cfg->cpol) spicon |= SPICON_CPOL_BIT;
  if (cfg->cpha) spicon |= SPICON_CPHA_BIT;
  *ht5023_reg16(HT5023_SPI_BASE, SPICON_OFFSET) = spicon;

  g_spi_active = true;
  return true;
}

bool ht5023_spi_begin(const ht5023_spi_master_cfg_t* cfg) {
  return ht5023_spi_master_init(cfg);
}

void ht5023_spi_end(void) {
  *ht5023_reg16(HT5023_SPI_BASE, SPICON_OFFSET) = 0u;
  g_spi_active = false;
}

bool ht5023_spi_is_active(void) {
  return g_spi_active;
}

void ht5023_spi_set_cs(bool high) {
  *ht5023_reg16(HT5023_SPI_BASE, SPISSN_OFFSET) = high ? SPISSN_SSN0_BIT : 0u;
}

uint8_t ht5023_spi_transfer_byte(uint8_t tx) {
  volatile uint16_t* spsta = ht5023_reg16(HT5023_SPI_BASE, SPISTA_OFFSET);
  volatile uint16_t* spdat = ht5023_reg16(HT5023_SPI_BASE, SPIDAT_OFFSET);

  *spdat = tx;
  for (uint32_t i = 0u; i < HT5023_SPI_POLL_TIMEOUT; ++i) {
    if (((*spsta) & SPISTA_SPIF_BIT) != 0u) {
      (void)*spsta;
      return (uint8_t)(*spdat & 0xFFu);
    }
  }

  return 0u;
}

bool ht5023_spi_wait_tx_complete(void) {
  volatile uint16_t* spsta = ht5023_reg16(HT5023_SPI_BASE, SPISTA_OFFSET);
  volatile uint16_t* spdat = ht5023_reg16(HT5023_SPI_BASE, SPIDAT_OFFSET);
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
    const uint8_t out = tx ? tx[i] : 0xFFu;
    const uint8_t in = ht5023_spi_transfer_byte(out);
    if (rx) rx[i] = in;
  }
}
