# Timer API

**Header:** `#include "ht5023/timer.h"`
**Source:** `lib/ht5023_timer/src/ht5023_timer.c`

---

## Overview

Polling driver for the four general-purpose timers TMR0–TMR3. Each timer
runs in periodic mode: it counts up from 0 to `period`, then sets the PRDIF
flag and resets.

`ht5023_timer_period_elapsed()` polls and auto-clears PRDIF — call it in
a loop to detect each period boundary without using interrupts.

---

## Types

### `ht5023_timer_id_t`

```c
typedef enum {
    HT5023_TMR0 = 0,
    HT5023_TMR1 = 1,
    HT5023_TMR2 = 2,
    HT5023_TMR3 = 3,
} ht5023_timer_id_t;
```

### `ht5023_timer_periodic_cfg_t`

```c
typedef struct {
    uint16_t prescaler;  // TMRDIV: clock divides by (prescaler + 1)
    uint16_t period;     // TMRPRD: counter wraps when it reaches this value
} ht5023_timer_periodic_cfg_t;
```

**Tick frequency formula:**

```
tick_hz = Fsys / ((prescaler + 1) * (period + 1))
```

**Example — 10 Hz.** Derive the prescaler from `HT5023_FSYS_HZ` rather than
writing a number: a fixed `9499` assumes the datasheet's 9.5 MHz and runs at
9.68 Hz on a 9.196 MHz part.

```c
prescaler = HT5023_FSYS_HZ / 1000 - 1   →  1000 Hz pre-divided clock
period    = 99                          →  1000 / 100 = 10 Hz tick
```

---

## Functions

### `ht5023_timer_periodic_start`

```c
bool ht5023_timer_periodic_start(ht5023_timer_id_t id,
                                  const ht5023_timer_periodic_cfg_t* cfg);
```

Enable the timer clock gate, write `prescaler` and `period`, clear all
interrupt flags, and start the counter in periodic mode.

Returns `false` if `cfg` is NULL or `id` is invalid.

---

### `ht5023_timer_stop`

```c
void ht5023_timer_stop(ht5023_timer_id_t id);
```

Halt the timer by clearing CNTEN in TMRCON. Does not reset the counter.

---

### `ht5023_timer_period_elapsed`

```c
bool ht5023_timer_period_elapsed(ht5023_timer_id_t id);
```

Returns `true` if the period flag (PRDIF) is set, and clears it. Returns
`false` if the period has not elapsed yet, or if `id` is invalid.

Call this in a polling loop:

```c
while (1) {
    if (ht5023_timer_period_elapsed(HT5023_TMR0)) {
        // runs once per timer period
        do_work();
    }
}
```

---

### `ht5023_timer_get_count`

```c
uint16_t ht5023_timer_get_count(ht5023_timer_id_t id);
```

Return the current value of the TMRCNT register (0 to `period`).

---

## Example — 10 Hz periodic task

```c
#include "ht5023/timer.h"

const ht5023_timer_periodic_cfg_t tmr_cfg = {
    .prescaler = 9499,  // Fsys/9500 = 1000 Hz
    .period    = 99,    // 1000/100  = 10 Hz
};

ht5023_timer_periodic_start(HT5023_TMR0, &tmr_cfg);

while (1) {
    if (ht5023_timer_period_elapsed(HT5023_TMR0)) {
        // called ~10 times per second
        led_toggle();
    }
}
```

See also: `examples/timer_poll/`
