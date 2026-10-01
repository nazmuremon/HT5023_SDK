# HT5023 Arduino Core

This package adds Arduino IDE / Arduino CLI support for the HT5023 board.

## Install Manually

Copy `arduino/hardware/ht5023/ht5023` into your Arduino hardware folder:

- Linux: `~/Arduino/hardware/ht5023/ht5023`
- Windows: `%USERPROFILE%\\Documents\\Arduino\\hardware\\ht5023\\ht5023`
- macOS: `~/Documents/Arduino/hardware/ht5023/ht5023`

After restarting Arduino IDE, select:

- `Tools -> Board -> HT5023 Board`

## Requirements

- `arm-none-eabi-gcc` and `arm-none-eabi-g++` in `PATH`
- `JLinkExe` in `PATH`
- `python3` in `PATH`

## Board Defaults

- CPU clock: `9.5 MHz`
- Upload transport: `J-Link SWD`
- Built-in LED: `PE3` (`LED_BUILTIN`)
- Serial: `UART2` on `PC11 = TX`, `PC12 = RX`
- SPI: `PC4 = MOSI`, `PC5 = MISO`, `PC6 = SCK`, `PC7 = SS`
- I2C: `PC13 = SCL`, `PC14 = SDA`

## Shipped Libraries

- `SPI`
- `Wire`
- `HT5023SDK`

`HT5023SDK` bundles the repo SDK libraries for Arduino sketches, including:

- `ht5023_boot_banner`
- `ht5023_buzzer`
- `ht5023_debug`
- `ht5023_dma`
- `ht5023_fb`
- `ht5023_font5x7`
- `ht5023_gfx`
- `ht5023_log`
- `ht5023_reset`
- `ht5023_ringbuf`
- `ht5023_st7789`
- `ht5023_text`
- `ht5023_timer`
- `ht5023_wdt`
- `ht5023_xpt2046`

Use it from a sketch with:

```cpp
#include <HT5023SDK.h>
```

The low-level HT5023 core driver headers are also available directly:

- `ht5023_uart.h`
- `ht5023_spi.h`
- `ht5023_i2c.h`
- `ht5023_systick.h`

## Examples

- `File -> Examples -> Basics -> Blink`
- `File -> Examples -> Communication -> SerialEcho`
- `File -> Examples -> Simulation -> ThreeBodyProblem`
- `File -> Examples -> HT5023SDK -> SDKDiagnostics`
