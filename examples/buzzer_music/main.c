// examples/buzzer_music/main.c — PA1 passive buzzer melody + dual-LED dance
#include <stddef.h>
#include <stdint.h>

#include "ht5023/board/blinky_gpio.h"
#include "ht5023/buzzer.h"

extern void led_gpio_init(void);

#ifndef BUZZER_EXAMPLE_CPU_HZ
// Default CPU clock estimate after reset divider (Fcpu ~= Fsys/2).
// Tune if pitch sounds off on your board.
#define BUZZER_EXAMPLE_CPU_HZ 2000000u
#endif

#define LED0_HW_MASK ((uint16_t)(1u << 3))  // PE3
#define LED1_HW_MASK ((uint16_t)(1u << 4))  // PE4
#define LEDS_HW_MASK ((uint16_t)(LED0_HW_MASK | LED1_HW_MASK))

typedef struct {
  uint16_t freq_hz;
  uint16_t duration_ms;
  uint16_t gap_ms;
} music_step_t;

static inline volatile uint16_t* reg_set(void) {
  return reg16(GPIOE_BASE_ADDR, GPIO_PTSET_OFFSET);
}

static inline volatile uint16_t* reg_clr(void) {
  return reg16(GPIOE_BASE_ADDR, GPIO_PTCLR_OFFSET);
}

static void leds_apply(uint8_t pattern) {
  uint16_t on_mask = 0u;
  if (pattern & 0x01u) on_mask |= LED0_HW_MASK;
  if (pattern & 0x02u) on_mask |= LED1_HW_MASK;
  const uint16_t off_mask = (uint16_t)(LEDS_HW_MASK & (uint16_t)~on_mask);

#if LED_ACTIVE_LOW
  *reg_set() = off_mask;
  *reg_clr() = on_mask;
#else
  *reg_clr() = off_mask;
  *reg_set() = on_mask;
#endif
}

static uint8_t dance_pattern(uint32_t phase) {
  static const uint8_t seq[] = {
    0x01u, 0x02u, 0x03u, 0x02u, 0x01u, 0x00u, 0x03u, 0x00u
  };
  return seq[phase % (sizeof(seq) / sizeof(seq[0]))];
}

static void play_note_dance(const ht5023_buzzer_t* buzzer,
                            uint32_t cpu_hz,
                            uint32_t* dance_phase,
                            const music_step_t* step) {
  if (!step || !buzzer) return;

  if (step->freq_hz == 0u) {
    leds_apply(0u);
    ht5023_buzzer_rest(buzzer, cpu_hz, step->duration_ms);
  } else {
    uint16_t remaining = step->duration_ms;
    while (remaining > 0u) {
      uint16_t slice = (remaining > 70u) ? 70u : remaining;
      leds_apply(dance_pattern((*dance_phase)++));
      ht5023_buzzer_play_tone(buzzer, cpu_hz, step->freq_hz, slice);
      remaining = (uint16_t)(remaining - slice);
    }
  }

  if (step->gap_ms) {
    leds_apply(((*dance_phase)++ & 1u) ? 0x01u : 0x02u);
    ht5023_buzzer_rest(buzzer, cpu_hz, step->gap_ms);
  }
}

#define N_G3 196u
#define N_C4 262u
#define N_D4 294u
#define N_E4 330u
#define N_F4 349u
#define N_G4 392u
#define N_A4 440u
#define T_Q 280u
#define T_E 140u
#define T_H 560u
#define T_GAP 24u
#define T_PHRASE 120u

#define STEP(note, dur) {(note), (dur), T_GAP}
#define STEP_G(note, dur, gap) {(note), (dur), (gap)}

// "Ode to Joy" (Beethoven) — public-domain melody, simplified monophonic line.
static const music_step_t k_song[] = {
  STEP(N_E4, T_Q), STEP(N_E4, T_Q), STEP(N_F4, T_Q), STEP(N_G4, T_Q),
  STEP(N_G4, T_Q), STEP(N_F4, T_Q), STEP(N_E4, T_Q), STEP(N_D4, T_Q),
  STEP(N_C4, T_Q), STEP(N_C4, T_Q), STEP(N_D4, T_Q), STEP(N_E4, T_Q),
  STEP_G(N_E4, T_E, T_GAP), STEP_G(N_D4, T_E, T_GAP), STEP_G(N_D4, T_H, T_PHRASE),

  STEP(N_E4, T_Q), STEP(N_E4, T_Q), STEP(N_F4, T_Q), STEP(N_G4, T_Q),
  STEP(N_G4, T_Q), STEP(N_F4, T_Q), STEP(N_E4, T_Q), STEP(N_D4, T_Q),
  STEP(N_C4, T_Q), STEP(N_C4, T_Q), STEP(N_D4, T_Q), STEP(N_E4, T_Q),
  STEP_G(N_D4, T_E, T_GAP), STEP_G(N_C4, T_E, T_GAP), STEP_G(N_C4, T_H, T_PHRASE),

  STEP(N_D4, T_Q), STEP(N_D4, T_Q), STEP(N_E4, T_Q), STEP(N_C4, T_Q),
  STEP(N_D4, T_Q), STEP_G(N_E4, T_E, T_GAP), STEP_G(N_F4, T_E, T_GAP), STEP_G(N_E4, T_Q, T_GAP), STEP(N_C4, T_Q),
  STEP(N_D4, T_Q), STEP_G(N_E4, T_E, T_GAP), STEP_G(N_F4, T_E, T_GAP), STEP_G(N_E4, T_Q, T_GAP), STEP(N_D4, T_Q),
  STEP(N_C4, T_Q), STEP(N_D4, T_Q), STEP_G(N_G3, T_H, T_PHRASE),

  STEP(N_E4, T_Q), STEP(N_E4, T_Q), STEP(N_F4, T_Q), STEP(N_G4, T_Q),
  STEP(N_G4, T_Q), STEP(N_F4, T_Q), STEP(N_E4, T_Q), STEP(N_D4, T_Q),
  STEP(N_C4, T_Q), STEP(N_C4, T_Q), STEP(N_D4, T_Q), STEP(N_E4, T_Q),
  STEP_G(N_D4, T_E, T_GAP), STEP_G(N_C4, T_E, T_GAP), STEP_G(N_C4, T_H, 500u),
};

int main(void) {
  ht5023_buzzer_t buzzer;
  uint32_t dance_phase = 0u;

  led_gpio_init();
  leds_apply(0u);

  ht5023_buzzer_init_pa1(&buzzer);
  ht5023_buzzer_gpio_init(&buzzer);

  while (1) {
    for (size_t i = 0; i < (sizeof(k_song) / sizeof(k_song[0])); ++i) {
      play_note_dance(&buzzer, BUZZER_EXAMPLE_CPU_HZ, &dance_phase, &k_song[i]);
    }
    leds_apply(0u);
    ht5023_buzzer_rest(&buzzer, BUZZER_EXAMPLE_CPU_HZ, 600u);
  }
}
