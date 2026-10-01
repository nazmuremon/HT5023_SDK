# HT5023 for VS Code

Build, flash and monitor Holtek HT5023 (Cortex-M0) boards without leaving the
editor.

## Install

```bash
./install.sh
```

Then **Ctrl+Shift+P → Developer: Reload Window**. That is the whole install —
the extension is plain JavaScript with no dependencies, so there is no npm step
and no marketplace account involved. `./install.sh --uninstall` removes it.

## What it does

Three kinds of project are recognised automatically, so the same buttons work
in all of them:

| Your workspace has | Build runs | Upload runs |
|---|---|---|
| `platformio.ini` | `pio run` | `pio run -t upload` |
| `Makefile` + `sdk/` | `make build EXAMPLE=<picked>` | that, then the SDK flash sequence |
| a `.ino` | `arduino-cli compile` | that, then `ht5023_upload.py` |

Everything that writes to the chip goes through the SDK's own Python tools —
the same ones the Flasher GUI and the Arduino uploader use — so there is one
implementation of the flash sequence, not three.

## Commands

All under **HT5023:** in the command palette.

| Command | Notes |
|---|---|
| Build | |
| Upload (J-Link / SWD) | Programs from `0x0000` |
| Upload over USB (bootloader) | Needs the bootloader installed once; app links at `0x2000` |
| Serial Monitor | `pio device monitor` if PlatformIO is present, else pyserial's miniterm |
| Clean | |
| Select SDK Example | Picks from `make list-examples` |
| Select Serial Port | Lists `/dev/ttyUSB*`, `/dev/ttyACM*` and `/dev/serial/by-id` |
| Install UART Bootloader | The one step that needs the J-Link; asks first |
| New PlatformIO Project | Scaffolds `platformio.ini` + `src/main.cpp` pointing at the SDK's platform |
| Check Toolchain | Reports what is installed and what is missing |
| Open the standalone Flasher GUI | The Tkinter app, for working outside VS Code |

The status bar (bottom left) carries the current example, **Build**, **Upload**
and the serial port.

## Settings

| Setting | Default | |
|---|---|---|
| `ht5023.sdkPath` | auto | The SDK checkout. Found from the workspace if empty |
| `ht5023.example` | `blinky` | Which `examples/<name>` the Makefile workflow builds |
| `ht5023.serialPort` | auto | Empty means pick the first USB adapter |
| `ht5023.serialBaud` | `115200` | |
| `ht5023.jlinkSpeed` | `1000` | SWD clock, kHz |
| `ht5023.pythonPath` | `python3` | Interpreter for the SDK's flash tools |

## Using libraries from the wider ecosystem

That is what PlatformIO is for here. In a PlatformIO project:

```ini
[env:ht5023]
platform = /path/to/HT5023/platformio
board = ht5023
framework = arduino
lib_deps =
    bblanchon/ArduinoJson@^7.0.0
    adafruit/Adafruit GFX Library@^1.11.9
```

`pio run` fetches them from the PlatformIO registry and compiles them against
the HT5023 Arduino core. Any library that is portable C++ and does not reach
for another MCU's registers should build; one that declares
`platforms = espressif32` in its manifest will be skipped by the dependency
finder regardless of whether the code would have worked.

## Requirements

`arm-none-eabi-gcc` and `make` are needed for everything. `JLinkExe` is needed
for SWD and for installing the bootloader; `arduino-cli` for `.ino` files;
PlatformIO for `platformio.ini` projects. **HT5023: Check Toolchain** reports
what is present, and `./install-linux.sh` in the SDK installs the lot.
