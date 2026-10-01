# Display API

The display stack has four layers, each building on the previous:

```
ST7789 driver     ← hardware pixel writes over SPI
    ↓
GFX surface       ← 2D drawing primitives (line, rect, circle…)
    ↓
Text renderer     ← 5×7 bitmap font, scalable, coloured
    ↓
Framebuffer (FB)  ← off-screen buffer with dirty-rect upload
```

---

## ST7789 LCD Driver

**Header:** `#include "ht5023/st7789.h"`
**Source:** `lib/ht5023_st7789/src/ht5023_st7789.c`

### Overview

Callback-based driver for the ST7789 RGB565 LCD controller, with an optional
HT5023 DMA-assisted SPI TX path. GPIO control (DC/CS/RST) and delays still go
through callbacks, while write traffic can run through polling SPI, DMA, or a
DMA-first path with polling fallback.

### Configuration

```c
typedef struct {
    // Required when DMA is not configured
    ht5023_st7789_write_fn      write;     // write bytes over SPI
    ht5023_st7789_pin_write_fn  dc_write;  // drive the D/C pin

    // Optional — may be NULL
    ht5023_st7789_pin_write_fn  cs_write;   // drive the CS pin
    ht5023_st7789_pin_write_fn  rst_write;  // drive the RST pin
    ht5023_st7789_delay_ms_fn   delay_ms;   // blocking ms delay
    const ht5023_st7789_dma_config_t* dma;  // optional DMA transport

    void* user;        // passed as-is to every callback

    uint16_t width;    // panel logical width in pixels
    uint16_t height;   // panel logical height in pixels
    uint16_t x_offset; // panel column offset (some 240×240 modules need 0)
    uint16_t y_offset; // panel row offset (some 240×240 modules need 80)
    bool invert_colors; // INVON command (most IPS panels need true)
    bool bgr_order;    // MADCTL BGR bit (set true if colours look inverted)
} ht5023_st7789_config_t;
```

Optional DMA transport:

```c
typedef struct {
    ht5023_dma_channel_t channel;
    ht5023_dma_request_t request;
    uintptr_t tx_reg_addr;   // usually the SPI data register
    uint32_t timeout_polls;  // timeout for each DMA launch
    size_t min_bytes;        // below this, fall back to .write when available
    uint8_t* stage_buffer;   // optional RAM staging buffer
    size_t stage_buffer_len; // staging buffer size in bytes
    uint8_t max_fail_streak; // 0 keeps DMA enabled after failures
} ht5023_st7789_dma_config_t;
```

Notes:
- If `.write` is `NULL`, DMA must be configured.
- With both `.write` and `.dma` configured, DMA is used first and `.write`
  becomes the fallback path.
- `stage_buffer` is optional scratch RAM for DMA-oriented builds.

**Callback signatures:**

```c
typedef void (*ht5023_st7789_write_fn)(const uint8_t* bytes, size_t len, void* user);
typedef void (*ht5023_st7789_pin_write_fn)(bool high, void* user);
typedef void (*ht5023_st7789_delay_ms_fn)(uint32_t ms, void* user);
```

### Colour helper

```c
static inline uint16_t ht5023_st7789_color565(uint8_t r, uint8_t g, uint8_t b);
```

Pack 8-bit R/G/B into the RGB565 format expected by the display.

### Functions

| Function | Description |
|----------|-------------|
| `ht5023_st7789_init(lcd, cfg)` | Initialise hardware: reset sequence, sleep-out, COLMOD, NORON, DISPON. Returns `false` on invalid cfg. |
| `ht5023_st7789_hard_reset(lcd)` | Toggle RST pin: high→10 ms→low→20 ms→high→120 ms. |
| `ht5023_st7789_set_rotation(lcd, rot)` | Set display orientation (0°/90°/180°/270°). Updates `lcd->width` and `lcd->height`. |
| `ht5023_st7789_get_dma_stats(lcd, stats)` | Read DMA configuration state plus success/fallback counters. |
| `ht5023_st7789_reset_dma_stats(lcd)` | Clear DMA counters and re-enable DMA if configured. |
| `ht5023_st7789_set_addr_window(lcd, x, y, w, h)` | Program CASET + RASET + RAMWR (prepares for pixel data). |
| `ht5023_st7789_write_pixels_rgb565(lcd, pixels, count)` | Stream `count` RGB565 pixels (big-endian byte order) after `set_addr_window`. |
| `ht5023_st7789_write_color_rgb565(lcd, color, count)` | Fill `count` pixels with a single colour. |
| `ht5023_st7789_fill_rect(lcd, x, y, w, h, color)` | Fill a rectangle. Clips to display bounds. |
| `ht5023_st7789_fill_screen(lcd, color)` | Fill the entire screen. |

### Example wiring (SPI on PC4–PC7, DC on PA0, RST on PA2)

```c
static void spi_write_cb(const uint8_t* b, size_t len, void* u) {
    (void)u;
    ht5023_spi_transfer(b, NULL, len);
}
static void cs_cb(bool high, void* u) { (void)u; ht5023_spi_set_cs(high); }
static void dc_cb(bool high, void* u) { gpio_write(PA0, high); }
static void rst_cb(bool high, void* u) { gpio_write(PA2, high); }
static void delay_cb(uint32_t ms, void* u) { (void)u; blocking_delay_ms(ms); }
static uint8_t st7789_dma_stage[256];

const ht5023_st7789_dma_config_t lcd_dma = {
    .channel = HT5023_DMA_CH0,
    .request = HT5023_DMA_REQ_SPI_TX,
    .tx_reg_addr = SPI_BASE_ADDR + SPI_SPIDAT_OFFSET,
    .timeout_polls = 30000u,
    .min_bytes = 32u,
    .stage_buffer = st7789_dma_stage,
    .stage_buffer_len = sizeof(st7789_dma_stage),
    .max_fail_streak = 2u,
};

ht5023_st7789_t lcd;
const ht5023_st7789_config_t lcd_cfg = {
    .write = spi_write_cb, .dc_write = dc_cb,
    .cs_write = cs_cb, .rst_write = rst_cb, .delay_ms = delay_cb,
    .dma = &lcd_dma,
    .width = 240, .height = 240,
    .invert_colors = true, .bgr_order = true,
};

ht5023_spi_master_init(&spi_cfg);
ht5023_st7789_init(&lcd, &lcd_cfg);
ht5023_st7789_fill_screen(&lcd, ht5023_st7789_color565(0, 0, 255));
```

---

## GFX — 2D Primitives

**Header:** `#include "ht5023/gfx.h"`
**Source:** `lib/ht5023_gfx/src/ht5023_gfx.c`

### Surface abstraction

```c
typedef struct {
    int16_t width;
    int16_t height;
    void* user;
    ht5023_gfx_draw_pixel_fn  draw_pixel;  // required
    ht5023_gfx_fill_rect_fn   fill_rect;   // optional fast-path
    ht5023_gfx_blit_rgb565_fn blit_rgb565; // optional fast-path
} ht5023_gfx_surface_t;
```

A surface is an abstract drawing target. Initialise one from an ST7789
instance or a framebuffer:

```c
// Direct-to-display surface
ht5023_gfx_surface_t surf;
ht5023_gfx_surface_init_st7789(&surf, &lcd);

// Off-screen framebuffer surface  (see Framebuffer section)
ht5023_fb_make_gfx_surface(&fb, &surf);
```

### Drawing functions

All functions accept signed `int16_t` coordinates and clip silently to the
surface bounds. Off-screen drawing is safe (no undefined behaviour).

```c
void ht5023_gfx_draw_pixel(s, x, y, color);
void ht5023_gfx_draw_hline(s, x, y, w, color);
void ht5023_gfx_draw_vline(s, x, y, h, color);
void ht5023_gfx_fill_rect(s, x, y, w, h, color);
void ht5023_gfx_draw_rect(s, x, y, w, h, color);     // outline only
void ht5023_gfx_draw_line(s, x0, y0, x1, y1, color); // Bresenham
void ht5023_gfx_draw_circle(s, cx, cy, r, color);    // outline
void ht5023_gfx_fill_circle(s, cx, cy, r, color);
void ht5023_gfx_draw_triangle(s, x0,y0, x1,y1, x2,y2, color);
void ht5023_gfx_blit_rgb565(s, x, y, w, h, pixels, stride);
bool ht5023_gfx_in_bounds(s, x, y);
```

---

## Text Renderer

**Header:** `#include "ht5023/text.h"` (also needs `ht5023/font5x7.h`)
**Source:** `lib/ht5023_text/`, `lib/ht5023_font5x7/`

### Style

```c
typedef struct {
    uint16_t fg;         // foreground colour (RGB565)
    uint16_t bg;         // background colour (RGB565, used when opaque_bg=true)
    bool opaque_bg;      // fill background rectangle behind each glyph
    uint8_t scale;       // pixel scale factor (1 = normal, 2 = 2×, …)
    uint8_t spacing_x;   // extra pixels between characters
    uint8_t spacing_y;   // extra pixels between lines
} ht5023_text_style_t;
```

Initialise with defaults (white on black, scale 1, 1-pixel spacing):
```c
ht5023_text_style_t st;
ht5023_text_default_style(&st, 0xFFFF, 0x0000);
```

### Functions

```c
void ht5023_text_draw_char(s, x, y, c, style);
void ht5023_text_draw_str(s, x, y, str, style);   // handles \n
uint16_t ht5023_text_char_advance(style);          // horizontal advance per char
uint16_t ht5023_text_line_height(style);           // vertical advance per line
```

The font is 5×7 pixels per glyph (printable ASCII). Characters outside the
supported range are rendered as `?`.

### Example

```c
ht5023_text_style_t st;
ht5023_text_default_style(&st, ht5023_st7789_color565(255, 255, 0), 0x0000);
st.opaque_bg = true;
st.scale = 2;

ht5023_text_draw_str(&surf, 10, 20, "Hello, HT5023!", &st);
```

---

## Framebuffer

**Header:** `#include "ht5023/fb.h"`
**Source:** `lib/ht5023_fb/src/ht5023_fb.c`

### Overview

An off-screen RGB565 framebuffer that tracks a dirty rectangle. Only the
modified region is uploaded to the display on flush, saving SPI bandwidth.

```c
typedef struct {
    uint16_t* pixels;    // backing pixel array (caller-allocated)
    uint16_t width;
    uint16_t height;
    uint16_t stride;     // pixels per row (may be > width)
    bool dirty;
    uint16_t dirty_x0, dirty_y0;  // dirty region (inclusive)
    uint16_t dirty_x1, dirty_y1;  // dirty region (exclusive)
} ht5023_fb_rgb565_t;
```

### Allocation

```c
// 240×240 pixels = 115200 bytes — allocate statically
static uint16_t fb_pixels[240 * 240];
ht5023_fb_rgb565_t fb;
ht5023_fb_init(&fb, fb_pixels, 240, 240, 240);
```

### Drawing into the framebuffer

Create a GFX surface backed by the framebuffer, then draw normally:

```c
ht5023_gfx_surface_t surf;
ht5023_fb_make_gfx_surface(&fb, &surf);

ht5023_gfx_fill_rect(&surf, 0, 0, 240, 240, 0x0000);       // clear
ht5023_gfx_draw_circle(&surf, 120, 120, 60, 0xF800);        // red circle
ht5023_text_draw_str(&surf, 80, 112, "HT5023", &style);
```

Or use the framebuffer API directly:

```c
ht5023_fb_set_pixel(&fb, x, y, color);
ht5023_fb_fill_rect(&fb, x, y, w, h, color);
ht5023_fb_clear(&fb, 0x0000);
```

### Flushing to the display

```c
// Upload only the dirty region, then clear the dirty flag
ht5023_fb_flush_dirty_to_st7789(&fb, &lcd, 0, 0);
```

Pass `dst_x`, `dst_y` to position the framebuffer on the display
(e.g. for a UI panel at a non-zero offset).

See also: `examples/st7789_demo/`, `examples/st7789_ui_demo/`
