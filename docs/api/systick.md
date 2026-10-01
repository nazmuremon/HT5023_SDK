# SysTick API

**Header:** `#include "ht5023/systick.h"`
**Source:** `lib/ht5023_systick/src/ht5023_systick.c`

---

## Overview

Helper for the Cortex-M0 core SysTick timer. SysTick fires `SysTick_Handler`
at the requested rate. You must define the handler in your application.

This is the recommended way to implement a millisecond tick counter.

---

## Functions

### `ht5023_systick_start`

```c
bool ht5023_systick_start(uint32_t cpu_hz, uint32_t tick_hz);
```

Configure and enable SysTick to fire `SysTick_Handler` at `tick_hz`
interrupts per second, using the processor core clock at `cpu_hz`.

Returns `false` if:
- Either argument is zero.
- `tick_hz > cpu_hz` (tick rate faster than core clock).
- The required reload value exceeds the 24-bit SysTick range.

> **CPU vs system clock.** SysTick counts Fcpu, which is Fsys / 2 out of reset
> (`SYSCLKDIV` = `001`). Pass the **CPU** clock, not Fsys — passing Fsys makes
> every tick last twice as long. Measured Fsys is 9.196 MHz, so Fcpu ≈ 4.598 MHz.
>
> ```c
> #include "ht5023/core/system.h"
> ht5023_systick_start(ht5023_fcpu_hz(), 1000u);  // reads the live divider
> ```

---

### `ht5023_systick_stop`

```c
void ht5023_systick_stop(void);
```

Disable SysTick by clearing the CSR register.

---

### `ht5023_systick_reload_value`

```c
uint32_t ht5023_systick_reload_value(uint32_t cpu_hz, uint32_t tick_hz);
```

Compute the SysTick RELOAD register value for the given rate without
configuring the hardware. Returns 0 on invalid input or overflow.

When `cpu_hz == tick_hz`, returns 0 — this is valid (one tick per clock
cycle), not an error.

---

## Interrupt handler

You must define `SysTick_Handler` in your application. It is declared as
a weak symbol in the startup code:

```c
volatile uint32_t g_ticks = 0;

void SysTick_Handler(void) {
    g_ticks++;
}
```

Read `g_ticks` from main to get a millisecond timestamp (when running at
1 kHz):

```c
uint32_t now = g_ticks;
if ((uint32_t)(now - last_event) >= 500u) {
    // 500 ms elapsed
    last_event = now;
}
```

---

## Example

```c
#include "ht5023/systick.h"

volatile uint32_t g_ms = 0;

void SysTick_Handler(void) { g_ms++; }

int main(void) {
    // Fcpu from the live SYSCLKDIV register (Fsys/2 out of reset)
    if (!ht5023_systick_start(ht5023_fcpu_hz(), 1000u)) {
        // handle error — should not happen with these values
        while (1) {}
    }

    while (1) {
        uint32_t now = g_ms;
        if ((now % 500) == 0) {
            led_toggle();
        }
    }
}
```

See also: `examples/systick_irq/`
