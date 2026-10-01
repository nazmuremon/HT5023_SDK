// ht5023_fb.c — RGB565 framebuffer tile with dirty-rect tracking
#include "ht5023_fb.h"

#include <stddef.h>

static void fb_mark_dirty_clip(ht5023_fb_rgb565_t* fb, int16_t x, int16_t y, int16_t w, int16_t h) {
  if (!fb || w <= 0 || h <= 0) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x >= (int16_t)fb->width || y >= (int16_t)fb->height || w <= 0 || h <= 0) return;
  if (x + w > (int16_t)fb->width) w = (int16_t)fb->width - x;
  if (y + h > (int16_t)fb->height) h = (int16_t)fb->height - y;
  if (w <= 0 || h <= 0) return;

  const uint16_t x0 = (uint16_t)x;
  const uint16_t y0 = (uint16_t)y;
  const uint16_t x1 = (uint16_t)(x + w);
  const uint16_t y1 = (uint16_t)(y + h);
  if (!fb->dirty) {
    fb->dirty = true;
    fb->dirty_x0 = x0; fb->dirty_y0 = y0; fb->dirty_x1 = x1; fb->dirty_y1 = y1;
  } else {
    if (x0 < fb->dirty_x0) fb->dirty_x0 = x0;
    if (y0 < fb->dirty_y0) fb->dirty_y0 = y0;
    if (x1 > fb->dirty_x1) fb->dirty_x1 = x1;
    if (y1 > fb->dirty_y1) fb->dirty_y1 = y1;
  }
}

bool ht5023_fb_init(ht5023_fb_rgb565_t* fb,
                    uint16_t* pixels,
                    uint16_t width,
                    uint16_t height,
                    uint16_t stride) {
  if (!fb || !pixels || width == 0u || height == 0u) return false;
  if (stride < width) stride = width;
  fb->pixels = pixels;
  fb->width = width;
  fb->height = height;
  fb->stride = stride;
  ht5023_fb_clear_dirty(fb);
  return true;
}

void ht5023_fb_clear_dirty(ht5023_fb_rgb565_t* fb) {
  if (!fb) return;
  fb->dirty = false;
  fb->dirty_x0 = fb->dirty_y0 = 0u;
  fb->dirty_x1 = fb->dirty_y1 = 0u;
}

void ht5023_fb_mark_dirty(ht5023_fb_rgb565_t* fb, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  fb_mark_dirty_clip(fb, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h);
}

void ht5023_fb_clear(ht5023_fb_rgb565_t* fb, uint16_t color) {
  if (!fb || !fb->pixels) return;
  for (uint16_t y = 0; y < fb->height; ++y) {
    for (uint16_t x = 0; x < fb->width; ++x) {
      fb->pixels[(size_t)y * fb->stride + x] = color;
    }
  }
  ht5023_fb_mark_dirty(fb, 0u, 0u, fb->width, fb->height);
}

void ht5023_fb_set_pixel(ht5023_fb_rgb565_t* fb, int16_t x, int16_t y, uint16_t color) {
  if (!fb || !fb->pixels) return;
  if (x < 0 || y < 0 || x >= (int16_t)fb->width || y >= (int16_t)fb->height) return;
  fb->pixels[(size_t)(uint16_t)y * fb->stride + (uint16_t)x] = color;
  fb_mark_dirty_clip(fb, x, y, 1, 1);
}

void ht5023_fb_fill_rect(ht5023_fb_rgb565_t* fb, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (!fb || !fb->pixels || w <= 0 || h <= 0) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x >= (int16_t)fb->width || y >= (int16_t)fb->height || w <= 0 || h <= 0) return;
  if (x + w > (int16_t)fb->width) w = (int16_t)fb->width - x;
  if (y + h > (int16_t)fb->height) h = (int16_t)fb->height - y;
  if (w <= 0 || h <= 0) return;

  for (int16_t row = 0; row < h; ++row) {
    uint16_t* dst = &fb->pixels[(size_t)(uint16_t)(y + row) * fb->stride + (uint16_t)x];
    for (int16_t col = 0; col < w; ++col) {
      dst[col] = color;
    }
  }
  fb_mark_dirty_clip(fb, x, y, w, h);
}

void ht5023_fb_blit_rgb565(ht5023_fb_rgb565_t* fb,
                           int16_t x,
                           int16_t y,
                           uint16_t w,
                           uint16_t h,
                           const uint16_t* pixels,
                           uint16_t stride) {
  if (!fb || !fb->pixels || !pixels || w == 0u || h == 0u) return;
  if (stride < w) stride = w;
  for (uint16_t row = 0; row < h; ++row) {
    const int16_t dy = (int16_t)(y + (int16_t)row);
    if (dy < 0 || dy >= (int16_t)fb->height) continue;
    for (uint16_t col = 0; col < w; ++col) {
      const int16_t dx = (int16_t)(x + (int16_t)col);
      if (dx < 0 || dx >= (int16_t)fb->width) continue;
      fb->pixels[(size_t)(uint16_t)dy * fb->stride + (uint16_t)dx] = pixels[(size_t)row * stride + col];
    }
  }
  fb_mark_dirty_clip(fb, x, y, (int16_t)w, (int16_t)h);
}

bool ht5023_fb_flush_dirty_to_st7789(ht5023_fb_rgb565_t* fb,
                                     ht5023_st7789_t* lcd,
                                     uint16_t dst_x,
                                     uint16_t dst_y) {
  if (!fb || !lcd || !fb->pixels || !fb->dirty) return false;
  const uint16_t x0 = fb->dirty_x0;
  const uint16_t y0 = fb->dirty_y0;
  const uint16_t w = (uint16_t)(fb->dirty_x1 - fb->dirty_x0);
  const uint16_t h = (uint16_t)(fb->dirty_y1 - fb->dirty_y0);
  if (w == 0u || h == 0u) { ht5023_fb_clear_dirty(fb); return false; }

  if (fb->stride == fb->width) {
    // Dirty rect may start at x0; rows are not contiguous across sub-rect unless x0==0 and w==stride.
    if (x0 == 0u && w == fb->width) {
      ht5023_st7789_set_addr_window(lcd, (uint16_t)(dst_x + x0), (uint16_t)(dst_y + y0), w, h);
      ht5023_st7789_write_pixels_rgb565(lcd, &fb->pixels[(size_t)y0 * fb->stride], (size_t)w * h);
    } else {
      for (uint16_t row = 0; row < h; ++row) {
        ht5023_st7789_set_addr_window(lcd, (uint16_t)(dst_x + x0), (uint16_t)(dst_y + y0 + row), w, 1u);
        ht5023_st7789_write_pixels_rgb565(lcd,
          &fb->pixels[(size_t)(y0 + row) * fb->stride + x0], w);
      }
    }
  } else {
    for (uint16_t row = 0; row < h; ++row) {
      ht5023_st7789_set_addr_window(lcd, (uint16_t)(dst_x + x0), (uint16_t)(dst_y + y0 + row), w, 1u);
      ht5023_st7789_write_pixels_rgb565(lcd,
        &fb->pixels[(size_t)(y0 + row) * fb->stride + x0], w);
    }
  }

  ht5023_fb_clear_dirty(fb);
  return true;
}

static void fb_surface_draw_pixel(void* user, int16_t x, int16_t y, uint16_t color) {
  ht5023_fb_set_pixel((ht5023_fb_rgb565_t*)user, x, y, color);
}

static void fb_surface_fill_rect(void* user, int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t color) {
  ht5023_fb_fill_rect((ht5023_fb_rgb565_t*)user, x, y, (int16_t)w, (int16_t)h, color);
}

static void fb_surface_blit(void* user,
                            int16_t x,
                            int16_t y,
                            uint16_t w,
                            uint16_t h,
                            const uint16_t* pixels,
                            uint16_t stride) {
  ht5023_fb_blit_rgb565((ht5023_fb_rgb565_t*)user, x, y, w, h, pixels, stride);
}

void ht5023_fb_make_gfx_surface(ht5023_fb_rgb565_t* fb, ht5023_gfx_surface_t* out_surface) {
  if (!fb || !out_surface) return;
  out_surface->width = (int16_t)fb->width;
  out_surface->height = (int16_t)fb->height;
  out_surface->user = fb;
  out_surface->draw_pixel = fb_surface_draw_pixel;
  out_surface->fill_rect = fb_surface_fill_rect;
  out_surface->blit_rgb565 = fb_surface_blit;
}

