// ht5023_buzzer.c — passive buzzer helper (GPIO square-wave, blocking)
#include "ht5023_buzzer.h"

#include <stddef.h>
#include <stdint.h>

#define HT5023_GPIOA_BASE             0x40011000u
#define GPIO_IOCFG_OFFSET             0x00u
#define GPIO_AFCFG_OFFSET             0x04u
#define GPIO_PTDIR_OFFSET             0x08u
#define GPIO_PTSET_OFFSET             0x14u
#define GPIO_PTCLR_OFFSET             0x18u
#define GPIO_PTOD_OFFSET              0x20u

static inline volatile uint16_t* reg16(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(base + off);
}

static void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; ++i) {
    __asm__ volatile ("nop");
  }
}

static uint16_t buzzer_mask(const ht5023_buzzer_t* buzzer) {
  return (uint16_t)(1u << (buzzer->pin & 0x0Fu));
}

// Very rough conversion for busy-wait delays. The constant intentionally
// underestimates CPU throughput to keep audible tones closer on -Os builds.
static uint32_t ms_to_cycles(uint32_t cpu_hz, uint32_t ms) {
  if (cpu_hz == 0u || ms == 0u) return 0u;
  uint64_t v = ((uint64_t)cpu_hz * (uint64_t)ms) / 1000u;
  v /= 8u;  // loop overhead compensation factor (empirical, best-effort)
  if (v > 0xFFFFFFFFu) v = 0xFFFFFFFFu;
  return (uint32_t)v;
}

static uint32_t half_period_cycles(uint32_t cpu_hz, uint16_t freq_hz) {
  if (cpu_hz == 0u || freq_hz == 0u) return 0u;
  uint64_t v = (uint64_t)cpu_hz / (uint64_t)freq_hz / 2u;
  v /= 8u;  // same compensation as ms path
  if (v < 1u) v = 1u;
  if (v > 0xFFFFFFFFu) v = 0xFFFFFFFFu;
  return (uint32_t)v;
}

void ht5023_buzzer_init_pa1(ht5023_buzzer_t* buzzer) {
  if (!buzzer) return;
  buzzer->gpio_base = HT5023_GPIOA_BASE;
  buzzer->pin = 1u;
  buzzer->active_high = true;
}

void ht5023_buzzer_gpio_init(const ht5023_buzzer_t* buzzer) {
  if (!buzzer) return;
  const uint16_t mask = buzzer_mask(buzzer);

  volatile uint16_t* iocfg = reg16(buzzer->gpio_base, GPIO_IOCFG_OFFSET);
  volatile uint16_t* afcfg = reg16(buzzer->gpio_base, GPIO_AFCFG_OFFSET);
  volatile uint16_t* ptdir = reg16(buzzer->gpio_base, GPIO_PTDIR_OFFSET);
  volatile uint16_t* ptod  = reg16(buzzer->gpio_base, GPIO_PTOD_OFFSET);

  // GPIO mode on PA1.
  *iocfg &= (uint16_t)~mask;
  *afcfg &= (uint16_t)~mask;

  // Push-pull output and drive disabled state.
  *ptod |= mask;
  *ptdir |= mask;
  ht5023_buzzer_set(buzzer, false);
}

void ht5023_buzzer_set(const ht5023_buzzer_t* buzzer, bool on) {
  if (!buzzer) return;
  const uint16_t mask = buzzer_mask(buzzer);
  const bool level_high = buzzer->active_high ? on : !on;
  if (level_high) {
    *reg16(buzzer->gpio_base, GPIO_PTSET_OFFSET) = mask;
  } else {
    *reg16(buzzer->gpio_base, GPIO_PTCLR_OFFSET) = mask;
  }
}

void ht5023_buzzer_rest(const ht5023_buzzer_t* buzzer, uint32_t cpu_hz, uint16_t duration_ms) {
  (void)buzzer;
  if (buzzer) ht5023_buzzer_set(buzzer, false);
  delay_cycles(ms_to_cycles(cpu_hz, duration_ms));
}

void ht5023_buzzer_play_tone(const ht5023_buzzer_t* buzzer,
                             uint32_t cpu_hz,
                             uint16_t freq_hz,
                             uint16_t duration_ms) {
  if (!buzzer) return;
  if (freq_hz == 0u || duration_ms == 0u) {
    ht5023_buzzer_rest(buzzer, cpu_hz, duration_ms);
    return;
  }

  const uint32_t half_cycles = half_period_cycles(cpu_hz, freq_hz);
  // toggles ~= 2 * freq * t
  uint32_t toggles = ((uint32_t)freq_hz * (uint32_t)duration_ms) / 500u;
  if (toggles == 0u) toggles = 1u;

  for (uint32_t i = 0; i < toggles; ++i) {
    ht5023_buzzer_set(buzzer, (i & 1u) == 0u);
    delay_cycles(half_cycles);
  }
  ht5023_buzzer_set(buzzer, false);
}

void ht5023_buzzer_play_sequence(const ht5023_buzzer_t* buzzer,
                                 uint32_t cpu_hz,
                                 const ht5023_buzzer_note_t* notes,
                                 size_t count) {
  if (!buzzer || !notes) return;
  for (size_t i = 0; i < count; ++i) {
    ht5023_buzzer_play_tone(buzzer, cpu_hz, notes[i].freq_hz, notes[i].duration_ms);
    if (notes[i].gap_ms) {
      ht5023_buzzer_rest(buzzer, cpu_hz, notes[i].gap_ms);
    }
  }
}

