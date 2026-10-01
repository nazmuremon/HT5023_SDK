// ht5023_st7789.h — ST7789 LCD controller driver (RGB565, callback-based bus)
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ht5023_dma.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HT5023_ST7789_ROTATION_0 = 0,
  HT5023_ST7789_ROTATION_90 = 1,
  HT5023_ST7789_ROTATION_180 = 2,
  HT5023_ST7789_ROTATION_270 = 3,
} ht5023_st7789_rotation_t;

typedef void (*ht5023_st7789_write_fn)(const uint8_t* bytes, size_t len, void* user);
typedef void (*ht5023_st7789_pin_write_fn)(bool high, void* user);
typedef void (*ht5023_st7789_delay_ms_fn)(uint32_t ms, void* user);
typedef bool (*ht5023_st7789_dma_wait_fn)(void* user);
typedef bool (*ht5023_st7789_dma_prime_fn)(uint8_t byte, void* user);

typedef struct {
  ht5023_dma_channel_t channel;
  ht5023_dma_request_t request;
  uintptr_t tx_reg_addr;
  ht5023_dma_channel_t rx_channel;
  ht5023_dma_request_t rx_request;
  uintptr_t rx_reg_addr;
  uint32_t timeout_polls;
  ht5023_st7789_dma_prime_fn prime_tx;
  ht5023_st7789_dma_wait_fn wait_complete;
  size_t min_bytes;
  uint8_t* stage_buffer;        // DMA staging storage used by the library transport.
  size_t stage_buffer_len;      // Size in bytes; DMA path needs at least 2 bytes.
  uint8_t* rx_stage_buffer;     // Optional SPI RX drain buffer for DMA-driven SPI bursts.
  size_t rx_stage_buffer_len;   // Size in bytes; must hold one word per transferred byte.
  uint8_t max_fail_streak;  // 0 keeps DMA enabled after failures.
} ht5023_st7789_dma_config_t;

typedef struct {
  // Required callbacks.
  // Provide `write` for the normal/polling path. If omitted, DMA must be configured.
  ht5023_st7789_write_fn write;
  ht5023_st7789_pin_write_fn dc_write;

  // Optional callbacks.
  ht5023_st7789_pin_write_fn cs_write;
  ht5023_st7789_pin_write_fn rst_write;
  ht5023_st7789_delay_ms_fn delay_ms;
  const ht5023_st7789_dma_config_t* dma;

  void* user;

  uint16_t width;
  uint16_t height;
  uint16_t x_offset;
  uint16_t y_offset;
  bool invert_colors;
  bool bgr_order;
} ht5023_st7789_config_t;

typedef struct {
  bool configured;
  bool enabled;
  ht5023_st7789_dma_config_t cfg;
  uint32_t success_count;
  uint32_t fallback_count;
  uint8_t fail_streak;
} ht5023_st7789_dma_state_t;

typedef struct {
  ht5023_st7789_config_t cfg;
  ht5023_st7789_rotation_t rotation;
  uint16_t width;
  uint16_t height;
  ht5023_st7789_dma_state_t dma;
} ht5023_st7789_t;

typedef struct {
  bool configured;
  bool enabled;
  uint32_t success_count;
  uint32_t fallback_count;
  uint8_t fail_streak;
} ht5023_st7789_dma_stats_t;

// Pack R(5), G(6), B(5) channels into a single RGB565 word.
static inline uint16_t ht5023_st7789_color565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)((((uint16_t)r & 0xF8u) << 8) |
                    (((uint16_t)g & 0xFCu) << 3) |
                    (((uint16_t)b & 0xF8u) >> 3));
}

bool ht5023_st7789_init(ht5023_st7789_t* lcd, const ht5023_st7789_config_t* cfg);
void ht5023_st7789_hard_reset(ht5023_st7789_t* lcd);
void ht5023_st7789_set_rotation(ht5023_st7789_t* lcd, ht5023_st7789_rotation_t rotation);
void ht5023_st7789_get_dma_stats(const ht5023_st7789_t* lcd, ht5023_st7789_dma_stats_t* out_stats);
void ht5023_st7789_reset_dma_stats(ht5023_st7789_t* lcd);

void ht5023_st7789_set_addr_window(ht5023_st7789_t* lcd,
                                   uint16_t x,
                                   uint16_t y,
                                   uint16_t w,
                                   uint16_t h);
void ht5023_st7789_write_pixels_rgb565(ht5023_st7789_t* lcd,
                                       const uint16_t* pixels,
                                       size_t count);
void ht5023_st7789_write_color_rgb565(ht5023_st7789_t* lcd, uint16_t color, size_t count);

void ht5023_st7789_fill_screen(ht5023_st7789_t* lcd, uint16_t color);
void ht5023_st7789_fill_rect(ht5023_st7789_t* lcd,
                             uint16_t x,
                             uint16_t y,
                             uint16_t w,
                             uint16_t h,
                             uint16_t color);

#ifdef __cplusplus
}
#endif
