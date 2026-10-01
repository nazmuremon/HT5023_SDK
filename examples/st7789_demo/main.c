// examples/st7789_demo/main.c — ST7789 SPI demo (color bars + moving box)
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/spi.h"
#include "ht5023/st7789.h"
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
// Some 240x240 modules need 80 here. Default is 0 for widest compatibility.
#define ST7789_DEMO_Y_OFFSET 0u
#endif

// Control pins (user can rewire/change macros):
// PA0 -> ST7789 DC, PA2 -> ST7789 RST
#define GPIOA_BASE_ADDR       0x40011000u
#define GPIO_IOCFG_OFFSET_A   0x00u
#define GPIO_AFCFG_OFFSET_A   0x04u
#define GPIO_PTDIR_OFFSET_A   0x08u
#define GPIO_PTSET_OFFSET_A   0x14u
#define GPIO_PTCLR_OFFSET_A   0x18u
#define GPIO_PTOD_OFFSET_A    0x20u

#define ST7789_DC_PIN   0u
#define ST7789_RST_PIN  2u

typedef struct {
  uint32_t gpio_base;
  uint8_t dc_pin;
  uint8_t rst_pin;
} st7789_hal_ctx_t;

static inline volatile uint16_t* reg16_local(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(base + off);
}

static void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; ++i) {
    __asm__ volatile ("nop");
  }
}

static void delay_ms_blocking(uint32_t ms) {
  if (ms == 0u) return;
  // Busy-loop approximation; tune ST7789_DEMO_CPU_HZ if timing-sensitive.
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

static void draw_color_bars(ht5023_st7789_t* lcd) {
  const uint16_t colors[] = {
    ht5023_st7789_color565(255, 0, 0),
    ht5023_st7789_color565(255, 128, 0),
    ht5023_st7789_color565(255, 255, 0),
    ht5023_st7789_color565(0, 200, 0),
    ht5023_st7789_color565(0, 160, 255),
    ht5023_st7789_color565(0, 0, 255),
    ht5023_st7789_color565(180, 0, 255),
    ht5023_st7789_color565(255, 255, 255),
  };
  const uint16_t bar_h = (uint16_t)(lcd->height / (sizeof(colors) / sizeof(colors[0])));
  uint16_t y = 0u;
  for (uint32_t i = 0; i < (sizeof(colors) / sizeof(colors[0])); ++i) {
    uint16_t h = bar_h;
    if (i == (sizeof(colors) / sizeof(colors[0])) - 1u) {
      h = (uint16_t)(lcd->height - y);
    }
    ht5023_st7789_fill_rect(lcd, 0u, y, lcd->width, h, colors[i]);
    y = (uint16_t)(y + h);
  }
}

static void draw_border(ht5023_st7789_t* lcd, uint16_t color) {
  ht5023_st7789_fill_rect(lcd, 0u, 0u, lcd->width, 4u, color);
  ht5023_st7789_fill_rect(lcd, 0u, (uint16_t)(lcd->height - 4u), lcd->width, 4u, color);
  ht5023_st7789_fill_rect(lcd, 0u, 0u, 4u, lcd->height, color);
  ht5023_st7789_fill_rect(lcd, (uint16_t)(lcd->width - 4u), 0u, 4u, lcd->height, color);
}

int main(void) {
  const ht5023_spi_master_cfg_t spi_cfg = {
    .spr = 2u,   // Fsys/8
    .cpol = false,
    .cpha = false,
  };
  st7789_hal_ctx_t hal = {
    .gpio_base = GPIOA_BASE_ADDR,
    .dc_pin = ST7789_DC_PIN,
    .rst_pin = ST7789_RST_PIN,
  };
  ht5023_st7789_t lcd;
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

  uint16_t x = 8u, y = 8u;
  int16_t dx = 6, dy = 5;
  const uint16_t box_w = 28u, box_h = 28u;

  led_gpio_init();
  gpio_pin_output_init(GPIOA_BASE_ADDR, ST7789_DC_PIN, true);
  gpio_pin_output_init(GPIOA_BASE_ADDR, ST7789_RST_PIN, true);

  (void)ht5023_spi_master_init(&spi_cfg);
  if (!ht5023_st7789_init(&lcd, &lcd_cfg)) {
    while (1) {
      led_toggle();
      delay_ms_blocking(200u);
    }
  }

  ht5023_st7789_set_rotation(&lcd, HT5023_ST7789_ROTATION_0);
  draw_color_bars(&lcd);
  draw_border(&lcd, ht5023_st7789_color565(255, 255, 255));

  for (;;) {
    // restore background bars under previous box
    draw_color_bars(&lcd);
    draw_border(&lcd, ht5023_st7789_color565(255, 255, 255));

    ht5023_st7789_fill_rect(&lcd, x, y, box_w, box_h, ht5023_st7789_color565(0, 0, 0));
    ht5023_st7789_fill_rect(&lcd, (uint16_t)(x + 4u), (uint16_t)(y + 4u),
                            (uint16_t)(box_w - 8u), (uint16_t)(box_h - 8u),
                            ht5023_st7789_color565(255, 255, 255));

    {
      int32_t next_x = (int32_t)x + dx;
      int32_t next_y = (int32_t)y + dy;
      const int32_t min_x = 4;
      const int32_t min_y = 4;
      const int32_t max_x = (int32_t)lcd.width - (int32_t)box_w - 4;
      const int32_t max_y = (int32_t)lcd.height - (int32_t)box_h - 4;

      if (next_x < min_x || next_x > max_x) {
        dx = (int16_t)(-dx);
        next_x = (int32_t)x + dx;
      }
      if (next_y < min_y || next_y > max_y) {
        dy = (int16_t)(-dy);
        next_y = (int32_t)y + dy;
      }
      x = (uint16_t)next_x;
      y = (uint16_t)next_y;
    }

    led_toggle();
    delay_ms_blocking(40u);
  }
}
