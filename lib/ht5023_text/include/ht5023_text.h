// ht5023_text.h — text drawing helpers using 5x7 font and gfx surfaces
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ht5023_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint16_t fg;
  uint16_t bg;
  bool opaque_bg;
  uint8_t scale;       // 1..N
  uint8_t spacing_x;   // pixels between glyphs
  uint8_t spacing_y;   // pixels between lines
} ht5023_text_style_t;

void ht5023_text_default_style(ht5023_text_style_t* style, uint16_t fg, uint16_t bg);
void ht5023_text_draw_char(const ht5023_gfx_surface_t* s,
                           int16_t x,
                           int16_t y,
                           char c,
                           const ht5023_text_style_t* style);
void ht5023_text_draw_str(const ht5023_gfx_surface_t* s,
                          int16_t x,
                          int16_t y,
                          const char* str,
                          const ht5023_text_style_t* style);
uint16_t ht5023_text_line_height(const ht5023_text_style_t* style);
uint16_t ht5023_text_char_advance(const ht5023_text_style_t* style);

#ifdef __cplusplus
}
#endif
