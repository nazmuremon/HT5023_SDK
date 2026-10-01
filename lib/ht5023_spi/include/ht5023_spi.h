// ht5023_spi.h — HT5023 SPI master polling driver
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t spr;      // 0..7 => Fsys/2..no-clock (datasheet SPR[2:0]); typical 1 => Fsys/4
  bool cpol;        // idle clock polarity
  bool cpha;        // clock phase
} ht5023_spi_master_cfg_t;

// Configures PC4/PC5/PC6/PC7 as SPI_MOSI/MISO/CLK/CS (first multiplex function).
void ht5023_spi_pins_init_pc4_pc5_pc6_pc7(void);

bool ht5023_spi_master_init(const ht5023_spi_master_cfg_t* cfg);
void ht5023_spi_set_cs(bool high);  // controls SPISSN.SSN0 when SPI enabled (master mode)

// Transfer one byte. Polls SPIF until the hardware sets it or a timeout expires.
// Returns the received byte, or 0x00 on timeout.
uint8_t ht5023_spi_transfer_byte(uint8_t tx);

// Waits for the current SPI byte to finish shifting out and clears SPIF via
// the documented SPISTA/SPIDAT read sequence.
bool ht5023_spi_wait_tx_complete(void);

// Transfer a block. tx may be NULL (sends 0xFF bytes). rx may be NULL (discards received).
void ht5023_spi_transfer(const uint8_t* tx, uint8_t* rx, size_t len);

#ifdef __cplusplus
}
#endif
