#pragma once

// Sketches and libraries expect the C library to be on the table already:
// malloc/free, the string functions and all of math.h come with Arduino.h on
// every other core, and libraries include none of them themselves.
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "binary.h"
#include "pins_arduino.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bool boolean;
typedef uint8_t byte;
typedef unsigned int word;

#define HIGH 0x1
#define LOW  0x0

#define INPUT         0x0
#define OUTPUT        0x1
#define INPUT_PULLUP  0x2

#define CHANGE  0x1
#define FALLING 0x2
#define RISING  0x3

typedef enum { LSBFIRST = 0, MSBFIRST = 1 } BitOrder;

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

#define PI        3.14159265358979323846
#define HALF_PI   1.57079632679489661923
#define TWO_PI    6.28318530717958647692
#define DEG_TO_RAD 0.01745329251994329577
#define RAD_TO_DEG 57.2957795130823208768

// min/max/abs are macros in C, but a macro called min() wrecks any C++ standard
// header that declares std::min -- ArduinoJson pulls in <string_view> and the
// build collapses. C++ gets templates instead, further down; this is the C path.
#ifndef __cplusplus
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define abs(x) ((x) > 0 ? (x) : -(x))
#endif
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x)     ((x) * (x))
#ifdef round
#undef round
#endif
#define round(x) ((x) >= 0 ? (long)((x) + 0.5) : (long)((x) - 0.5))

#define bit(b)          (1UL << (b))
#define bitRead(value, bitnum) (((value) >> (bitnum)) & 0x1)
#define bitSet(value, bitnum) ((value) |= (1UL << (bitnum)))
#define bitClear(value, bitnum) ((value) &= ~(1UL << (bitnum)))
#define bitWrite(value, bitnum, bitvalue) ((bitvalue) ? bitSet((value), (bitnum)) : bitClear((value), (bitnum)))
#define lowByte(w)      ((uint8_t)((w) & 0xFF))
#define highByte(w)     ((uint8_t)(((w) >> 8) & 0xFF))

#define interrupts() __asm__ volatile("cpsie i" : : : "memory")
#define noInterrupts() __asm__ volatile("cpsid i" : : : "memory")

// Two clocks (HT502X User Manual 3.2). F_CPU here is Fsys, the clock the UART,
// SPI and I2C dividers count -- 9.196 MHz as measured on the board, not the
// datasheet's 9.5 MHz. The CPU and SysTick run from Fcpu = Fsys >> SYSCLK_DIV,
// and SYSCLK_DIV resets to /2. millis(), micros() and delay() count Fcpu, so
// getting it wrong makes every delay in a sketch twice as long.
// Override F_CPU (build.f_cpu) after measuring a differently trimmed part.
#ifndef HT5023_SYSTICK_HZ
#define HT5023_SYSTICK_HZ ((unsigned long)(F_CPU) / 2UL)
#endif

// Arduino means CPU clock cycles here, which on this part is Fcpu, not F_CPU.
#define clockCyclesPerMicrosecond() ((unsigned long)(HT5023_SYSTICK_HZ / 1000000L))
#define clockCyclesToMicroseconds(a) ((a) / clockCyclesPerMicrosecond())
#define microsecondsToClockCycles(a) ((a) * clockCyclesPerMicrosecond())

#define NOT_AN_INTERRUPT 0xFFu

void init(void);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);
void digitalToggle(uint8_t pin);
unsigned long millis(void);
unsigned long micros(void);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void randomSeed(unsigned long seed);
long map(long value, long from_low, long from_high, long to_low, long to_high);
void shiftOut(uint8_t data_pin, uint8_t clock_pin, uint8_t bit_order, uint8_t value);
uint8_t shiftIn(uint8_t data_pin, uint8_t clock_pin, uint8_t bit_order);
int analogRead(uint8_t pin);
void analogWrite(uint8_t pin, int value);
void analogReference(uint8_t mode);
void attachInterrupt(uint8_t interrupt_num, void (*user_func)(void), int mode);
void detachInterrupt(uint8_t interrupt_num);
void yield(void);

#ifdef __cplusplus
}

// The Arduino API's min/max/abs for C++: templates, so mixed argument types
// still work and no standard header is poisoned by a macro of the same name.
template <class T, class L>
auto min(const T& a, const L& b) -> decltype(b < a ? b : a) {
  return (b < a) ? b : a;
}
template <class T, class L>
auto max(const T& a, const L& b) -> decltype(b < a ? a : b) {
  return (b < a) ? a : b;
}
template <class T>
auto abs(const T& x) -> decltype(x < 0 ? -x : x) {
  return (x < 0) ? -x : x;
}

class __FlashStringHelper;
#define F(string_literal) (string_literal)

// Default arguments are C++ only, so these live outside the extern "C" block
// above -- Arduino.h is also included from the core's .c files.
extern "C" {
unsigned long pulseIn(uint8_t pin, uint8_t state,
                      unsigned long timeout = 1000000UL);
unsigned long pulseInLong(uint8_t pin, uint8_t state,
                          unsigned long timeout = 1000000UL);
void tone(uint8_t pin, unsigned int frequency, unsigned long duration = 0UL);
void noTone(uint8_t pin);
char* dtostrf(double value, signed char width, unsigned char prec, char* s);
char* ltoa(long value, char* s, int base);
char* ultoa(unsigned long value, char* s, int base);
}

long random(long max);
long random(long min, long max);

#include "avr/pgmspace.h"
#include "WCharacter.h"
#include "WString.h"
#include "Printable.h"
#include "Print.h"
#include "Stream.h"
#include "HardwareSerial.h"

extern HardwareSerial Serial0;
extern HardwareSerial Serial2;
extern HardwareSerial& Serial;

#define SERIAL_PORT_MONITOR Serial
#define SERIAL_PORT_HARDWARE Serial2

#endif
