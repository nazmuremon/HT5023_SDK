// ht5023_gfx.c — small 2D graphics primitives for RGB565 surfaces
#include "ht5023_gfx.h"

#include <stddef.h>

static int16_t iabs16(int16_t v) { return (v < 0) ? (int16_t)-v : v; }

static void st7789_surface_draw_pixel(void* user, int16_t x, int16_t y, uint16_t color) {
  ht5023_st7789_t* lcd = (ht5023_st7789_t*)user;
  if (!lcd || x < 0 || y < 0) return;
  if (x >= (int16_t)lcd->width || y >= (int16_t)lcd->height) return;
  ht5023_st7789_fill_rect(lcd, (uint16_t)x, (uint16_t)y, 1u, 1u, color);
}

static void st7789_surface_fill_rect(void* user,
                                     int16_t x,
                                     int16_t y,
                                     uint16_t w,
                                     uint16_t h,
                                     uint16_t color) {
  ht5023_st7789_t* lcd = (ht5023_st7789_t*)user;
  if (!lcd || x < 0 || y < 0) return;
  ht5023_st7789_fill_rect(lcd, (uint16_t)x, (uint16_t)y, w, h, color);
}

static void st7789_surface_blit_rgb565(void* user,
                                       int16_t x,
                                       int16_t y,
                                       uint16_t w,
                                       uint16_t h,
                                       const uint16_t* pixels,
                                       uint16_t stride) {
  ht5023_st7789_t* lcd = (ht5023_st7789_t*)user;
  if (!lcd || !pixels || x < 0 || y < 0 || w == 0u || h == 0u) return;
  if (x >= (int16_t)lcd->width || y >= (int16_t)lcd->height) return;

  if (stride == w) {
    ht5023_st7789_set_addr_window(lcd, (uint16_t)x, (uint16_t)y, w, h);
    ht5023_st7789_write_pixels_rgb565(lcd, pixels, (size_t)w * (size_t)h);
    return;
  }

  for (uint16_t row = 0; row < h; ++row) {
    ht5023_st7789_set_addr_window(lcd, (uint16_t)x, (uint16_t)(y + (int16_t)row), w, 1u);
    ht5023_st7789_write_pixels_rgb565(lcd, pixels + ((size_t)row * stride), w);
  }
}

void ht5023_gfx_surface_init_st7789(ht5023_gfx_surface_t* s, ht5023_st7789_t* lcd) {
  if (!s || !lcd) return;
  s->width = (int16_t)lcd->width;
  s->height = (int16_t)lcd->height;
  s->user = lcd;
  s->draw_pixel = st7789_surface_draw_pixel;
  s->fill_rect = st7789_surface_fill_rect;
  s->blit_rgb565 = st7789_surface_blit_rgb565;
}

bool ht5023_gfx_in_bounds(const ht5023_gfx_surface_t* s, int16_t x, int16_t y) {
  return s && x >= 0 && y >= 0 && x < s->width && y < s->height;
}

void ht5023_gfx_draw_pixel(const ht5023_gfx_surface_t* s, int16_t x, int16_t y, uint16_t color) {
  if (!s || !s->draw_pixel) return;
  if (!ht5023_gfx_in_bounds(s, x, y)) return;
  s->draw_pixel(s->user, x, y, color);
}

void ht5023_gfx_draw_hline(const ht5023_gfx_surface_t* s, int16_t x, int16_t y, int16_t w, uint16_t color) {
  if (!s || w <= 0 || y < 0 || y >= s->height) return;
  if (x < 0) { w += x; x = 0; }
  if (x >= s->width || w <= 0) return;
  if (x + w > s->width) w = (int16_t)(s->width - x);
  if (w <= 0) return;

  if (s->fill_rect) {
    s->fill_rect(s->user, x, y, (uint16_t)w, 1u, color);
    return;
  }
  for (int16_t i = 0; i < w; ++i) {
    s->draw_pixel(s->user, (int16_t)(x + i), y, color);
  }
}

void ht5023_gfx_draw_vline(const ht5023_gfx_surface_t* s, int16_t x, int16_t y, int16_t h, uint16_t color) {
  if (!s || h <= 0 || x < 0 || x >= s->width) return;
  if (y < 0) { h += y; y = 0; }
  if (y >= s->height || h <= 0) return;
  if (y + h > s->height) h = (int16_t)(s->height - y);
  if (h <= 0) return;

  if (s->fill_rect) {
    s->fill_rect(s->user, x, y, 1u, (uint16_t)h, color);
    return;
  }
  for (int16_t i = 0; i < h; ++i) {
    s->draw_pixel(s->user, x, (int16_t)(y + i), color);
  }
}

void ht5023_gfx_fill_rect(const ht5023_gfx_surface_t* s,
                          int16_t x,
                          int16_t y,
                          int16_t w,
                          int16_t h,
                          uint16_t color) {
  if (!s || w <= 0 || h <= 0) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x >= s->width || y >= s->height || w <= 0 || h <= 0) return;
  if (x + w > s->width) w = (int16_t)(s->width - x);
  if (y + h > s->height) h = (int16_t)(s->height - y);
  if (w <= 0 || h <= 0) return;

  if (s->fill_rect) {
    s->fill_rect(s->user, x, y, (uint16_t)w, (uint16_t)h, color);
    return;
  }

  for (int16_t row = 0; row < h; ++row) {
    ht5023_gfx_draw_hline(s, x, (int16_t)(y + row), w, color);
  }
}

void ht5023_gfx_draw_rect(const ht5023_gfx_surface_t* s,
                          int16_t x,
                          int16_t y,
                          int16_t w,
                          int16_t h,
                          uint16_t color) {
  if (!s || w <= 0 || h <= 0) return;
  ht5023_gfx_draw_hline(s, x, y, w, color);
  if (h > 1) ht5023_gfx_draw_hline(s, x, (int16_t)(y + h - 1), w, color);
  if (h > 2) {
    ht5023_gfx_draw_vline(s, x, (int16_t)(y + 1), (int16_t)(h - 2), color);
    if (w > 1) ht5023_gfx_draw_vline(s, (int16_t)(x + w - 1), (int16_t)(y + 1), (int16_t)(h - 2), color);
  }
}

void ht5023_gfx_draw_line(const ht5023_gfx_surface_t* s,
                          int16_t x0,
                          int16_t y0,
                          int16_t x1,
                          int16_t y1,
                          uint16_t color) {
  if (!s) return;
  // Use int32_t for the Bresenham error accumulator to prevent overflow when
  // dx or dy approach the int16_t range (e.g. a 240-pixel diagonal line has
  // err up to 240 and e2 = 2*err = 480, which fits int16 — but defensive code
  // avoids relying on display-specific size assumptions).
  int32_t dx = (int32_t)iabs16((int16_t)(x1 - x0));
  int16_t sx = x0 < x1 ? 1 : -1;
  int32_t dy = -(int32_t)iabs16((int16_t)(y1 - y0));
  int16_t sy = y0 < y1 ? 1 : -1;
  int32_t err = dx + dy;

  for (;;) {
    ht5023_gfx_draw_pixel(s, x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int32_t e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 = (int16_t)(x0 + sx); }
    if (e2 <= dx) { err += dx; y0 = (int16_t)(y0 + sy); }
  }
}

void ht5023_gfx_draw_circle(const ht5023_gfx_surface_t* s, int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  if (!s || r < 0) return;
  int16_t x = r;
  int16_t y = 0;
  int16_t err = 1 - r;
  while (x >= y) {
    ht5023_gfx_draw_pixel(s, (int16_t)(cx + x), (int16_t)(cy + y), color);
    ht5023_gfx_draw_pixel(s, (int16_t)(cx + y), (int16_t)(cy + x), color);
    ht5023_gfx_draw_pixel(s, (int16_t)(cx - y), (int16_t)(cy + x), color);
    ht5023_gfx_draw_pixel(s, (int16_t)(cx - x), (int16_t)(cy + y), color);
    ht5023_gfx_draw_pixel(s, (int16_t)(cx - x), (int16_t)(cy - y), color);
    ht5023_gfx_draw_pixel(s, (int16_t)(cx - y), (int16_t)(cy - x), color);
    ht5023_gfx_draw_pixel(s, (int16_t)(cx + y), (int16_t)(cy - x), color);
    ht5023_gfx_draw_pixel(s, (int16_t)(cx + x), (int16_t)(cy - y), color);
    ++y;
    if (err < 0) err = (int16_t)(err + 2 * y + 1);
    else { --x; err = (int16_t)(err + 2 * (y - x) + 1); }
  }
}

void ht5023_gfx_fill_circle(const ht5023_gfx_surface_t* s, int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  if (!s || r < 0) return;
  int16_t x = r;
  int16_t y = 0;
  int16_t err = 1 - r;
  while (x >= y) {
    ht5023_gfx_draw_hline(s, (int16_t)(cx - x), (int16_t)(cy + y), (int16_t)(2 * x + 1), color);
    ht5023_gfx_draw_hline(s, (int16_t)(cx - x), (int16_t)(cy - y), (int16_t)(2 * x + 1), color);
    ht5023_gfx_draw_hline(s, (int16_t)(cx - y), (int16_t)(cy + x), (int16_t)(2 * y + 1), color);
    ht5023_gfx_draw_hline(s, (int16_t)(cx - y), (int16_t)(cy - x), (int16_t)(2 * y + 1), color);
    ++y;
    if (err < 0) err = (int16_t)(err + 2 * y + 1);
    else { --x; err = (int16_t)(err + 2 * (y - x) + 1); }
  }
}

void ht5023_gfx_draw_triangle(const ht5023_gfx_surface_t* s,
                              int16_t x0,
                              int16_t y0,
                              int16_t x1,
                              int16_t y1,
                              int16_t x2,
                              int16_t y2,
                              uint16_t color) {
  ht5023_gfx_draw_line(s, x0, y0, x1, y1, color);
  ht5023_gfx_draw_line(s, x1, y1, x2, y2, color);
  ht5023_gfx_draw_line(s, x2, y2, x0, y0, color);
}

void ht5023_gfx_blit_rgb565(const ht5023_gfx_surface_t* s,
                            int16_t x,
                            int16_t y,
                            uint16_t w,
                            uint16_t h,
                            const uint16_t* pixels,
                            uint16_t stride) {
  if (!s || !pixels || w == 0u || h == 0u) return;
  if (stride == 0u) stride = w;
  if (s->blit_rgb565) {
    s->blit_rgb565(s->user, x, y, w, h, pixels, stride);
    return;
  }
  for (uint16_t row = 0; row < h; ++row) {
    for (uint16_t col = 0; col < w; ++col) {
      ht5023_gfx_draw_pixel(s, (int16_t)(x + (int16_t)col), (int16_t)(y + (int16_t)row),
                            pixels[(size_t)row * stride + col]);
    }
  }
}
