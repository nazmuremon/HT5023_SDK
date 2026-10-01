#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t spr;
  bool cpol;
  bool cpha;
} ht5023_spi_master_cfg_t;

void ht5023_spi_pins_init_pc4_pc5_pc6_pc7(void);
bool ht5023_spi_master_init(const ht5023_spi_master_cfg_t* cfg);
bool ht5023_spi_begin(const ht5023_spi_master_cfg_t* cfg);
void ht5023_spi_end(void);
bool ht5023_spi_is_active(void);
void ht5023_spi_set_cs(bool high);
uint8_t ht5023_spi_transfer_byte(uint8_t tx);
bool ht5023_spi_wait_tx_complete(void);
void ht5023_spi_transfer(const uint8_t* tx, uint8_t* rx, size_t len);

#ifdef __cplusplus
}
#endif
