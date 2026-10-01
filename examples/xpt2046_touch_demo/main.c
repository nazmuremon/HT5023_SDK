// examples/xpt2046_touch_demo/main.c — XPT2046 touch polling demo (SPI + UART2)
#include <stdbool.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/spi.h"
#include "ht5023/uart.h"
#include "ht5023/xpt2046.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_toggle(void);

#ifndef UART_EXAMPLE_SYSCLK_HZ
#define UART_EXAMPLE_SYSCLK_HZ HT5023_FSYS_HZ
#endif
#ifndef UART_EXAMPLE_BAUD
#define UART_EXAMPLE_BAUD 9600u
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
#ifndef TOUCH_OUT_WIDTH
#define TOUCH_OUT_WIDTH 240u
#endif
#ifndef TOUCH_OUT_HEIGHT
#define TOUCH_OUT_HEIGHT 240u
#endif

// Touch CS and optional PENIRQ on GPIOA
#define GPIOA_BASE_ADDR       0x40011000u
#define GPIO_IOCFG_OFFSET_A   0x00u
#define GPIO_AFCFG_OFFSET_A   0x04u
#define GPIO_PTDIR_OFFSET_A   0x08u
#define GPIO_PTUP_OFFSET_A    0x0Cu
#define GPIO_PTDAT_OFFSET_A   0x10u
#define GPIO_PTSET_OFFSET_A   0x14u
#define GPIO_PTCLR_OFFSET_A   0x18u
#define GPIO_PTOD_OFFSET_A    0x20u

#define TOUCH_CS_PIN      3u   // PA3 -> XPT2046 CS
#define TOUCH_PENIRQ_PIN  4u   // PA4 <- XPT2046 PENIRQ (optional)

static inline volatile uint16_t* reg16_local(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(base + off);
}

static void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; ++i) __asm__ volatile ("nop");
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

static void gpio_pin_input_pullup_init(uint8_t pin) {
  const uint16_t mask = (uint16_t)(1u << (pin & 0x0Fu));
  *reg16_local(GPIOA_BASE_ADDR, GPIO_IOCFG_OFFSET_A) &= (uint16_t)~mask;
  *reg16_local(GPIOA_BASE_ADDR, GPIO_AFCFG_OFFSET_A) &= (uint16_t)~mask;
  *reg16_local(GPIOA_BASE_ADDR, GPIO_PTDIR_OFFSET_A) &= (uint16_t)~mask;
  *reg16_local(GPIOA_BASE_ADDR, GPIO_PTUP_OFFSET_A) |= mask;
}

static void gpio_pin_write(uint8_t pin, bool high) {
  const uint16_t mask = (uint16_t)(1u << (pin & 0x0Fu));
  if (high) *reg16_local(GPIOA_BASE_ADDR, GPIO_PTSET_OFFSET_A) = mask;
  else *reg16_local(GPIOA_BASE_ADDR, GPIO_PTCLR_OFFSET_A) = mask;
}

static bool gpio_pin_read(uint8_t pin) {
  const uint16_t mask = (uint16_t)(1u << (pin & 0x0Fu));
  return ((*reg16_local(GPIOA_BASE_ADDR, GPIO_PTDAT_OFFSET_A)) & mask) != 0u;
}

static uint8_t spi_xfer_cb(uint8_t tx, void* user) {
  (void)user;
  return ht5023_spi_transfer_byte(tx);
}

static void touch_cs_cb(bool high, void* user) {
  (void)user;
  gpio_pin_write(TOUCH_CS_PIN, high);
}

static bool touch_penirq_cb(void* user) {
  (void)user;
  return gpio_pin_read(TOUCH_PENIRQ_PIN);
}

static void uputc(char c) { ht5023_uart_write_byte(HT5023_UART_ID_2, (uint8_t)c); }
static void uputs(const char* s) { ht5023_uart_write_str(HT5023_UART_ID_2, s); }

static void udec16(uint16_t v) {
  char buf[6];
  uint8_t n = 0;
  if (v == 0u) { uputc('0'); return; }
  while (v && n < sizeof(buf)) { buf[n++] = (char)('0' + (v % 10u)); v /= 10u; }
  while (n) uputc(buf[--n]);
}

int main(void) {
  const ht5023_uart_config_t uart_cfg = {
    .sysclk_hz = UART_EXAMPLE_SYSCLK_HZ, .baud = UART_EXAMPLE_BAUD,
    .enable_tx = true, .enable_rx = true,
  };
  const ht5023_spi_master_cfg_t spi_cfg = {
    .spr = 3u, .cpol = false, .cpha = false  // conservative speed for touch controller
  };
  ht5023_xpt2046_t touch;
  const ht5023_xpt2046_config_t touch_cfg = {
    .spi_transfer_byte = spi_xfer_cb,
    .cs_write = touch_cs_cb,
    .penirq_read = touch_penirq_cb,
    .user = 0,
    .raw_x_min = TOUCH_RAW_X_MIN,
    .raw_x_max = TOUCH_RAW_X_MAX,
    .raw_y_min = TOUCH_RAW_Y_MIN,
    .raw_y_max = TOUCH_RAW_Y_MAX,
    .out_width = TOUCH_OUT_WIDTH,
    .out_height = TOUCH_OUT_HEIGHT,
    .swap_xy = false,
    .invert_x = false,
    .invert_y = false,
  };
  uint16_t idle_ctr = 0u;

  led_gpio_init();
  gpio_pin_output_init(TOUCH_CS_PIN, true);
  gpio_pin_input_pullup_init(TOUCH_PENIRQ_PIN);

  ht5023_uart2_pins_init_pc11_pc12();
  (void)ht5023_uart_init(HT5023_UART_ID_2, &uart_cfg);
  (void)ht5023_spi_master_init(&spi_cfg);
  ht5023_spi_set_cs(true);  // keep SSN0 inactive; touch uses PA3 CS callback
  (void)ht5023_xpt2046_init(&touch, &touch_cfg);

  uputs("\r\nHT5023 XPT2046 touch demo (SPI + UART2)\r\n");
  uputs("SPI pins: PC4=MOSI PC5=MISO PC6=CLK (PC7 unused or display CS)\r\n");
  uputs("Touch pins: PA3=CS, PA4=PENIRQ(optional active-low)\r\n");
  uputs("Adjust TOUCH_RAW_* calibration macros if mapped coords are wrong.\r\n\r\n");

  for (;;) {
    ht5023_xpt2046_point_t pt;
    ht5023_xpt2046_raw_sample_t raw;

    if (ht5023_xpt2046_read_raw_sample(&touch, &raw) && ht5023_xpt2046_read_point(&touch, &pt)) {
      uputs("TOUCH raw(");
      udec16(raw.x_raw); uputc(','); udec16(raw.y_raw);
      uputs(") z1="); udec16(raw.z1_raw);
      uputs(" xy("); udec16(pt.x); uputc(','); udec16(pt.y); uputs(")\r\n");
      led_toggle();
      idle_ctr = 0u;
      delay_cycles(70000u);
    } else {
      if (++idle_ctr >= 80u) {
        idle_ctr = 0u;
        uputs("idle\r\n");
      }
      delay_cycles(25000u);
    }
  }
}

