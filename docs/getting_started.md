# Getting Started

## Prerequisites

| Tool | Version |
|------|---------|
| `arm-none-eabi-gcc` | 10+ recommended |
| `arm-none-eabi-objcopy` | bundled with toolchain |
| J-Link (optional) | for `make flash` |
| Serial terminal | 9600 8N1 (PuTTY, minicom, screen…) |

Install the ARM toolchain:
```bash
# Ubuntu / Debian
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi

# macOS (Homebrew)
brew install --cask gcc-arm-embedded
```

---

## Build

```bash
# Build the default example (blinky)
make

# Build a specific example
make EXAMPLE=uart
make EXAMPLE=st7789_demo
make EXAMPLE=xpt2046_touch_demo
```

Build artifacts land in `build/`:

| File | Description |
|------|-------------|
| `build/app.elf` | Linked ELF (for debugging) |
| `build/app.bin` | Raw binary (for flashing) |
| `build/app.lst` | Disassembly listing |
| `build/app.map` | Linker map |

Available example names:

```
blinky          led_dance       buzzer_music    uart
uart_ansi       uart_ringbuf    boot_banner     wdt_monitor
timer_poll      reset_info      spi_loopback    i2c_scan
systick_irq     st7789_demo     st7789_ui_demo  xpt2046_touch_demo
```

---

## Flash

```bash
# Using the included J-Link script
bash scripts/run_flash.sh
```

Or program manually with J-Link Commander:
```
device HT5023
si SWD
speed 4000
loadbin build/app.bin, 0x00000000
r
g
exit
```

---

## Serial console

Most examples print over **UART2** at **9600 baud, 8N1**:

| Signal | Pin |
|--------|-----|
| TX2 | PC11 |
| RX2 | PC12 |

Connect a USB-UART adapter (3.3 V logic) to PC11/PC12 and open a terminal at
9600 8N1.

---

## First example: `blinky`

`examples/blinky/main.c` is the simplest possible application — it blinks the
two on-board LEDs (PE3, PE4) without any peripheral drivers:

```c
#include "ht5023/board/blinky_gpio.h"

int main(void) {
    led_gpio_init();
    while (1) {
        led_toggle();
        // busy-wait ~500 ms
        for (volatile uint32_t i = 0; i < 500000; ++i) {}
    }
}
```

```bash
make EXAMPLE=blinky
bash scripts/run_flash.sh
```

---

## Second example: `uart`

Echoes characters received on UART2 and prints a heartbeat every ~1 second:

```bash
make EXAMPLE=uart
bash scripts/run_flash.sh
# open terminal at 9600 8N1 on PC11/PC12
```

---

## System clock

`SystemInit()` (called automatically from startup) configures the chip with:
- HRC oscillator enabled: Fsys 9.5 MHz nominal, **9.196 MHz measured** (`HT5023_FSYS_HZ`);
  the CPU and SysTick run at Fsys/2 (`HT5023_FCPU_HZ`, `ht5023_fcpu_hz()`)
- LRC (low-speed RC) disabled
- Watchdog **disabled** by default

If you need the watchdog or LRC, edit
`sdk/cores/ht5023/src/system_ht5023.c`.

Pass the actual system clock to every driver that needs it via
`sysclk_hz` in the configuration structure — the SDK never reads a
global clock variable.

---

## Include path

The Makefile already sets up all include paths. In your own application:

```c
// Include everything
#include "ht5023/sdk.h"

// Or include only what you need
#include "ht5023/uart.h"
#include "ht5023/spi.h"
```
