// ht5023_gfx.h — 2D graphics primitives for RGB565 surfaces
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ht5023_st7789.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ht5023_gfx_draw_pixel_fn)(void* user, int16_t x, int16_t y, uint16_t color);
typedef void (*ht5023_gfx_fill_rect_fn)(void* user,
                                        int16_t x,
                                        int16_t y,
                                        uint16_t w,
                                        uint16_t h,
                                        uint16_t color);
typedef void (*ht5023_gfx_blit_rgb565_fn)(void* user,
                                          int16_t x,
                                          int16_t y,
                                          uint16_t w,
                                          uint16_t h,
                                          const uint16_t* pixels,
                                          uint16_t stride);

typedef struct {
  int16_t width;
  int16_t height;
  void* user;
  ht5023_gfx_draw_pixel_fn draw_pixel;
  ht5023_gfx_fill_rect_fn fill_rect;      // optional fast path
  ht5023_gfx_blit_rgb565_fn blit_rgb565;  // optional fast path
} ht5023_gfx_surface_t;

void ht5023_gfx_surface_init_st7789(ht5023_gfx_surface_t* s, ht5023_st7789_t* lcd);

bool ht5023_gfx_in_bounds(const ht5023_gfx_surface_t* s, int16_t x, int16_t y);
void ht5023_gfx_draw_pixel(const ht5023_gfx_surface_t* s, int16_t x, int16_t y, uint16_t color);
void ht5023_gfx_draw_hline(const ht5023_gfx_surface_t* s, int16_t x, int16_t y, int16_t w, uint16_t color);
void ht5023_gfx_draw_vline(const ht5023_gfx_surface_t* s, int16_t x, int16_t y, int16_t h, uint16_t color);
void ht5023_gfx_fill_rect(const ht5023_gfx_surface_t* s,
                          int16_t x,
                          int16_t y,
                          int16_t w,
                          int16_t h,
                          uint16_t color);
void ht5023_gfx_draw_rect(const ht5023_gfx_surface_t* s,
                          int16_t x,
                          int16_t y,
                          int16_t w,
                          int16_t h,
                          uint16_t color);
void ht5023_gfx_draw_line(const ht5023_gfx_surface_t* s,
                          int16_t x0,
                          int16_t y0,
                          int16_t x1,
                          int16_t y1,
                          uint16_t color);
void ht5023_gfx_draw_circle(const ht5023_gfx_surface_t* s, int16_t cx, int16_t cy, int16_t r, uint16_t color);
void ht5023_gfx_fill_circle(const ht5023_gfx_surface_t* s, int16_t cx, int16_t cy, int16_t r, uint16_t color);
void ht5023_gfx_draw_triangle(const ht5023_gfx_surface_t* s,
                              int16_t x0,
                              int16_t y0,
                              int16_t x1,
                              int16_t y1,
                              int16_t x2,
                              int16_t y2,
                              uint16_t color);

void ht5023_gfx_blit_rgb565(const ht5023_gfx_surface_t* s,
                            int16_t x,
                            int16_t y,
                            uint16_t w,
                            uint16_t h,
                            const uint16_t* pixels,
                            uint16_t stride);

#ifdef __cplusplus
}
#endif
