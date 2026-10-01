// examples/dashboard_demo/main.c — animated ST7789 dashboard example (DMA-backed SPI writes)
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
#define ST7789_DEMO_HEIGHT 240u
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

#define HEADER_H              26
#define CARD_Y                34
#define CARD_W                72
#define CARD_H                42
#define CARD_GAP              6
#define CARD1_X               8
#define CARD2_X               (CARD1_X + CARD_W + CARD_GAP)
#define CARD3_X               (CARD2_X + CARD_W + CARD_GAP)
#define CHIPS_Y               79
#define TREND_X               8
#define TREND_Y               94
#define TREND_W               224
#define TREND_H               80
#define STATUS_X              8
#define STATUS_Y              178
#define STATUS_W              224
#define STATUS_H              54

#define TREND_POINTS          52
#define SPARK_POINTS          32
#define ST7789_DMA_MIN_BYTES      64u
#define ST7789_DMA_TIMEOUT_POLLS  30000u
#define ST7789_DMA_MAX_FAILS      2u
#define ST7789_DMA_STAGE_BYTES    2048u
#define PANEL_BUF_W              220u
#define PANEL_BUF_H               60u

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

static uint8_t g_trend_hist[TREND_POINTS];
static uint8_t g_load_hist[SPARK_POINTS];
static uint8_t g_dma_stage[ST7789_DMA_STAGE_BYTES] __attribute__((aligned(4)));
static uint8_t g_dma_rx_stage[ST7789_DMA_STAGE_BYTES] __attribute__((aligned(4)));
static uint16_t g_panel_pixels[PANEL_BUF_W * PANEL_BUF_H];
static ht5023_fb_rgb565_t g_panel_fb;

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

static void trend_seed(uint8_t v) {
  for (uint8_t i = 0u; i < TREND_POINTS; ++i) g_trend_hist[i] = v;
}

static void load_seed(uint8_t v) {
  for (uint8_t i = 0u; i < SPARK_POINTS; ++i) g_load_hist[i] = v;
}

static void trend_push(uint8_t v) {
  for (uint8_t i = 0u; i < (uint8_t)(TREND_POINTS - 1u); ++i) g_trend_hist[i] = g_trend_hist[i + 1u];
  g_trend_hist[TREND_POINTS - 1u] = v;
}

static void load_push(uint8_t v) {
  for (uint8_t i = 0u; i < (uint8_t)(SPARK_POINTS - 1u); ++i) g_load_hist[i] = g_load_hist[i + 1u];
  g_load_hist[SPARK_POINTS - 1u] = v;
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

static void draw_static_layout(const ht5023_gfx_surface_t* screen) {
  ht5023_text_style_t body;

  for (int16_t y = 0; y < (int16_t)ST7789_DEMO_HEIGHT; y += 2) {
    uint8_t r = (uint8_t)(4 + (y / 20));
    uint8_t g = (uint8_t)(12 + (y / 9));
    uint8_t b = (uint8_t)(26 + (y / 4));
    ht5023_gfx_fill_rect(screen, 0, y, (int16_t)ST7789_DEMO_WIDTH, 2, ht5023_st7789_color565(r, g, b));
  }
  for (int16_t x = 0; x < (int16_t)ST7789_DEMO_WIDTH; x += 24) {
    ht5023_gfx_draw_vline(screen, x, HEADER_H, (int16_t)(ST7789_DEMO_HEIGHT - HEADER_H),
                          ht5023_st7789_color565(9, 20, 32));
  }

  draw_panel_frame(screen,
                   0,
                   0,
                   (int16_t)ST7789_DEMO_WIDTH,
                   HEADER_H,
                   ht5023_st7789_color565(138, 238, 255),
                   ht5023_st7789_color565(32, 70, 96));

  draw_panel_frame(screen, CARD1_X, CARD_Y, CARD_W, CARD_H,
                   ht5023_st7789_color565(128, 245, 214), ht5023_st7789_color565(30, 63, 56));
  draw_panel_frame(screen, CARD2_X, CARD_Y, CARD_W, CARD_H,
                   ht5023_st7789_color565(255, 208, 122), ht5023_st7789_color565(74, 50, 24));
  draw_panel_frame(screen, CARD3_X, CARD_Y, CARD_W, CARD_H,
                   ht5023_st7789_color565(204, 176, 255), ht5023_st7789_color565(54, 44, 84));
  draw_panel_frame(screen, TREND_X, TREND_Y, TREND_W, TREND_H,
                   ht5023_st7789_color565(122, 230, 255), ht5023_st7789_color565(24, 52, 76));
  draw_panel_frame(screen, STATUS_X, STATUS_Y, STATUS_W, STATUS_H,
                   ht5023_st7789_color565(255, 188, 126), ht5023_st7789_color565(74, 45, 25));

  ht5023_text_default_style(&body, ht5023_st7789_color565(198, 226, 245), 0u);
  body.opaque_bg = false;
  ht5023_text_draw_str(screen, TREND_X + 8, TREND_Y + 8, "FLOW + ANALYTICS", &body);
  ht5023_text_draw_str(screen, STATUS_X + 8, STATUS_Y + 7, "SYSTEM HEALTH", &body);
}

static void draw_header_dynamic(const ht5023_gfx_surface_t* screen, uint16_t frame, bool link_ok) {
  ht5023_text_style_t title;
  ht5023_text_style_t body;
  char tick[8];
  uint8_t pulse = (uint8_t)(24u + ((uint8_t)(sin64s((uint8_t)(frame * 2u)) + 54) * 78u) / 108u);

  ht5023_gfx_fill_rect(screen,
                       2,
                       2,
                       (int16_t)(ST7789_DEMO_WIDTH - 4u),
                       (int16_t)(HEADER_H - 4),
                       ht5023_st7789_color565((uint8_t)(7u + pulse / 6u), (uint8_t)(18u + pulse / 4u), (uint8_t)(30u + pulse)));
  ht5023_gfx_draw_hline(screen, 4, 4, 110, ht5023_st7789_color565(145, 255, 236));
  ht5023_gfx_draw_hline(screen, 4, 5, 96, ht5023_st7789_color565(72, 218, 255));
  ht5023_gfx_fill_rect(screen, 4, 8, 2, 12, ht5023_st7789_color565(255, 220, 120));

  ht5023_text_default_style(&title, ht5023_st7789_color565(252, 255, 255), 0u);
  title.opaque_bg = false;
  title.scale = 2u;
  ht5023_text_draw_str(screen, 10, 7, "AERONET", &title);

  ht5023_text_default_style(&body, ht5023_st7789_color565(198, 230, 245), 0u);
  body.opaque_bg = false;
  ht5023_text_draw_str(screen, 130, 6, "COMMAND UI", &body);
  ht5023_text_draw_str(screen,
                       130,
                       15,
                       link_ok ? "UPLINK: STABLE" : "UPLINK: RETRY",
                       &body);

  (void)u32_to_dec(tick, (uint8_t)sizeof(tick), frame);
  ht5023_text_draw_str(screen, 202, 15, tick, &body);
}

static void draw_header_dma_stats(const ht5023_gfx_surface_t* screen, const st7789_dma_view_t* dma) {
  ht5023_text_style_t st;
  char d_ok[10];
  char d_fb[10];

  ht5023_gfx_fill_rect(screen, 176, 4, 60, 9, ht5023_st7789_color565(16, 30, 46));
  ht5023_gfx_draw_rect(screen, 176, 4, 60, 9, ht5023_st7789_color565(52, 112, 146));
  ht5023_text_default_style(&st, ht5023_st7789_color565(188, 232, 246), 0u);
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

  ht5023_text_draw_str(screen, 179, 4, d_ok, &st);
  ht5023_text_draw_str(screen, 206, 4, d_fb, &st);
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
    ht5023_text_draw_str(screen, (int16_t)(x + w - 16), (int16_t)(y + h - 12), suffix, &st_label);
  }
}

static void draw_cards_dynamic(const ht5023_gfx_surface_t* screen,
                               uint32_t rpm,
                               uint32_t temp_c,
                               uint32_t speed_kmh) {
  draw_card_dynamic(screen,
                    CARD1_X,
                    CARD_Y,
                    CARD_W,
                    CARD_H,
                    "RPM",
                    rpm,
                    0,
                    ht5023_st7789_color565(16, 34, 34));

  draw_card_dynamic(screen,
                    CARD2_X,
                    CARD_Y,
                    CARD_W,
                    CARD_H,
                    "TEMP",
                    temp_c,
                    "C",
                    ht5023_st7789_color565(34, 26, 18));

  draw_card_dynamic(screen,
                    CARD3_X,
                    CARD_Y,
                    CARD_W,
                    CARD_H,
                    "SPD",
                    speed_kmh,
                    "K",
                    ht5023_st7789_color565(24, 20, 38));
}

static void draw_cards_dynamic_composited(const ht5023_gfx_surface_t* screen,
                                          ht5023_st7789_t* lcd,
                                          uint32_t rpm,
                                          uint32_t temp_c,
                                          uint32_t speed_kmh) {
  ht5023_gfx_surface_t panel;
  ht5023_text_style_t st_label;
  ht5023_text_style_t st_val;
  char buf[16];
  const int16_t w = (int16_t)(CARD_W - 4u);
  const int16_t h = (int16_t)(CARD_H - 4u);

  if (!panel_begin((uint16_t)w, (uint16_t)h, ht5023_st7789_color565(18, 34, 36), &panel)) {
    draw_cards_dynamic(screen, rpm, temp_c, speed_kmh);
    return;
  }

  ht5023_text_default_style(&st_label, ht5023_st7789_color565(212, 230, 240), 0u);
  st_label.opaque_bg = false;
  ht5023_text_default_style(&st_val, ht5023_st7789_color565(252, 255, 255), 0u);
  st_val.opaque_bg = false;
  st_val.scale = 2u;

  // Card 1
  ht5023_fb_clear(&g_panel_fb, ht5023_st7789_color565(18, 38, 42));
  ht5023_gfx_draw_hline(&panel, 0, 0, w, ht5023_st7789_color565(120, 255, 216));
  ht5023_text_draw_str(&panel, 4, 3, "RPM", &st_label);
  format_value(buf, (uint8_t)sizeof(buf), rpm, 0);
  ht5023_text_draw_str(&panel, 4, 16, buf, &st_val);
  panel_flush(lcd, (uint16_t)(CARD1_X + 2), (uint16_t)(CARD_Y + 2));

  // Card 2
  ht5023_fb_clear(&g_panel_fb, ht5023_st7789_color565(42, 30, 18));
  ht5023_gfx_draw_hline(&panel, 0, 0, w, ht5023_st7789_color565(255, 214, 130));
  ht5023_text_draw_str(&panel, 4, 3, "TEMP", &st_label);
  format_value(buf, (uint8_t)sizeof(buf), temp_c, 0);
  ht5023_text_draw_str(&panel, 4, 16, buf, &st_val);
  ht5023_text_draw_str(&panel, (int16_t)(w - 14), (int16_t)(h - 10), "C", &st_label);
  panel_flush(lcd, (uint16_t)(CARD2_X + 2), (uint16_t)(CARD_Y + 2));

  // Card 3
  ht5023_fb_clear(&g_panel_fb, ht5023_st7789_color565(28, 24, 44));
  ht5023_gfx_draw_hline(&panel, 0, 0, w, ht5023_st7789_color565(204, 170, 255));
  ht5023_text_draw_str(&panel, 4, 3, "SPD", &st_label);
  format_value(buf, (uint8_t)sizeof(buf), speed_kmh, 0);
  ht5023_text_draw_str(&panel, 4, 16, buf, &st_val);
  ht5023_text_draw_str(&panel, (int16_t)(w - 14), (int16_t)(h - 10), "K", &st_label);
  panel_flush(lcd, (uint16_t)(CARD3_X + 2), (uint16_t)(CARD_Y + 2));
}

static void draw_chip(const ht5023_gfx_surface_t* screen,
                      int16_t x,
                      int16_t y,
                      const char* label,
                      bool on) {
  ht5023_text_style_t st;
  uint16_t edge = on ? ht5023_st7789_color565(255, 245, 190) : ht5023_st7789_color565(88, 108, 126);
  uint16_t fill = on ? ht5023_st7789_color565(70, 120, 94) : ht5023_st7789_color565(24, 34, 48);

  ht5023_gfx_fill_rect(screen, x, y, 70, 12, fill);
  ht5023_gfx_draw_rect(screen, x, y, 70, 12, edge);
  ht5023_text_default_style(&st, ht5023_st7789_color565(220, 232, 241), 0u);
  st.opaque_bg = false;
  ht5023_text_draw_str(screen, (int16_t)(x + 5), (int16_t)(y + 2), label, &st);
}

static void draw_chip_row(const ht5023_gfx_surface_t* screen, uint8_t mode_idx, bool link_ok) {
  draw_chip(screen, CARD1_X, CHIPS_Y, "AUTO", mode_idx == 0u);
  draw_chip(screen, CARD2_X, CHIPS_Y, "MANUAL", mode_idx == 1u);
  draw_chip(screen, CARD3_X, CHIPS_Y, link_ok ? "SYNCED" : "RETRY", link_ok);
}

static void draw_chip_row_composited(const ht5023_gfx_surface_t* screen,
                                     ht5023_st7789_t* lcd,
                                     uint8_t mode_idx,
                                     bool link_ok) {
  ht5023_gfx_surface_t panel;
  ht5023_text_style_t st;

  if (!panel_begin(70u, 12u, ht5023_st7789_color565(24, 36, 50), &panel)) {
    draw_chip_row(screen, mode_idx, link_ok);
    return;
  }

  ht5023_text_default_style(&st, ht5023_st7789_color565(230, 238, 246), 0u);
  st.opaque_bg = false;

  // AUTO
  ht5023_fb_clear(&g_panel_fb,
                  (mode_idx == 0u) ? ht5023_st7789_color565(74, 126, 96) : ht5023_st7789_color565(24, 36, 50));
  ht5023_gfx_draw_rect(&panel, 0, 0, 70, 12,
                       (mode_idx == 0u) ? ht5023_st7789_color565(255, 245, 190) : ht5023_st7789_color565(98, 122, 144));
  ht5023_text_draw_str(&panel, 5, 2, "AUTO", &st);
  panel_flush(lcd, CARD1_X, CHIPS_Y);

  // MANUAL
  ht5023_fb_clear(&g_panel_fb,
                  (mode_idx == 1u) ? ht5023_st7789_color565(86, 74, 118) : ht5023_st7789_color565(24, 36, 50));
  ht5023_gfx_draw_rect(&panel, 0, 0, 70, 12,
                       (mode_idx == 1u) ? ht5023_st7789_color565(220, 196, 255) : ht5023_st7789_color565(98, 122, 144));
  ht5023_text_draw_str(&panel, 5, 2, "MANUAL", &st);
  panel_flush(lcd, CARD2_X, CHIPS_Y);

  // LINK CHIP
  ht5023_fb_clear(&g_panel_fb, link_ok ? ht5023_st7789_color565(56, 122, 104) : ht5023_st7789_color565(62, 42, 44));
  ht5023_gfx_draw_rect(&panel, 0, 0, 70, 12,
                       link_ok ? ht5023_st7789_color565(188, 255, 232) : ht5023_st7789_color565(218, 126, 126));
  ht5023_text_draw_str(&panel, 5, 2, link_ok ? "SYNCED" : "RETRY", &st);
  panel_flush(lcd, CARD3_X, CHIPS_Y);
}

static void draw_dial_widget(const ht5023_gfx_surface_t* screen,
                             int16_t cx,
                             int16_t cy,
                             int16_t r,
                             uint8_t pct,
                             uint16_t color) {
  ht5023_text_style_t st;
  char buf[8];
  uint8_t phase;
  int16_t nx;
  int16_t ny;

  ht5023_gfx_fill_circle(screen, cx, cy, r, ht5023_st7789_color565(16, 24, 34));
  ht5023_gfx_draw_circle(screen, cx, cy, r, ht5023_st7789_color565(138, 168, 196));
  ht5023_gfx_draw_circle(screen, cx, cy, (int16_t)(r - 2), ht5023_st7789_color565(58, 84, 106));

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
  ht5023_gfx_fill_circle(screen, cx, cy, 2, ht5023_st7789_color565(255, 255, 255));

  ht5023_text_default_style(&st, ht5023_st7789_color565(218, 234, 244), 0u);
  st.opaque_bg = false;
  format_value(buf, (uint8_t)sizeof(buf), pct, "%");
  ht5023_text_draw_str(screen, (int16_t)(cx - 8), (int16_t)(cy + r + 1), buf, &st);
}

static void draw_trend_dynamic(const ht5023_gfx_surface_t* screen,
                               uint8_t latest,
                               uint8_t load,
                               uint8_t battery) {
  const int16_t gx = TREND_X + 8;
  const int16_t gy = TREND_Y + 18;
  const int16_t gw = 154;
  const int16_t gh = TREND_H - 30;
  ht5023_text_style_t st;
  char pct[8];

  ht5023_gfx_fill_rect(screen,
                       (int16_t)(TREND_X + 2),
                       (int16_t)(TREND_Y + 18),
                       (int16_t)(TREND_W - 4),
                       (int16_t)(TREND_H - 20),
                       ht5023_st7789_color565(10, 18, 26));

  for (uint8_t i = 1u; i <= 3u; ++i) {
    int16_t yy = (int16_t)(gy + ((int16_t)(gh - 1) * i) / 4);
    ht5023_gfx_draw_hline(screen, gx, yy, gw, ht5023_st7789_color565(28, 48, 66));
  }

  for (uint8_t i = 0u; i < (uint8_t)(TREND_POINTS - 1u); ++i) {
    int16_t x0 = (int16_t)(gx + ((int32_t)(gw - 1) * i) / (TREND_POINTS - 1));
    int16_t x1 = (int16_t)(gx + ((int32_t)(gw - 1) * (i + 1u)) / (TREND_POINTS - 1));
    int16_t y0 = (int16_t)(gy + (gh - 1) - ((int32_t)(gh - 1) * g_trend_hist[i]) / 100);
    int16_t y1 = (int16_t)(gy + (gh - 1) - ((int32_t)(gh - 1) * g_trend_hist[i + 1u]) / 100);
    ht5023_gfx_draw_line(screen, x0, y0, x1, y1, ht5023_st7789_color565(78, 250, 220));
  }

  ht5023_gfx_fill_circle(screen,
                         (int16_t)(gx + gw - 1),
                         (int16_t)(gy + (gh - 1) - ((int32_t)(gh - 1) * latest) / 100),
                         2,
                         ht5023_st7789_color565(255, 230, 120));

  ht5023_gfx_draw_vline(screen, (int16_t)(gx + gw + 8), (int16_t)(TREND_Y + 18), (int16_t)(TREND_H - 20),
                        ht5023_st7789_color565(40, 64, 86));
  draw_dial_widget(screen, (int16_t)(TREND_X + 186), (int16_t)(TREND_Y + 36), 15, load,
                   ht5023_st7789_color565(92, 255, 214));
  draw_dial_widget(screen, (int16_t)(TREND_X + 214), (int16_t)(TREND_Y + 36), 11, battery,
                   ht5023_st7789_color565(255, 192, 88));

  ht5023_text_default_style(&st, ht5023_st7789_color565(188, 214, 238), 0u);
  st.opaque_bg = false;
  format_value(pct, (uint8_t)sizeof(pct), latest, "%");
  ht5023_text_draw_str(screen, (int16_t)(TREND_X + TREND_W - 30), (int16_t)(TREND_Y + 8), pct, &st);
}

static void draw_trend_dynamic_composited(const ht5023_gfx_surface_t* screen,
                                          ht5023_st7789_t* lcd,
                                          uint8_t latest,
                                          uint8_t load,
                                          uint8_t battery) {
  ht5023_gfx_surface_t panel;
  ht5023_text_style_t st;
  char pct[8];
  const int16_t gx = 6;
  const int16_t gy = 0;
  const int16_t gw = 154;
  const int16_t gh = 50;

  if (!panel_begin((uint16_t)(TREND_W - 4), (uint16_t)(TREND_H - 20),
                   ht5023_st7789_color565(12, 22, 30), &panel)) {
    draw_trend_dynamic(screen, latest, load, battery);
    return;
  }

  for (uint8_t i = 1u; i <= 3u; ++i) {
    int16_t yy = (int16_t)(gy + ((int16_t)(gh - 1) * i) / 4);
    ht5023_gfx_draw_hline(&panel, gx, yy, gw, ht5023_st7789_color565(34, 58, 76));
  }
  for (uint8_t i = 0u; i < (uint8_t)(TREND_POINTS - 1u); ++i) {
    int16_t x0 = (int16_t)(gx + ((int32_t)(gw - 1) * i) / (TREND_POINTS - 1));
    int16_t x1 = (int16_t)(gx + ((int32_t)(gw - 1) * (i + 1u)) / (TREND_POINTS - 1));
    int16_t y0 = (int16_t)(gy + (gh - 1) - ((int32_t)(gh - 1) * g_trend_hist[i]) / 100);
    int16_t y1 = (int16_t)(gy + (gh - 1) - ((int32_t)(gh - 1) * g_trend_hist[i + 1u]) / 100);
    ht5023_gfx_draw_line(&panel, x0, y0, x1, y1, ht5023_st7789_color565(102, 246, 228));
  }
  ht5023_gfx_fill_circle(&panel,
                         (int16_t)(gx + gw - 1),
                         (int16_t)(gy + (gh - 1) - ((int32_t)(gh - 1) * latest) / 100),
                         2,
                         ht5023_st7789_color565(255, 216, 118));
  ht5023_gfx_draw_vline(&panel, (int16_t)(gx + gw + 8), 0, (int16_t)(TREND_H - 20),
                        ht5023_st7789_color565(46, 72, 94));
  draw_dial_widget(&panel, 184, 18, 15, load, ht5023_st7789_color565(112, 255, 228));
  draw_dial_widget(&panel, 212, 18, 11, battery, ht5023_st7789_color565(255, 206, 106));
  panel_flush(lcd, (uint16_t)(TREND_X + 2), (uint16_t)(TREND_Y + 18));

  // Keep top-right percentage in the original panel header strip.
  ht5023_text_default_style(&st, ht5023_st7789_color565(204, 228, 244), 0u);
  st.opaque_bg = false;
  format_value(pct, (uint8_t)sizeof(pct), latest, "%");
  ht5023_text_draw_str(screen, (int16_t)(TREND_X + TREND_W - 30), (int16_t)(TREND_Y + 8), pct, &st);
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
  ht5023_gfx_fill_rect(screen, x, y, w, h, ht5023_st7789_color565(18, 24, 30));
  ht5023_gfx_draw_rect(screen, x, y, w, h, ht5023_st7789_color565(138, 150, 170));
  fill_w = (uint16_t)(((uint32_t)(w - 2) * pct) / 100u);
  if (fill_w > 0u) {
    ht5023_gfx_fill_rect(screen, (int16_t)(x + 1), (int16_t)(y + 1), (int16_t)fill_w, (int16_t)(h - 2), fg);
  }
}

static void draw_status_dynamic(const ht5023_gfx_surface_t* screen,
                                uint8_t load,
                                uint8_t battery,
                                bool link_ok,
                                uint16_t frame,
                                uint8_t temp_c) {
  ht5023_text_style_t st;
  static const char* const EVENTS[4] = {
    "FLOW OK",
    "CACHE UP",
    "TEMP HI",
    "DMA BST",
  };
  const char* evt = EVENTS[(frame / 20u) & 3u];
  uint16_t led_ok = link_ok ? ht5023_st7789_color565(90, 255, 130) : ht5023_st7789_color565(90, 60, 40);
  uint16_t led_warn = (!link_ok && ((frame / 6u) & 1u))
    ? ht5023_st7789_color565(255, 100, 80)
    : ht5023_st7789_color565(60, 34, 30);

  ht5023_gfx_fill_rect(screen,
                       (int16_t)(STATUS_X + 2),
                       (int16_t)(STATUS_Y + 17),
                       (int16_t)(STATUS_W - 4),
                       (int16_t)(STATUS_H - 19),
                       ht5023_st7789_color565(22, 18, 14));

  ht5023_text_default_style(&st, ht5023_st7789_color565(215, 220, 228), 0u);
  st.opaque_bg = false;
  ht5023_text_draw_str(screen, STATUS_X + 8, STATUS_Y + 22, "LOAD", &st);
  ht5023_text_draw_str(screen, STATUS_X + 8, STATUS_Y + 37, "BATT", &st);

  draw_bar(screen,
           (int16_t)(STATUS_X + 46),
           (int16_t)(STATUS_Y + 22),
           128,
           9,
           load,
           ht5023_st7789_color565(70, 220, 255));
  draw_bar(screen,
           (int16_t)(STATUS_X + 46),
           (int16_t)(STATUS_Y + 37),
           128,
           9,
           battery,
           ht5023_st7789_color565(255, 210, 90));

  ht5023_gfx_fill_circle(screen, (int16_t)(STATUS_X + 191), (int16_t)(STATUS_Y + 29), 4, led_ok);
  ht5023_gfx_fill_circle(screen, (int16_t)(STATUS_X + 191), (int16_t)(STATUS_Y + 43), 4, led_warn);

  ht5023_text_draw_str(screen, STATUS_X + 200, STATUS_Y + 25, "NET", &st);
  ht5023_text_draw_str(screen, STATUS_X + 200, STATUS_Y + 39, "ALM", &st);

  ht5023_gfx_fill_rect(screen, (int16_t)(STATUS_X + 138), (int16_t)(STATUS_Y + 18), 46, 12,
                       ht5023_st7789_color565(28, 24, 20));
  ht5023_text_draw_str(screen, STATUS_X + 140, STATUS_Y + 21, evt, &st);

  ht5023_gfx_fill_rect(screen, (int16_t)(STATUS_X + 138), (int16_t)(STATUS_Y + 33), 46, 14,
                       ht5023_st7789_color565(22, 18, 16));
  for (uint8_t i = 0u; i < (uint8_t)(SPARK_POINTS - 1u); ++i) {
    int16_t x0 = (int16_t)(STATUS_X + 140 + ((int16_t)i * 42) / (SPARK_POINTS - 1));
    int16_t x1 = (int16_t)(STATUS_X + 140 + ((int16_t)(i + 1u) * 42) / (SPARK_POINTS - 1));
    int16_t y0 = (int16_t)(STATUS_Y + 45 - ((int16_t)g_load_hist[i] * 8) / 100);
    int16_t y1 = (int16_t)(STATUS_Y + 45 - ((int16_t)g_load_hist[i + 1u] * 8) / 100);
    ht5023_gfx_draw_line(screen, x0, y0, x1, y1, ht5023_st7789_color565(255, 156, 90));
  }

  {
    char tbuf[8];
    format_value(tbuf, (uint8_t)sizeof(tbuf), temp_c, "C");
    ht5023_text_draw_str(screen, STATUS_X + 186, STATUS_Y + 22, tbuf, &st);
  }
}

static void draw_status_dynamic_composited(const ht5023_gfx_surface_t* screen,
                                           ht5023_st7789_t* lcd,
                                           uint8_t load,
                                           uint8_t battery,
                                           bool link_ok,
                                           uint16_t frame,
                                           uint8_t temp_c) {
  ht5023_gfx_surface_t panel;
  ht5023_text_style_t st;
  static const char* const EVENTS[4] = {
    "FLOW OK",
    "CACHE UP",
    "TEMP HI",
    "DMA BST",
  };
  const char* evt = EVENTS[(frame / 20u) & 3u];
  uint16_t led_ok = link_ok ? ht5023_st7789_color565(90, 255, 130) : ht5023_st7789_color565(90, 60, 40);
  uint16_t led_warn = (!link_ok && ((frame / 6u) & 1u))
    ? ht5023_st7789_color565(255, 100, 80)
    : ht5023_st7789_color565(60, 34, 30);

  if (!panel_begin((uint16_t)(STATUS_W - 4), (uint16_t)(STATUS_H - 19),
                   ht5023_st7789_color565(24, 20, 16), &panel)) {
    draw_status_dynamic(screen, load, battery, link_ok, frame, temp_c);
    return;
  }

  ht5023_text_default_style(&st, ht5023_st7789_color565(224, 228, 234), 0u);
  st.opaque_bg = false;
  ht5023_text_draw_str(&panel, 6, 5, "LOAD", &st);
  ht5023_text_draw_str(&panel, 6, 20, "BATT", &st);

  draw_bar(&panel, 44, 5, 128, 9, load, ht5023_st7789_color565(84, 226, 255));
  draw_bar(&panel, 44, 20, 128, 9, battery, ht5023_st7789_color565(255, 218, 100));

  ht5023_gfx_fill_circle(&panel, 189, 12, 4, led_ok);
  ht5023_gfx_fill_circle(&panel, 189, 26, 4, led_warn);
  ht5023_text_draw_str(&panel, 198, 8, "NET", &st);
  ht5023_text_draw_str(&panel, 198, 22, "ALM", &st);

  ht5023_gfx_fill_rect(&panel, 136, 1, 46, 12, ht5023_st7789_color565(30, 26, 22));
  ht5023_gfx_draw_rect(&panel, 136, 1, 46, 12, ht5023_st7789_color565(98, 86, 68));
  ht5023_text_draw_str(&panel, 138, 4, evt, &st);

  ht5023_gfx_fill_rect(&panel, 136, 16, 46, 14, ht5023_st7789_color565(22, 20, 18));
  for (uint8_t i = 0u; i < (uint8_t)(SPARK_POINTS - 1u); ++i) {
    int16_t x0 = (int16_t)(138 + ((int16_t)i * 42) / (SPARK_POINTS - 1));
    int16_t x1 = (int16_t)(138 + ((int16_t)(i + 1u) * 42) / (SPARK_POINTS - 1));
    int16_t y0 = (int16_t)(28 - ((int16_t)g_load_hist[i] * 8) / 100);
    int16_t y1 = (int16_t)(28 - ((int16_t)g_load_hist[i + 1u] * 8) / 100);
    ht5023_gfx_draw_line(&panel, x0, y0, x1, y1, ht5023_st7789_color565(255, 172, 112));
  }
  {
    char tbuf[8];
    format_value(tbuf, (uint8_t)sizeof(tbuf), temp_c, "C");
    ht5023_text_draw_str(&panel, 184, 5, tbuf, &st);
  }
  panel_flush(lcd, (uint16_t)(STATUS_X + 2), (uint16_t)(STATUS_Y + 17));
}

int main(void) {
  const ht5023_spi_master_cfg_t spi_cfg = { .spr = 2u, .cpol = false, .cpha = false };
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

  ht5023_st7789_set_rotation(&lcd, HT5023_ST7789_ROTATION_0);
  ht5023_gfx_surface_init_st7789(&screen, &lcd);

  trend_seed(52u);
  load_seed(40u);
  draw_static_layout(&screen);

  for (;;) {
    const st7789_dma_view_t dma = st7789_dma_view(&lcd);
    int16_t wave_a = sin64s((uint8_t)(frame + 0u));
    int16_t wave_b = sin64s((uint8_t)(frame + 18u));
    int16_t wave_c = sin64s((uint8_t)(frame + 36u));
    uint32_t rpm = 1000u + (uint32_t)(wave_a + 54) * 27u;
    uint32_t temp = 36u + (uint32_t)(wave_b + 54) / 3u;
    uint32_t speed = 20u + (uint32_t)(wave_c + 54);
    uint8_t load = (uint8_t)(28u + ((uint16_t)(wave_a + 54) * 66u) / 108u);
    uint8_t battery = (uint8_t)(40u + ((uint16_t)(wave_b + 54) * 55u) / 108u);
    uint8_t trend = (uint8_t)((load + (uint8_t)(speed / 2u)) / 2u);
    uint8_t mode_idx = (uint8_t)((frame / 36u) % 2u);
    bool link_ok = ((frame / 14u) & 7u) != 0u;

    trend_push(trend);
    load_push(load);

    draw_header_dynamic(&screen, frame, link_ok);
    draw_header_dma_stats(&screen, &dma);
    draw_cards_dynamic_composited(&screen, &lcd, rpm, temp, speed);
    draw_chip_row_composited(&screen, &lcd, mode_idx, link_ok);
    draw_trend_dynamic_composited(&screen, &lcd, trend, load, battery);
    draw_status_dynamic_composited(&screen, &lcd, load, battery, link_ok, frame, (uint8_t)temp);

    led_toggle();
    delay_ms_blocking(70u);
    ++frame;
  }
}
