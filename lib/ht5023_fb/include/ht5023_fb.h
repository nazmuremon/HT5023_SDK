// ht5023_fb.h — RGB565 framebuffer with dirty-rect tracking
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ht5023_gfx.h"
#include "ht5023_st7789.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint16_t* pixels;
  uint16_t width;
  uint16_t height;
  uint16_t stride;  // pixels per row in backing buffer

  bool dirty;
  uint16_t dirty_x0;
  uint16_t dirty_y0;
  uint16_t dirty_x1;  // exclusive
  uint16_t dirty_y1;  // exclusive
} ht5023_fb_rgb565_t;

bool ht5023_fb_init(ht5023_fb_rgb565_t* fb,
                    uint16_t* pixels,
                    uint16_t width,
                    uint16_t height,
                    uint16_t stride);
void ht5023_fb_clear(ht5023_fb_rgb565_t* fb, uint16_t color);
void ht5023_fb_set_pixel(ht5023_fb_rgb565_t* fb, int16_t x, int16_t y, uint16_t color);
void ht5023_fb_fill_rect(ht5023_fb_rgb565_t* fb, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void ht5023_fb_blit_rgb565(ht5023_fb_rgb565_t* fb,
                           int16_t x,
                           int16_t y,
                           uint16_t w,
                           uint16_t h,
                           const uint16_t* pixels,
                           uint16_t stride);

void ht5023_fb_mark_dirty(ht5023_fb_rgb565_t* fb, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void ht5023_fb_clear_dirty(ht5023_fb_rgb565_t* fb);

// Upload the dirty region to the ST7789 at (dst_x, dst_y) on the display.
// Clears the dirty flag after upload. Returns false if nothing was dirty.
bool ht5023_fb_flush_dirty_to_st7789(ht5023_fb_rgb565_t* fb,
                                     ht5023_st7789_t* lcd,
                                     uint16_t dst_x,
                                     uint16_t dst_y);

void ht5023_fb_make_gfx_surface(ht5023_fb_rgb565_t* fb, ht5023_gfx_surface_t* out_surface);

#ifdef __cplusplus
}
#endif
