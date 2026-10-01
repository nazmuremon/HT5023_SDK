# Buzzer API

**Header:** `#include "ht5023/buzzer.h"`
**Source:** `lib/ht5023_buzzer/src/ht5023_buzzer.c`

---

## Overview

Blocking GPIO square-wave driver for a passive buzzer. The driver toggles a
GPIO pin at the requested frequency using a busy-wait loop, producing audible
tones without requiring any timer peripheral.

> **Timing accuracy.** The delay loop is calibrated empirically with a /8
> compensation factor on the loop-overhead count. Actual pitch will vary
> slightly with optimisation level and CPU clock accuracy. For precise
> frequency generation use a hardware timer instead.
>
> **Do not use with an RTOS.** These functions spin the CPU for the full note
> duration and will starve other tasks.

---

## Types

### `ht5023_buzzer_t`

```c
typedef struct {
    uint32_t gpio_base;   // GPIO port base address (e.g. 0x40011000 for GPIOA)
    uint8_t  pin;         // pin number within the port (0–15)
    bool     active_high; // true: high = buzzer on; false: low = buzzer on
} ht5023_buzzer_t;
```

### `ht5023_buzzer_note_t`

```c
typedef struct {
    uint16_t freq_hz;      // tone frequency; 0 = rest (silence)
    uint16_t duration_ms;  // note duration in milliseconds
    uint16_t gap_ms;       // inter-note silence after the note (0 = none)
} ht5023_buzzer_note_t;
```

---

## Functions

### `ht5023_buzzer_init_pa1`

```c
void ht5023_buzzer_init_pa1(ht5023_buzzer_t* buzzer);
```

Convenience initialiser: fills `buzzer` for the default buzzer pin **PA1**
(`gpio_base = 0x40011000`, `pin = 1`, `active_high = true`). Does nothing if
`buzzer` is NULL.

---

### `ht5023_buzzer_gpio_init`

```c
void ht5023_buzzer_gpio_init(const ht5023_buzzer_t* buzzer);
```

Configure the GPIO pin as a push-pull output and drive it to the inactive
state. Must be called before `play_tone` or `set`. Does nothing if `buzzer`
is NULL.

---

### `ht5023_buzzer_set`

```c
void ht5023_buzzer_set(const ht5023_buzzer_t* buzzer, bool on);
```

Drive the buzzer pin immediately. Respects the `active_high` polarity field.

---

### `ht5023_buzzer_play_tone`

```c
void ht5023_buzzer_play_tone(const ht5023_buzzer_t* buzzer,
                             uint32_t cpu_hz,
                             uint16_t freq_hz,
                             uint16_t duration_ms);
```

Toggle the pin at `freq_hz` for `duration_ms` milliseconds (blocking). Leaves
the pin inactive on return. If `freq_hz` is 0 it calls `ht5023_buzzer_rest`.

---

### `ht5023_buzzer_rest`

```c
void ht5023_buzzer_rest(const ht5023_buzzer_t* buzzer,
                        uint32_t cpu_hz,
                        uint16_t duration_ms);
```

Silence the buzzer and wait for `duration_ms` milliseconds (blocking).

---

### `ht5023_buzzer_play_sequence`

```c
void ht5023_buzzer_play_sequence(const ht5023_buzzer_t*       buzzer,
                                 uint32_t                      cpu_hz,
                                 const ht5023_buzzer_note_t*  notes,
                                 size_t                        count);
```

Play `count` notes from the `notes` array in order. For each entry, plays the
tone for `duration_ms` then waits `gap_ms` of silence. A `freq_hz` of 0
produces a rest for `duration_ms`.

---

## Example

```c
#include "ht5023/buzzer.h"

// Not the real Fcpu: the driver's busy loop is only an empirical estimate,
// so this is a tuning value -- examples/buzzer_music uses 2000000u and says
// to adjust it if the pitch sounds off. 9500000 plays 4.75x lower and longer.
#define CPU_HZ  2000000u

// C4 major arpeggio: C4, E4, G4, C5
static const ht5023_buzzer_note_t melody[] = {
    { .freq_hz = 261, .duration_ms = 150, .gap_ms = 30 },
    { .freq_hz = 330, .duration_ms = 150, .gap_ms = 30 },
    { .freq_hz = 392, .duration_ms = 150, .gap_ms = 30 },
    { .freq_hz = 523, .duration_ms = 300, .gap_ms = 60 },
};

ht5023_buzzer_t buz;
ht5023_buzzer_init_pa1(&buz);
ht5023_buzzer_gpio_init(&buz);

ht5023_buzzer_play_sequence(&buz, CPU_HZ,
                             melody,
                             sizeof(melody) / sizeof(melody[0]));
```

See also: `examples/buzzer_music/`
