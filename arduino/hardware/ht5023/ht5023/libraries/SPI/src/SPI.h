#pragma once

#include <Arduino.h>

#define SPI_MODE0 0x00u
#define SPI_MODE1 0x04u
#define SPI_MODE2 0x08u
#define SPI_MODE3 0x0Cu

#define SPI_CLOCK_DIV2   2u
#define SPI_CLOCK_DIV4   4u
#define SPI_CLOCK_DIV8   8u
#define SPI_CLOCK_DIV16  16u
#define SPI_CLOCK_DIV32  32u
#define SPI_CLOCK_DIV64  64u
#define SPI_CLOCK_DIV128 128u

class SPISettings {
 public:
  SPISettings(uint32_t clock = 1000000u, uint8_t bit_order = MSBFIRST, uint8_t data_mode = SPI_MODE0);

 private:
  friend class SPIClass;
  uint8_t spr_ = 1u;
  uint8_t bit_order_ = MSBFIRST;
  bool cpol_ = false;
  bool cpha_ = false;
};

class SPIClass {
 public:
  void begin(void);
  void end(void);

  void beginTransaction(const SPISettings& settings);
  void endTransaction(void);

  void setBitOrder(uint8_t bit_order);
  void setDataMode(uint8_t data_mode);
  void setClockDivider(uint8_t divider);

  uint8_t transfer(uint8_t data);
  uint16_t transfer16(uint16_t data);
  void transfer(void* buffer, size_t count);

  void usingInterrupt(uint8_t interrupt_number);
  void notUsingInterrupt(uint8_t interrupt_number);

 private:
  void applySettings(const SPISettings& settings);

  SPISettings current_settings_;
  bool begun_ = false;
};

extern SPIClass SPI;
