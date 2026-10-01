// examples/energy_meter_dashboard/main.c — dark 320x240 smart energy meter dashboard on ST7789 using DMA-backed SPI
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/dma.h"
#include "ht5023/fb.h"
#include "ht5023/gfx.h"
#include "ht5023/spi.h"
#include "ht5023/st7789.h"
#include "ht5023/text.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_toggle(void);

#ifndef DASH_CPU_HZ
#define DASH_CPU_HZ HT5023_FCPU_HZ
#endif
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

#define LOGICAL_W             320u
#define LOGICAL_H             240u

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

#define HEADER_H              28
#define HERO_X                8
#define HERO_Y                38
#define HERO_W                182
#define HERO_H                120
#define SIDEBAR_X             198
#define SIDEBAR_Y             38
#define SIDEBAR_W             114
#define CARD_W                SIDEBAR_W
#define CARD_H                34
#define CARD_GAP              8
#define CARD1_X               SIDEBAR_X
#define CARD2_X               SIDEBAR_X
#define CARD3_X               SIDEBAR_X
#define CARD1_Y               SIDEBAR_Y
#define CARD2_Y               (CARD1_Y + CARD_H + CARD_GAP)
#define CARD3_Y               (CARD2_Y + CARD_H + CARD_GAP)
#define CHIP_W                52
#define CHIP_H                12
#define CHIP_GAP              6
#define CHIP1_X               (HERO_X + 8)
#define CHIP2_X               (CHIP1_X + CHIP_W + CHIP_GAP)
#define CHIP3_X               (CHIP2_X + CHIP_W + CHIP_GAP)
#define CHIPS_Y               (HERO_Y + 24)
#define TREND_X               HERO_X
#define TREND_Y               HERO_Y
#define TREND_W               HERO_W
#define TREND_H               HERO_H
#define STATUS_X              8
#define STATUS_Y              168
#define STATUS_W              304
#define STATUS_H              64

#define TREND_POINTS          52
#define SPARK_POINTS          32
#define ST7789_DMA_MIN_BYTES      32u
#define ST7789_DMA_TIMEOUT_POLLS  30000u
#define ST7789_DMA_MAX_FAILS      2u
#define ST7789_DMA_STAGE_BYTES    2048u
#define PANEL_BUF_W              316u
#define PANEL_BUF_H               40u

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

static uint8_t g_kw_hist[TREND_POINTS];
static uint8_t g_demand_hist[SPARK_POINTS];
static uint8_t g_dma_stage[ST7789_DMA_STAGE_BYTES] __attribute__((aligned(4)));
static uint8_t g_dma_rx_stage[ST7789_DMA_STAGE_BYTES] __attribute__((aligned(4)));
static uint16_t g_panel_pixels[PANEL_BUF_W * PANEL_BUF_H];
static ht5023_fb_rgb565_t g_panel_fb;

static void draw_dial_widget(const ht5023_gfx_surface_t* screen,
                             int16_t cx,
                             int16_t cy,
                             int16_t r,
                             uint8_t pct,
                             uint16_t color,
                             const char* label);

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
  delay_cycles((DASH_CPU_HZ / 9000u) * ms);
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

static void st7789_cs_cb(bool high, void* user) {
  (void)user;
  ht5023_spi_set_cs(high);
}

static void st7789_dc_cb(bool high, void* user) {
  st7789_hal_ctx_t* ctx = (st7789_hal_ctx_t*)user;
  gpio_pin_write(ctx->gpio_base, ctx->dc_pin, high);
}

static void st7789_rst_cb(bool high, void* user) {
  st7789_hal_ctx_t* ctx = (st7789_hal_ctx_t*)user;
  gpio_pin_write(ctx->gpio_base, ctx->rst_pin, high);
}

static void st7789_delay_ms_cb(uint32_t ms, void* user) {
  (void)user;
  delay_ms_blocking(ms);
}

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

static uint8_t u32_to_dec(char* out, uint8_t out_sz, uint32_t v) {
  char tmp[10];
  uint8_t n = 0u;
  uint8_t t = 0u;

  if (out_sz == 0u) return 0u;
  do {
    tmp[t++] = (char)('0' + (v % 10u));
    v /= 10u;
  } while (v != 0u && t < (uint8_t)sizeof(tmp));

  while (t != 0u && n < (uint8_t)(out_sz - 1u)) {
    out[n++] = tmp[--t];
  }
  out[n] = '\0';
  return n;
}

static void format_value(char* out, uint8_t out_sz, uint32_t v, const char* suffix) {
  uint8_t n = u32_to_dec(out, out_sz, v);
  while (suffix && *suffix && n < (uint8_t)(out_sz - 1u)) {
    out[n++] = *suffix++;
  }
  out[n] = '\0';
}

static void draw_panel_frame(const ht5023_gfx_surface_t* screen,
                             int16_t x,
                             int16_t y,
                             int16_t w,
                             int16_t h,
                             uint16_t c0,
                             uint16_t c1) {
  ht5023_gfx_draw_rect(screen, x, y, w, h, c0);
  ht5023_gfx_draw_rect(screen, (int16_t)(x + 1), (int16_t)(y + 1), (int16_t)(w - 2), (int16_t)(h - 2), c1);
}

static void draw_meter_card_panel(const ht5023_gfx_surface_t* panel,
                                  const char* label,
                                  uint32_t value,
                                  const char* unit,
                                  const char* tag,
                                  uint16_t base,
                                  uint16_t stripe,
                                  uint16_t accent,
                                  uint8_t gauge_pct) {
  ht5023_text_style_t st_label;
  ht5023_text_style_t st_value;
  ht5023_text_style_t st_tag;
  char buf[16];
  int16_t w = panel->width;
  int16_t h = panel->height;
  int16_t fill_w;

  if (gauge_pct > 100u) gauge_pct = 100u;
  ht5023_fb_clear(&g_panel_fb, base);

  for (int16_t y = 2; y < h; y += 4) {
    ht5023_gfx_fill_rect(panel, 0, y, w, 1, stripe);
  }
  ht5023_gfx_fill_rect(panel, 0, 0, w, 2, accent);
  ht5023_gfx_fill_rect(panel, 4, (int16_t)(h - 9), (int16_t)(w - 8), 5, ht5023_st7789_color565(8, 12, 18));
  ht5023_gfx_draw_rect(panel, 4, (int16_t)(h - 9), (int16_t)(w - 8), 5, ht5023_st7789_color565(36, 52, 66));
  fill_w = (int16_t)((((uint32_t)(w - 10)) * gauge_pct) / 100u);
  if (fill_w > 0) {
    ht5023_gfx_fill_rect(panel, 5, (int16_t)(h - 8), fill_w, 3, accent);
  }

  ht5023_gfx_fill_rect(panel, (int16_t)(w - 26), 4, 20, 8, ht5023_st7789_color565(8, 12, 18));
  ht5023_gfx_draw_rect(panel, (int16_t)(w - 26), 4, 20, 8, accent);

  ht5023_text_default_style(&st_label, ht5023_st7789_color565(212, 224, 236), 0u);
  st_label.opaque_bg = false;
  ht5023_text_default_style(&st_value, ht5023_st7789_color565(252, 255, 255), 0u);
  st_value.opaque_bg = false;
  st_value.scale = 2u;
  ht5023_text_default_style(&st_tag, accent, 0u);
  st_tag.opaque_bg = false;

  ht5023_text_draw_str(panel, 4, 4, label, &st_label);
  if (tag) ht5023_text_draw_str(panel, (int16_t)(w - 24), 5, tag, &st_tag);

  format_value(buf, (uint8_t)sizeof(buf), value, 0);
  ht5023_text_draw_str(panel, 4, 16, buf, &st_value);
  if (unit) ht5023_text_draw_str(panel, (int16_t)(w - 14), (int16_t)(h - 12), unit, &st_label);
}

static void kw_seed(uint8_t v) {
  for (uint8_t i = 0u; i < TREND_POINTS; ++i) g_kw_hist[i] = v;
}

static void demand_seed(uint8_t v) {
  for (uint8_t i = 0u; i < SPARK_POINTS; ++i) g_demand_hist[i] = v;
}

static void kw_push(uint8_t v) {
  for (uint8_t i = 0u; i < (uint8_t)(TREND_POINTS - 1u); ++i) g_kw_hist[i] = g_kw_hist[i + 1u];
  g_kw_hist[TREND_POINTS - 1u] = v;
}

static void demand_push(uint8_t v) {
  for (uint8_t i = 0u; i < (uint8_t)(SPARK_POINTS - 1u); ++i) g_demand_hist[i] = g_demand_hist[i + 1u];
  g_demand_hist[SPARK_POINTS - 1u] = v;
}

static bool panel_begin(uint16_t w, uint16_t h, uint16_t bg, ht5023_gfx_surface_t* out) {
  if (!out || w == 0u || h == 0u || w > PANEL_BUF_W || h > PANEL_BUF_H) return false;
  if (!ht5023_fb_init(&g_panel_fb, g_panel_pixels, w, h, w)) return false;
  ht5023_fb_make_gfx_surface(&g_panel_fb, out);
  ht5023_fb_clear(&g_panel_fb, bg);
  return true;
}

static void panel_flush(ht5023_st7789_t* lcd, uint16_t x, uint16_t y) {
  (void)ht5023_fb_flush_dirty_to_st7789(&g_panel_fb, lcd, x, y);
}

static void draw_trend_band(const ht5023_gfx_surface_t* panel,
                            int16_t band_y,
                            uint8_t latest_kw_pct,
                            uint8_t power_factor_pct,
                            uint8_t freq_pct) {
  const int16_t gx = 6;
  const int16_t gy = (int16_t)(8 - band_y);
  const int16_t gw = 92;
  const int16_t gh = 46;

  for (uint8_t i = 1u; i <= 3u; ++i) {
    int16_t yy = (int16_t)(gy + ((int16_t)(gh - 1) * i) / 4);
    ht5023_gfx_draw_hline(panel, gx, yy, gw, ht5023_st7789_color565(18, 30, 44));
  }
  for (uint8_t i = 1u; i < 6u; ++i) {
    int16_t xx = (int16_t)(gx + ((int16_t)(gw - 1) * i) / 6);
    ht5023_gfx_draw_vline(panel, xx, gy, gh, ht5023_st7789_color565(10, 20, 30));
  }
  for (uint8_t i = 0u; i < (uint8_t)(TREND_POINTS - 1u); ++i) {
    int16_t x0 = (int16_t)(gx + ((int32_t)(gw - 1) * i) / (TREND_POINTS - 1));
    int16_t x1 = (int16_t)(gx + ((int32_t)(gw - 1) * (i + 1u)) / (TREND_POINTS - 1));
    int16_t y0 = (int16_t)(gy + (gh - 1) - ((int32_t)(gh - 1) * g_kw_hist[i]) / 100);
    int16_t y1 = (int16_t)(gy + (gh - 1) - ((int32_t)(gh - 1) * g_kw_hist[i + 1u]) / 100);
    ht5023_gfx_draw_line(panel, x0, y0, x1, y1, ht5023_st7789_color565(78, 182, 206));
  }

  ht5023_gfx_fill_circle(panel,
                         (int16_t)(gx + gw - 1),
                         (int16_t)(gy + (gh - 1) - ((int32_t)(gh - 1) * latest_kw_pct) / 100),
                         2,
                         ht5023_st7789_color565(188, 150, 82));
  ht5023_gfx_draw_vline(panel, (int16_t)(gx + gw + 10), gy, gh,
                        ht5023_st7789_color565(24, 38, 52));
  draw_dial_widget(panel,
                   124,
                   (int16_t)(30 - band_y),
                   14,
                   power_factor_pct,
                   ht5023_st7789_color565(88, 168, 192),
                   "PF");
  draw_dial_widget(panel,
                   154,
                   (int16_t)(30 - band_y),
                   11,
                   freq_pct,
                   ht5023_st7789_color565(184, 144, 82),
                   "HZ");
}

static void draw_static_layout(const ht5023_gfx_surface_t* screen) {
  ht5023_text_style_t body;

  for (int16_t y = 0; y < (int16_t)LOGICAL_H; y += 2) {
    uint8_t r = (uint8_t)(1 + (y / 80));
    uint8_t g = (uint8_t)(4 + (y / 24));
    uint8_t b = (uint8_t)(9 + (y / 10));
    ht5023_gfx_fill_rect(screen, 0, y, (int16_t)LOGICAL_W, 2, ht5023_st7789_color565(r, g, b));
  }
  ht5023_gfx_fill_circle(screen, 282, 38, 34, ht5023_st7789_color565(4, 10, 20));
  ht5023_gfx_fill_circle(screen, 282, 38, 20, ht5023_st7789_color565(8, 22, 34));
  ht5023_gfx_fill_circle(screen, 36, 210, 28, ht5023_st7789_color565(4, 12, 18));
  ht5023_gfx_fill_circle(screen, 36, 210, 16, ht5023_st7789_color565(10, 24, 22));
  for (int16_t x = 0; x < (int16_t)LOGICAL_W; x += 24) {
    ht5023_gfx_draw_vline(screen,
                          x,
                          HEADER_H,
                          (int16_t)(LOGICAL_H - HEADER_H),
                          ht5023_st7789_color565(5, 14, 22));
  }
  for (int16_t d = -30; d < 250; d += 24) {
    ht5023_gfx_draw_line(screen, d, LOGICAL_H, (int16_t)(d + 70), HEADER_H,
                         ht5023_st7789_color565(4, 9, 14));
  }
  for (uint8_t i = 0u; i < 9u; ++i) {
    int16_t px = (int16_t)(22 + i * 32);
    int16_t py = (int16_t)(HEADER_H + 10 + ((i & 1u) ? 8 : 0));
    ht5023_gfx_fill_circle(screen, px, py, 1, ht5023_st7789_color565(24, 54, 70));
  }

  draw_panel_frame(screen,
                   0,
                   0,
                   (int16_t)LOGICAL_W,
                   HEADER_H,
                   ht5023_st7789_color565(70, 180, 172),
                   ht5023_st7789_color565(14, 38, 42));

  draw_panel_frame(screen, HERO_X, HERO_Y, HERO_W, HERO_H,
                   ht5023_st7789_color565(74, 168, 196), ht5023_st7789_color565(12, 24, 38));
  draw_panel_frame(screen, CARD1_X, CARD1_Y, CARD_W, CARD_H,
                   ht5023_st7789_color565(82, 164, 208), ht5023_st7789_color565(14, 28, 40));
  draw_panel_frame(screen, CARD2_X, CARD2_Y, CARD_W, CARD_H,
                   ht5023_st7789_color565(84, 174, 124), ht5023_st7789_color565(12, 34, 22));
  draw_panel_frame(screen, CARD3_X, CARD3_Y, CARD_W, CARD_H,
                   ht5023_st7789_color565(198, 152, 84), ht5023_st7789_color565(40, 26, 12));
  draw_panel_frame(screen, STATUS_X, STATUS_Y, STATUS_W, STATUS_H,
                   ht5023_st7789_color565(92, 172, 112), ht5023_st7789_color565(12, 28, 16));

  ht5023_text_default_style(&body, ht5023_st7789_color565(172, 194, 214), 0u);
  body.opaque_bg = false;
  ht5023_text_draw_str(screen, TREND_X + 8, TREND_Y + 8, "ENERGY FLOW", &body);
  ht5023_text_draw_str(screen, STATUS_X + 8, STATUS_Y + 8, "PLANT HEALTH + EXPORT", &body);
  ht5023_text_draw_str(screen, CARD1_X + 8, CARD1_Y - 10, "INPUTS", &body);

  ht5023_gfx_draw_line(screen, (int16_t)(HERO_X + 18), (int16_t)(HERO_Y + 84),
                       (int16_t)(HERO_X + 80), (int16_t)(HERO_Y + 58),
                       ht5023_st7789_color565(46, 124, 144));
  ht5023_gfx_draw_line(screen, (int16_t)(HERO_X + 80), (int16_t)(HERO_Y + 58),
                       (int16_t)(HERO_X + 138), (int16_t)(HERO_Y + 80),
                       ht5023_st7789_color565(122, 166, 82));
  ht5023_gfx_draw_line(screen, (int16_t)(HERO_X + 80), (int16_t)(HERO_Y + 58),
                       (int16_t)(HERO_X + 150), (int16_t)(HERO_Y + 38),
                       ht5023_st7789_color565(178, 140, 82));
  ht5023_gfx_fill_circle(screen, (int16_t)(HERO_X + 18), (int16_t)(HERO_Y + 84), 5,
                         ht5023_st7789_color565(54, 128, 168));
  ht5023_gfx_fill_circle(screen, (int16_t)(HERO_X + 80), (int16_t)(HERO_Y + 58), 8,
                         ht5023_st7789_color565(82, 176, 188));
  ht5023_gfx_fill_circle(screen, (int16_t)(HERO_X + 138), (int16_t)(HERO_Y + 80), 5,
                         ht5023_st7789_color565(102, 172, 104));
  ht5023_gfx_fill_circle(screen, (int16_t)(HERO_X + 150), (int16_t)(HERO_Y + 38), 5,
                         ht5023_st7789_color565(208, 164, 92));
}

static void draw_header_dma_stats(const ht5023_gfx_surface_t* screen, const st7789_dma_view_t* dma) {
  ht5023_text_style_t st;
  char d_ok[10];
  char d_fb[10];

  ht5023_gfx_fill_rect(screen, 250, 2, 60, 9, ht5023_st7789_color565(8, 18, 22));
  ht5023_gfx_draw_rect(screen, 250, 2, 60, 9, ht5023_st7789_color565(44, 102, 98));
  ht5023_text_default_style(&st, ht5023_st7789_color565(176, 222, 216), 0u);
  st.opaque_bg = false;

  d_ok[0] = 'D';
  d_ok[1] = ':';
  (void)u32_to_dec(&d_ok[2], (uint8_t)(sizeof(d_ok) - 2u), dma ? dma->dma_ok : 0u);
  d_fb[0] = 'F';
  d_fb[1] = ':';
  if (dma && !dma->dma_enabled) {
    d_fb[2] = 'O';
    d_fb[3] = 'F';
    d_fb[4] = 'F';
    d_fb[5] = '\0';
  } else {
    (void)u32_to_dec(&d_fb[2], (uint8_t)(sizeof(d_fb) - 2u), dma ? dma->dma_fallback : 0u);
  }

  ht5023_text_draw_str(screen, 253, 2, d_ok, &st);
  ht5023_text_draw_str(screen, 280, 2, d_fb, &st);
}

static void draw_header_dynamic(const ht5023_gfx_surface_t* screen,
                                uint16_t frame,
                                bool relay_on,
                                const st7789_dma_view_t* dma) {
  ht5023_text_style_t title;
  ht5023_text_style_t body;
  char tick[8];
  uint8_t pulse = (uint8_t)(22u + ((uint8_t)(sin64s((uint8_t)(frame * 2u)) + 54) * 82u) / 108u);

  ht5023_gfx_fill_rect(screen,
                       2,
                       2,
                       (int16_t)(LOGICAL_W - 8u),
                       (int16_t)(HEADER_H - 4),
                       ht5023_st7789_color565((uint8_t)(3u + pulse / 18u), (uint8_t)(10u + pulse / 6u), (uint8_t)(16u + pulse / 5u)));
  ht5023_gfx_draw_hline(screen, 4, 4, 136, ht5023_st7789_color565(86, 196, 184));
  ht5023_gfx_draw_hline(screen, 4, 5, 112, ht5023_st7789_color565(52, 142, 170));
  ht5023_gfx_fill_rect(screen, 4, 8, 2, 12, ht5023_st7789_color565(180, 148, 74));

  ht5023_text_default_style(&title, ht5023_st7789_color565(252, 255, 255), 0u);
  title.opaque_bg = false;
  title.scale = 2u;
  ht5023_text_draw_str(screen, 10, 7, "SMART", &title);

  ht5023_text_default_style(&body, ht5023_st7789_color565(178, 198, 214), 0u);
  body.opaque_bg = false;
  ht5023_text_draw_str(screen, 94, 6, "ENERGY METER", &body);
  ht5023_text_draw_str(screen, 94, 15, relay_on ? "RELAY CLOSED" : "RELAY OPEN", &body);
  ht5023_gfx_fill_rect(screen, 168, 5, 44, 8, ht5023_st7789_color565(8, 16, 20));
  ht5023_gfx_draw_rect(screen, 168, 5, 44, 8, ht5023_st7789_color565(86, 196, 184));
  ht5023_text_draw_str(screen, 172, 6, "LIVE", &body);
  ht5023_gfx_draw_hline(screen, 168, 18, (int16_t)(18 + (pulse / 2u)),
                        ht5023_st7789_color565(180, 148, 74));

  (void)u32_to_dec(tick, (uint8_t)sizeof(tick), frame);
  ht5023_text_draw_str(screen, 224, 15, tick, &body);
  draw_header_dma_stats(screen, dma);
}

static void draw_header_dynamic_composited(ht5023_st7789_t* lcd,
                                           uint16_t frame,
                                           bool relay_on,
                                           const st7789_dma_view_t* dma) {
  ht5023_gfx_surface_t panel;

  if (!panel_begin((uint16_t)(LOGICAL_W - 4u), (uint16_t)(HEADER_H - 4u), ht5023_st7789_color565(2, 8, 14), &panel)) {
    return;
  }

  draw_header_dynamic(&panel, frame, relay_on, dma);
  panel_flush(lcd, 2u, 2u);
}

static void draw_trend_badge_composited(const ht5023_gfx_surface_t* screen,
                                        ht5023_st7789_t* lcd,
                                        uint8_t latest_kw_pct) {
  ht5023_gfx_surface_t panel;
  ht5023_text_style_t st;
  char pct[8];

  if (!panel_begin(28u, 8u, ht5023_st7789_color565(4, 10, 16), &panel)) {
    ht5023_text_default_style(&st, ht5023_st7789_color565(166, 186, 204), 0u);
    st.opaque_bg = false;
    format_value(pct, (uint8_t)sizeof(pct), latest_kw_pct, "%");
    ht5023_text_draw_str(screen, (int16_t)(TREND_X + TREND_W - 30), (int16_t)(TREND_Y + 8), pct, &st);
    return;
  }

  ht5023_text_default_style(&st, ht5023_st7789_color565(166, 186, 204), 0u);
  st.opaque_bg = false;
  format_value(pct, (uint8_t)sizeof(pct), latest_kw_pct, "%");
  ht5023_text_draw_str(&panel, 0, 0, pct, &st);
  panel_flush(lcd, (uint16_t)(TREND_X + TREND_W - 30), (uint16_t)(TREND_Y + 8));
}

static void draw_card_dynamic(const ht5023_gfx_surface_t* screen,
                              int16_t x,
                              int16_t y,
                              int16_t w,
                              int16_t h,
                              const char* label,
                              uint32_t value,
                              const char* suffix,
                              uint16_t bg) {
  ht5023_text_style_t st_label;
  ht5023_text_style_t st_val;
  char buf[16];

  ht5023_gfx_fill_rect(screen, (int16_t)(x + 2), (int16_t)(y + 2), (int16_t)(w - 4), (int16_t)(h - 4), bg);

  ht5023_text_default_style(&st_label, ht5023_st7789_color565(206, 224, 235), 0u);
  st_label.opaque_bg = false;
  ht5023_text_draw_str(screen, (int16_t)(x + 6), (int16_t)(y + 5), label, &st_label);

  ht5023_text_default_style(&st_val, ht5023_st7789_color565(255, 255, 255), 0u);
  st_val.opaque_bg = false;
  st_val.scale = 2u;
  format_value(buf, (uint8_t)sizeof(buf), value, 0);
  ht5023_text_draw_str(screen, (int16_t)(x + 6), (int16_t)(y + 18), buf, &st_val);

  if (suffix) {
    ht5023_text_draw_str(screen, (int16_t)(x + w - 14), (int16_t)(y + h - 12), suffix, &st_label);
  }
}

static void draw_cards_dynamic(const ht5023_gfx_surface_t* screen,
                               uint32_t voltage_v,
                               uint32_t current_a,
                               uint32_t power_w) {
  draw_card_dynamic(screen,
                    CARD1_X,
                    CARD1_Y,
                    CARD_W,
                    CARD_H,
                    "VAC",
                    voltage_v,
                    "V",
                    ht5023_st7789_color565(18, 34, 44));

  draw_card_dynamic(screen,
                    CARD2_X,
                    CARD2_Y,
                    CARD_W,
                    CARD_H,
                    "AMP",
                    current_a,
                    "A",
                    ht5023_st7789_color565(14, 42, 28));

  draw_card_dynamic(screen,
                    CARD3_X,
                    CARD3_Y,
                    CARD_W,
                    CARD_H,
                    "PWR",
                    power_w,
                    "W",
                    ht5023_st7789_color565(40, 28, 18));
}

static void draw_cards_dynamic_composited(const ht5023_gfx_surface_t* screen,
                                          ht5023_st7789_t* lcd,
                                          uint32_t voltage_v,
                                          uint32_t current_a,
                                          uint32_t power_w) {
  ht5023_gfx_surface_t panel;
  const int16_t w = (int16_t)(CARD_W - 4u);
  const int16_t h = (int16_t)(CARD_H - 4u);

  if (!panel_begin((uint16_t)w, (uint16_t)h, ht5023_st7789_color565(18, 34, 36), &panel)) {
    draw_cards_dynamic(screen, voltage_v, current_a, power_w);
    return;
  }

  draw_meter_card_panel(&panel,
                        "VAC",
                        voltage_v,
                        "V",
                        "L1",
                        ht5023_st7789_color565(10, 20, 30),
                        ht5023_st7789_color565(14, 28, 42),
                        ht5023_st7789_color565(92, 188, 228),
                        (uint8_t)((voltage_v > 210u) ? ((voltage_v - 210u) * 3u) : 0u));
  panel_flush(lcd, (uint16_t)(CARD1_X + 2), (uint16_t)(CARD1_Y + 2));

  draw_meter_card_panel(&panel,
                        "AMP",
                        current_a,
                        "A",
                        "NOW",
                        ht5023_st7789_color565(8, 24, 16),
                        ht5023_st7789_color565(12, 34, 22),
                        ht5023_st7789_color565(102, 204, 136),
                        (uint8_t)(current_a * 6u));
  panel_flush(lcd, (uint16_t)(CARD2_X + 2), (uint16_t)(CARD2_Y + 2));

  draw_meter_card_panel(&panel,
                        "PWR",
                        power_w,
                        "W",
                        "NET",
                        ht5023_st7789_color565(24, 16, 10),
                        ht5023_st7789_color565(38, 24, 14),
                        ht5023_st7789_color565(216, 164, 88),
                        (uint8_t)(power_w / 35u));
  panel_flush(lcd, (uint16_t)(CARD3_X + 2), (uint16_t)(CARD3_Y + 2));
}

static void draw_chip(const ht5023_gfx_surface_t* screen,
                      int16_t x,
                      int16_t y,
                      const char* label,
                      bool on,
                      uint16_t fill_on,
                      uint16_t edge_on) {
  ht5023_text_style_t st;
  uint16_t edge = on ? edge_on : ht5023_st7789_color565(54, 68, 82);
  uint16_t fill = on ? fill_on : ht5023_st7789_color565(12, 18, 28);

  ht5023_gfx_fill_rect(screen, x, y, CHIP_W, CHIP_H, fill);
  ht5023_gfx_draw_rect(screen, x, y, CHIP_W, CHIP_H, edge);
  ht5023_text_default_style(&st, ht5023_st7789_color565(188, 202, 214), 0u);
  st.opaque_bg = false;
  ht5023_text_draw_str(screen, (int16_t)(x + 5), (int16_t)(y + 2), label, &st);
}

static void draw_chip_row(const ht5023_gfx_surface_t* screen, uint8_t source_idx, bool relay_on) {
  draw_chip(screen, CHIP1_X, CHIPS_Y, "GRID", source_idx == 0u,
            ht5023_st7789_color565(38, 84, 102), ht5023_st7789_color565(132, 224, 255));
  draw_chip(screen, CHIP2_X, CHIPS_Y, "SOLAR", source_idx == 1u,
            ht5023_st7789_color565(72, 100, 40), ht5023_st7789_color565(196, 255, 158));
  draw_chip(screen, CHIP3_X, CHIPS_Y, relay_on ? "LOAD" : "IDLE", relay_on,
            ht5023_st7789_color565(56, 122, 104), ht5023_st7789_color565(188, 255, 232));
}

static void draw_chip_row_composited(const ht5023_gfx_surface_t* screen,
                                     ht5023_st7789_t* lcd,
                                     uint8_t source_idx,
                                     bool relay_on) {
  ht5023_gfx_surface_t panel;
  ht5023_text_style_t st;

  if (!panel_begin(CHIP_W, CHIP_H, ht5023_st7789_color565(12, 18, 28), &panel)) {
    draw_chip_row(screen, source_idx, relay_on);
    return;
  }

  ht5023_text_default_style(&st, ht5023_st7789_color565(196, 206, 214), 0u);
  st.opaque_bg = false;

  ht5023_fb_clear(&g_panel_fb,
                  (source_idx == 0u) ? ht5023_st7789_color565(22, 50, 66) : ht5023_st7789_color565(12, 18, 28));
  ht5023_gfx_draw_rect(&panel, 0, 0, CHIP_W, CHIP_H,
                       (source_idx == 0u) ? ht5023_st7789_color565(82, 164, 208) : ht5023_st7789_color565(54, 68, 82));
  ht5023_text_draw_str(&panel, 5, 2, "GRID", &st);
  panel_flush(lcd, CHIP1_X, CHIPS_Y);

  ht5023_fb_clear(&g_panel_fb,
                  (source_idx == 1u) ? ht5023_st7789_color565(40, 58, 24) : ht5023_st7789_color565(12, 18, 28));
  ht5023_gfx_draw_rect(&panel, 0, 0, CHIP_W, CHIP_H,
                       (source_idx == 1u) ? ht5023_st7789_color565(112, 182, 98) : ht5023_st7789_color565(54, 68, 82));
  ht5023_text_draw_str(&panel, 5, 2, "SOLAR", &st);
  panel_flush(lcd, CHIP2_X, CHIPS_Y);

  ht5023_fb_clear(&g_panel_fb, relay_on ? ht5023_st7789_color565(26, 62, 48) : ht5023_st7789_color565(42, 20, 20));
  ht5023_gfx_draw_rect(&panel, 0, 0, CHIP_W, CHIP_H,
                       relay_on ? ht5023_st7789_color565(92, 172, 112) : ht5023_st7789_color565(156, 84, 84));
  ht5023_text_draw_str(&panel, 5, 2, relay_on ? "LOAD" : "IDLE", &st);
  panel_flush(lcd, CHIP3_X, CHIPS_Y);
}

static void draw_dial_widget(const ht5023_gfx_surface_t* screen,
                             int16_t cx,
                             int16_t cy,
                             int16_t r,
                             uint8_t pct,
                             uint16_t color,
                             const char* label) {
  ht5023_text_style_t st;
  char buf[8];
  uint8_t phase;
  int16_t nx;
  int16_t ny;

  ht5023_gfx_fill_circle(screen, cx, cy, r, ht5023_st7789_color565(10, 14, 20));
  ht5023_gfx_draw_circle(screen, cx, cy, r, ht5023_st7789_color565(96, 116, 136));
  ht5023_gfx_draw_circle(screen, cx, cy, (int16_t)(r - 2), color);

  for (uint8_t i = 0u; i < 8u; ++i) {
    uint8_t p = (uint8_t)(44u + i * 5u);
    int16_t tx0 = (int16_t)(cx + ((int32_t)sin64s(p) * (r - 1)) / 54);
    int16_t ty0 = (int16_t)(cy - ((int32_t)sin64s((uint8_t)(p + 16u)) * (r - 1)) / 54);
    int16_t tx1 = (int16_t)(cx + ((int32_t)sin64s(p) * (r - 4)) / 54);
    int16_t ty1 = (int16_t)(cy - ((int32_t)sin64s((uint8_t)(p + 16u)) * (r - 4)) / 54);
    ht5023_gfx_draw_line(screen, tx0, ty0, tx1, ty1, ht5023_st7789_color565(110, 136, 156));
  }

  phase = (uint8_t)(44u + ((uint16_t)pct * 38u) / 100u);
  nx = (int16_t)(cx + ((int32_t)sin64s(phase) * (r - 5)) / 54);
  ny = (int16_t)(cy - ((int32_t)sin64s((uint8_t)(phase + 16u)) * (r - 5)) / 54);
  ht5023_gfx_draw_line(screen, cx, cy, nx, ny, color);
  ht5023_gfx_fill_circle(screen, cx, cy, 2, color);

  ht5023_text_default_style(&st, ht5023_st7789_color565(218, 234, 244), 0u);
  st.opaque_bg = false;
  format_value(buf, (uint8_t)sizeof(buf), pct, "%");
  ht5023_text_draw_str(screen, (int16_t)(cx - 8), (int16_t)(cy + r + 1), buf, &st);
  if (label) {
    ht5023_text_draw_str(screen, (int16_t)(cx - 7), (int16_t)(cy - r - 10), label, &st);
  }
}

static void draw_trend_dynamic(const ht5023_gfx_surface_t* screen,
                               uint8_t latest_kw_pct,
                               uint8_t power_factor_pct,
                               uint8_t freq_pct) {
  const int16_t gx = TREND_X + 8;
  const int16_t gy = TREND_Y + 50;
  const int16_t gw = 92;
  const int16_t gh = 46;
  ht5023_text_style_t st;
  char pct[8];
  char flow[10];

  ht5023_gfx_fill_rect(screen,
                       (int16_t)(TREND_X + 2),
                       (int16_t)(TREND_Y + 42),
                       (int16_t)(TREND_W - 4),
                       (int16_t)(TREND_H - 46),
                       ht5023_st7789_color565(6, 10, 18));

  for (uint8_t i = 1u; i <= 3u; ++i) {
    int16_t yy = (int16_t)(gy + ((int16_t)(gh - 1) * i) / 4);
    ht5023_gfx_draw_hline(screen, gx, yy, gw, ht5023_st7789_color565(18, 30, 44));
  }

  for (uint8_t i = 0u; i < (uint8_t)(TREND_POINTS - 1u); ++i) {
    int16_t x0 = (int16_t)(gx + ((int32_t)(gw - 1) * i) / (TREND_POINTS - 1));
    int16_t x1 = (int16_t)(gx + ((int32_t)(gw - 1) * (i + 1u)) / (TREND_POINTS - 1));
    int16_t y0 = (int16_t)(gy + (gh - 1) - ((int32_t)(gh - 1) * g_kw_hist[i]) / 100);
    int16_t y1 = (int16_t)(gy + (gh - 1) - ((int32_t)(gh - 1) * g_kw_hist[i + 1u]) / 100);
    ht5023_gfx_draw_line(screen, x0, y0, x1, y1, ht5023_st7789_color565(78, 182, 206));
  }

  ht5023_gfx_fill_circle(screen,
                         (int16_t)(gx + gw - 1),
                         (int16_t)(gy + (gh - 1) - ((int32_t)(gh - 1) * latest_kw_pct) / 100),
                         2,
                         ht5023_st7789_color565(188, 150, 82));

  ht5023_gfx_draw_vline(screen, (int16_t)(gx + gw + 10), gy, gh,
                        ht5023_st7789_color565(24, 38, 52));
  draw_dial_widget(screen, (int16_t)(TREND_X + 124), (int16_t)(TREND_Y + 72), 14, power_factor_pct,
                   ht5023_st7789_color565(88, 168, 192), "PF");
  draw_dial_widget(screen, (int16_t)(TREND_X + 154), (int16_t)(TREND_Y + 72), 11, freq_pct,
                   ht5023_st7789_color565(184, 144, 82), "HZ");

  ht5023_text_default_style(&st, ht5023_st7789_color565(166, 186, 204), 0u);
  st.opaque_bg = false;
  format_value(pct, (uint8_t)sizeof(pct), latest_kw_pct, "%");
  format_value(flow, (uint8_t)sizeof(flow), (uint32_t)(latest_kw_pct * 32u), "W");
  ht5023_text_draw_str(screen, (int16_t)(TREND_X + 66), (int16_t)(TREND_Y + 53), flow, &st);
  ht5023_text_draw_str(screen, (int16_t)(TREND_X + TREND_W - 30), (int16_t)(TREND_Y + 8), pct, &st);
}

static void draw_trend_dynamic_composited(const ht5023_gfx_surface_t* screen,
                                          ht5023_st7789_t* lcd,
                                          uint8_t latest_kw_pct,
                                          uint8_t power_factor_pct,
                                          uint8_t freq_pct) {
  const uint16_t panel_w = (uint16_t)(TREND_W - 4);
  const uint16_t panel_h = (uint16_t)(TREND_H - 46);
  const uint16_t band_h0 = 36u;
  const uint16_t band_h1 = (uint16_t)(panel_h - band_h0);
  ht5023_gfx_surface_t panel;

  if (band_h1 == 0u ||
      !panel_begin(panel_w, band_h0, ht5023_st7789_color565(6, 10, 18), &panel)) {
    draw_trend_dynamic(screen, latest_kw_pct, power_factor_pct, freq_pct);
    return;
  }

  draw_trend_band(&panel, 0, latest_kw_pct, power_factor_pct, freq_pct);
  panel_flush(lcd, (uint16_t)(TREND_X + 2), (uint16_t)(TREND_Y + 42));

  if (!panel_begin(panel_w, band_h1, ht5023_st7789_color565(6, 10, 18), &panel)) {
    draw_trend_dynamic(screen, latest_kw_pct, power_factor_pct, freq_pct);
    return;
  }
  draw_trend_band(&panel, (int16_t)band_h0, latest_kw_pct, power_factor_pct, freq_pct);
  panel_flush(lcd, (uint16_t)(TREND_X + 2), (uint16_t)(TREND_Y + 42 + band_h0));
  draw_trend_badge_composited(screen, lcd, latest_kw_pct);
}

static void draw_bar(const ht5023_gfx_surface_t* screen,
                     int16_t x,
                     int16_t y,
                     int16_t w,
                     int16_t h,
                     uint8_t pct,
                     uint16_t fg) {
  uint16_t fill_w;
  if (pct > 100u) pct = 100u;
  ht5023_gfx_fill_rect(screen, x, y, w, h, ht5023_st7789_color565(8, 14, 18));
  ht5023_gfx_draw_rect(screen, x, y, w, h, ht5023_st7789_color565(58, 72, 84));
  fill_w = (uint16_t)(((uint32_t)(w - 2) * pct) / 100u);
  if (fill_w > 0u) {
    ht5023_gfx_fill_rect(screen, (int16_t)(x + 1), (int16_t)(y + 1), (int16_t)fill_w, (int16_t)(h - 2), fg);
  }
}

static void draw_status_dynamic(const ht5023_gfx_surface_t* screen,
                                uint8_t demand_pct,
                                uint8_t export_pct,
                                bool relay_on,
                                uint16_t frame,
                                uint16_t energy_kwh) {
  ht5023_text_style_t st;
  static const char* const EVENTS[4] = {
    "T1 LIVE",
    "DEMAND",
    "PF OK",
    "DMA OK",
  };
  const char* evt = EVENTS[(frame / 20u) & 3u];
  uint16_t led_ok = relay_on ? ht5023_st7789_color565(90, 255, 130) : ht5023_st7789_color565(80, 92, 102);
  uint16_t led_warn = (!relay_on && ((frame / 6u) & 1u))
    ? ht5023_st7789_color565(255, 100, 80)
    : ht5023_st7789_color565(60, 34, 30);

  ht5023_gfx_fill_rect(screen,
                       (int16_t)(STATUS_X + 2),
                       (int16_t)(STATUS_Y + 17),
                       (int16_t)(STATUS_W - 4),
                       (int16_t)(STATUS_H - 19),
                       ht5023_st7789_color565(8, 12, 10));

  ht5023_text_default_style(&st, ht5023_st7789_color565(176, 186, 192), 0u);
  st.opaque_bg = false;
  ht5023_text_draw_str(screen, STATUS_X + 8, STATUS_Y + 22, "DMND", &st);
  ht5023_text_draw_str(screen, STATUS_X + 8, STATUS_Y + 37, "EXPT", &st);

  draw_bar(screen,
           (int16_t)(STATUS_X + 46),
           (int16_t)(STATUS_Y + 22),
           128,
           9,
           demand_pct,
           ht5023_st7789_color565(80, 220, 255));
  draw_bar(screen,
           (int16_t)(STATUS_X + 46),
           (int16_t)(STATUS_Y + 37),
           128,
           9,
           export_pct,
           ht5023_st7789_color565(255, 210, 90));

  ht5023_gfx_fill_circle(screen, (int16_t)(STATUS_X + 191), (int16_t)(STATUS_Y + 29), 4, led_ok);
  ht5023_gfx_fill_circle(screen, (int16_t)(STATUS_X + 191), (int16_t)(STATUS_Y + 43), 4, led_warn);

  ht5023_text_draw_str(screen, STATUS_X + 200, STATUS_Y + 25, "RL", &st);
  ht5023_text_draw_str(screen, STATUS_X + 200, STATUS_Y + 39, "AL", &st);

  ht5023_gfx_fill_rect(screen, (int16_t)(STATUS_X + 138), (int16_t)(STATUS_Y + 18), 46, 12,
                       ht5023_st7789_color565(14, 18, 14));
  ht5023_text_draw_str(screen, STATUS_X + 140, STATUS_Y + 21, evt, &st);

  ht5023_gfx_fill_rect(screen, (int16_t)(STATUS_X + 138), (int16_t)(STATUS_Y + 33), 46, 14,
                       ht5023_st7789_color565(10, 14, 10));
  for (uint8_t i = 0u; i < (uint8_t)(SPARK_POINTS - 1u); ++i) {
    int16_t x0 = (int16_t)(STATUS_X + 140 + ((int16_t)i * 42) / (SPARK_POINTS - 1));
    int16_t x1 = (int16_t)(STATUS_X + 140 + ((int16_t)(i + 1u) * 42) / (SPARK_POINTS - 1));
    int16_t y0 = (int16_t)(STATUS_Y + 45 - ((int16_t)g_demand_hist[i] * 8) / 100);
    int16_t y1 = (int16_t)(STATUS_Y + 45 - ((int16_t)g_demand_hist[i + 1u] * 8) / 100);
    ht5023_gfx_draw_line(screen, x0, y0, x1, y1, ht5023_st7789_color565(90, 170, 118));
  }

  {
    char ebuf[10];
    format_value(ebuf, (uint8_t)sizeof(ebuf), energy_kwh, "K");
    ht5023_text_draw_str(screen, STATUS_X + 184, STATUS_Y + 22, ebuf, &st);
  }
}

static void draw_status_dynamic_composited(const ht5023_gfx_surface_t* screen,
                                           ht5023_st7789_t* lcd,
                                           uint8_t demand_pct,
                                           uint8_t export_pct,
                                           bool relay_on,
                                           uint16_t frame,
                                           uint16_t energy_kwh) {
  ht5023_gfx_surface_t panel;
  ht5023_text_style_t st;
  static const char* const EVENTS[4] = {
    "T1 LIVE",
    "DEMAND",
    "PF OK",
    "DMA OK",
  };
  const char* evt = EVENTS[(frame / 20u) & 3u];
  uint16_t led_ok = relay_on ? ht5023_st7789_color565(90, 255, 130) : ht5023_st7789_color565(80, 92, 102);
  uint16_t led_warn = (!relay_on && ((frame / 6u) & 1u))
    ? ht5023_st7789_color565(255, 100, 80)
    : ht5023_st7789_color565(60, 34, 30);

  if (!panel_begin((uint16_t)(STATUS_W - 4), (uint16_t)(STATUS_H - 19),
                   ht5023_st7789_color565(8, 12, 10), &panel)) {
    draw_status_dynamic(screen, demand_pct, export_pct, relay_on, frame, energy_kwh);
    return;
  }

  ht5023_text_default_style(&st, ht5023_st7789_color565(182, 190, 194), 0u);
  st.opaque_bg = false;
  ht5023_text_draw_str(&panel, 6, 5, "DMND", &st);
  ht5023_text_draw_str(&panel, 6, 20, "EXPT", &st);

  draw_bar(&panel, 44, 5, 128, 9, demand_pct, ht5023_st7789_color565(84, 226, 255));
  draw_bar(&panel, 44, 20, 128, 9, export_pct, ht5023_st7789_color565(255, 218, 100));

  ht5023_gfx_fill_circle(&panel, 189, 12, 4, led_ok);
  ht5023_gfx_fill_circle(&panel, 189, 26, 4, led_warn);
  ht5023_text_draw_str(&panel, 198, 8, "RL", &st);
  ht5023_text_draw_str(&panel, 198, 22, "AL", &st);

  ht5023_gfx_fill_rect(&panel, 136, 1, 46, 12, ht5023_st7789_color565(12, 16, 14));
  ht5023_gfx_draw_rect(&panel, 136, 1, 46, 12, ht5023_st7789_color565(70, 102, 74));
  ht5023_text_draw_str(&panel, 138, 4, evt, &st);

  ht5023_gfx_fill_rect(&panel, 136, 16, 46, 14, ht5023_st7789_color565(10, 14, 10));
  for (uint8_t i = 0u; i < (uint8_t)(SPARK_POINTS - 1u); ++i) {
    int16_t x0 = (int16_t)(138 + ((int16_t)i * 42) / (SPARK_POINTS - 1));
    int16_t x1 = (int16_t)(138 + ((int16_t)(i + 1u) * 42) / (SPARK_POINTS - 1));
    int16_t y0 = (int16_t)(28 - ((int16_t)g_demand_hist[i] * 8) / 100);
    int16_t y1 = (int16_t)(28 - ((int16_t)g_demand_hist[i + 1u] * 8) / 100);
    ht5023_gfx_draw_line(&panel, x0, y0, x1, y1, ht5023_st7789_color565(90, 170, 118));
  }
  {
    char ebuf[10];
    format_value(ebuf, (uint8_t)sizeof(ebuf), energy_kwh, "K");
    ht5023_text_draw_str(&panel, 184, 5, ebuf, &st);
  }
  panel_flush(lcd, (uint16_t)(STATUS_X + 2), (uint16_t)(STATUS_Y + 17));
}

int main(void) {
  const ht5023_spi_master_cfg_t spi_cfg = { .spr = 0u, .cpol = false, .cpha = false };
  st7789_hal_ctx_t hal = {
    .gpio_base = GPIOA_BASE_ADDR,
    .dc_pin = ST7789_DC_PIN,
    .rst_pin = ST7789_RST_PIN,
  };
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
    .stage_buffer = g_dma_stage,
    .stage_buffer_len = sizeof(g_dma_stage),
    .rx_stage_buffer = g_dma_rx_stage,
    .rx_stage_buffer_len = sizeof(g_dma_rx_stage),
    .max_fail_streak = ST7789_DMA_MAX_FAILS,
  };
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
  ht5023_st7789_t lcd;
  ht5023_gfx_surface_t screen;
  uint16_t frame = 0u;
  uint16_t energy_kwh = 18u;

  led_gpio_init();
  gpio_pin_output_init(GPIOA_BASE_ADDR, ST7789_DC_PIN, true);
  gpio_pin_output_init(GPIOA_BASE_ADDR, ST7789_RST_PIN, true);

  (void)ht5023_spi_master_init(&spi_cfg);
  if (!ht5023_st7789_init(&lcd, &lcd_cfg)) {
    for (;;) {
      led_toggle();
      delay_ms_blocking(160u);
    }
  }

  ht5023_st7789_set_rotation(&lcd, HT5023_ST7789_ROTATION_90);
  ht5023_gfx_surface_init_st7789(&screen, &lcd);

  kw_seed(44u);
  demand_seed(36u);
  draw_static_layout(&screen);

  for (;;) {
    const st7789_dma_view_t dma = st7789_dma_view(&lcd);
    int16_t wave_a = sin64s((uint8_t)(frame + 0u));
    int16_t wave_b = sin64s((uint8_t)(frame + 18u));
    int16_t wave_c = sin64s((uint8_t)(frame + 36u));
    uint32_t voltage_v = 224u + (uint32_t)(wave_a + 54) / 3u;
    uint32_t current_a = 6u + (uint32_t)(wave_b + 54u) / 6u;
    uint32_t power_w = 850u + (uint32_t)(wave_a + 54u) * 24u;
    uint8_t kw_pct = (uint8_t)(24u + ((uint16_t)(wave_a + 54) * 70u) / 108u);
    uint8_t power_factor_pct = (uint8_t)(78u + ((uint16_t)(wave_b + 54) * 20u) / 108u);
    uint8_t freq_pct = (uint8_t)(92u + ((uint16_t)(wave_c + 54) * 8u) / 108u);
    uint8_t demand_pct = (uint8_t)(28u + ((uint16_t)(wave_a + 54) * 62u) / 108u);
    uint8_t export_pct = (uint8_t)(((uint16_t)(wave_c + 54) * 30u) / 108u);
    uint8_t source_idx = (uint8_t)((frame / 40u) & 1u);
    bool relay_on = ((frame / 12u) & 7u) != 0u;

    kw_push(kw_pct);
    demand_push(demand_pct);
    if ((frame % 16u) == 0u) energy_kwh++;

    draw_header_dynamic_composited(&lcd, frame, relay_on, &dma);
    draw_cards_dynamic_composited(&screen, &lcd, voltage_v, current_a, power_w);
    draw_chip_row_composited(&screen, &lcd, source_idx, relay_on);
    draw_trend_dynamic_composited(&screen, &lcd, kw_pct, power_factor_pct, freq_pct);
    draw_status_dynamic_composited(&screen, &lcd, demand_pct, export_pct, relay_on, frame, energy_kwh);

    led_toggle();
    delay_ms_blocking(16u);
    ++frame;
  }
}
