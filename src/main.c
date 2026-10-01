// main.c — blink an LED
#include <stdint.h>
#include "board_gpio.h"

extern void led_gpio_init(void);
extern void led_on(void);
extern void led_off(void);
extern void led_toggle(void);

// crude busy-wait delay (tuned for ~10MHz default RC)
static void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; ++i) __asm__ volatile ("nop");
}

#ifndef BLINK_HZ
#define BLINK_HZ 2
#endif

int main(void) {
  led_gpio_init();
  // brief hello blink pattern
  for (int i = 0; i < 4; ++i) {
    led_toggle(); delay_cycles(300000);
  }

  // steady blink
  while (1) {
    led_toggle();
    // crude period ~ (2 * delay) ~= 0.5 s at ~10 MHz with this constant
    delay_cycles(500000);
  }
}
