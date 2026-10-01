// examples/led_dance/main.c — LED dance patterns on PE3/PE4
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"

extern void led_gpio_init(void);
extern void led_on(void);
extern void led_off(void);
extern void led_toggle(void);

static void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; ++i) {
    __asm__ volatile ("nop");
  }
}

static void beat(uint32_t cycles) {
  delay_cycles(cycles);
}

int main(void) {
  led_gpio_init();

  while (1) {
    // Warm-up blink burst.
    for (int i = 0; i < 4; ++i) {
      led_toggle();
      beat(120000);
    }

    // Slow groove.
    for (int i = 0; i < 6; ++i) {
      led_toggle();
      beat(260000);
    }

    // Fast strobe run.
    for (int i = 0; i < 16; ++i) {
      led_toggle();
      beat(50000);
    }

    // Pause + accent hits.
    led_off();
    beat(220000);
    led_on();
    beat(90000);
    led_off();
    beat(90000);
    led_on();
    beat(180000);

    // Triplet-style section.
    for (int i = 0; i < 9; ++i) {
      led_toggle();
      beat((i % 3) == 2 ? 180000 : 70000);
    }

    // Final flourish.
    for (int i = 0; i < 10; ++i) {
      led_toggle();
      beat(35000 + (uint32_t)i * 12000u);
    }

    led_off();
    beat(300000);
  }
}

