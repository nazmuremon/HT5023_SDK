// examples/touch_ui_demo/main.c — ST7789 + XPT2046 touch UI demo (draw + color + clear)
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/gfx.h"
#include "ht5023/spi.h"
#include "ht5023/st7789.h"
#include "ht5023/text.h"
#include "ht5023/xpt2046.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_toggle(void);

#ifndef TOUCH_UI_CPU_HZ
#define TOUCH_UI_CPU_HZ HT5023_FCPU_HZ
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

#ifndef TOUCH_RAW_X_MIN
#define TOUCH_RAW_X_MIN 200u
#endif
#ifndef TOUCH_RAW_X_MAX
#define TOUCH_RAW_X_MAX 3900u
#endif
#ifndef TOUCH_RAW_Y_MIN
#define TOUCH_RAW_Y_MIN 200u
#endif
#ifndef TOUCH_RAW_Y_MAX
#define TOUCH_RAW_Y_MAX 3900u
#endif
#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY false
#endif
#ifndef TOUCH_INVERT_X
#define TOUCH_INVERT_X false
#endif
#ifndef TOUCH_INVERT_Y
#define TOUCH_INVERT_Y false
#endif
#ifndef TOUCH_USE_PENIRQ
#define TOUCH_USE_PENIRQ 0u
#endif

#define RGB565(r, g, b) (uint16_t)((((uint16_t)(r) & 0xF8u) << 8) | \
                                   (((uint16_t)(g) & 0xFCu) << 3) | \
                                   (((uint16_t)(b) & 0xF8u) >> 3))

#define GPIOA_BASE_ADDR       0x40011000u
#define GPIO_IOCFG_OFFSET_A   0x00u
#define GPIO_AFCFG_OFFSET_A   0x04u
#define GPIO_PTDIR_OFFSET_A   0x08u
#define GPIO_PTUP_OFFSET_A    0x0Cu
#define GPIO_PTDAT_OFFSET_A   0x10u
#define GPIO_PTSET_OFFSET_A   0x14u
#define GPIO_PTCLR_OFFSET_A   0x18u
#define GPIO_PTOD_OFFSET_A    0x20u

#define ST7789_DC_PIN         0u
#define ST7789_RST_PIN        2u
#define TOUCH_CS_PIN          3u
#define TOUCH_PENIRQ_PIN      4u

#define UI_HEADER_H           24
#define UI_CANVAS_X           8
#define UI_CANVAS_Y           32
#define UI_CANVAS_W           224
#define UI_CANVAS_H           150
#define UI_BUTTON_Y           196
#define UI_BUTTON_W           68
#define UI_BUTTON_H           34
#define UI_GAP                8
#define UI_BTN_RED_X          8
#define UI_BTN_CYAN_X         (UI_BTN_RED_X + UI_BUTTON_W + UI_GAP)
#define UI_BTN_CLEAR_X        (UI_BTN_CYAN_X + UI_BUTTON_W + UI_GAP)

typedef struct {
  uint32_t gpio_base;
  uint8_t dc_pin;
  uint8_t rst_pin;
} st7789_hal_ctx_t;

typedef struct {
  uint16_t brush_color;
  bool pen_was_down;
  bool last_canvas_valid;
  uint16_t last_x;
  uint16_t last_y;
} ui_state_t;

static inline volatile uint16_t* reg16_local(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(base + off);
}

static void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; ++i) __asm__ volatile ("nop");
}

static void delay_ms_blocking(uint32_t ms) {
  delay_cycles((TOUCH_UI_CPU_HZ / 9000u) * ms);
}

static void gpio_pin_output_init(uint8_t pin, bool high) {
  const uint16_t mask = (uint16_t)(1u << (pin & 0x0Fu));
  *reg16_local(GPIOA_BASE_ADDR, GPIO_IOCFG_OFFSET_A) &= (uint16_t)~mask;
  *reg16_local(GPIOA_BASE_ADDR, GPIO_AFCFG_OFFSET_A) &= (uint16_t)~mask;
  *reg16_local(GPIOA_BASE_ADDR, GPIO_PTOD_OFFSET_A) |= mask;
  if (high) *reg16_local(GPIOA_BASE_ADDR, GPIO_PTSET_OFFSET_A) = mask;
  else *reg16_local(GPIOA_BASE_ADDR, GPIO_PTCLR_OFFSET_A) = mask;
  *reg16_local(GPIOA_BASE_ADDR, GPIO_PTDIR_OFFSET_A) |= mask;
}

#if TOUCH_USE_PENIRQ
static void gpio_pin_input_pullup_init(uint8_t pin) {
  const uint16_t mask = (uint16_t)(1u << (pin & 0x0Fu));
  *reg16_local(GPIOA_BASE_ADDR, GPIO_IOCFG_OFFSET_A) &= (uint16_t)~mask;
  *reg16_local(GPIOA_BASE_ADDR, GPIO_AFCFG_OFFSET_A) &= (uint16_t)~mask;
  *reg16_local(GPIOA_BASE_ADDR, GPIO_PTDIR_OFFSET_A) &= (uint16_t)~mask;
  *reg16_local(GPIOA_BASE_ADDR, GPIO_PTUP_OFFSET_A) |= mask;
}
#endif

static void gpio_pin_write(uint8_t pin, bool high) {
  const uint16_t mask = (uint16_t)(1u << (pin & 0x0Fu));
  if (high) *reg16_local(GPIOA_BASE_ADDR, GPIO_PTSET_OFFSET_A) = mask;
  else *reg16_local(GPIOA_BASE_ADDR, GPIO_PTCLR_OFFSET_A) = mask;
}

#if TOUCH_USE_PENIRQ
static bool gpio_pin_read(uint8_t pin) {
  const uint16_t mask = (uint16_t)(1u << (pin & 0x0Fu));
  return ((*reg16_local(GPIOA_BASE_ADDR, GPIO_PTDAT_OFFSET_A)) & mask) != 0u;
}
#endif

static void st7789_spi_write_cb(const uint8_t* bytes, size_t len, void* user) {
  (void)user;
  ht5023_spi_transfer(bytes, 0, len);
}

static void st7789_cs_cb(bool high, void* user) {
  (void)user;
  ht5023_spi_set_cs(high);
}

static void st7789_dc_cb(bool high, void* user) {
  st7789_hal_ctx_t* ctx = (st7789_hal_ctx_t*)user;
  gpio_pin_write(ctx->dc_pin, high);
}

static void st7789_rst_cb(bool high, void* user) {
  st7789_hal_ctx_t* ctx = (st7789_hal_ctx_t*)user;
  gpio_pin_write(ctx->rst_pin, high);
}

static void st7789_delay_ms_cb(uint32_t ms, void* user) {
  (void)user;
  delay_ms_blocking(ms);
}

static uint8_t touch_spi_xfer_cb(uint8_t tx, void* user) {
  (void)user;
  return ht5023_spi_transfer_byte(tx);
}

static void touch_cs_cb(bool high, void* user) {
  (void)user;
  gpio_pin_write(TOUCH_CS_PIN, high);
}

#if TOUCH_USE_PENIRQ
static bool touch_penirq_cb(void* user) {
  (void)user;
  return gpio_pin_read(TOUCH_PENIRQ_PIN);
}
#endif

static bool point_in_rect(uint16_t x,
                          uint16_t y,
                          int16_t rx,
                          int16_t ry,
                          int16_t rw,
                          int16_t rh) {
  return (x >= (uint16_t)rx) && (x < (uint16_t)(rx + rw)) &&
         (y >= (uint16_t)ry) && (y < (uint16_t)(ry + rh));
}

static void ui_draw_header(const ht5023_gfx_surface_t* screen, const char* mode_label) {
  ht5023_text_style_t title;
  ht5023_text_style_t body;

  ht5023_gfx_fill_rect(screen, 0, 0, ST7789_DEMO_WIDTH, UI_HEADER_H, RGB565(14, 24, 40));
  ht5023_gfx_draw_hline(screen, 0, UI_HEADER_H - 1, ST7789_DEMO_WIDTH, RGB565(90, 190, 255));

  ht5023_text_default_style(&title, RGB565(255, 255, 255), 0u);
  title.opaque_bg = false;
  title.scale = 2u;
  ht5023_text_draw_str(screen, 8, 4, "TOUCH UI", &title);

  ht5023_text_default_style(&body, RGB565(190, 220, 245), 0u);
  body.opaque_bg = false;
  ht5023_text_draw_str(screen, 140, 8, mode_label, &body);
}

static void ui_draw_button(const ht5023_gfx_surface_t* screen,
                           int16_t x,
                           int16_t y,
                           int16_t w,
                           int16_t h,
                           uint16_t fill,
                           uint16_t border,
                           const char* label,
                           bool active) {
  ht5023_text_style_t text;
  uint16_t edge = active ? RGB565(255, 255, 255) : border;

  ht5023_gfx_fill_rect(screen, x, y, w, h, fill);
  ht5023_gfx_draw_rect(screen, x, y, w, h, edge);
  if (active) {
    ht5023_gfx_draw_rect(screen, (int16_t)(x + 1), (int16_t)(y + 1), (int16_t)(w - 2), (int16_t)(h - 2), edge);
  }

  ht5023_text_default_style(&text, RGB565(255, 255, 255), 0u);
  text.opaque_bg = false;
  text.scale = 2u;
  ht5023_text_draw_str(screen, (int16_t)(x + 8), (int16_t)(y + 10), label, &text);
}

static void ui_draw_controls(const ht5023_gfx_surface_t* screen, uint16_t brush_color) {
  ui_draw_button(screen,
                 UI_BTN_RED_X,
                 UI_BUTTON_Y,
                 UI_BUTTON_W,
                 UI_BUTTON_H,
                 RGB565(150, 22, 22),
                 RGB565(255, 120, 120),
                 "RED",
                 brush_color == RGB565(255, 64, 64));

  ui_draw_button(screen,
                 UI_BTN_CYAN_X,
                 UI_BUTTON_Y,
                 UI_BUTTON_W,
                 UI_BUTTON_H,
                 RGB565(10, 90, 110),
                 RGB565(120, 230, 255),
                 "CYAN",
                 brush_color == RGB565(70, 255, 255));

  ui_draw_button(screen,
                 UI_BTN_CLEAR_X,
                 UI_BUTTON_Y,
                 UI_BUTTON_W,
                 UI_BUTTON_H,
                 RGB565(52, 52, 64),
                 RGB565(180, 180, 205),
                 "CLEAR",
                 false);
}

static void ui_draw_canvas_frame(const ht5023_gfx_surface_t* screen) {
  ht5023_gfx_fill_rect(screen, 0, UI_HEADER_H, ST7789_DEMO_WIDTH, ST7789_DEMO_HEIGHT - UI_HEADER_H,
                       RGB565(8, 10, 18));
  ht5023_gfx_fill_rect(screen,
                       UI_CANVAS_X,
                       UI_CANVAS_Y,
                       UI_CANVAS_W,
                       UI_CANVAS_H,
                       RGB565(20, 24, 30));
  ht5023_gfx_draw_rect(screen, UI_CANVAS_X, UI_CANVAS_Y, UI_CANVAS_W, UI_CANVAS_H, RGB565(90, 90, 120));
}

static void ui_clear_canvas(const ht5023_gfx_surface_t* screen) {
  ht5023_gfx_fill_rect(screen,
                       UI_CANVAS_X + 1,
                       UI_CANVAS_Y + 1,
                       UI_CANVAS_W - 2,
                       UI_CANVAS_H - 2,
                       RGB565(20, 24, 30));
}

static void ui_draw_splash(const ht5023_gfx_surface_t* screen) {
  ht5023_text_style_t t;
  ht5023_text_default_style(&t, RGB565(180, 190, 210), 0u);
  t.opaque_bg = false;
  ht5023_text_draw_str(screen, UI_CANVAS_X + 20, UI_CANVAS_Y + 62, "DRAW HERE", &t);
  ht5023_text_draw_str(screen, UI_CANVAS_X + 14, UI_CANVAS_Y + 76, "TAP BUTTONS BELOW", &t);
}

int main(void) {
  const ht5023_spi_master_cfg_t spi_cfg = {
    .spr = 3u,
    .cpol = false,
    .cpha = false,
  };
  st7789_hal_ctx_t hal = {
    .gpio_base = GPIOA_BASE_ADDR,
    .dc_pin = ST7789_DC_PIN,
    .rst_pin = ST7789_RST_PIN,
  };
  ht5023_st7789_t lcd;
  ht5023_gfx_surface_t screen;
  ht5023_xpt2046_t touch;
  ui_state_t ui = {
    .brush_color = RGB565(255, 64, 64),
    .pen_was_down = false,
    .last_canvas_valid = false,
    .last_x = 0u,
    .last_y = 0u,
  };
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
#if TOUCH_USE_PENIRQ
  const ht5023_xpt2046_config_t touch_cfg = {
    .spi_transfer_byte = touch_spi_xfer_cb,
    .cs_write = touch_cs_cb,
    .penirq_read = touch_penirq_cb,
    .user = 0,
    .raw_x_min = TOUCH_RAW_X_MIN,
    .raw_x_max = TOUCH_RAW_X_MAX,
    .raw_y_min = TOUCH_RAW_Y_MIN,
    .raw_y_max = TOUCH_RAW_Y_MAX,
    .out_width = ST7789_DEMO_WIDTH,
    .out_height = ST7789_DEMO_HEIGHT,
    .swap_xy = TOUCH_SWAP_XY,
    .invert_x = TOUCH_INVERT_X,
    .invert_y = TOUCH_INVERT_Y,
  };
#else
  const ht5023_xpt2046_config_t touch_cfg = {
    .spi_transfer_byte = touch_spi_xfer_cb,
    .cs_write = touch_cs_cb,
    .penirq_read = 0,
    .user = 0,
    .raw_x_min = TOUCH_RAW_X_MIN,
    .raw_x_max = TOUCH_RAW_X_MAX,
    .raw_y_min = TOUCH_RAW_Y_MIN,
    .raw_y_max = TOUCH_RAW_Y_MAX,
    .out_width = ST7789_DEMO_WIDTH,
    .out_height = ST7789_DEMO_HEIGHT,
    .swap_xy = TOUCH_SWAP_XY,
    .invert_x = TOUCH_INVERT_X,
    .invert_y = TOUCH_INVERT_Y,
  };
#endif

  led_gpio_init();
  gpio_pin_output_init(ST7789_DC_PIN, true);
  gpio_pin_output_init(ST7789_RST_PIN, true);
  gpio_pin_output_init(TOUCH_CS_PIN, true);
#if TOUCH_USE_PENIRQ
  gpio_pin_input_pullup_init(TOUCH_PENIRQ_PIN);
#endif

  (void)ht5023_spi_master_init(&spi_cfg);
  ht5023_spi_set_cs(true);  // Keep hardware SSN idle when touch transactions use PA3.

  if (!ht5023_st7789_init(&lcd, &lcd_cfg)) {
    for (;;) {
      led_toggle();
      delay_ms_blocking(200u);
    }
  }
  ht5023_st7789_set_rotation(&lcd, HT5023_ST7789_ROTATION_0);
  ht5023_gfx_surface_init_st7789(&screen, &lcd);
  (void)ht5023_xpt2046_init(&touch, &touch_cfg);

  ui_draw_canvas_frame(&screen);
  ui_draw_header(&screen, "MODE: RED");
  ui_draw_controls(&screen, ui.brush_color);
  ui_draw_splash(&screen);

  for (;;) {
    ht5023_xpt2046_point_t pt;
    const bool pen_down = ht5023_xpt2046_read_point(&touch, &pt);
    const bool pen_edge = pen_down && !ui.pen_was_down;

    if (pen_down) {
      bool in_canvas = point_in_rect(pt.x,
                                     pt.y,
                                     UI_CANVAS_X + 1,
                                     UI_CANVAS_Y + 1,
                                     UI_CANVAS_W - 2,
                                     UI_CANVAS_H - 2);

      if (point_in_rect(pt.x, pt.y, UI_BTN_RED_X, UI_BUTTON_Y, UI_BUTTON_W, UI_BUTTON_H) && pen_edge) {
        ui.brush_color = RGB565(255, 64, 64);
        ui_draw_header(&screen, "MODE: RED");
        ui_draw_controls(&screen, ui.brush_color);
        led_toggle();
      } else if (point_in_rect(pt.x, pt.y, UI_BTN_CYAN_X, UI_BUTTON_Y, UI_BUTTON_W, UI_BUTTON_H) && pen_edge) {
        ui.brush_color = RGB565(70, 255, 255);
        ui_draw_header(&screen, "MODE: CYAN");
        ui_draw_controls(&screen, ui.brush_color);
        led_toggle();
      } else if (point_in_rect(pt.x, pt.y, UI_BTN_CLEAR_X, UI_BUTTON_Y, UI_BUTTON_W, UI_BUTTON_H) && pen_edge) {
        ui_clear_canvas(&screen);
        ui_draw_header(&screen, "MODE: CLEARED");
        ui_draw_controls(&screen, ui.brush_color);
        ui.last_canvas_valid = false;
        led_toggle();
      } else if (in_canvas) {
        if (ui.last_canvas_valid) {
          ht5023_gfx_draw_line(&screen,
                               (int16_t)ui.last_x,
                               (int16_t)ui.last_y,
                               (int16_t)pt.x,
                               (int16_t)pt.y,
                               ui.brush_color);
        }
        ht5023_gfx_fill_circle(&screen, (int16_t)pt.x, (int16_t)pt.y, 2, ui.brush_color);
        ui.last_x = pt.x;
        ui.last_y = pt.y;
        ui.last_canvas_valid = true;
      } else {
        ui.last_canvas_valid = false;
      }
    } else {
      ui.last_canvas_valid = false;
    }

    ui.pen_was_down = pen_down;
    delay_cycles(9000u);
  }
}
