// ht5023_font5x7.h — compact 5x7 bitmap font helpers
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HT5023_FONT5X7_WIDTH  5u
#define HT5023_FONT5X7_HEIGHT 7u

// Glyph format: 5 columns, LSB at top row, bit set => foreground pixel.
// Returns a valid glyph pointer for printable ASCII subset; unsupported chars map to '?'.
const uint8_t* ht5023_font5x7_glyph(char c);
bool ht5023_font5x7_is_supported(char c);

#ifdef __cplusplus
}
#endif
