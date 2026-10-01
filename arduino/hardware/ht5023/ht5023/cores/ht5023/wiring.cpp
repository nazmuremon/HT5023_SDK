#include <stdlib.h>

#include "Arduino.h"

#include "ht5023_gpio.h"
#include "ht5023_systick.h"
#include "ht5023_spi.h"
#include "ht5023_system.h"

#define SYST_CSR (*(volatile uint32_t*)0xE000E010u)
#define SYST_CVR (*(volatile uint32_t*)0xE000E018u)
#define SYST_CSR_COUNTFLAG (1u << 16)

static volatile uint32_t g_millis_ticks = 0u;

static bool ht5023_valid_pin(uint8_t pin) {
  return pin < NUM_DIGITAL_PINS && g_pinDescription[pin].port_base != 0u;
}

extern "C" void SysTick_Handler(void) {
  ++g_millis_ticks;
}

void init(void) {
  SystemCoreClock = (uint32_t)HT5023_SYSTICK_HZ;   // CMSIS: the CPU clock
  (void)ht5023_systick_start((uint32_t)HT5023_SYSTICK_HZ, 1000u);
}

void pinMode(uint8_t pin, uint8_t mode) {
  if (!ht5023_valid_pin(pin)) return;

  const PinDescription& desc = g_pinDescription[pin];
  ht5023_gpio_pin_mode(desc.port_base, desc.pin, (ht5023_gpio_mode_t)mode);
}

void digitalWrite(uint8_t pin, uint8_t value) {
  if (!ht5023_valid_pin(pin)) return;

  if (pin == SS && ht5023_spi_is_active()) {
    ht5023_spi_set_cs(value != LOW);
    return;
  }

  const PinDescription& desc = g_pinDescription[pin];
  ht5023_gpio_write(desc.port_base, desc.pin, value != LOW);
}

int digitalRead(uint8_t pin) {
  if (!ht5023_valid_pin(pin)) return LOW;

  const PinDescription& desc = g_pinDescription[pin];
  return ht5023_gpio_read(desc.port_base, desc.pin) ? HIGH : LOW;
}

void digitalToggle(uint8_t pin) {
  if (!ht5023_valid_pin(pin)) return;

  const PinDescription& desc = g_pinDescription[pin];
  ht5023_gpio_toggle(desc.port_base, desc.pin);
}

unsigned long millis(void) {
  return g_millis_ticks;
}

unsigned long micros(void) {
  const uint32_t reload =
      ht5023_systick_reload_value((uint32_t)HT5023_SYSTICK_HZ, 1000u) + 1u;
  uint32_t ticks_before = 0u;
  uint32_t ticks_after = 0u;
  uint32_t current = 0u;
  uint32_t status = 0u;

  do {
    ticks_before = g_millis_ticks;
    current = SYST_CVR;
    status = SYST_CSR;
    ticks_after = g_millis_ticks;
  } while (ticks_before != ticks_after);

  uint32_t elapsed = reload - current;
  if ((status & SYST_CSR_COUNTFLAG) != 0u && elapsed < reload) {
    ++ticks_before;
  }

  const uint64_t micros_total =
      ((uint64_t)ticks_before * 1000u) +
      (((uint64_t)elapsed * 1000000u) / (uint64_t)HT5023_SYSTICK_HZ);
  return (unsigned long)micros_total;
}

void delay(unsigned long ms) {
  const unsigned long start = millis();
  while ((millis() - start) < ms) {
    yield();
  }
}

void delayMicroseconds(unsigned int us) {
  const unsigned long start = micros();
  while ((micros() - start) < us) {
  }
}

long random(long max_value) {
  if (max_value <= 0) return 0;
  return random(0, max_value);
}

long random(long min_value, long max_value) {
  if (max_value <= min_value) return min_value;
  return (rand() % (max_value - min_value)) + min_value;
}

void randomSeed(unsigned long seed) {
  srand((unsigned int)seed);
}

long map(long value, long from_low, long from_high, long to_low, long to_high) {
  if (from_high == from_low) return to_low;
  return (value - from_low) * (to_high - to_low) / (from_high - from_low) + to_low;
}

void shiftOut(uint8_t data_pin, uint8_t clock_pin, uint8_t bit_order, uint8_t value) {
  for (uint8_t i = 0; i < 8u; ++i) {
    const uint8_t bit_value = (bit_order == LSBFIRST) ? ((value >> i) & 0x1u)
                                                      : ((value >> (7u - i)) & 0x1u);
    digitalWrite(data_pin, bit_value ? HIGH : LOW);
    digitalWrite(clock_pin, HIGH);
    digitalWrite(clock_pin, LOW);
  }
}

uint8_t shiftIn(uint8_t data_pin, uint8_t clock_pin, uint8_t bit_order) {
  uint8_t value = 0u;
  for (uint8_t i = 0; i < 8u; ++i) {
    digitalWrite(clock_pin, HIGH);
    const uint8_t bit_value = (uint8_t)(digitalRead(data_pin) ? 1u : 0u);
    if (bit_order == LSBFIRST) {
      value |= (uint8_t)(bit_value << i);
    } else {
      value |= (uint8_t)(bit_value << (7u - i));
    }
    digitalWrite(clock_pin, LOW);
  }
  return value;
}

int analogRead(uint8_t pin) {
  (void)pin;
  return 0;
}

void analogWrite(uint8_t pin, int value) {
  digitalWrite(pin, value > 127 ? HIGH : LOW);
}

void analogReference(uint8_t mode) {
  (void)mode;
}
