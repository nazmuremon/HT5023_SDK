// examples/st7789_ui_dma_demo/main.c — classy full-screen ST7789 UI demo with SPI TX DMA
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/debug.h"
#include "ht5023/dma.h"
#include "ht5023/fb.h"
#include "ht5023/gfx.h"
#include "ht5023/log.h"
#include "ht5023/spi.h"
#include "ht5023/st7789.h"
#include "ht5023/text.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_toggle(void);

#ifndef ST7789_DEMO_CPU_HZ
#define ST7789_DEMO_CPU_HZ HT5023_FCPU_HZ
#endif
#ifndef ST7789_DEMO_UART_SYSCLK_HZ
#define ST7789_DEMO_UART_SYSCLK_HZ HT5023_FSYS_HZ
#endif
// Defaults target a common 240x320 ST7789 panel, rotated to a logical 320x240 full-screen UI.
#ifndef ST7789_DEMO_WIDTH
#define ST7789_DEMO_WIDTH 240u
#endif
#ifndef ST7789_DEMO_HEIGHT
#define ST7789_DEMO_HEIGHT 320u
#endif
#ifndef ST7789_DEMO_X_OFFSET
#define ST7789_DEMO_X_OFFSET 0u
#endif
#ifndef ST7789_DEMO_Y_OFFSET
#define ST7789_DEMO_Y_OFFSET 0u
#endif

#define GPIOA_BASE_ADDR       0x40011000u
#define SPI_BASE_ADDR         0x4000B000u
#define SPI_SPIDAT_OFFSET     0x08u
#define GPIO_IOCFG_OFFSET_A   0x00u
#define GPIO_AFCFG_OFFSET_A   0x04u
#define GPIO_PTDIR_OFFSET_A   0x08u
#define GPIO_PTSET_OFFSET_A   0x14u
#define GPIO_PTCLR_OFFSET_A   0x18u
#define GPIO_PTOD_OFFSET_A    0x20u

#define ST7789_DC_PIN         0u
#define ST7789_RST_PIN        2u

#define LOGICAL_W             320u
#define LOGICAL_H             240u

#define PAD                   8u
#define HEADER_H              28u
#define MAIN_Y                36u
#define MAIN_H                140u
#define HERO_X                8u
#define HERO_Y                MAIN_Y
#define HERO_W                196u
#define HERO_H                MAIN_H
#define SIDEBAR_X             (HERO_X + HERO_W + PAD)
#define SIDEBAR_Y             MAIN_Y
#define SIDEBAR_W             100u
#define CARD_H                67u
#define CARD_GAP              6u
#define CARD1_Y               SIDEBAR_Y
#define CARD2_Y               (SIDEBAR_Y + CARD_H + CARD_GAP)

#define BOTTOM_Y              184u
#define BOTTOM_H              48u
#define HUD_TILE_X            8u
#define HUD_TILE_Y            BOTTOM_Y
#define HUD_TILE_W            152u
#define HUD_TILE_H            BOTTOM_H
#define STATUS_X              (HUD_TILE_X + HUD_TILE_W + PAD)
#define STATUS_Y              BOTTOM_Y
#define STATUS_W              152u
#define STATUS_H              BOTTOM_H

#define ICON_W                16u
#define ICON_H                16u

#define ST7789_DMA_MIN_BYTES      8u
#define ST7789_DMA_STAGE_BYTES    2048u
#define ST7789_DMA_TIMEOUT_POLLS  200000u
#define ST7789_DMA_SRAM_BASE      0x8000u
#define DEBUG_UART_BAUD           9600u
#ifndef ST7789_DEMO_LOG_LEVEL
#define ST7789_DEMO_LOG_LEVEL HT5023_LOG_LEVEL_NONE
#endif

typedef struct {
  uint32_t gpio_base;
  uint8_t dc_pin;
  uint8_t rst_pin;
} st7789_hal_ctx_t;

typedef struct {
  bool dma_enabled;
  uint32_t dma_ok;
  uint32_t dma_fallback;
} st7789_dma_view_t;

typedef struct {
  bool panel_320x240;
  bool fb_flush_ok;
  bool dma_seen;
  bool gfx_draw_ok;
} visual_test_state_t;

static uint8_t g_st7789_dma_stage[ST7789_DMA_STAGE_BYTES] __attribute__((aligned(4)));
static uint8_t g_st7789_dma_rx_stage[ST7789_DMA_STAGE_BYTES] __attribute__((aligned(4)));
static uint16_t g_hud_tile_pixels[HUD_TILE_W * HUD_TILE_H];
static uint16_t g_icon_16x16[ICON_W * ICON_H];

static const uint8_t SIN64[64] = {
   64, 69, 75, 80, 85, 90, 94, 98,102,106,109,112,114,116,117,118,
  118,118,117,116,114,112,109,106,102, 98, 94, 90, 85, 80, 75, 69,
   64, 59, 53, 48, 43, 39, 34, 30, 26, 22, 19, 16, 14, 12, 11, 11,
   10, 10, 11, 12, 14, 16, 19, 22, 26, 30, 34, 39, 43, 48, 53, 59,
};

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
  if (!bytes || len == 0u) return;
  ht5023_spi_transfer(bytes, 0, len);
}

static bool st7789_spi_prime_tx_cb(uint8_t byte, void* user) {
  (void)user;
  *reg16_local(SPI_BASE_ADDR, SPI_SPIDAT_OFFSET) = byte;
  return true;
}

static bool st7789_spi_wait_complete_cb(void* user) {
  (void)user;
  return ht5023_spi_wait_tx_complete();
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

static st7789_dma_view_t st7789_dma_view(const ht5023_st7789_t* lcd) {
  ht5023_st7789_dma_stats_t stats;
  st7789_dma_view_t view = {0};
  ht5023_st7789_get_dma_stats(lcd, &stats);
  view.dma_enabled = stats.configured && stats.enabled;
  view.dma_ok = stats.success_count;
  view.dma_fallback = stats.fallback_count;
  return view;
}

static int16_t sin64s(uint8_t phase) {
  return (int16_t)SIN64[phase & 63u] - 64;
}

static uint8_t append_u32(char* dst, uint8_t n, uint32_t v) {
  char tmp[10];
  uint8_t t = 0u;
  if (v == 0u) { dst[n++] = '0'; return n; }
  while (v != 0u && t < sizeof(tmp)) {
    tmp[t++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  while (t != 0u) dst[n++] = tmp[--t];
  return n;
}

static void draw_text_num(const ht5023_gfx_surface_t* s,
                          int16_t x,
                          int16_t y,
                          const char* prefix,
                          uint32_t v,
                          const ht5023_text_style_t* st) {
  char buf[24];
  uint8_t n = 0u;
  while (prefix && *prefix && n < (uint8_t)(sizeof(buf) - 1u)) buf[n++] = *prefix++;
  n = append_u32(buf, n, v);
  buf[n] = '\0';
  ht5023_text_draw_str(s, x, y, buf, st);
}

static void draw_panel_rect(const ht5023_gfx_surface_t* s,
                            int16_t x,
                            int16_t y,
                            int16_t w,
                            int16_t h,
                            uint16_t outer,
                            uint16_t inner) {
  ht5023_gfx_draw_rect(s, x, y, w, h, outer);
  ht5023_gfx_draw_rect(s, (int16_t)(x + 1), (int16_t)(y + 1), (int16_t)(w - 2), (int16_t)(h - 2), inner);
}

static void icon_init(void) {
  for (uint8_t y = 0u; y < ICON_H; ++y) {
    for (uint8_t x = 0u; x < ICON_W; ++x) {
      int8_t dx = (int8_t)x - 7;
      int8_t dy = (int8_t)y - 7;
      uint8_t man = (uint8_t)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
      uint16_t c;
      if (man <= 5u) c = ht5023_st7789_color565((uint8_t)(80u + x * 9u), (uint8_t)(180u - y * 6u), 255u);
      else if (man == 6u) c = ht5023_st7789_color565(255u, 255u, 255u);
      else if ((x > 6u && x < 10u) && y > 8u && y < 15u) c = ht5023_st7789_color565(255u, 170u, 40u);
      else c = ht5023_st7789_color565(14u, 16u, 26u);
      g_icon_16x16[(size_t)y * ICON_W + x] = c;
    }
  }
}

static void draw_header(ht5023_gfx_surface_t* screen, uint16_t frame, const st7789_dma_view_t* dma) {
  ht5023_text_style_t title;
  ht5023_text_style_t body;
  uint16_t line = ht5023_st7789_color565(120, 220, 255);

  for (uint8_t y = 0u; y < HEADER_H; y += 2u) {
    uint16_t c = ht5023_st7789_color565((uint8_t)(6u + y * 2u), (uint8_t)(10u + y), (uint8_t)(18u + y * 4u));
    ht5023_gfx_fill_rect(screen, 0, y, LOGICAL_W, 2, c);
  }
  ht5023_gfx_draw_hline(screen, 0, (int16_t)(HEADER_H - 1u), LOGICAL_W, line);

  ht5023_text_default_style(&title, ht5023_st7789_color565(255,255,255), 0u);
  title.opaque_bg = false;
  title.scale = 2u;
  ht5023_text_draw_str(screen, 10, 7, "NEON OPS / DMA DISPLAY", &title);

  ht5023_text_default_style(&body, ht5023_st7789_color565(190,225,255), 0u);
  body.opaque_bg = false;
  ht5023_text_draw_str(screen, 242, 8, "ST7789", &body);
  ht5023_text_draw_str(screen, 242, 16, "320x240", &body);

  // Animated header chips.
  {
    uint8_t p = (uint8_t)((frame / 2u) & 63u);
    int16_t w1 = (int16_t)(22 + (SIN64[p] - 10u) / 6u);
    int16_t w2 = (int16_t)(16 + (SIN64[(uint8_t)(p + 21u)] - 10u) / 8u);
    int16_t x = 202;
    ht5023_gfx_fill_rect(screen, x, 8, 34, 6, ht5023_st7789_color565(18, 34, 52));
    ht5023_gfx_draw_rect(screen, x, 8, 34, 6, ht5023_st7789_color565(70, 110, 160));
    ht5023_gfx_fill_rect(screen, (int16_t)(x + 2), 10, w1, 2, ht5023_st7789_color565(80, 255, 220));

    x = 202;
    ht5023_gfx_fill_rect(screen, x, 18, 34, 6, ht5023_st7789_color565(18, 26, 46));
    ht5023_gfx_draw_rect(screen, x, 18, 34, 6, ht5023_st7789_color565(70, 90, 150));
    ht5023_gfx_fill_rect(screen, (int16_t)(x + 2), 20, w2, 2, ht5023_st7789_color565(255, 90, 210));
  }

  (void)dma;
}

static void draw_static_chrome(ht5023_gfx_surface_t* screen) {
  ht5023_gfx_fill_rect(screen, 0, HEADER_H, LOGICAL_W, (int16_t)(LOGICAL_H - HEADER_H),
                       ht5023_st7789_color565(4, 5, 9));

  draw_panel_rect(screen, HERO_X, HERO_Y, HERO_W, HERO_H,
                  ht5023_st7789_color565(95, 235, 255), ht5023_st7789_color565(26, 58, 82));
  draw_panel_rect(screen, SIDEBAR_X, CARD1_Y, SIDEBAR_W, CARD_H,
                  ht5023_st7789_color565(120, 160, 255), ht5023_st7789_color565(34, 42, 72));
  draw_panel_rect(screen, SIDEBAR_X, CARD2_Y, SIDEBAR_W, CARD_H,
                  ht5023_st7789_color565(255, 120, 210), ht5023_st7789_color565(68, 28, 64));
  draw_panel_rect(screen, HUD_TILE_X, HUD_TILE_Y, HUD_TILE_W, HUD_TILE_H,
                  ht5023_st7789_color565(140, 220, 255), ht5023_st7789_color565(34, 46, 70));
  draw_panel_rect(screen, STATUS_X, STATUS_Y, STATUS_W, STATUS_H,
                  ht5023_st7789_color565(255, 180, 100), ht5023_st7789_color565(70, 42, 26));
}

static void render_hero_scene(ht5023_gfx_surface_t* screen, uint16_t frame) {
  const int16_t x = (int16_t)(HERO_X + 2u);
  const int16_t y = (int16_t)(HERO_Y + 2u);
  const int16_t w = (int16_t)(HERO_W - 4u);
  const int16_t h = (int16_t)(HERO_H - 4u);
  const int16_t horizon = (int16_t)(HERO_Y + 86u);
  ht5023_text_style_t st;

  for (int16_t row = 0; row < h; row += 2) {
    int16_t yy = (int16_t)(y + row);
    uint16_t c;
    if (yy < horizon) {
      uint8_t t = (uint8_t)(yy - y);
      c = ht5023_st7789_color565((uint8_t)(7u + t / 3u), (uint8_t)(6u + t / 7u), (uint8_t)(24u + t));
    } else {
      uint8_t t = (uint8_t)(yy - horizon);
      c = ht5023_st7789_color565((uint8_t)(3u + t / 6u), (uint8_t)(6u + t / 5u), (uint8_t)(14u + t / 4u));
    }
    ht5023_gfx_fill_rect(screen, x, yy, w, 2, c);
  }

  // Stars
  for (uint8_t i = 0u; i < 52u; ++i) {
    uint8_t px = (uint8_t)((uint16_t)(i * 29u + frame * 3u + i * i) % (HERO_W - 8u));
    uint8_t py = (uint8_t)((uint16_t)(i * 17u + i * 7u) % 52u);
    uint16_t c = (((frame + i) & 3u) == 0u)
      ? ht5023_st7789_color565(255,255,255)
      : ht5023_st7789_color565(130,200,255);
    ht5023_gfx_draw_pixel(screen, (int16_t)(HERO_X + 4u + px), (int16_t)(HERO_Y + 8u + py), c);
  }

  // Sun
  {
    int16_t sx = (int16_t)(HERO_X + 142u);
    int16_t sy = (int16_t)(HERO_Y + 44u);
    ht5023_gfx_fill_circle(screen, sx, sy, 22, ht5023_st7789_color565(255, 118, 64));
    ht5023_gfx_draw_circle(screen, sx, sy, 22, ht5023_st7789_color565(255, 230, 110));
    ht5023_gfx_draw_circle(screen, sx, sy, 26, ht5023_st7789_color565(200, 60, 220));
    for (int16_t dy = -14; dy <= 14; dy += 4) {
      ht5023_gfx_draw_hline(screen, (int16_t)(sx - 16), (int16_t)(sy + dy), 32, ht5023_st7789_color565(36, 10, 28));
    }
  }

  // Horizon + perspective grid
  ht5023_gfx_draw_hline(screen, (int16_t)(HERO_X + 2u), horizon, (int16_t)(HERO_W - 4u),
                        ht5023_st7789_color565(255, 70, 180));
  {
    uint8_t phase = (uint8_t)(frame % 12u);
    for (int16_t yy = (int16_t)(HERO_Y + HERO_H - 4u) - phase; yy > horizon; yy -= 12) {
      ht5023_gfx_draw_hline(screen, (int16_t)(HERO_X + 2u), yy, (int16_t)(HERO_W - 4u),
                            ht5023_st7789_color565(70, 220, 255));
    }
    for (uint8_t i = 0u; i <= 8u; ++i) {
      int16_t xx = (int16_t)(HERO_X + 2u + (i * (HERO_W - 5u)) / 8u);
      ht5023_gfx_draw_line(screen, (int16_t)(HERO_X + HERO_W / 2u), horizon, xx,
                           (int16_t)(HERO_Y + HERO_H - 3u), ht5023_st7789_color565(42, 175, 220));
    }
  }

  // Mountains
  ht5023_gfx_draw_line(screen, (int16_t)(HERO_X + 8u),  horizon + 8, (int16_t)(HERO_X + 40u), horizon - 14, ht5023_st7789_color565(90, 35, 130));
  ht5023_gfx_draw_line(screen, (int16_t)(HERO_X + 40u), horizon - 14, (int16_t)(HERO_X + 78u), horizon + 8, ht5023_st7789_color565(90, 35, 130));
  ht5023_gfx_draw_line(screen, (int16_t)(HERO_X + 60u), horizon + 8, (int16_t)(HERO_X + 102u), horizon - 18, ht5023_st7789_color565(120, 50, 160));
  ht5023_gfx_draw_line(screen, (int16_t)(HERO_X + 102u), horizon - 18, (int16_t)(HERO_X + 150u), horizon + 8, ht5023_st7789_color565(120, 50, 160));

  // Radar/drone motif
  {
    int16_t cx = (int16_t)(HERO_X + 66u + (sin64s((uint8_t)(frame * 2u)) / 2));
    int16_t cy = (int16_t)(HERO_Y + 62u + (sin64s((uint8_t)(frame + 13u)) / 3));
    int16_t a = (int16_t)(sin64s((uint8_t)(frame * 4u)) / 3);
    int16_t b = (int16_t)(sin64s((uint8_t)(frame * 4u + 16u)) / 3);

    ht5023_gfx_draw_circle(screen, cx, cy, 17, ht5023_st7789_color565(70, 255, 220));
    ht5023_gfx_draw_circle(screen, cx, cy, 22, ht5023_st7789_color565(34, 120, 190));
    ht5023_gfx_draw_line(screen, cx - 13, cy, cx + 13, cy, ht5023_st7789_color565(240, 240, 255));
    ht5023_gfx_draw_line(screen, cx, cy - 13, cx, cy + 13, ht5023_st7789_color565(255, 100, 200));
    ht5023_gfx_draw_triangle(screen,
                             (int16_t)(cx + a), (int16_t)(cy - 10 + b),
                             (int16_t)(cx - 11 - b), (int16_t)(cy + 8 + a),
                             (int16_t)(cx + 11 + b), (int16_t)(cy + 8 - a),
                             ht5023_st7789_color565(255, 215, 70));
    ht5023_gfx_fill_circle(screen, cx, cy, 3, ht5023_st7789_color565(255, 255, 255));
  }

  // Moving icon and trail.
  {
    int16_t px = (int16_t)(HERO_X + 112u + (sin64s((uint8_t)(frame * 2u + 10u)) / 2));
    int16_t py = (int16_t)(HERO_Y + 62u + (sin64s((uint8_t)(frame * 3u + 5u)) / 4));
    ht5023_gfx_blit_rgb565(screen, px, py, ICON_W, ICON_H, g_icon_16x16, ICON_W);
    for (uint8_t t = 1u; t <= 4u; ++t) {
      ht5023_gfx_fill_circle(screen,
                             (int16_t)(px - (int16_t)(t * 5u)),
                             (int16_t)(py + 7u + ((frame + t) & 1u)),
                             1,
                             ht5023_st7789_color565(255, (uint8_t)(160u - t * 20u), 60u));
    }
  }

  // Scan beam.
  {
    int16_t sx = (int16_t)(HERO_X + 150u + ((frame * 3u) % 34u));
    ht5023_gfx_draw_vline(screen, sx, (int16_t)(HERO_Y + 8u), 64, ht5023_st7789_color565(120, 255, 220));
    ht5023_gfx_draw_vline(screen, (int16_t)(sx + 1), (int16_t)(HERO_Y + 10u), 60, ht5023_st7789_color565(40, 170, 140));
  }

  ht5023_text_default_style(&st, ht5023_st7789_color565(220, 235, 255), 0u);
  st.opaque_bg = false;
  ht5023_text_draw_str(screen, (int16_t)(HERO_X + 8u), (int16_t)(HERO_Y + 8u), "MAIN FEED", &st);
  ht5023_text_draw_str(screen, (int16_t)(HERO_X + 8u), (int16_t)(HERO_Y + 18u), "DMA-ACCELERATED LCD WRITES", &st);
}

static void render_telemetry_card(ht5023_gfx_surface_t* screen, uint16_t frame, const st7789_dma_view_t* dma) {
  const int16_t x = (int16_t)(SIDEBAR_X + 2u);
  const int16_t y = (int16_t)(CARD1_Y + 2u);
  const int16_t w = (int16_t)(SIDEBAR_W - 4u);
  const int16_t h = (int16_t)(CARD_H - 4u);
  ht5023_text_style_t st;

  for (int16_t row = 0; row < h; row += 2) {
    uint16_t c = ht5023_st7789_color565((uint8_t)(10u + row), (uint8_t)(14u + row / 2u), (uint8_t)(22u + row * 2u));
    ht5023_gfx_fill_rect(screen, x, (int16_t)(y + row), w, 2, c);
  }
  ht5023_gfx_draw_rect(screen, x, y, w, h, ht5023_st7789_color565(90, 130, 220));

  ht5023_text_default_style(&st, ht5023_st7789_color565(236,240,255), 0u);
  st.opaque_bg = false;
  ht5023_text_draw_str(screen, (int16_t)(x + 6), (int16_t)(y + 6), "TELEMETRY", &st);

  // Pulse rings
  {
    int16_t cx = (int16_t)(x + 26);
    int16_t cy = (int16_t)(y + 35);
    ht5023_gfx_draw_circle(screen, cx, cy, 8, ht5023_st7789_color565(80, 255, 220));
    ht5023_gfx_draw_circle(screen, cx, cy, (int16_t)(12 + ((frame >> 1u) & 3u)), ht5023_st7789_color565(35, 130, 180));
    ht5023_gfx_fill_circle(screen, cx, cy, 2, ht5023_st7789_color565(255,255,255));
    ht5023_gfx_draw_line(screen, cx, cy, (int16_t)(cx + (sin64s((uint8_t)(frame * 4u)) / 4)),
                         (int16_t)(cy - (sin64s((uint8_t)(frame * 4u + 16u)) / 4)),
                         ht5023_st7789_color565(255, 90, 210));
  }

  // Bars
  for (uint8_t i = 0u; i < 4u; ++i) {
    uint8_t amp = (uint8_t)(8u + (SIN64[(uint8_t)(frame * 3u + i * 9u)] - 10u) / 6u);
    int16_t bx = (int16_t)(x + 46u + i * 10u);
    ht5023_gfx_draw_rect(screen, bx, (int16_t)(y + 23u), 6, 28, ht5023_st7789_color565(55, 70, 110));
    ht5023_gfx_fill_rect(screen, (int16_t)(bx + 1), (int16_t)(y + 50u - amp), 4, amp,
                         (i & 1u) ? ht5023_st7789_color565(80, 220, 255)
                                  : ht5023_st7789_color565(255, 190, 70));
  }

  {
    ht5023_text_style_t st2;
    ht5023_text_default_style(&st2, ht5023_st7789_color565(180, 255, 220), 0u);
    st2.opaque_bg = false;
    draw_text_num(screen, (int16_t)(x + 46u), (int16_t)(y + 8u), "DMA ", dma ? dma->dma_ok : 0u, &st2);
  }
}

static void draw_test_chip(const ht5023_gfx_surface_t* s,
                           int16_t x,
                           int16_t y,
                           const char* label,
                           bool pass) {
  ht5023_text_style_t st;
  uint16_t border = pass ? ht5023_st7789_color565(110, 255, 180) : ht5023_st7789_color565(255, 210, 70);
  uint16_t fill = pass ? ht5023_st7789_color565(18, 46, 34) : ht5023_st7789_color565(52, 38, 16);
  uint16_t fg = pass ? ht5023_st7789_color565(200, 255, 220) : ht5023_st7789_color565(255, 230, 130);
  ht5023_gfx_fill_rect(s, x, y, 40, 10, fill);
  ht5023_gfx_draw_rect(s, x, y, 40, 10, border);
  ht5023_text_default_style(&st, fg, fill);
  st.opaque_bg = false;
  ht5023_text_draw_str(s, (int16_t)(x + 3), (int16_t)(y + 2), label, &st);
}

static void render_tests_card(ht5023_gfx_surface_t* screen, uint16_t frame, const visual_test_state_t* tests) {
  const int16_t x = (int16_t)(SIDEBAR_X + 2u);
  const int16_t y = (int16_t)(CARD2_Y + 2u);
  const int16_t w = (int16_t)(SIDEBAR_W - 4u);
  const int16_t h = (int16_t)(CARD_H - 4u);
  ht5023_text_style_t st;

  for (int16_t row = 0; row < h; row += 2) {
    uint16_t c = ht5023_st7789_color565((uint8_t)(18u + row), (uint8_t)(7u + row / 4u), (uint8_t)(20u + row));
    ht5023_gfx_fill_rect(screen, x, (int16_t)(y + row), w, 2, c);
  }
  ht5023_gfx_draw_rect(screen, x, y, w, h, ht5023_st7789_color565(255, 120, 210));

  ht5023_text_default_style(&st, ht5023_st7789_color565(255, 235, 250), 0u);
  st.opaque_bg = false;
  ht5023_text_draw_str(screen, (int16_t)(x + 6), (int16_t)(y + 6), "VISUAL TESTS", &st);

  // RGB stripe mini test.
  ht5023_gfx_fill_rect(screen, (int16_t)(x + 6), (int16_t)(y + 19), 26, 8, ht5023_st7789_color565(255, 0, 0));
  ht5023_gfx_fill_rect(screen, (int16_t)(x + 32), (int16_t)(y + 19), 26, 8, ht5023_st7789_color565(0, 255, 0));
  ht5023_gfx_fill_rect(screen, (int16_t)(x + 58), (int16_t)(y + 19), 26, 8, ht5023_st7789_color565(0, 80, 255));
  ht5023_gfx_draw_rect(screen, (int16_t)(x + 6), (int16_t)(y + 19), 78, 8, ht5023_st7789_color565(255,255,255));

  // Checker + alignment target.
  for (uint8_t cy = 0u; cy < 4u; ++cy) {
    for (uint8_t cx = 0u; cx < 6u; ++cx) {
      uint16_t c = (((cx + cy + frame) & 1u) == 0u)
        ? ht5023_st7789_color565(240, 240, 255)
        : ht5023_st7789_color565(24, 26, 40);
      ht5023_gfx_fill_rect(screen, (int16_t)(x + 6u + cx * 4u), (int16_t)(y + 31u + cy * 4u), 4, 4, c);
    }
  }
  ht5023_gfx_draw_rect(screen, (int16_t)(x + 34u), (int16_t)(y + 31u), 18, 18, ht5023_st7789_color565(100, 230, 255));
  ht5023_gfx_draw_line(screen, (int16_t)(x + 34u), (int16_t)(y + 40u), (int16_t)(x + 51u), (int16_t)(y + 40u),
                       ht5023_st7789_color565(255, 255, 255));
  ht5023_gfx_draw_line(screen, (int16_t)(x + 43u), (int16_t)(y + 31u), (int16_t)(x + 43u), (int16_t)(y + 48u),
                       ht5023_st7789_color565(255, 255, 255));

  draw_test_chip(screen, (int16_t)(x + 56u), (int16_t)(y + 31u), tests->gfx_draw_ok ? "GFX OK" : "GFX ?", tests->gfx_draw_ok);
  draw_test_chip(screen, (int16_t)(x + 56u), (int16_t)(y + 43u), tests->fb_flush_ok ? "FB OK" : "FB WAIT", tests->fb_flush_ok);
  draw_test_chip(screen, (int16_t)(x + 6u),  (int16_t)(y + 55u), tests->dma_seen ? "DMA OK" : "DMA WAIT", tests->dma_seen);
  draw_test_chip(screen, (int16_t)(x + 48u), (int16_t)(y + 55u), tests->panel_320x240 ? "320x240" : "SIZE ?", tests->panel_320x240);
}

static void render_hud_tile(ht5023_fb_rgb565_t* fb, uint16_t frame, const st7789_dma_view_t* dma) {
  ht5023_gfx_surface_t tile;
  ht5023_text_style_t st;
  const uint16_t bg = ht5023_st7789_color565(14, 16, 24);
  const uint16_t grid = ht5023_st7789_color565(46, 68, 100);
  const uint16_t cyan = ht5023_st7789_color565(100, 240, 255);

  ht5023_fb_make_gfx_surface(fb, &tile);
  ht5023_fb_clear(fb, bg);
  ht5023_gfx_draw_rect(&tile, 0, 0, HUD_TILE_W, HUD_TILE_H, ht5023_st7789_color565(180, 220, 255));

  for (uint8_t x = 4u; x < 144u; x += 8u) ht5023_gfx_draw_vline(&tile, x, 4, 34, grid);
  for (uint8_t y = 4u; y < 38u; y += 6u) ht5023_gfx_draw_hline(&tile, 4, y, 144, grid);

  for (uint8_t x = 0u; x < 138u; ++x) {
    uint8_t p0 = (uint8_t)(frame * 3u + x);
    uint8_t p1 = (uint8_t)(frame * 3u + x + 1u);
    int16_t y0 = (int16_t)(20 + (sin64s(p0) / 5) + (sin64s((uint8_t)(p0 * 2u + 7u)) / 9));
    int16_t y1 = (int16_t)(20 + (sin64s(p1) / 5) + (sin64s((uint8_t)(p1 * 2u + 7u)) / 9));
    ht5023_gfx_draw_line(&tile, (int16_t)(5 + x), (int16_t)(6 + y0), (int16_t)(6 + x), (int16_t)(6 + y1), cyan);
  }
  ht5023_gfx_draw_hline(&tile, 4, 24, 144, ht5023_st7789_color565(255, 70, 160));

  // right-side compact bars
  for (uint8_t i = 0u; i < 4u; ++i) {
    uint8_t amp = (uint8_t)(6u + (SIN64[(uint8_t)(frame * 2u + i * 13u)] - 10u) / 8u);
    int16_t bx = (int16_t)(150u + i * 6u);
    ht5023_gfx_fill_rect(&tile, bx, (int16_t)(34u - amp), 4, amp,
                         (i & 1u) ? ht5023_st7789_color565(255, 180, 60)
                                  : ht5023_st7789_color565(100, 220, 255));
  }

  ht5023_text_default_style(&st, ht5023_st7789_color565(235,240,255), bg);
  st.opaque_bg = false;
  ht5023_text_draw_str(&tile, 6, 2, "ANALYZER", &st);

  {
    ht5023_text_style_t st2;
    ht5023_text_default_style(&st2, ht5023_st7789_color565(180, 255, 215), bg);
    st2.opaque_bg = false;
    draw_text_num(&tile, 6, 38, "DMA:", dma ? dma->dma_ok : 0u, &st2);
    draw_text_num(&tile, 52, 38, "FB:", dma ? dma->dma_fallback : 0u, &st2);
    draw_text_num(&tile, 94, 38, "F:", frame, &st2);
  }
}

static void render_status_panel(ht5023_gfx_surface_t* screen,
                                uint16_t frame,
                                const st7789_dma_view_t* dma,
                                const visual_test_state_t* tests) {
  const int16_t x = (int16_t)(STATUS_X + 2u);
  const int16_t y = (int16_t)(STATUS_Y + 2u);
  const int16_t w = (int16_t)(STATUS_W - 4u);
  const int16_t h = (int16_t)(STATUS_H - 4u);
  ht5023_text_style_t st;

  for (int16_t row = 0; row < h; row += 2) {
    uint16_t c = ht5023_st7789_color565((uint8_t)(22u + row), (uint8_t)(14u + row / 2u), (uint8_t)(10u + row / 4u));
    ht5023_gfx_fill_rect(screen, x, (int16_t)(y + row), w, 2, c);
  }
  ht5023_gfx_draw_rect(screen, x, y, w, h, ht5023_st7789_color565(255, 190, 100));

  ht5023_text_default_style(&st, ht5023_st7789_color565(255,245,220), 0u);
  st.opaque_bg = false;
  ht5023_text_draw_str(screen, (int16_t)(x + 6u), (int16_t)(y + 4u), "SYSTEM / TESTS", &st);

  // progress rails
  for (uint8_t r = 0u; r < 3u; ++r) {
    uint8_t p = (uint8_t)((frame * (r + 2u) * 3u) % 120u);
    int16_t ry = (int16_t)(y + 18u + r * 8u);
    ht5023_gfx_draw_rect(screen, (int16_t)(x + 6u), ry, 120, 5, ht5023_st7789_color565(90, 70, 44));
    ht5023_gfx_fill_rect(screen, (int16_t)(x + 7u), (int16_t)(ry + 1), p, 3,
                         (r == 0u) ? ht5023_st7789_color565(255, 120, 70) :
                         (r == 1u) ? ht5023_st7789_color565(100, 220, 255) :
                                     ht5023_st7789_color565(255, 90, 210));
  }

  // test badges
  draw_test_chip(screen, (int16_t)(x + 6u),   (int16_t)(y + 42u), "DMA OK",  tests->dma_seen);
  draw_test_chip(screen, (int16_t)(x + 48u),  (int16_t)(y + 42u), "FB OK",   tests->fb_flush_ok);
  draw_test_chip(screen, (int16_t)(x + 90u),  (int16_t)(y + 42u), "320x240", tests->panel_320x240);

  {
    ht5023_text_style_t st2;
    ht5023_text_default_style(&st2, ht5023_st7789_color565(255, 230, 160), 0u);
    st2.opaque_bg = false;
    draw_text_num(screen, (int16_t)(x + 132u), (int16_t)(y + 18u), "D", dma ? dma->dma_ok : 0u, &st2);
    draw_text_num(screen, (int16_t)(x + 132u), (int16_t)(y + 28u), "P", dma ? dma->dma_fallback : 0u, &st2);
  }
}

static void draw_size_warning(ht5023_gfx_surface_t* screen, uint16_t w, uint16_t h) {
  ht5023_text_style_t t;
  ht5023_gfx_fill_rect(screen, 0, 0, (int16_t)w, (int16_t)h, ht5023_st7789_color565(20, 0, 0));
  ht5023_gfx_draw_rect(screen, 8, 8, (int16_t)(w - 16u), (int16_t)(h - 16u), ht5023_st7789_color565(255, 120, 120));
  ht5023_text_default_style(&t, ht5023_st7789_color565(255,255,255), 0u);
  t.opaque_bg = false;
  t.scale = 2u;
  ht5023_text_draw_str(screen, 18, 20, "SIZE MISMATCH", &t);
  t.scale = 1u;
  ht5023_text_draw_str(screen, 18, 48, "Expected logical 320x240 after rotation.", &t);
  ht5023_text_draw_str(screen, 18, 60, "Override ST7789_DEMO_WIDTH/HEIGHT/X_OFFSET/Y_OFFSET", &t);
  ht5023_text_draw_str(screen, 18, 72, "for your panel and keep rotation=90 for this layout.", &t);
}

int main(void) {
  const ht5023_spi_master_cfg_t spi_cfg = { .spr = 2u, .cpol = false, .cpha = false };
  st7789_hal_ctx_t hal = { .gpio_base = GPIOA_BASE_ADDR, .dc_pin = ST7789_DC_PIN, .rst_pin = ST7789_RST_PIN };
  const ht5023_st7789_dma_config_t lcd_dma_cfg = {
    .channel = HT5023_DMA_CH0,
    .request = HT5023_DMA_REQ_SPI_TX,
    .tx_reg_addr = (uintptr_t)(SPI_BASE_ADDR + SPI_SPIDAT_OFFSET),
    .rx_channel = HT5023_DMA_CH1,
    .rx_request = HT5023_DMA_REQ_SPI_RX,
    .rx_reg_addr = (uintptr_t)(SPI_BASE_ADDR + SPI_SPIDAT_OFFSET),
    .timeout_polls = ST7789_DMA_TIMEOUT_POLLS,
    .prime_tx = st7789_spi_prime_tx_cb,
    .wait_complete = st7789_spi_wait_complete_cb,
    .min_bytes = ST7789_DMA_MIN_BYTES,
    .stage_buffer = g_st7789_dma_stage,
    .stage_buffer_len = sizeof(g_st7789_dma_stage),
    .rx_stage_buffer = g_st7789_dma_rx_stage,
    .rx_stage_buffer_len = sizeof(g_st7789_dma_rx_stage),
    .max_fail_streak = 0u,
  };
  ht5023_st7789_t lcd;
  ht5023_gfx_surface_t screen;
  ht5023_fb_rgb565_t hud_fb;
  visual_test_state_t tests = {0};
  const ht5023_st7789_config_t lcd_cfg = {
    .write = st7789_spi_write_cb,
    .dc_write = st7789_dc_cb,
    .cs_write = st7789_cs_cb,
    .rst_write = st7789_rst_cb,
    .delay_ms = st7789_delay_ms_cb,
    .dma = &lcd_dma_cfg,
    .user = &hal,
    .width = ST7789_DEMO_WIDTH,
    .height = ST7789_DEMO_HEIGHT,
    .x_offset = ST7789_DEMO_X_OFFSET,
    .y_offset = ST7789_DEMO_Y_OFFSET,
    .invert_colors = true,
    .bgr_order = true,
  };

  (void)ht5023_debug_uart2_init_log(ST7789_DEMO_UART_SYSCLK_HZ, DEBUG_UART_BAUD, ST7789_DEMO_LOG_LEVEL);
  ht5023_log_write(HT5023_LOG_LEVEL_INFO,
                   "ui_dma",
                   "boot cpu=%lu uart_fsys=%lu",
                   (unsigned long)ST7789_DEMO_CPU_HZ,
                   (unsigned long)ST7789_DEMO_UART_SYSCLK_HZ);
  ht5023_dma_set_request_bias(0);
  ht5023_dma_set_sram_base(ST7789_DMA_SRAM_BASE);
  ht5023_log_write(HT5023_LOG_LEVEL_INFO,
                   "ui_dma",
                   "dma cfg bias=%d sram_base=0x%x",
                   0,
                   (unsigned)ST7789_DMA_SRAM_BASE);
  ht5023_log_write(HT5023_LOG_LEVEL_INFO,
                   "ui_dma",
                   "panel cfg w=%u h=%u xoff=%u yoff=%u",
                   (unsigned)ST7789_DEMO_WIDTH,
                   (unsigned)ST7789_DEMO_HEIGHT,
                   (unsigned)ST7789_DEMO_X_OFFSET,
                   (unsigned)ST7789_DEMO_Y_OFFSET);

  led_gpio_init();
  gpio_pin_output_init(GPIOA_BASE_ADDR, ST7789_DC_PIN, true);
  gpio_pin_output_init(GPIOA_BASE_ADDR, ST7789_RST_PIN, true);
  icon_init();

  (void)ht5023_spi_master_init(&spi_cfg);
  ht5023_log_write(HT5023_LOG_LEVEL_INFO, "ui_dma", "spi init spr=%u", (unsigned)spi_cfg.spr);
  if (!ht5023_st7789_init(&lcd, &lcd_cfg)) {
    ht5023_log_write(HT5023_LOG_LEVEL_ERROR, "ui_dma", "lcd init failed");
    for (;;) { led_toggle(); delay_ms_blocking(200u); }
  }

  ht5023_st7789_set_rotation(&lcd, HT5023_ST7789_ROTATION_90);
  ht5023_log_write(HT5023_LOG_LEVEL_INFO,
                   "ui_dma",
                   "rotation=%u lcd=%ux%u",
                   (unsigned)lcd.rotation,
                   (unsigned)lcd.width,
                   (unsigned)lcd.height);
  ht5023_gfx_surface_init_st7789(&screen, &lcd);
  (void)ht5023_fb_init(&hud_fb, g_hud_tile_pixels, HUD_TILE_W, HUD_TILE_H, HUD_TILE_W);

  tests.gfx_draw_ok = true;
  tests.panel_320x240 = (lcd.width == LOGICAL_W && lcd.height == LOGICAL_H);

  if (!tests.panel_320x240) {
    ht5023_log_write(HT5023_LOG_LEVEL_ERROR,
                     "ui_dma",
                     "size mismatch lcd=%ux%u expected=%ux%u",
                     (unsigned)lcd.width,
                     (unsigned)lcd.height,
                     (unsigned)LOGICAL_W,
                     (unsigned)LOGICAL_H);
    draw_size_warning(&screen, lcd.width, lcd.height);
    for (;;) { led_toggle(); delay_ms_blocking(300u); }
  }

  draw_static_chrome(&screen);
  ht5023_log_write(HT5023_LOG_LEVEL_INFO, "ui_dma", "static chrome drawn");

  for (uint16_t frame = 0u;; ++frame) {
    const st7789_dma_view_t dma = st7789_dma_view(&lcd);
    bool flushed;

    draw_header(&screen, frame, &dma);
    render_hero_scene(&screen, frame);
    render_telemetry_card(&screen, frame, &dma);
    render_hud_tile(&hud_fb, frame, &dma);
    flushed = ht5023_fb_flush_dirty_to_st7789(&hud_fb, &lcd, HUD_TILE_X, HUD_TILE_Y);
    tests.fb_flush_ok = tests.fb_flush_ok || flushed;
    tests.dma_seen = (dma.dma_ok != 0u);
    render_tests_card(&screen, frame, &tests);
    render_status_panel(&screen, frame, &dma, &tests);

    if ((frame % 30u) == 0u) {
      ht5023_log_write(HT5023_LOG_LEVEL_INFO,
                       "ui_dma",
                       "frame=%u dma_en=%u dma_ok=%lu dma_fb=%lu flushed=%u",
                       (unsigned)frame,
                       (unsigned)dma.dma_enabled,
                       (unsigned long)dma.dma_ok,
                       (unsigned long)dma.dma_fallback,
                       (unsigned)flushed);
    }

    if ((frame & 3u) == 0u) led_toggle();
    delay_ms_blocking(22u);
  }
}
