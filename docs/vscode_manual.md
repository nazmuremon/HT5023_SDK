# HT5023 in VS Code — user manual

Everything needed to write, build, flash and debug HT5023 firmware from VS
Code. The HT5023 is a Holtek ARM Cortex-M0 (256 KiB flash, 32 KiB SRAM); this
SDK gives it an Arduino-compatible core so code and libraries written for the
wider ecosystem run on it.

---

## 1. One-time setup

```bash
cd /path/to/HT5023
./install-linux.sh              # toolchain, Python, J-Link, arduino-cli, launcher
./vscode-extension/install.sh   # the VS Code extension
```

Then in VS Code: **Ctrl+Shift+P → Developer: Reload Window**.

Check it worked with **Ctrl+Shift+P → HT5023: Check Toolchain**. You want:

```
  [ok]  arm-none-eabi-gcc     /usr/bin/arm-none-eabi-gcc
  [ok]  make                  /usr/bin/make
  [ok]  JLinkExe (SWD)        /usr/bin/JLinkExe
  [ok]  arduino-cli (.ino)    ~/.local/bin/arduino-cli
  [ok]  pio (PlatformIO)      ~/.local/bin/pio
```

`arm-none-eabi-gcc` and `make` are required. The rest are needed only for the
workflow that uses them.

### Recommended extensions

| Extension | For |
|---|---|
| `platformio.platformio-ide` | PlatformIO projects, library manager |
| `ms-vscode.cpptools` | C/C++ IntelliSense |
| `marus25.cortex-debug` | Step debugging over SWD |

---

## 2. Pick a workflow

Three ways to work, all supported by the same **Build** / **Upload** /
**Monitor** buttons. The extension detects which one you are in.

| | Use when | Project marker |
|---|---|---|
| **PlatformIO** | You want libraries from the registry | `platformio.ini` |
| **SDK / Makefile** | Bare-metal C against the SDK drivers | `Makefile` + `sdk/` |
| **Arduino sketch** | A single `.ino` | a `.ino` file |

### PlatformIO — the one to start with

**Ctrl+Shift+P → HT5023: New PlatformIO Project**, pick a folder. You get:

```ini
[env:ht5023]
platform = /path/to/HT5023/platformio
board = ht5023
framework = arduino
monitor_speed = 115200
upload_protocol = jlink
```

`framework = arduino` is required — it is what supplies `Arduino.h`, and with
it the library ecosystem.

### SDK / Makefile

Open the SDK folder itself. **HT5023: Select SDK Example** chooses which of the
33 examples in `examples/` the buttons act on; the choice shows in the status
bar and is saved per workspace.

### Arduino sketch

Open a `.ino`. It must sit in a folder with the same name — `Blink/Blink.ino` —
because that is what `arduino-cli` compiles.

---

## 3. Build, upload, monitor

Status bar, bottom left:

```
$(circuit-board) HT5023: blinky   $(tools) Build   $(arrow-up) Upload   $(plug) ttyUSB0
```

Or the command palette:

| Command | What runs |
|---|---|
| **HT5023: Build** | `pio run` / `make build EXAMPLE=…` / `arduino-cli compile` |
| **HT5023: Upload (J-Link / SWD)** | builds, then programs from `0x0000` |
| **HT5023: Upload over USB (bootloader)** | builds at `0x2000`, sends over the adapter |
| **HT5023: Serial Monitor** | `pio device monitor`, or pyserial's miniterm |
| **HT5023: Clean** | |
| **HT5023: Select Serial Port** | lists `/dev/ttyUSB*`, `/dev/ttyACM*` |

Build output goes to the terminal panel with the `$gcc` problem matcher, so
compiler errors become clickable entries in the Problems view.

### Two ways to program the chip

The HT5023 has **no factory UART ISP** — the manual lists SWD as the only
programming interface. So:

- **SWD (J-Link)** always works. Programs from `0x0000`. This is the default.
- **USB bootloader** needs a one-time setup: **HT5023: Install UART Bootloader**
  (which itself needs the J-Link, once). After that the board programs over a
  plain USB-UART adapter with no probe attached, and the application links at
  `0x2000` instead.

For PlatformIO, the bootloader path is:

```ini
upload_protocol = bootloader
upload_port = /dev/ttyUSB0
```

The linker script switches automatically with the protocol — get that wrong and
the board programs cleanly and never runs.

---

## 4. Using libraries from the ecosystem

This is what PlatformIO is for here.

```ini
lib_deps =
    bblanchon/ArduinoJson@^7.0.0
    adafruit/Adafruit GFX Library@^1.11.9
```

`pio run` fetches them and compiles against the HT5023 core. Both of those are
verified working on this part.

**What will and will not work.** A library builds here if it is portable C++
that talks to hardware through Arduino APIs (`digitalWrite`, `Wire`, `SPI`,
`Serial`, `millis`). It will not if it:

- reaches for another MCU's registers or headers (`avr/io.h`, `esp_system.h`,
  `stm32f1xx.h`);
- needs a peripheral this part lacks — WiFi, Bluetooth, USB device, DAC;
- declares `platforms = espressif32` (or any list without `*`) in
  `library.properties`, in which case PlatformIO's dependency finder skips it
  whatever the code would have done;
- assumes an interrupt-driven `Serial` — the HT5023 UART driver is polling.

With 256 KiB flash and 32 KiB RAM there is plenty of room for most libraries.

---

## 5. Debugging over SWD

Requires `marus25.cortex-debug` and a J-Link.

**SDK / Makefile projects** already have launch configurations in `.vscode/`:
`HT5023: Debug With J-Link` builds and starts a session; `HT5023: Attach To
Running Target` connects without reprogramming. Breakpoints, stepping and
register views work as usual.

**PlatformIO projects** use `pio debug`, or add a cortex-debug configuration
pointing at `.pio/build/ht5023/firmware.elf`.

Semihosting output (from the `semihosting` examples) appears through the J-Link
GDB server rather than the serial port.

---

## 6. Board reference

### Serial

`Serial` is **UART2**, and this is the pair the debug log and USB uploads use:

| Signal | HT5023 pin | Wire to the adapter's |
|---|---|---|
| TX | `PC11` | RX |
| RX | `PC12` | TX |
| GND | GND | GND (must be common) |

Default 115200 8N1. Some older examples use 9600.

### Arduino pin aliases

| Alias | Pad |
|---|---|
| `LED_BUILTIN` / `PIN_LED1` | `PE3` |
| `PIN_LED2` | `PE4` |
| `MOSI` / `MISO` / `SCK` / `SS` | `PC4` / `PC5` / `PC6` / `PC7` |
| `SCL` / `SDA` | `PC13` / `PC14` |

Every pad is also available by name as `PIN_P<port><bit>` — `PIN_PA0`,
`PIN_PB11`, `PIN_PF7` and so on. Use those when wiring something pin by pin.

> **Two pads are shared with the two-digit LCD wiring.** `LED_BUILTIN` (`PE3`)
> is LCD segment **g**, and `SCL` (`PC13`) is LCD segment **b**. On a board with
> that display fitted, blinking the built-in LED drives a display segment, and
> using `Wire` fights one. See [lcd_pinout.md](lcd_pinout.md).

### Do not drive these

| Pad | Why |
|---|---|
| `PB13`, `PB15` | SWDIO / SWCLK — writing to them ends the debug connection |
| `PC11`, `PC12` | the serial link the log and USB uploads run over |

### Memory

| | |
|---|---|
| Flash | 256 KiB (`0x00000000`–`0x0003FFFF`) |
| SRAM | 32 KiB (`0x20000000`) |
| Standalone image | links at `0x0000` |
| Bootloader + app | bootloader `0x0000`–`0x1FFF`, app from `0x2000` |

> **`0x0FC0` holds the flash option bytes.** A non-`0xFF` byte at `0x0FC1`
> enables read protection and stops the part being read or debugged until a mass
> erase. The linker scripts keep code out of that word and the flash tool
> refuses to write it — do not work around either.

---

## 7. Troubleshooting

**"No serial port available" / permission denied.** Serial devices belong to
the `dialout` group. `sudo usermod -aG dialout $USER`, then log out and back in
— the group is only picked up at login.

**"no J-Link probe is connected".** Check the probe is plugged in and the target
powered: `bash scripts/run_detect.sh` should print `Cortex-M0 identified` and a
`VTref` around 3.3 V. Or switch to the USB bootloader path.

**Upload says "Writing target memory failed".** That is J-Link's generic
`loadbin`, which cannot program this part — SEGGER ships no flash algorithm for
it. Everything in this SDK goes through `tools/ht5023_flash_seq.py` instead. If
you see that message, something is calling `loadbin` directly.

**The debug log stays empty.** Either the example never writes to a UART (the
Flasher says so explicitly), or it only logs over semihosting — which needs the
debugger attached, so it prints nothing when uploaded over USB. Try `usb_echo`,
`text_back` or `random_sentence`.

**A library will not compile.** Check §4. Most failures are a library reaching
for another MCU's headers, or a `platforms =` line that excludes this one.

**PlatformIO cannot find the platform.** `platform =` must be an absolute path
to the SDK's `platformio/` folder, or a relative one from the project.

**The LCD shows garbage or stays blank.** See [lcd_pinout.md](lcd_pinout.md) —
`LcdHw` cannot drive segments on port C, and the GPIO schemes have specific
wiring requirements.

---

## 8. Where things are

```
sdk/            core startup, system init, linker scripts, public headers
lib/            19 driver modules (uart, spi, i2c, timer, wdt, st7789, gfx, …)
examples/       33 runnable examples, built with make EXAMPLE=<name>
sketches/       Arduino .ino sketches
arduino/        the Arduino core — the single source of truth for the framework
platformio/     the PlatformIO dev-platform
tools/          flasher GUI, flash sequence, bootloader client
bootloader/     the resident UART bootloader
vscode-extension/  this extension
docs/           documentation
```

Outside VS Code, `HT5023 Flasher.sh` opens a standalone editor-and-programmer
that needs nothing installed but Python — useful on a machine where you would
rather not set up an IDE.
