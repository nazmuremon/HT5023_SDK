// ht5023_buzzer.h — passive buzzer helper (GPIO square-wave, blocking)
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t gpio_base;
  uint8_t pin;
  bool active_high;
} ht5023_buzzer_t;

typedef struct {
  uint16_t freq_hz;      // 0 => rest
  uint16_t duration_ms;
  uint16_t gap_ms;       // inter-note silence after the note
} ht5023_buzzer_note_t;

// Convenience helper for PA1 (user-requested buzzer pin).
void ht5023_buzzer_init_pa1(ht5023_buzzer_t* buzzer);

void ht5023_buzzer_gpio_init(const ht5023_buzzer_t* buzzer);
void ht5023_buzzer_set(const ht5023_buzzer_t* buzzer, bool on);

// Blocking tone playback (best-effort timing; depends on CPU clock and loop overhead).
// These functions spin the CPU for the full duration and should not be used with RTOS.
void ht5023_buzzer_play_tone(const ht5023_buzzer_t* buzzer,
                             uint32_t cpu_hz,
                             uint16_t freq_hz,
                             uint16_t duration_ms);
void ht5023_buzzer_rest(const ht5023_buzzer_t* buzzer,
                        uint32_t cpu_hz,
                        uint16_t duration_ms);

void ht5023_buzzer_play_sequence(const ht5023_buzzer_t* buzzer,
                                 uint32_t cpu_hz,
                                 const ht5023_buzzer_note_t* notes,
                                 size_t count);

#ifdef __cplusplus
}
#endif
