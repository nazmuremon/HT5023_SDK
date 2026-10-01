#include "SPI.h"

#include "ht5023_spi.h"

SPIClass SPI;

static uint8_t ht5023_spi_clock_to_spr(uint32_t clock_hz) {
  if (clock_hz == 0u) return 6u;

  for (uint8_t spr = 0u; spr <= 6u; ++spr) {
    const uint32_t divider = (uint32_t)(2u << spr);
    const uint32_t actual = (uint32_t)F_CPU / divider;
    if (actual <= clock_hz) return spr;
  }
  return 6u;
}

SPISettings::SPISettings(uint32_t clock, uint8_t bit_order, uint8_t data_mode)
    : spr_(ht5023_spi_clock_to_spr(clock)),
      bit_order_(bit_order),
      cpol_((data_mode & SPI_MODE2) != 0u),
      cpha_((data_mode & SPI_MODE1) != 0u) {
}

void SPIClass::begin(void) {
  if (begun_) return;
  applySettings(current_settings_);
  begun_ = true;
}

void SPIClass::end(void) {
  ht5023_spi_end();
  begun_ = false;
}

void SPIClass::beginTransaction(const SPISettings& settings) {
  current_settings_ = settings;
  applySettings(current_settings_);
  begun_ = true;
}

void SPIClass::endTransaction(void) {
  ht5023_spi_set_cs(true);
}

void SPIClass::setBitOrder(uint8_t bit_order) {
  current_settings_ = SPISettings((uint32_t)F_CPU / (2u << current_settings_.spr_), bit_order,
                                  (current_settings_.cpol_ ? SPI_MODE2 : 0u) |
                                      (current_settings_.cpha_ ? SPI_MODE1 : 0u));
  applySettings(current_settings_);
}

void SPIClass::setDataMode(uint8_t data_mode) {
  current_settings_ = SPISettings((uint32_t)F_CPU / (2u << current_settings_.spr_),
                                  current_settings_.bit_order_, data_mode);
  applySettings(current_settings_);
}

void SPIClass::setClockDivider(uint8_t divider) {
  uint32_t clock = (divider == 0u) ? (uint32_t)F_CPU : ((uint32_t)F_CPU / divider);
  current_settings_ = SPISettings(clock, current_settings_.bit_order_,
                                  (current_settings_.cpol_ ? SPI_MODE2 : 0u) |
                                      (current_settings_.cpha_ ? SPI_MODE1 : 0u));
  applySettings(current_settings_);
}

uint8_t SPIClass::transfer(uint8_t data) {
  if (!begun_) begin();

  uint8_t out = data;
  if (current_settings_.bit_order_ == LSBFIRST) {
    out = (uint8_t)((out * 0x0202020202ULL & 0x010884422010ULL) % 1023u);
  }

  uint8_t in = ht5023_spi_transfer_byte(out);
  if (current_settings_.bit_order_ == LSBFIRST) {
    in = (uint8_t)((in * 0x0202020202ULL & 0x010884422010ULL) % 1023u);
  }
  return in;
}

uint16_t SPIClass::transfer16(uint16_t data) {
  uint16_t value = 0u;
  value = (uint16_t)(transfer((uint8_t)(data >> 8)) << 8);
  value |= transfer((uint8_t)(data & 0xFFu));
  return value;
}

void SPIClass::transfer(void* buffer, size_t count) {
  if (!buffer || count == 0u) return;
  uint8_t* bytes = (uint8_t*)buffer;
  for (size_t i = 0; i < count; ++i) {
    bytes[i] = transfer(bytes[i]);
  }
}

void SPIClass::usingInterrupt(uint8_t interrupt_number) {
  (void)interrupt_number;
}

void SPIClass::notUsingInterrupt(uint8_t interrupt_number) {
  (void)interrupt_number;
}

void SPIClass::applySettings(const SPISettings& settings) {
  ht5023_spi_master_cfg_t cfg = {
      .spr = settings.spr_,
      .cpol = settings.cpol_,
      .cpha = settings.cpha_,
  };
  (void)ht5023_spi_begin(&cfg);
  ht5023_spi_set_cs(true);
}
