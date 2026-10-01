// ht5023_xpt2046.c — XPT2046 resistive touch controller helper
#include "ht5023_xpt2046.h"

#include <string.h>

// XPT2046 command bytes (12-bit, differential mode, power-down between conversions)
#define XPT2046_CMD_Y   0x90u
#define XPT2046_CMD_X   0xD0u
#define XPT2046_CMD_Z1  0xB0u
#define XPT2046_CMD_Z2  0xC0u

static uint16_t clamp_map_u16(uint16_t v,
                              uint16_t in_min,
                              uint16_t in_max,
                              uint16_t out_max,
                              bool invert) {
  if (out_max == 0u) return 0u;
  if (in_max <= in_min) return 0u;
  if (v < in_min) v = in_min;
  if (v > in_max) v = in_max;
  uint32_t num = (uint32_t)(v - in_min) * (uint32_t)(out_max - 1u);
  uint32_t den = (uint32_t)(in_max - in_min);
  uint16_t out = (uint16_t)(num / den);
  return invert ? (uint16_t)((out_max - 1u) - out) : out;
}

bool ht5023_xpt2046_init(ht5023_xpt2046_t* touch, const ht5023_xpt2046_config_t* cfg) {
  if (!touch || !cfg) return false;
  if (!cfg->spi_transfer_byte || !cfg->cs_write) return false;
  memset(touch, 0, sizeof(*touch));
  touch->cfg = *cfg;
  touch->cfg.cs_write(true, touch->cfg.user);
  return true;
}

bool ht5023_xpt2046_pen_is_down(const ht5023_xpt2046_t* touch) {
  if (!touch) return false;
  if (!touch->cfg.penirq_read) return true;  // unknown => allow sampling
  return !touch->cfg.penirq_read(touch->cfg.user);  // active-low PENIRQ
}

uint16_t ht5023_xpt2046_read_channel_raw(const ht5023_xpt2046_t* touch, uint8_t command) {
  if (!touch) return 0u;
  touch->cfg.cs_write(false, touch->cfg.user);
  (void)touch->cfg.spi_transfer_byte(command, touch->cfg.user);
  const uint8_t hi = touch->cfg.spi_transfer_byte(0x00u, touch->cfg.user);
  const uint8_t lo = touch->cfg.spi_transfer_byte(0x00u, touch->cfg.user);
  touch->cfg.cs_write(true, touch->cfg.user);
  return (uint16_t)((((uint16_t)hi << 8) | lo) >> 3);
}

bool ht5023_xpt2046_read_raw_sample(const ht5023_xpt2046_t* touch, ht5023_xpt2046_raw_sample_t* out) {
  if (!touch || !out) return false;
  out->touched = ht5023_xpt2046_pen_is_down(touch);
  if (!out->touched && touch->cfg.penirq_read) {
    out->x_raw = out->y_raw = out->z1_raw = out->z2_raw = 0u;
    return false;
  }

  // Average 3 samples for X/Y to reduce noise.
  uint32_t xs = 0u;
  uint32_t ys = 0u;
  for (uint8_t i = 0; i < 3u; ++i) {
    xs += ht5023_xpt2046_read_channel_raw(touch, XPT2046_CMD_X);
    ys += ht5023_xpt2046_read_channel_raw(touch, XPT2046_CMD_Y);
  }
  out->x_raw = (uint16_t)(xs / 3u);
  out->y_raw = (uint16_t)(ys / 3u);
  out->z1_raw = ht5023_xpt2046_read_channel_raw(touch, XPT2046_CMD_Z1);
  out->z2_raw = ht5023_xpt2046_read_channel_raw(touch, XPT2046_CMD_Z2);

  // Basic pressure sanity filter.
  out->touched = (out->z1_raw > 20u) && (out->x_raw > 50u || out->y_raw > 50u);
  return out->touched;
}

bool ht5023_xpt2046_read_point(const ht5023_xpt2046_t* touch, ht5023_xpt2046_point_t* out) {
  if (!touch || !out) return false;
  ht5023_xpt2046_raw_sample_t raw;
  if (!ht5023_xpt2046_read_raw_sample(touch, &raw)) {
    out->touched = false;
    out->x = 0u;
    out->y = 0u;
    return false;
  }

  uint16_t rx = raw.x_raw;
  uint16_t ry = raw.y_raw;
  if (touch->cfg.swap_xy) {
    uint16_t t = rx; rx = ry; ry = t;
  }

  out->touched = true;
  if (touch->cfg.out_width && touch->cfg.out_height &&
      touch->cfg.raw_x_max > touch->cfg.raw_x_min &&
      touch->cfg.raw_y_max > touch->cfg.raw_y_min) {
    out->x = clamp_map_u16(rx,
                           touch->cfg.raw_x_min,
                           touch->cfg.raw_x_max,
                           touch->cfg.out_width,
                           touch->cfg.invert_x);
    out->y = clamp_map_u16(ry,
                           touch->cfg.raw_y_min,
                           touch->cfg.raw_y_max,
                           touch->cfg.out_height,
                           touch->cfg.invert_y);
  } else {
    out->x = rx;
    out->y = ry;
  }
  return true;
}

