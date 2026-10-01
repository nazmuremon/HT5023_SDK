#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t sysclk_hz;
  uint32_t bus_hz;
} ht5023_i2c_master_cfg_t;

void ht5023_i2c_pins_init_pc13_pc14(void);
bool ht5023_i2c_master_init(const ht5023_i2c_master_cfg_t* cfg);
bool ht5023_i2c_begin(const ht5023_i2c_master_cfg_t* cfg);
bool ht5023_i2c_probe_7bit(uint8_t addr7);
bool ht5023_i2c_write(uint8_t addr7, const uint8_t* data, size_t len);
bool ht5023_i2c_read(uint8_t addr7, uint8_t* data, size_t len);
bool ht5023_i2c_write_read(uint8_t addr7,
                           const uint8_t* wr_data, size_t wr_len,
                           uint8_t* rd_data, size_t rd_len);

#ifdef __cplusplus
}
#endif
