// examples/blinky/main.c — blink LEDs on PE3 + PE4
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"

extern void led_gpio_init(void);
extern void led_toggle(void);

static void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; ++i) {
    __asm__ volatile ("nop");
  }
}

int main(void) {
  led_gpio_init();

  for (int i = 0; i < 5; ++i) {
    led_toggle();
    delay_cycles(100);
  }

  while (1) {
    led_toggle();
    delay_cycles(95000);  // ~100 ms per state (scaled from 250000 ~= 500 ms)
  }
}

