# Arduino support

Sketches and Arduino libraries run on the HT5023 through the hardware package
in `arduino/hardware/ht5023/ht5023`. Write ordinary Arduino code — `setup()`,
`loop()`, `Serial`, `String`, `Wire`, `SPI` — and build it from the Flasher, the
Arduino IDE, or `arduino-cli`.

## Install

```
python arduino/install.py
```

That copies the package into your Arduino sketchbook and prints where it went.
It asks `arduino-cli` for the sketchbook path rather than guessing, which
matters on Windows: with OneDrive turned on, Documents is redirected and the
IDE reads `~/OneDrive/Documents/Arduino`, not `~/Documents/Arduino`. Restart the
IDE afterwards and pick **Tools → Board → HT5023 Board**.

`python arduino/install.py --where` prints the destination without installing.

## Two flash layouts

**Tools → Flash layout** decides where the sketch is linked and how it is
uploaded:

| Layout | Linked at | Uploaded by | When |
| --- | --- | --- | --- |
| Standalone | 0x0000 | J-Link over SWD | The sketch owns the whole part |
| UART bootloader | 0x2000 | USB-UART on PC11/PC12 | No debug probe, just a USB cable |

The bootloader layout needs the resident bootloader programmed once over SWD.
Press **Install Bootloader (J-Link, once)** in the Flasher, or by hand:

```
make bootloader          # builds build/boot.bin
```

and flash `build/boot.bin` at 0x0000. After that every sketch goes over USB —
press the board's reset button when the uploader asks, or wire DTR/RTS to RST
and let it do that for you. The part has no factory ISP, which is why the first
program has to arrive through the probe.

Choosing Standalone and uploading over SWD **overwrites the bootloader**, since
the sketch then starts at 0x0000.

## From the Flasher

The Arduino row across the top has everything a sketch needs:

- **New Sketch** — creates `sketches/<Name>/<Name>.ino` from a template and
  opens it. (arduino-cli compiles a *folder*, and the folder has to carry the
  sketch's name, so this gets the layout right for you.)
- **Libraries…** — lists what is installed and installs more by name, through
  `arduino-cli`. They are the same libraries the Arduino IDE uses.
- **Install Bootloader (J-Link, once)** — builds the UART bootloader and
  programs it at 0x0000. This is the one and only step that needs the debug
  probe; afterwards the board is programmed over the USB cable alone.
- **adapter resets the board (DTR/RTS → RST)** — tick this if your adapter's
  DTR or RTS line is wired to the board's reset pin, and uploads stop asking
  you to press the button.

Then choose **J-Link (SWD)** or **USB (bootloader)** in the toolbar and press
**Compile & Upload**. The Flasher picks the matching flash layout, calls
`arduino-cli`, programs the board and opens the debug log, so `Serial.print()`
output appears in the same window and the send box types back to the sketch.

A USB upload leaves the sketch parked in the bootloader until the debug log has
the port, and only then starts it — otherwise everything printed in `setup()`,
usually the one banner a sketch ever prints, is gone before anything is
listening.

`arduino-cli` comes bundled with the Arduino IDE; the Flasher finds it there and
installs the core itself the first time if the board is not registered yet.

## From the command line

```
arduino-cli compile -b ht5023:ht5023:ht5023:layout=bootloader sketches/Blink
arduino-cli upload  -b ht5023:ht5023:ht5023:layout=bootloader -p COM11 sketches/Blink
```

Swap `layout=standalone` to program over SWD (no `-p` needed).

## What the core provides

`Serial` is UART2 on **PC11 (TX)** and **PC12 (RX)** — the pins a USB-UART
adapter uses. `Serial0` is UART0 on PC3/PC2 if you need a second port.

- digital and timing: `pinMode`, `digitalWrite`, `digitalRead`, `digitalToggle`,
  `millis`, `micros`, `delay`, `delayMicroseconds`, `pulseIn`, `tone`, `noTone`
- data: the full `String` class, `Print`, `Printable`, `Stream` with
  `parseInt`/`parseFloat`/`find`/`readStringUntil`, `F()`, `PROGMEM`,
  `isDigit`/`isAlpha`/…, `dtostrf`, `random`, `map`, `shiftIn`/`shiftOut`
- buses: `Wire` (I2C) and `SPI`, plus the repo's own modules through
  `#include <HT5023SDK.h>`

`analogRead`/`analogWrite` are stubs: the ADC and PWM are not wired into the
core yet, so `analogWrite` falls back to on/off on the pin.

## Third-party libraries

Ordinary Arduino libraries install and build normally -- `arduino-cli lib
install "<name>"`, or Library Manager in the IDE. Verified compiling against
this core:

| Library | Notes |
| --- | --- |
| Adafruit GFX + SSD1306 | fonts and bitmaps through PROGMEM, I2C through `Wire` |
| Adafruit BusIO | the transport layer under most Adafruit sensor drivers |
| ArduinoJson | heavy C++; pulls in the standard headers |
| OneWire | uses the direct port-access macros |
| Adafruit NeoPixel | builds, but see the timing caveat below |
| DHT sensor library | |
| LiquidCrystal | |

What will not build, and why:

- Libraries that need per-architecture hardware timers reject this core
  outright -- `Servo` stops at its own `#error` listing the cores it supports.
  Anything driving PWM or capture the same way will do likewise.
- Bit-banged protocols with cycle-counted inner loops (NeoPixel's 800 kHz
  stream is the usual example) compile but will not meet their timing: they
  carry hand-written delays per architecture and have none for this part, which
  runs at about 4.6 MHz.
- `analogRead` returns 0 and `analogWrite` falls back to on/off, so a library
  built around the ADC or real PWM will compile and then do nothing useful.

## Timing

The part runs two clocks. `F_CPU` is **Fsys, 9.196 MHz** as measured on the
board (the datasheet says 9.5 MHz) — the clock `Serial`, `Wire` and `SPI`
dividers count. `millis()` and friends are driven by SysTick, which counts
**Fcpu = Fsys / 2 = 4.598 MHz**; the core derives `HT5023_SYSTICK_HZ` from
`F_CPU` for that. Measured against a host clock, `millis()` tracks real time to
within 0.01 %.

If your part is trimmed differently, measure it and override `F_CPU` — both
clocks follow:

```
arduino-cli compile --build-property "build.f_cpu=9250000L" ...
```

## Bundled sketches

- `Basics → Blink`
- `Communication → SerialEcho`
- `Simulation → ThreeBodyProblem`
- `HT5023SDK → SDKDiagnostics`

and in the repo itself, `sketches/Blink` and `sketches/SerialCommand` — the
latter is a small command interpreter that exercises `String`, `Stream` and
`Print` the way a third-party library would.

## A note on this UART

The receiver holds a single byte and has no FIFO. Printing while characters are
still arriving loses them, so read a whole line first and answer afterwards —
`sketches/SerialCommand` shows the pattern. Echoing each character as it is
typed is the usual way to lose every second one.
