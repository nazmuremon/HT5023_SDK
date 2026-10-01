# HT5023 SDK Documentation

**Version 1.0.0** — Bare-metal SDK for the HT5023/HT502X Cortex-M0 microcontroller.

---

## Contents

| Document | Description |
|----------|-------------|
| [Getting Started](getting_started.md) | Build system, toolchain, flash, first run |
| [Examples](examples.md) | Walk-through of all 14 example applications |
| [VS Code Manual](vscode_manual.md) | Setup, the three workflows, build/flash/monitor, debugging, troubleshooting |
| [LCD Pinout](lcd_pinout.md) | Two-digit TN segment panel — current and previous pin maps |
| **API Reference** | |
| [UART](api/uart.md) | Up to 6 UARTs, blocking polling driver |
| [SPI](api/spi.md) | SPI master, polling with timeout |
| [I2C](api/i2c.md) | I2C master, polling, combined write-read |
| [Timer](api/timer.md) | TMR0–TMR3 periodic polling timers |
| [SysTick](api/systick.md) | Cortex-M0 core timer, interrupt-driven |
| [Watchdog](api/wdt.md) | Hardware watchdog timer |
| [Reset Status](api/reset.md) | Read and clear reset cause flags |
| [Ring Buffer](api/ringbuf.md) | ISR-safe byte FIFO |
| [Display](api/display.md) | ST7789 LCD, 2D graphics, text, framebuffer |
| [Touch](api/xpt2046.md) | XPT2046 resistive touch controller |
| [Buzzer](api/buzzer.md) | Passive buzzer, square-wave tone generation |
| [Boot Banner](api/boot_banner.md) | Colored UART system-info printer |

---

## Hardware summary

| Resource | Detail |
|----------|--------|
| Core | ARM Cortex-M0 |
| Flash | 256 KiB program + 1 KiB info |
| SRAM | 32 KiB |
| System clock | HRC: Fsys 9.196 MHz measured (9.5 MHz nominal); CPU runs at Fsys/2 (see note) |
| UART | 6× (UART0–UART5) |
| SPI | 1× master |
| I2C | 1× master |
| Timers | TMR0–TMR3 (general purpose) + SysTick |
| WDT | Hardware watchdog |

> **Clock note.** There are two clocks. **Fsys** clocks the UART, SPI, I²C and
> timer dividers; **Fcpu** clocks the CPU and SysTick and is Fsys/2 out of reset
> (`SYSCLKDIV` = `001`). The datasheet gives the HRC as 9.5 MHz; on the
> development board Fsys measures **9.196 MHz**, which is what `HT5023_FSYS_HZ`
> in `ht5023/core/system.h` defaults to and every example uses — `-0.22 %` baud
> error at 115200 instead of `-2.65 %`. The HRC is not trimmed per part: on a
> different board, measure it and build with
> `make EXAMPLE=... EXTRA_CFLAGS=-DHT5023_FSYS_HZ=<Hz>`.

---

## SDK layout

```
sdk/
  include/ht5023/     ← stable public headers (use these)
  cores/ht5023/       ← startup, linker script, device.h
  boards/ht5023_blinky/ ← board-support (LED GPIO)
lib/ht5023_*/         ← driver implementations
examples/             ← 14 runnable applications
docs/                 ← this documentation
```

The single convenience header `#include "ht5023/sdk.h"` pulls in every public
API at once.

---

## Version macros

```c
#include "ht5023/version.h"

// String form
HT5023_SDK_VERSION_STR          // "1.0.0"

// Integer form (for comparisons)
HT5023_SDK_VERSION              // encoded uint32

// Compile-time version guard
#if HT5023_SDK_AT_LEAST(1, 0, 0)
  // feature available from 1.0.0
#endif
```
