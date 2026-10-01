// examples/st7789_ui_demo/main.c — ST7789 UI demo (gfx/text/sprite/fb dirty-rect)
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/fb.h"
#include "ht5023/gfx.h"
#include "ht5023/spi.h"
#include "ht5023/st7789.h"
#include "ht5023/text.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_toggle(void);

#ifndef ST7789_DEMO_CPU_HZ
#define ST7789_DEMO_CPU_HZ HT5023_FCPU_HZ
#endif
#ifndef ST7789_DEMO_WIDTH
#define ST7789_DEMO_WIDTH 240u
#endif
#ifndef ST7789_DEMO_HEIGHT
#define ST7789_DEMO_HEIGHT 240u
#endif
#ifndef ST7789_DEMO_X_OFFSET
#define ST7789_DEMO_X_OFFSET 0u
#endif
#ifndef ST7789_DEMO_Y_OFFSET
#define ST7789_DEMO_Y_OFFSET 0u
#endif

#define GPIOA_BASE_ADDR       0x40011000u
#define GPIO_IOCFG_OFFSET_A   0x00u
#define GPIO_AFCFG_OFFSET_A   0x04u
#define GPIO_PTDIR_OFFSET_A   0x08u
#define GPIO_PTSET_OFFSET_A   0x14u
#define GPIO_PTCLR_OFFSET_A   0x18u
#define GPIO_PTOD_OFFSET_A    0x20u

#define ST7789_DC_PIN   0u
#define ST7789_RST_PIN  2u

#define TILE_W 48u
#define TILE_H 32u
#define TILE_DST_X 12u
#define TILE_DST_Y 196u

typedef struct {
  uint32_t gpio_base;
  uint8_t dc_pin;
  uint8_t rst_pin;
} st7789_hal_ctx_t;

static uint16_t g_tile_pixels[TILE_W * TILE_H];
static uint16_t g_sprite_12x12[12u * 12u];

static inline volatile uint16_t* reg16_local(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(base + off);
}

static void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; ++i) __asm__ volatile ("nop");
}

static void delay_ms_blocking(uint32_t ms) {
  delay_cycles((ST7789_DEMO_CPU_HZ / 9000u) * ms);
}

static void gpio_pin_output_init(uint32_t base, uint8_t pin, bool high) {
  const uint16_t mask = (uint16_t)(1u << (pin & 0x0Fu));
  *reg16_local(base, GPIO_IOCFG_OFFSET_A) &= (uint16_t)~mask;
  *reg16_local(base, GPIO_AFCFG_OFFSET_A) &= (uint16_t)~mask;
  *reg16_local(base, GPIO_PTOD_OFFSET_A) |= mask;
  if (high) *reg16_local(base, GPIO_PTSET_OFFSET_A) = mask;
  else *reg16_local(base, GPIO_PTCLR_OFFSET_A) = mask;
  *reg16_local(base, GPIO_PTDIR_OFFSET_A) |= mask;
}

static void gpio_pin_write(uint32_t base, uint8_t pin, bool high) {
  const uint16_t mask = (uint16_t)(1u << (pin & 0x0Fu));
  if (high) *reg16_local(base, GPIO_PTSET_OFFSET_A) = mask;
  else *reg16_local(base, GPIO_PTCLR_OFFSET_A) = mask;
}

static void st7789_spi_write_cb(const uint8_t* bytes, size_t len, void* user) {
  (void)user;
  ht5023_spi_transfer(bytes, 0, len);
}
static void st7789_cs_cb(bool high, void* user) { (void)user; ht5023_spi_set_cs(high); }
static void st7789_dc_cb(bool high, void* user) {
  st7789_hal_ctx_t* ctx = (st7789_hal_ctx_t*)user;
  gpio_pin_write(ctx->gpio_base, ctx->dc_pin, high);
}
static void st7789_rst_cb(bool high, void* user) {
  st7789_hal_ctx_t* ctx = (st7789_hal_ctx_t*)user;
  gpio_pin_write(ctx->gpio_base, ctx->rst_pin, high);
}
static void st7789_delay_ms_cb(uint32_t ms, void* user) { (void)user; delay_ms_blocking(ms); }

static void sprite_init(void) {
  for (uint16_t y = 0; y < 12u; ++y) {
    for (uint16_t x = 0; x < 12u; ++x) {
      const int16_t dx = (int16_t)x - 5;
      const int16_t dy = (int16_t)y - 5;
      uint16_t c = ht5023_st7789_color565((uint8_t)(x * 18u), (uint8_t)(y * 18u), 220u);
      if ((dx * dx + dy * dy) < 20) c = ht5023_st7789_color565(255, 255, 255);
      if (x == 0u || y == 0u || x == 11u || y == 11u) c = ht5023_st7789_color565(0, 0, 0);
      g_sprite_12x12[(size_t)y * 12u + x] = c;
    }
  }
}

static void draw_static_scene(ht5023_gfx_surface_t* screen) {
  ht5023_text_style_t title;
  ht5023_text_default_style(&title, ht5023_st7789_color565(255,255,255), 0u);
  title.opaque_bg = false;
  title.scale = 2u;

  ht5023_text_style_t body;
  ht5023_text_default_style(&body, ht5023_st7789_color565(230,230,230), 0u);
  body.opaque_bg = false;

  ht5023_gfx_fill_rect(screen, 0, 0, screen->width, screen->height, ht5023_st7789_color565(8, 10, 18));
  ht5023_gfx_fill_rect(screen, 0, 0, screen->width, 32, ht5023_st7789_color565(20, 28, 44));
  ht5023_text_draw_str(screen, 8, 8, "ST7789 UI DEMO", &title);

  ht5023_gfx_draw_rect(screen, 8, 40, 224, 144, ht5023_st7789_color565(255,255,255));
  ht5023_gfx_draw_line(screen, 8, 40, 232, 184, ht5023_st7789_color565(255,100,80));
  ht5023_gfx_draw_line(screen, 232, 40, 8, 184, ht5023_st7789_color565(80,180,255));
  ht5023_gfx_draw_circle(screen, 120, 112, 46, ht5023_st7789_color565(255, 220, 40));
  ht5023_gfx_fill_circle(screen, 120, 112, 20, ht5023_st7789_color565(45, 220, 120));
  ht5023_gfx_draw_triangle(screen, 28, 172, 120, 62, 212, 172, ht5023_st7789_color565(255,255,255));

  ht5023_text_draw_str(screen, 16, 48, "GFX: LINE RECT CIRCLE TRI", &body);
  ht5023_text_draw_str(screen, 16, 60, "TEXT: 5X7 FONT", &body);
  ht5023_text_draw_str(screen, 16, 72, "BLIT: RGB565 SPRITE", &body);
  ht5023_text_draw_str(screen, 16, 84, "FB: DIRTY TILE FLUSH", &body);
}

static void render_tile(ht5023_fb_rgb565_t* fb, uint16_t frame) {
  ht5023_gfx_surface_t tile;
  ht5023_text_style_t st;
  const uint16_t bg = ht5023_st7789_color565(24, 20, 34);
  const uint16_t fg = ht5023_st7789_color565(255, 255, 255);
  const uint16_t accent = ht5023_st7789_color565(90, 180, 255);
  const uint16_t pulse = ht5023_st7789_color565((uint8_t)(80u + (frame * 5u) % 140u), 100u, 220u);

  ht5023_fb_make_gfx_surface(fb, &tile);
  ht5023_fb_clear(fb, bg);

  ht5023_gfx_draw_rect(&tile, 0, 0, TILE_W, TILE_H, fg);
  ht5023_gfx_draw_hline(&tile, 1, 11, TILE_W - 2, accent);
  ht5023_gfx_draw_vline(&tile, 23, 12, TILE_H - 13, accent);
  ht5023_gfx_fill_circle(&tile, 36, 22, 6, pulse);

  // sprite blit into framebuffer tile
  ht5023_gfx_blit_rgb565(&tile, (int16_t)(2 + (frame % 10u)), 16, 12u, 12u, g_sprite_12x12, 12u);

  ht5023_text_default_style(&st, fg, bg);
  st.opaque_bg = false;
  ht5023_text_draw_str(&tile, 3, 2, "FB TILE", &st);

  // simple frame counter using text library
  {
    char txt[12];
    uint16_t v = frame;
    uint8_t n = 0;
    txt[n++] = 'F'; txt[n++] = ':';
    if (v >= 100u) txt[n++] = (char)('0' + ((v / 100u) % 10u));
    if (v >= 10u) txt[n++] = (char)('0' + ((v / 10u) % 10u));
    txt[n++] = (char)('0' + (v % 10u));
    txt[n] = '\0';
    ht5023_text_draw_str(&tile, 2, 12, txt, &st);
  }
}

int main(void) {
  const ht5023_spi_master_cfg_t spi_cfg = { .spr = 2u, .cpol = false, .cpha = false };
  st7789_hal_ctx_t hal = { .gpio_base = GPIOA_BASE_ADDR, .dc_pin = ST7789_DC_PIN, .rst_pin = ST7789_RST_PIN };
  ht5023_st7789_t lcd;
  ht5023_gfx_surface_t screen;
  ht5023_fb_rgb565_t tile_fb;
  const ht5023_st7789_config_t lcd_cfg = {
    .write = st7789_spi_write_cb,
    .dc_write = st7789_dc_cb,
    .cs_write = st7789_cs_cb,
    .rst_write = st7789_rst_cb,
    .delay_ms = st7789_delay_ms_cb,
    .user = &hal,
    .width = ST7789_DEMO_WIDTH,
    .height = ST7789_DEMO_HEIGHT,
    .x_offset = ST7789_DEMO_X_OFFSET,
    .y_offset = ST7789_DEMO_Y_OFFSET,
    .invert_colors = true,
    .bgr_order = true,
  };

  uint16_t frame = 0u;
  uint16_t sx = 180u, sy = 144u;
  int16_t sdx = 2, sdy = 2;

  led_gpio_init();
  gpio_pin_output_init(GPIOA_BASE_ADDR, ST7789_DC_PIN, true);
  gpio_pin_output_init(GPIOA_BASE_ADDR, ST7789_RST_PIN, true);
  sprite_init();

  (void)ht5023_spi_master_init(&spi_cfg);
  if (!ht5023_st7789_init(&lcd, &lcd_cfg)) {
    for (;;) { led_toggle(); delay_ms_blocking(200u); }
  }
  ht5023_st7789_set_rotation(&lcd, HT5023_ST7789_ROTATION_0);
  ht5023_gfx_surface_init_st7789(&screen, &lcd);
  (void)ht5023_fb_init(&tile_fb, g_tile_pixels, TILE_W, TILE_H, TILE_W);

  draw_static_scene(&screen);

  for (;;) {
    // erase previous sprite area with background color and redraw local border lines if overlapped
    ht5023_gfx_fill_rect(&screen, (int16_t)sx, (int16_t)sy, 12, 12, ht5023_st7789_color565(8, 10, 18));

    if ((int16_t)sx + sdx < 12 || (int16_t)sx + sdx > 216) sdx = (int16_t)-sdx;
    if ((int16_t)sy + sdy < 96 || (int16_t)sy + sdy > 176) sdy = (int16_t)-sdy;
    sx = (uint16_t)((int16_t)sx + sdx);
    sy = (uint16_t)((int16_t)sy + sdy);
    ht5023_gfx_blit_rgb565(&screen, (int16_t)sx, (int16_t)sy, 12u, 12u, g_sprite_12x12, 12u);

    render_tile(&tile_fb, frame++);
    (void)ht5023_fb_flush_dirty_to_st7789(&tile_fb, &lcd, TILE_DST_X, TILE_DST_Y);

    led_toggle();
    delay_ms_blocking(45u);
  }
}
