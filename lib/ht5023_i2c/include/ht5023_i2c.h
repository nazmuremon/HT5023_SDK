// ht5023_i2c.h — HT5023 I2C master polling driver
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t sysclk_hz;     // Fsys used in CR formula
  uint32_t bus_hz;        // e.g. 100000
} ht5023_i2c_master_cfg_t;

// Datasheet pin table for HT5023 shows I2C on PC13(SCL)/PC14(SDA).
void ht5023_i2c_pins_init_pc13_pc14(void);

bool ht5023_i2c_master_init(const ht5023_i2c_master_cfg_t* cfg);

// Probe 7-bit address by issuing START + SLA+W and checking ACK.
bool ht5023_i2c_probe_7bit(uint8_t addr7);

// Basic write/read transactions.
bool ht5023_i2c_write(uint8_t addr7, const uint8_t* data, size_t len);
bool ht5023_i2c_read(uint8_t addr7, uint8_t* data, size_t len);

// Combined write-then-read with a repeated START (no bus release between phases).
// Useful for register-addressed reads common in sensor/EEPROM protocols.
// wr_len may be 0 (degenerates to a plain read). rd_len may be 0 (plain write).
bool ht5023_i2c_write_read(uint8_t addr7,
                           const uint8_t* wr_data, size_t wr_len,
                           uint8_t*       rd_data, size_t rd_len);

#ifdef __cplusplus
}
#endif
