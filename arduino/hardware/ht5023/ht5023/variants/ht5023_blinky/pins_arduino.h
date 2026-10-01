#pragma once

#include <stdint.h>

#include "ht5023_device.h"

typedef struct {
  uint32_t port_base;
  uint8_t pin;
} PinDescription;

static const uint8_t PIN_SERIAL_RX = 0u;
static const uint8_t PIN_SERIAL_TX = 1u;

static const uint8_t PIN_SPI_MOSI = 7u;
static const uint8_t PIN_SPI_MISO = 8u;
static const uint8_t PIN_SPI_SCK = 9u;
static const uint8_t PIN_SPI_SS = 10u;

static const uint8_t PIN_WIRE_SCL = 11u;
static const uint8_t PIN_WIRE_SDA = 12u;

static const uint8_t PIN_LED1 = 13u;
static const uint8_t PIN_LED2 = 14u;

static const uint8_t LED_BUILTIN = PIN_LED1;

// Every pin on every port, named PIN_P<port><bit>. Pins 0-14 above keep their
// numbers; these are the same pads under their datasheet names, which is what
// you want when wiring something like an LCD panel pin by pin.
//
// Two of these will bite if you drive them: PB13 and PB15 are SWDIO and SWCLK,
// so writing to them ends the debug connection, and PC11/PC12 are the serial
// link the Flasher's log and USB uploads run over.
static const uint8_t PIN_PA0 = 2u;
static const uint8_t PIN_PA1 = 3u;
static const uint8_t PIN_PA2 = 4u;
static const uint8_t PIN_PA3 = 5u;
static const uint8_t PIN_PA4 = 6u;
static const uint8_t PIN_PA5 = 15u;
static const uint8_t PIN_PA6 = 16u;
static const uint8_t PIN_PA7 = 17u;
static const uint8_t PIN_PA8 = 18u;
static const uint8_t PIN_PA9 = 19u;
static const uint8_t PIN_PA10 = 20u;
static const uint8_t PIN_PA11 = 21u;
static const uint8_t PIN_PA12 = 22u;
static const uint8_t PIN_PA13 = 23u;
static const uint8_t PIN_PB0 = 24u;
static const uint8_t PIN_PB1 = 25u;
static const uint8_t PIN_PB2 = 26u;
static const uint8_t PIN_PB3 = 27u;
static const uint8_t PIN_PB4 = 28u;
static const uint8_t PIN_PB5 = 29u;
static const uint8_t PIN_PB6 = 30u;
static const uint8_t PIN_PB7 = 31u;
static const uint8_t PIN_PB8 = 32u;
static const uint8_t PIN_PB9 = 33u;
static const uint8_t PIN_PB10 = 34u;
static const uint8_t PIN_PB11 = 35u;
static const uint8_t PIN_PB12 = 36u;
static const uint8_t PIN_PB13 = 37u;
static const uint8_t PIN_PB14 = 38u;
static const uint8_t PIN_PB15 = 39u;
static const uint8_t PIN_PC0 = 40u;
static const uint8_t PIN_PC1 = 41u;
static const uint8_t PIN_PC2 = 42u;
static const uint8_t PIN_PC3 = 43u;
static const uint8_t PIN_PC4 = 7u;
static const uint8_t PIN_PC5 = 8u;
static const uint8_t PIN_PC6 = 9u;
static const uint8_t PIN_PC7 = 10u;
static const uint8_t PIN_PC8 = 44u;
static const uint8_t PIN_PC9 = 45u;
static const uint8_t PIN_PC10 = 46u;
static const uint8_t PIN_PC11 = 1u;
static const uint8_t PIN_PC12 = 0u;
static const uint8_t PIN_PC13 = 11u;
static const uint8_t PIN_PC14 = 12u;
static const uint8_t PIN_PC15 = 47u;
static const uint8_t PIN_PD0 = 48u;
static const uint8_t PIN_PD1 = 49u;
static const uint8_t PIN_PD2 = 50u;
static const uint8_t PIN_PD3 = 51u;
static const uint8_t PIN_PD4 = 52u;
static const uint8_t PIN_PD5 = 53u;
static const uint8_t PIN_PD6 = 54u;
static const uint8_t PIN_PD7 = 55u;
static const uint8_t PIN_PD8 = 56u;
static const uint8_t PIN_PD9 = 57u;
static const uint8_t PIN_PD10 = 58u;
static const uint8_t PIN_PD11 = 59u;
static const uint8_t PIN_PD12 = 60u;
static const uint8_t PIN_PD13 = 61u;
static const uint8_t PIN_PD14 = 62u;
static const uint8_t PIN_PD15 = 63u;
static const uint8_t PIN_PE0 = 64u;
static const uint8_t PIN_PE1 = 65u;
static const uint8_t PIN_PE2 = 66u;
static const uint8_t PIN_PE3 = 13u;
static const uint8_t PIN_PE4 = 14u;
static const uint8_t PIN_PE5 = 67u;
static const uint8_t PIN_PE6 = 68u;
static const uint8_t PIN_PE7 = 69u;
static const uint8_t PIN_PE8 = 70u;
static const uint8_t PIN_PE9 = 71u;
static const uint8_t PIN_PE10 = 72u;
static const uint8_t PIN_PE11 = 73u;
static const uint8_t PIN_PE12 = 74u;
static const uint8_t PIN_PE13 = 75u;
static const uint8_t PIN_PE14 = 76u;
static const uint8_t PIN_PE15 = 77u;
static const uint8_t PIN_PF0 = 78u;
static const uint8_t PIN_PF1 = 79u;
static const uint8_t PIN_PF2 = 80u;
static const uint8_t PIN_PF3 = 81u;
static const uint8_t PIN_PF4 = 82u;
static const uint8_t PIN_PF5 = 83u;
static const uint8_t PIN_PF6 = 84u;
static const uint8_t PIN_PF7 = 85u;
static const uint8_t PIN_PF8 = 86u;
static const uint8_t PIN_PF9 = 87u;
static const uint8_t PIN_PF10 = 88u;
static const uint8_t PIN_PF11 = 89u;
static const uint8_t PIN_PF12 = 90u;
static const uint8_t PIN_PF13 = 91u;
static const uint8_t PIN_PF14 = 92u;
static const uint8_t PIN_PF15 = 93u;

static const uint8_t A0 = PIN_PA0;
static const uint8_t A1 = PIN_PA1;
static const uint8_t A2 = PIN_PA2;
static const uint8_t A3 = PIN_PA3;
static const uint8_t A4 = PIN_PA4;

static const uint8_t SDA = PIN_WIRE_SDA;
static const uint8_t SCL = PIN_WIRE_SCL;
static const uint8_t SS = PIN_SPI_SS;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

#define NUM_DIGITAL_PINS 94u
#define NUM_ANALOG_INPUTS 5u

#define analogInputToDigitalPin(p) ((p) < NUM_ANALOG_INPUTS ? ((p) + A0) : -1)
#define digitalPinHasPWM(p) (0)
#define digitalPinToInterrupt(p) (NOT_AN_INTERRUPT)

extern const PinDescription g_pinDescription[NUM_DIGITAL_PINS];

// Direct port access, the AVR way. Libraries that bit-bang a protocol (OneWire,
// DHT, NeoPixel and friends) reach for these instead of digitalWrite because on
// an AVR the call overhead would break their timing. Here they resolve to the
// same GPIO registers digitalWrite uses -- the port is 16 bits wide, and the
// data register reads back the pad, so the input and output register are one
// and the same.
#define IO_REG_TYPE uint16_t
#define digitalPinToPort(P) (g_pinDescription[P].port_base)
#define digitalPinToBitMask(P) ((uint16_t)(1u << g_pinDescription[P].pin))
#define portOutputRegister(port)   ((volatile uint16_t*)((port) + HT5023_GPIO_PTDAT_OFFSET))
#define portInputRegister(port)   ((volatile uint16_t*)((port) + HT5023_GPIO_PTDAT_OFFSET))
#define portModeRegister(port)   ((volatile uint16_t*)((port) + HT5023_GPIO_PTDIR_OFFSET))

