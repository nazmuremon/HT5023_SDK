#include "Arduino.h"
#include "pins_arduino.h"

// Index = Arduino pin number; see pins_arduino.h for the names.
const PinDescription g_pinDescription[NUM_DIGITAL_PINS] = {
    {HT5023_GPIOC_BASE, 12u},  // PC12
    {HT5023_GPIOC_BASE, 11u},  // PC11
    {HT5023_GPIOA_BASE, 0u},  // PA0
    {HT5023_GPIOA_BASE, 1u},  // PA1
    {HT5023_GPIOA_BASE, 2u},  // PA2
    {HT5023_GPIOA_BASE, 3u},  // PA3
    {HT5023_GPIOA_BASE, 4u},  // PA4
    {HT5023_GPIOC_BASE, 4u},  // PC4
    {HT5023_GPIOC_BASE, 5u},  // PC5
    {HT5023_GPIOC_BASE, 6u},  // PC6
    {HT5023_GPIOC_BASE, 7u},  // PC7
    {HT5023_GPIOC_BASE, 13u},  // PC13
    {HT5023_GPIOC_BASE, 14u},  // PC14
    {HT5023_GPIOE_BASE, 3u},  // PE3
    {HT5023_GPIOE_BASE, 4u},  // PE4
    {HT5023_GPIOA_BASE, 5u},  // PA5
    {HT5023_GPIOA_BASE, 6u},  // PA6
    {HT5023_GPIOA_BASE, 7u},  // PA7
    {HT5023_GPIOA_BASE, 8u},  // PA8
    {HT5023_GPIOA_BASE, 9u},  // PA9
    {HT5023_GPIOA_BASE, 10u},  // PA10
    {HT5023_GPIOA_BASE, 11u},  // PA11
    {HT5023_GPIOA_BASE, 12u},  // PA12
    {HT5023_GPIOA_BASE, 13u},  // PA13
    {HT5023_GPIOB_BASE, 0u},  // PB0
    {HT5023_GPIOB_BASE, 1u},  // PB1
    {HT5023_GPIOB_BASE, 2u},  // PB2
    {HT5023_GPIOB_BASE, 3u},  // PB3
    {HT5023_GPIOB_BASE, 4u},  // PB4
    {HT5023_GPIOB_BASE, 5u},  // PB5
    {HT5023_GPIOB_BASE, 6u},  // PB6
    {HT5023_GPIOB_BASE, 7u},  // PB7
    {HT5023_GPIOB_BASE, 8u},  // PB8
    {HT5023_GPIOB_BASE, 9u},  // PB9
    {HT5023_GPIOB_BASE, 10u},  // PB10
    {HT5023_GPIOB_BASE, 11u},  // PB11
    {HT5023_GPIOB_BASE, 12u},  // PB12
    {HT5023_GPIOB_BASE, 13u},  // PB13
    {HT5023_GPIOB_BASE, 14u},  // PB14
    {HT5023_GPIOB_BASE, 15u},  // PB15
    {HT5023_GPIOC_BASE, 0u},  // PC0
    {HT5023_GPIOC_BASE, 1u},  // PC1
    {HT5023_GPIOC_BASE, 2u},  // PC2
    {HT5023_GPIOC_BASE, 3u},  // PC3
    {HT5023_GPIOC_BASE, 8u},  // PC8
    {HT5023_GPIOC_BASE, 9u},  // PC9
    {HT5023_GPIOC_BASE, 10u},  // PC10
    {HT5023_GPIOC_BASE, 15u},  // PC15
    {HT5023_GPIOD_BASE, 0u},  // PD0
    {HT5023_GPIOD_BASE, 1u},  // PD1
    {HT5023_GPIOD_BASE, 2u},  // PD2
    {HT5023_GPIOD_BASE, 3u},  // PD3
    {HT5023_GPIOD_BASE, 4u},  // PD4
    {HT5023_GPIOD_BASE, 5u},  // PD5
    {HT5023_GPIOD_BASE, 6u},  // PD6
    {HT5023_GPIOD_BASE, 7u},  // PD7
    {HT5023_GPIOD_BASE, 8u},  // PD8
    {HT5023_GPIOD_BASE, 9u},  // PD9
    {HT5023_GPIOD_BASE, 10u},  // PD10
    {HT5023_GPIOD_BASE, 11u},  // PD11
    {HT5023_GPIOD_BASE, 12u},  // PD12
    {HT5023_GPIOD_BASE, 13u},  // PD13
    {HT5023_GPIOD_BASE, 14u},  // PD14
    {HT5023_GPIOD_BASE, 15u},  // PD15
    {HT5023_GPIOE_BASE, 0u},  // PE0
    {HT5023_GPIOE_BASE, 1u},  // PE1
    {HT5023_GPIOE_BASE, 2u},  // PE2
    {HT5023_GPIOE_BASE, 5u},  // PE5
    {HT5023_GPIOE_BASE, 6u},  // PE6
    {HT5023_GPIOE_BASE, 7u},  // PE7
    {HT5023_GPIOE_BASE, 8u},  // PE8
    {HT5023_GPIOE_BASE, 9u},  // PE9
    {HT5023_GPIOE_BASE, 10u},  // PE10
    {HT5023_GPIOE_BASE, 11u},  // PE11
    {HT5023_GPIOE_BASE, 12u},  // PE12
    {HT5023_GPIOE_BASE, 13u},  // PE13
    {HT5023_GPIOE_BASE, 14u},  // PE14
    {HT5023_GPIOE_BASE, 15u},  // PE15
    {HT5023_GPIOF_BASE, 0u},  // PF0
    {HT5023_GPIOF_BASE, 1u},  // PF1
    {HT5023_GPIOF_BASE, 2u},  // PF2
    {HT5023_GPIOF_BASE, 3u},  // PF3
    {HT5023_GPIOF_BASE, 4u},  // PF4
    {HT5023_GPIOF_BASE, 5u},  // PF5
    {HT5023_GPIOF_BASE, 6u},  // PF6
    {HT5023_GPIOF_BASE, 7u},  // PF7
    {HT5023_GPIOF_BASE, 8u},  // PF8
    {HT5023_GPIOF_BASE, 9u},  // PF9
    {HT5023_GPIOF_BASE, 10u},  // PF10
    {HT5023_GPIOF_BASE, 11u},  // PF11
    {HT5023_GPIOF_BASE, 12u},  // PF12
    {HT5023_GPIOF_BASE, 13u},  // PF13
    {HT5023_GPIOF_BASE, 14u},  // PF14
    {HT5023_GPIOF_BASE, 15u},  // PF15
};

// PA7 drives the buzzer on the energy-meter PCB: R84 (1k) into an AO3400A
// gate held down by R85 (100k). Out of reset PA7 is an input with its ~88k
// pull-up on, which lifts the gate to about 1.75 V -- enough to switch the
// MOSFET on -- so any sketch that never touched PA7 buzzed nonstop.
//
// main() calls this after init() and before setup(): one short beep so a
// reset is audible, then PA7 is left a push-pull output driven low with its
// pull-up disabled. A sketch that uses PA7 just sets it up again. The make
// examples get the same from sdk/boards/ht5023_blinky/src/board_buzzer.c.
#ifndef HT5023_BOOT_BEEP_MS
#define HT5023_BOOT_BEEP_MS 80  // 0 = no beep, PA7 is still silenced
#endif

void initVariant(void) {
  pinMode(PIN_PA7, INPUT);     // INPUT, unlike INPUT_PULLUP, turns the pull-up off
  digitalWrite(PIN_PA7, LOW);  // low before it becomes an output
  pinMode(PIN_PA7, OUTPUT);
#if HT5023_BOOT_BEEP_MS > 0
  digitalWrite(PIN_PA7, HIGH);
  delay(HT5023_BOOT_BEEP_MS);
  digitalWrite(PIN_PA7, LOW);
#endif
}
