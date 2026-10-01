// ht5023_xpt2046.h — XPT2046 resistive touch controller helper
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t (*ht5023_xpt2046_spi_transfer_byte_fn)(uint8_t tx, void* user);
typedef void (*ht5023_xpt2046_pin_write_fn)(bool high, void* user);
typedef bool (*ht5023_xpt2046_pin_read_fn)(void* user);

typedef struct {
  ht5023_xpt2046_spi_transfer_byte_fn spi_transfer_byte;
  ht5023_xpt2046_pin_write_fn cs_write;
  ht5023_xpt2046_pin_read_fn penirq_read;  // optional, active-low on most modules
  void* user;

  // Optional mapping (raw -> display coordinates)
  uint16_t raw_x_min;
  uint16_t raw_x_max;
  uint16_t raw_y_min;
  uint16_t raw_y_max;
  uint16_t out_width;
  uint16_t out_height;
  bool swap_xy;
  bool invert_x;
  bool invert_y;
} ht5023_xpt2046_config_t;

typedef struct {
  ht5023_xpt2046_config_t cfg;
} ht5023_xpt2046_t;

typedef struct {
  bool touched;
  uint16_t x_raw;
  uint16_t y_raw;
  uint16_t z1_raw;
  uint16_t z2_raw;
} ht5023_xpt2046_raw_sample_t;

typedef struct {
  bool touched;
  uint16_t x;
  uint16_t y;
} ht5023_xpt2046_point_t;

bool ht5023_xpt2046_init(ht5023_xpt2046_t* touch, const ht5023_xpt2046_config_t* cfg);
bool ht5023_xpt2046_pen_is_down(const ht5023_xpt2046_t* touch);

// Read one raw 12-bit ADC sample from the given command channel.
// Drives CS, so touch may not be const in terms of hardware state.
uint16_t ht5023_xpt2046_read_channel_raw(const ht5023_xpt2046_t* touch, uint8_t command);

bool ht5023_xpt2046_read_raw_sample(const ht5023_xpt2046_t* touch, ht5023_xpt2046_raw_sample_t* out);
bool ht5023_xpt2046_read_point(const ht5023_xpt2046_t* touch, ht5023_xpt2046_point_t* out);

#ifdef __cplusplus
}
#endif
