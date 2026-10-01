// ht5023_text.c — text drawing helpers using 5x7 font and gfx surfaces
#include "ht5023_text.h"

#include "ht5023_font5x7.h"

static const ht5023_text_style_t k_default_style = {
  .fg = 0xFFFFu,
  .bg = 0x0000u,
  .opaque_bg = false,
  .scale = 1u,
  .spacing_x = 1u,
  .spacing_y = 1u,
};

static const ht5023_text_style_t* resolve_style(const ht5023_text_style_t* style) {
  return style ? style : &k_default_style;
}

uint16_t ht5023_text_char_advance(const ht5023_text_style_t* style) {
  const ht5023_text_style_t* st = resolve_style(style);
  uint8_t scale = st->scale ? st->scale : 1u;
  return (uint16_t)(HT5023_FONT5X7_WIDTH * scale + st->spacing_x);
}

uint16_t ht5023_text_line_height(const ht5023_text_style_t* style) {
  const ht5023_text_style_t* st = resolve_style(style);
  uint8_t scale = st->scale ? st->scale : 1u;
  return (uint16_t)(HT5023_FONT5X7_HEIGHT * scale + st->spacing_y);
}

void ht5023_text_default_style(ht5023_text_style_t* style, uint16_t fg, uint16_t bg) {
  if (!style) return;
  *style = k_default_style;
  style->fg = fg;
  style->bg = bg;
}

void ht5023_text_draw_char(const ht5023_gfx_surface_t* s,
                           int16_t x,
                           int16_t y,
                           char c,
                           const ht5023_text_style_t* style) {
  const ht5023_text_style_t* st = resolve_style(style);
  const uint8_t scale = st->scale ? st->scale : 1u;
  const uint8_t* glyph = ht5023_font5x7_glyph(c);
  if (!s || !glyph) return;

  if (st->opaque_bg) {
    ht5023_gfx_fill_rect(s, x, y,
                         (int16_t)(HT5023_FONT5X7_WIDTH * scale),
                         (int16_t)(HT5023_FONT5X7_HEIGHT * scale),
                         st->bg);
  }

  for (uint8_t col = 0; col < HT5023_FONT5X7_WIDTH; ++col) {
    uint8_t bits = glyph[col];
    for (uint8_t row = 0; row < HT5023_FONT5X7_HEIGHT; ++row) {
      if ((bits >> row) & 0x01u) {
        const int16_t px = (int16_t)(x + (int16_t)(col * scale));
        const int16_t py = (int16_t)(y + (int16_t)(row * scale));
        if (scale == 1u) {
          ht5023_gfx_draw_pixel(s, px, py, st->fg);
        } else {
          ht5023_gfx_fill_rect(s, px, py, scale, scale, st->fg);
        }
      }
    }
  }
}

void ht5023_text_draw_str(const ht5023_gfx_surface_t* s,
                          int16_t x,
                          int16_t y,
                          const char* str,
                          const ht5023_text_style_t* style) {
  if (!s || !str) return;
  const ht5023_text_style_t* st = resolve_style(style);
  const int16_t adv = (int16_t)ht5023_text_char_advance(st);
  const int16_t line_h = (int16_t)ht5023_text_line_height(st);
  int16_t cx = x;
  int16_t cy = y;

  for (const char* p = str; *p; ++p) {
    if (*p == '\r') continue;
    if (*p == '\n') {
      cx = x;
      cy = (int16_t)(cy + line_h);
      continue;
    }
    ht5023_text_draw_char(s, cx, cy, *p, st);
    cx = (int16_t)(cx + adv);
  }
}

