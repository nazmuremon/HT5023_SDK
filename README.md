# HT5023 SDK (GCC, Cortex-M0)

Bare-metal HT5023 SDK with:
- `sdk/cores` for startup/system/linker
- `sdk/include/ht5023/...` namespaced public headers
- driver libraries (`UART`, `WDT`, `timer`, `SPI`, `I2C`, `SysTick`, reset flags)
- utility libraries (`ringbuf`, `boot_banner`)
- board support for the demo blinky board (`PE3`, `PE4`)
- runnable examples (UART2 on `PC11`/`PC12`)

## SDK Layout
- `sdk/cores/ht5023`: core startup, system init, linker script, device/core headers
- `sdk/boards/ht5023_blinky`: board support (LED GPIO)
- `sdk/include/ht5023`: stable public include namespace
- `lib/ht5023_*`: driver and utility implementations (current source location)
- `examples`: example applications built with `EXAMPLE=...`
- `scripts`: J-Link detect/flash helper scripts

See `sdk/README.md` and `sdk/ARCHITECTURE.md` for conventions and migration notes.

## Supported platforms

Linux, Windows and macOS. The toolchain is the same everywhere
(`arm-none-eabi-gcc` + `make`); only the way you install it differs.

### Linux — one-click setup

```bash
./install-linux.sh
```

Installs the toolchain, `make`, Tkinter and pyserial for your distro
(apt / dnf / pacman / zypper / apk), puts you in the `dialout` group so serial
adapters open without `sudo`, registers **HT5023 Flasher** in the application
menu, and finishes by test-compiling `blinky`. It asks before each step that
needs `sudo`, and it is safe to re-run.

Then start the GUI in whichever way suits you:

- the **HT5023 Flasher** entry in your application menu,
- double-click `HT5023 Flasher.sh` in the project folder,
- or `./'HT5023 Flasher.sh'` from a terminal.

Useful flags: `--yes` (no prompts, for scripted installs) and `--no-packages`
(skip everything requiring `sudo`).

If you would rather install the packages yourself:

```bash
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi make \
                 python3 python3-tk python3-serial     # Debian/Ubuntu
sudo dnf install arm-none-eabi-gcc-cs arm-none-eabi-binutils make \
                 python3 python3-tkinter python3-pyserial   # Fedora
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-binutils make \
               python tk python-pyserial               # Arch
```

`install-linux.sh` also offers to fetch and install the SEGGER J-Link software,
which is needed only for SWD programming and semihosting — a board already
carrying the UART bootloader uploads over the USB adapter alone. That step
downloads from <https://www.segger.com/downloads/jlink/> and **accepts SEGGER's
licence agreement on your behalf**, so skip it and install by hand if you would
rather read the terms first. On Ubuntu/Debian that is:

```bash
curl -fL -d 'accept_license_agreement=accepted' \
     -o JLink_Linux_x86_64.deb \
     https://www.segger.com/downloads/jlink/JLink_Linux_x86_64.deb
sudo apt install ./JLink_Linux_x86_64.deb
```

It installs to `/opt/SEGGER/JLink_V<version>/`, symlinks `JLinkExe` and
`JLinkGDBServerCLExe` into `/usr/bin`, and drops the udev rules the probe needs
into `/etc/udev/rules.d/99-jlink.rules`. Replug the probe afterwards.

### Windows

Double-click `HT5023 Flasher.bat`. It expects `arm-none-eabi-gcc` (the Arm GNU
Toolchain or PlatformIO's copy) and `make` from MSYS2.

## Build
```bash
make
make EXAMPLE=uart
make EXAMPLE=boot_banner
```

Artifacts:
- `build/app.elf`
- `build/app.bin`
- `build/app.lst`

## Examples
Examples are listed in `examples/README.md`.

Common UART examples use:
- `PC11 = TX2`
- `PC12 = RX2`
- terminal `9600 8N1`

## Flash (J-Link)
Use the included HT5023 J-Link helper:
```bash
bash scripts/run_flash.sh
```

Useful variants:
```bash
make flash
make flash SPEED=100
make flash ERASE_MODE=pages
make flash RUN_AFTER_FLASH=0
```

## One-Click Flasher GUI
A self-contained editor + programmer (`tools/ht5023_flasher.py`, Python +
Tkinter, no install step). Launch it with:

| Platform | One-click                                                        |
| -------- | ---------------------------------------------------------------- |
| Linux    | **HT5023 Flasher** in the application menu, or `HT5023 Flasher.sh` |
| Windows  | `HT5023 Flasher.bat`                                             |
| Any      | `python3 tools/ht5023_flasher.py`                                |

It finds `arm-none-eabi-gcc`, `make`, the J-Link software and `arduino-cli` on
`PATH` first and in each platform's usual install locations second, so nothing
has to be configured by hand.

- **Compile & Upload (J-Link)** (`F5`) builds the example that owns the open
  file and programs it over SWD.
- The **DEBUG LOG** panel at the bottom streams the target's output in real
  time, byte by byte — partial lines appear immediately instead of waiting for
  a newline. It opens by itself after every successful upload.
  - `source` — **Serial** (UART2 `PC11`/`PC12` through a USB-TTL adapter),
    **Semihosting** (over the J-Link cable, no extra wiring), or **Auto**, which
    uses a serial adapter when one is plugged in and falls back to semihosting.
  - `port` / `baud` — serial port (`COM11` on Windows, `/dev/ttyUSB0` on Linux;
    press ↻ to rescan) and baud rate, default 115200
    (`text_back` and `uart_at` default to 115200 8N1; the older UART examples
    still use 9600).
  - `follow`, `timestamps`, `auto-open after upload`, `Clear` (`Ctrl+L`), and
    **Start Log** / **Stop Log** (`F6`).
- Uploading while the log is open releases the J-Link, flashes, then reopens it.
- **Send box** under the log types back to the device — for AT commands and any
  interactive example. Enter sends, Up/Down recall history, `line ending` picks
  `CRLF`/`CR`/`LF`/none, `raw keys` sends every keystroke immediately (for
  char-at-a-time examples), `echo` shows what was sent. It types over **either**
  link: the serial adapter, or — with only the J-Link attached — the semihosting
  terminal channel, which reaches examples that read it (`text_back`). Adapters
  plugged in later are picked up within a few seconds; a port that is busy or
  missing says which, and lists what is there.
- The Explorer has a **↻ Refresh** (`Ctrl+R`) that re-reads the folder tree and the
  open file, keeping expanded folders and your selection — use it after files are
  added or changed outside the app.
- **The log is selectable and copyable**: drag to select, `Ctrl+C` to copy,
  `Ctrl+A` to select all, or right-click for Copy / Copy all / Select all /
  Save to file / Clear. It stays read-only — typing in it does nothing.

Serial monitoring needs `pyserial` (`pip install pyserial`); semihosting needs
`arm-none-eabi-gdb` and `JLinkGDBServerCL.exe`, both already required by the
toolchain and J-Link install.

## Flashing over USB (no J-Link)

`bootloader/` is a UART firmware loader. Install it once over SWD and after that
applications go over the USB-UART adapter — the chip has no factory ISP, so this
is what makes USB programming possible at all.

```bash
make bootloader          # then flash build/boot.bin with the J-Link, once
```

**Testing the link:** flash `usb_echo` — it has no options, talks only on UART2
at 115200 8N1, and answers every line with `echo: <text>`. If that works, the
USB path is good and anything else is configuration.

In the Flasher, the toolbar has **via: J-Link (SWD) / USB (bootloader)** next to
**Compile & Upload**. Choosing **USB (bootloader)**:

1. builds the example with `BOOT_APP=1`, so it links at `0x2000` instead of `0x0000`,
2. sends it over the port selected in the DEBUG LOG panel,
3. verifies every 256-byte chunk against a checksum the target computes by
   reading its own flash back, then starts the application.

**Press reset when the log says `PRESS RESET on the board now`** — the adapter's
DTR/RTS is not wired to reset on this board, so entry needs the button. Wire DTR
or RTS to the reset pin if you want that automated.

From the command line:

```bash
make EXAMPLE=text_back BOOT_APP=1
python tools/ht5023_uart_flash.py --port COM11 --file build/app.bin
```

The J-Link is then only needed to replace the bootloader itself, or to recover a
board whose application blocks bootloader entry.

**Memory map:** bootloader `0x0000-0x1FFF`, application from `0x2000`. Building
without `BOOT_APP=1` still produces a standalone image for SWD, so both
workflows coexist. The Cortex-M0 has no VTOR, so the bootloader keeps the vector
table and forwards exceptions to the application's table at `0x2000` — meaning
interrupt-driven applications keep working under it.

## USB-UART (FTDI / CP210x / CH340) host link
The Flasher talks to the target two ways, and you can mix them:

| | Programming | Log out | Typing in |
|---|---|---|---|
| **J-Link (SWD)** | yes — and required once, to install the bootloader | semihosting | semihosting |
| **USB-UART adapter** | yes, through the bootloader (see above) | UART2 | UART2 |

The chip itself has no UART ISP: the HT502X manual lists SWD as the only debug
and programming interface (`PB13` = SWIO, `PB15` = SWCLK). USB programming works
because `bootloader/` provides the missing piece in software.

**Wiring for the log/console link:**

| HT5023 | direction | adapter |
|--------|-----------|---------|
| `PC11` (TX2) | -> | adapter **RX** |
| `PC12` (RX2) | <- | adapter **TX** |
| GND | — | GND (must be common) |

In the Flasher set the **DEBUG LOG** source to **Serial**, pick the port and
`115200`. Adapters are recognised by chip (FTDI, CP210x, CH340, PL2303) and
sorted above other ports, so **Auto** picks one as soon as it is plugged in.
On Linux they are also recognised by device name — `/dev/ttyUSB*` (USB-serial
bridge) and `/dev/ttyACM*` (USB CDC) rank first, while the `/dev/ttyS*` nodes
the kernel registers with nothing behind them are hidden entirely so the real
adapter is easy to find.

### Linux: "permission denied" opening the port

Serial devices belong to the `dialout` group (`uucp` on Arch). Until you are in
it, the adapter is listed but cannot be opened:

```bash
sudo usermod -aG dialout $USER
```

Then **log out and back in** — group membership is only picked up at login, so
the change appears to do nothing until you do. `./install-linux.sh` handles this
for you, and the Flasher says so in the debug log when it hits the error.

For SWD, the J-Link also needs its udev rules; SEGGER's `.deb`/`.rpm` installs
them, and the tarball ships them in `99-jlink.rules` for copying into
`/etc/udev/rules.d/`.

Examples that log over UART2 rather than the debug cable:

```bash
make EXAMPLE=text_back  CFLAGS="... -DTEXT_BACK_LINK=1 -DTEXT_BACK_PORT=2"
make EXAMPLE=wisun_get  CFLAGS="... -DWISUN_LOG=2"      # 3 = both channels
make EXAMPLE=uart_bridge CFLAGS="... -DBRIDGE_HOST=2"   # type over the adapter
```

`random_sentence`, `button_press`, `uart_at` and `boot_banner` already log to
UART2 by default.

Using the adapter as the host link is also *faster* than semihosting: a
semihosting read costs a debugger trap of several milliseconds, while a UART poll
is a register read — which is why `uart_bridge` can watch the device continuously
when driven over an adapter (`BRIDGE_HOST=2`) but not over the J-Link.

## VS Code
Open the repo root in VS Code. The workspace now includes:
- one-click `HT5023: One-Click Flash` using workspace defaults
- `Terminal -> Run Task...` for `HT5023: Build`, `Clean`, `Flash`, `One-Click Flash`, `Build Example...`, `List Examples`, and J-Link detect/register helpers
- C/C++ IntelliSense include-path configuration for the SDK, board support, and libraries
- launch and attach Cortex-Debug configurations for J-Link

Recommended extensions:
- `ms-vscode.cpptools`
- `marus25.cortex-debug`

Requirements:
- `arm-none-eabi-gcc` must be in `PATH`
- `JLinkExe` must be in `PATH` for flash/debug tasks

Suggested workflow:
1. Set the example in [`.vscode/settings.json`](.vscode/settings.json) (`ht5023.example`) if you do not want `blinky` — or use **HT5023: Select SDK Example** from the extension.
2. Run `HT5023: One-Click Flash` to build and program the board with the default settings.
3. Use `Run and Debug` with `HT5023: Debug With J-Link` or `HT5023: Attach To Running Target`.

## Arduino
Ordinary Arduino sketches and libraries run on this part. The hardware package
lives in `arduino/hardware/ht5023/ht5023`; install it into your sketchbook with:

```
python3 arduino/install.py
```

It asks `arduino-cli` where the sketchbook is instead of guessing -- on Windows
with OneDrive, Documents is redirected and the IDE reads
`~/OneDrive/Documents/Arduino`.

**Building `.ino` sketches needs `arduino-cli`.** The Makefile examples in
`examples/` do not. On Linux `./install-linux.sh` (step 4) downloads it into
`~/.local/bin`, verifies the release checksum and installs the core -- no sudo.
To do it by hand:

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh \
  | BINDIR=~/.local/bin sh
python3 arduino/install.py
arduino-cli board listall | grep ht5023      # HT5023 Board  ht5023:ht5023:ht5023
```

On Windows the Arduino IDE bundles `arduino-cli`, so installing the IDE is
enough; the Flasher finds the bundled copy on its own.

Two flash layouts, chosen under **Tools -> Flash layout**:

- **Standalone** -- linked at 0x0000, programmed over SWD with the J-Link.
- **UART bootloader** -- linked at 0x2000, uploaded over USB-UART on PC11/PC12
  through the resident bootloader (`make bootloader`, then flash `build/boot.bin`
  once over SWD).

The Flasher's Arduino row does the whole job: **New Sketch**, **Libraries…**
(install any Arduino library by name) and **Install Bootloader (J-Link, once)**
-- the only step the probe is needed for. After that, open a `.ino`, pick
`via: USB (bootloader)` and press **Compile & Upload**: it builds with
`arduino-cli`, programs the board over USB and opens the debug log, so
`Serial.print()` lands in the same window. There are two sketches to start from
in `sketches/`.

```
arduino-cli compile -b ht5023:ht5023:ht5023:layout=bootloader sketches/Blink
arduino-cli upload  -b ht5023:ht5023:ht5023:layout=bootloader -p /dev/ttyUSB0 sketches/Blink
# ...or -p COM11 on Windows
```

`Serial` is UART2 (PC11 = TX, PC12 = RX). The core provides the full `String`
class, `Print`/`Printable`/`Stream` with the parsing helpers, `Wire`, `SPI`,
`PROGMEM`, `pulseIn`, `tone` and the rest of the surface third-party libraries
expect. See [`arduino/README.md`](arduino/README.md) for the details, including
why `millis()` uses `HT5023_SYSTICK_HZ` rather than `F_CPU`.

## VS Code extension

```bash
./vscode-extension/install.sh      # then Ctrl+Shift+P -> Developer: Reload Window
```

Adds **HT5023:** commands and status-bar Build / Upload / Monitor buttons. It
recognises all three project shapes and runs the right thing for each:

| Workspace | Build | Upload |
|-----------|-------|--------|
| `platformio.ini` | `pio run` | `pio run -t upload` |
| `Makefile` + `sdk/` | `make build EXAMPLE=<picked>` | that, then the SDK flash sequence |
| a `.ino` | `arduino-cli compile` | that, then `ht5023_upload.py` |

There is also a packaged `vscode-extension/ht5023-1.0.0.vsix` if you would
rather install it the usual way (`code --install-extension`).

**[docs/vscode_manual.md](docs/vscode_manual.md) is the full manual** — setup,
the three workflows, using registry libraries, debugging over SWD, the board pin
reference and troubleshooting. [vscode-extension/README.md](vscode-extension/README.md)
lists every command and setting.

Plain VS Code works without the extension too — `.vscode/` ships `make` tasks
and cortex-debug launch configurations.

## PlatformIO

This is how you use libraries from the wider ecosystem. The platform in
`platformio/` builds Arduino-framework projects against the SDK's own core, so
`lib_deps` pulls from the PlatformIO registry and compiles against the HT5023.

```ini
[env:ht5023]
platform = /path/to/HT5023/platformio
board = ht5023
framework = arduino
monitor_speed = 115200

upload_protocol = jlink          ; or: bootloader (+ upload_port = /dev/ttyUSB0)

lib_deps =
    bblanchon/ArduinoJson@^7.0.0
    adafruit/Adafruit GFX Library@^1.11.9
```

```bash
pio run              # build
pio run -t upload    # program over SWD, or the bootloader
pio device monitor   # serial log
```

Both of those library examples are verified to compile and link on this part.
Any portable-C++ library should work; one whose manifest declares
`platforms = espressif32` (or similar) is skipped by PlatformIO's dependency
finder whatever its code does.

**`framework = arduino` is required** — it is what supplies `Arduino.h`, and
with it the ecosystem. The board declares 256 KiB flash and 32 KiB RAM, and
`upload_protocol = bootloader` automatically links the image at `0x2000` for
the resident bootloader.

Uploading does not use J-Link's `loadbin`: SEGGER ships no flash algorithm for
this part, so programming goes through the chip's own unlock/erase/program
registers in `tools/ht5023_flash_seq.py` — the one copy of that sequence, shared
with the Flasher GUI and the Arduino uploader.

## Notes
- `SystemInit()` disables WDT by default by shutting down LRC (configurable in `src/system_ht5023.c` and mirrored in `sdk/cores/ht5023/src/system_ht5023.c`).
- If you need RTC/LRC-dependent features, re-enable LRC/WDT behavior for those builds.
