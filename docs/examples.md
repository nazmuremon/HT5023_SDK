# Examples

All examples live in `examples/<name>/main.c` and are built from the repo root:

```bash
make EXAMPLE=<name>
```

Omitting `EXAMPLE` builds the default `blinky` example.

---

## blinky *(default)*

**Purpose:** Minimal LED toggle — the embedded "hello world".

Toggles both LEDs on **PE3** and **PE4** using raw GPIO writes and a
busy-wait delay loop. No SDK peripherals required beyond the GPIO helper.

```bash
make
```

**Wiring:** LEDs on PE3 and PE4 (board default; no external connections needed).

---

## led_dance

**Purpose:** Demonstrates more complex LED timing patterns.

Runs a choreographed sequence of bursts, slow pulses, fast strobes, accents,
and triplet rhythms on the same PE3 / PE4 LEDs as `blinky`.

```bash
make EXAMPLE=led_dance
```

**Wiring:** Same as `blinky` — PE3, PE4 LEDs.

---

## buzzer_music

**Purpose:** Passive buzzer melody with simultaneous LED dance.

Plays a multi-note sequence through a passive buzzer on **PA1** while the
LED dance pattern runs alongside it. Demonstrates `ht5023_buzzer_play_sequence`
and shows how to interleave two blocking animation loops.

```bash
make EXAMPLE=buzzer_music
```

**Wiring:**

| Signal | Pin |
|--------|-----|
| Buzzer+ | PA1 |
| Buzzer− | GND |
| LED A | PE3 |
| LED B | PE4 |

---

## button_press

**Purpose:** Read the user button, log the press, and acknowledge it on the LEDs.

Each press prints `Button pressed (count=N)` to the debug console and blinks the
PE3 / PE4 LEDs twice; releasing prints `Button released`. The pin is sampled with
a ~16 ms debounce so contact bounce cannot register as several presses.

```bash
make EXAMPLE=button_press
```

**Button wiring:** not fixed by the board support package — set it at the top of
`examples/button_press/main.c`:

| Define | Default | Meaning |
|--------|---------|---------|
| `BUTTON_PORT_BASE` | `0x40011400` (GPIOE) | Port base: PA `0x40011000`, PB `0x40011100`, PC `0x40011200`, PD `0x40011300`, PE `0x40011400` |
| `BUTTON_ACTIVE_LOW` | `1` | `1` = button shorts the pin to GND (internal pull-up used), `0` = button drives it high |
| `BUTTON_PIN` | `5` (PE5) | Bit number within the port |
| `BUTTON_PULLUP` | follows `BUTTON_ACTIVE_LOW` | Internal pull-up (`PTUP` bit **cleared** enables it on this part) |

If the log says the button reads as held at boot, `BUTTON_PIN` or
`BUTTON_ACTIVE_LOW` does not match your board.

**Finding the pin:** `BUTTON_SCAN` (default `1`) watches every other GPIO input
and reports the one that changes, with the defines to paste in:

```
PIN CHANGE: PC5 high -> low   <- set BUTTON_PORT_BASE 0x40011200u, BUTTON_PIN 5u, BUTTON_ACTIVE_LOW 1
```

Press the button, note the line, update the defines, then set `BUTTON_SCAN` to
`0`. The scan enables internal pull-ups on the pins it watches so unconnected
ones read a steady high, and skips the SWD link (`PB13`/`PB15`), UART2
(`PC11`/`PC12`), the LEDs (`PE3`/`PE4`), outputs, and pins in peripheral mode.

**Where the output appears:** both debug channels carry it, so the Flasher's
debug log shows it on either source:

- **Serial** — UART2 on `PC11` = TX2 at `9600 8N1` (USB-TTL adapter).
- **Semihosting** — over the J-Link SWD cable, no extra wiring.

Semihosting stays off unless a debugger opts in: the example only issues
`bkpt 0xAB` once the Flasher's monitor sets `g_semihost_enable` at `main()`. A
plain flash-and-run therefore logs over UART2 only and can never stall on an
unserviced semihosting trap.

Note that the flag is the *only* gate. Do not add a `DHCSR.C_DEBUGEN`
(`0xE000EDF0`) "is a debugger attached" test: the ARMv6-M Debug Control Block is
documented as debugger-access-only, and a software read of it on this part is
unreliable — measured returning `0` while the debugger simultaneously read
`0x00030003`, which silently disables semihosting for good.

Expected console:

```
HT5023 button demo ready.
Press the user button -> this console logs it and the LEDs blink twice.
Button pressed (count=1)
Button released
Button pressed (count=2)
Button released
```

---

## uart

**Purpose:** UART echo and heartbeat — the serial "hello world".

Initialises UART2 at 9600 8N1, prints a startup message, then loops:
echoes received bytes back to the sender and periodically prints a heartbeat
counter.

```bash
make EXAMPLE=uart
```

**Wiring:** PC11 → TX (host RX), PC12 ← RX (host TX). Open a terminal at
9600 8N1.

---

## usb_echo

**Purpose:** Answer the question "is my USB serial link working?" with no
configuration to get wrong.

UART2 only, 115200 8N1, no semihosting, no auto-detection, no link selection:

```
HT5023 usb_echo ready on UART2 (PC11=TX2, PC12=RX2) @ 115200 8N1
type anything and press Enter
> hello usb
echo: hello usb
```

```bash
make EXAMPLE=usb_echo              # standalone, flash with the J-Link
make EXAMPLE=usb_echo BOOT_APP=1   # for the UART bootloader
```

**Wiring:** `PC11` = TX2 -> adapter RX, `PC12` = RX2 <- adapter TX, GND common.
If `ht5023_uart_init()` rejects the baud/clock pair the LEDs blink fast forever,
so even a total failure is visible. The LEDs also toggle on every byte received,
which tells you data is arriving before a line is even finished.

**Why it does not echo each character as you type.** This UART holds a single
byte with no FIFO, and transmitting one character takes a full character time —
during which the next incoming byte is overwritten. An early version echoed per
character and turned `hello usb` into `helo sb`. It now collects the whole line
before replying. The same reason is why `uart_at` ships with `UART_AT_ECHO 0`.

---

## text_back

**Purpose:** The simplest round-trip check for the UART link and the Flasher's
send box — whatever you type comes straight back, twice.

```
> hello
hello
hello
```

```bash
make EXAMPLE=text_back
```

**Link:** works over either channel, chosen by `TEXT_BACK_LINK` (default `3` =
auto):

- **Semihosting — J-Link cable only, no extra wiring.** Used automatically when
  the Flasher's monitor arms it. Set the log source to **Semihosting**, line
  ending **CRLF**, and type in the send box. Your keystrokes go through the GDB
  server's terminal channel to the target's `SYS_READC`.
- **UART2** — `PC11` = TX2 -> adapter RX, `PC12` = RX2 <- adapter TX, GND common,
  `115200 8N1`. Used when no debugger armed semihosting. Source **Serial**.

Semihosting reads block: while the target waits for a character it sits in the
debugger, so the LEDs pause between keystrokes. That is expected.

| Define | Default | Meaning |
|--------|---------|---------|
| `TEXT_BACK_LINK` | `3` | `3` = auto (semihosting when armed, else UART), `2` = semihosting only, `1` = UART only |
| `TEXT_BACK_PORT` | `1` | `1` = UART1 on `PC0`/`PC1`, `2` = UART2 on `PC11`/`PC12`, `0` = UART0 on `PC3`/`PC2` |
| `TEXT_BACK_BAUD` | `115200u` | Line speed |
| `TEXT_BACK_DATA_BITS` | `8` | Data bits (driver programs 8; other values rejected at compile time) |
| `TEXT_BACK_PARITY` | `'N'` | Parity (driver programs none) |
| `TEXT_BACK_STOP_BITS` | `1` | Stop bits (driver programs 1) |
| `TEXT_BACK_SYSCLK_HZ` | `HT5023_FSYS_HZ` | Fsys feeding the divider (9.196 MHz measured) |
| `TEXT_BACK_REPEAT` | `2u` | How many copies come back |
| `TEXT_BACK_ECHO` | `0` | Echo characters as typed (off, so the reply is exactly two copies) |
| `TEXT_BACK_NUMBERED` | `0` | `1: hello` / `2: hello` instead of plain copies |
| `TEXT_BACK_EOL` | `"
"` | Terminator on each reply line |
| `TEXT_BACK_LINE_MAX` | `128u` | Longest accepted line; longer reports `(line too long)` |
| `TEXT_BACK_LED` | `1` | Toggle LEDs per received byte |

A line is complete on `CR` or `LF`, so send with a line ending (Enter). Empty
lines are ignored. If `ht5023_uart_init()` rejects the baud/sysclk pair the LEDs
blink continuously instead of failing silently.

---

## uart1_selftest

**Purpose:** Prove a UART actually transmits and receives, with nothing attached
but a wire. Short `PC0` (TX1) to `PC1` (RX1), flash, and watch the debug log with
source **Semihosting**:

```
PTDAT sanity on PC0: latch parked at 0, input+pull-up reads 1  -> PTDAT reflects the pad
PC0 drives -> PC1 senses (pull-up on)   low: drv=0 sense=0   high: drv=1 sense=1   -> LINKED
continuity: PASS (the pins are connected)
UART1 loopback self-test: UART1 PC0=TX1 PC1=RX1 @ 115200 8N1
[1] sent "AT" -> got "AT"  PASS
byte sweep 0x00..0xFF: 256/256 returned correctly
RESULT: UART1 PASS -- TX1 and RX1 both work
```

```bash
make EXAMPLE=uart1_selftest
```

It runs three checks, in order, so a failure tells you *where* the problem is:

1. **PTDAT sanity** — parks the output latch at 0, then reads the pin as an input
   with its pull-up on. Reading 1 proves `PTDAT` reports the pad and not a stale
   latch, so the continuity result below can be trusted.
2. **Continuity** — drives each pin in turn as plain GPIO and watches the other,
   with the pull-up on and off. `not linked` means the wire is the problem, not
   the UART, and no amount of baud fiddling will help.
3. **Loopback** — echoes three strings and every byte value 0x00..0xFF.

Note the receiver holds a single byte, so the test reads each character back
before sending the next; blasting a whole string first leaves only its last byte.

| Define | Default | Meaning |
|--------|---------|---------|
| `SELFTEST_PORT` | `1` | `1` = UART1 `PC0`/`PC1`, `2` = UART2 `PC11`/`PC12`, `0` = UART0 `PC3`/`PC2` |
| `SELFTEST_BAUD` | `115200u` | Line speed |
| `SELFTEST_SYSCLK_HZ` | `HT5023_FSYS_HZ` | Fsys feeding the divider (9.196 MHz measured) |
| `SELFTEST_TIMEOUT_MS` | `200u` | Per-byte echo timeout |
| `SELFTEST_SCAN` | `0` | Pin-pair short scanner — off, it false-positives on floating pins |

---

## wisun_get

**Purpose:** Ask a Wi-SUN module for its configuration and log the answer — a
concrete end-to-end check that the MCU can talk to a real device.

Sends `wisun get wisun` on UART1 and prints every line that comes back:

```
> wisun get wisun
[wisun] wisun.network_name = "cyberwisun"
[wisun] wisun.network_size = small (1)
[wisun] wisun.tx_power = 20.0
[wisun] wisun.mac_address = 34:10:f4:ff:fe:a8:7a:1d
[wisun] wisun.device_type = FFN (0)
[wisun] >
(1481 bytes received)
```

```bash
make EXAMPLE=wisun_get
```

**Wiring:** `PC0` = TX1 -> module **RX**, `PC1` = RX1 <- module **TX**, GND
common, `115200 8N1`. TX and RX cross over — with them straight through the
module never hears the command and stays silent at every baud rate.

| Define | Default | Meaning |
|--------|---------|---------|
| `WISUN_CMD` | `"wisun get wisun "` | Command sent each round |
| `WISUN_EOL` | `"
"` | Terminator; some CLIs want `"
"` only |
| `WISUN_PORT` | `1` | `1` = UART1 `PC0`/`PC1`, `2` = UART2, `0` = UART0 |
| `WISUN_BAUD` | `115200u` | Module line speed |
| `WISUN_SYSCLK_HZ` | `HT5023_FSYS_HZ` | Fsys feeding the divider (9.196 MHz measured) |
| `WISUN_PERIOD_MS` | `5000u` | Repeat interval; `0` asks exactly once |
| `WISUN_BUF_MAX` | `2048u` | Capture buffer for one reply |
| `WISUN_TAG` | `"[wisun] "` | Prefix on module lines |

**Why it captures before printing.** This UART holds a single byte and has no
interrupt or DMA, so at 115200 a byte lands every ~87 µs — while one semihosting
write halts the core for milliseconds. Logging during reception therefore loses
most of the reply: an early version of this example printed as it read and
returned 6-15 byte fragments like `l_mask = --` instead of the full 1481 bytes.
The receive loop now tight-polls into RAM with no delays and no output, and
prints only once the module goes quiet. `uart_bridge` does the same per burst.

---

## uart_bridge

**Purpose:** Type commands at a device wired to UART1 and see its full reply —
the interactive counterpart to `wisun_get`.

```
> wisun get wisun
[dev] wisun get wisun
[dev] wisun.network_name = "cyberwisun"
[dev] wisun.mac_address = 34:10:f4:ff:fe:a8:7a:1d
[dev] >
(1495 bytes)
```

```bash
make EXAMPLE=uart_bridge
```

Set the Flasher's log source to **Semihosting**, press **Start Log**, then type
in the send box and press Enter. Typing travels over the J-Link, so the host side
needs no serial adapter.

**Wiring:** `PC0` = TX1 -> device **RX**, `PC1` = RX1 <- device **TX**, GND
common, `115200 8N1` by default. TX and RX cross over.

| Define | Default | Meaning |
|--------|---------|---------|
| `BRIDGE_DEV_PORT` | `1` | Device UART: `1` = UART1 `PC0`/`PC1`, `2` = UART2, `0` = UART0 |
| `BRIDGE_DEV_BAUD` | `115200u` | Device line speed — must match the module |
| `BRIDGE_HOST` | `1` | `1` = semihosting, `2` = UART2 adapter |
| `BRIDGE_DEV_EOL` | `"
"` | Appended to what you type; some devices want `"
"` |
| `BRIDGE_BURST_MAX` | `3072u` | Bytes of one reply buffered before printing |
| `BRIDGE_FIRST_WAIT_SPINS` | `400000u` | Patience for the first reply byte (~0.5 s) |
| `BRIDGE_IDLE_SPINS` | `40000u` | Silence that ends a reply (~50 ms) — raise if one reply splits in two |
| `BRIDGE_TURNAROUND_MS` | `0u` | Delay after sending, for devices that need to turn the line around |
| `BRIDGE_DEVICE_PEEK_EVERY` | `0u` | Poll the device while idle for unsolicited output; `0` = only after a command |
| `BRIDGE_ECHO_COMMAND` / `_SHOW_COUNT` | `1` / `1` | Echo `> command`; print `(N bytes)` |
| `BRIDGE_TAG` | `"[dev] "` | Prefix on device lines |

**Timing, and why it is built this way.** Two hardware facts shape the loop, and
both caused real data loss before being handled:

- Reading a typed character costs a semihosting trap of several milliseconds, so
  the host is polled in a tight loop with *nothing* between the reads. Putting
  other work between them loses keystrokes — an earlier version that polled the
  device in the same loop never received typed input at all.
- The UART holds a single byte with no interrupt or DMA (a byte arrives every
  ~87 µs at 115200) and one log write halts the core for milliseconds. So a reply
  is captured into RAM by a tight poll that never logs, and printed afterwards.
  Printing while receiving returned 6-15 byte fragments instead of 1.5 kB.
  The device's echo is drained byte-by-byte *while* the command is transmitted,
  for the same reason.

The trade-off: while waiting for you to type, the device is not being read
(unless `BRIDGE_DEVICE_PEEK_EVERY` is non-zero), so unsolicited output appears
after your next command. Setting `BRIDGE_HOST=2` and using a USB-serial adapter
removes that limitation, since UART reads are cheap polls.

If a reply arrives split across two prints, raise `BRIDGE_IDLE_SPINS`. If it is
truncated with a `BRIDGE_BURST_MAX` notice, raise that.

---

## uart_at

**Purpose:** Receive data over UART and reply — an AT-command target you can
type at from the Flasher's send box.

Every received line is reported back as `RX[n]: "..."`, then handled:

| Command | Reply |
|---------|-------|
| `AT` | `OK` |
| `AT+INFO` | `+INFO: HT5023 uart_at, UART2 PC11=TX2 PC12=RX2 @ 115200 8N1, ...` |
| `AT+HELP` | list of commands |
| `AT+BAUD?` | `+BAUD: 9600` |
| `AT+ECHO=0` / `=1` | turn character echo off/on |
| `AT+LED=0` / `=1` | LEDs off/on |
| `AT+BLINK` | blink the LEDs twice |
| anything else | `ERROR` |

```bash
make EXAMPLE=uart_at
```

**Wiring:** `PC11` = TX2 -> adapter RX, `PC12` = RX2 <- adapter TX, GND common,
`115200 8N1` (defaults).
**Clock accuracy matters at 115200.** The divider is `Fsys / (2 * (SREL + 1))`,
and the datasheet gives the HRC as 9.5 MHz — but on the development board it
measures 9.196 MHz. Assuming 9.5 MHz programs `SREL = 40`, which on the real
clock is 112146 baud, **-2.65 %**: marginal, and enough to garble long bursts.
`HT5023_FSYS_HZ` (9.196 MHz) programs `SREL = 39` instead, **-0.22 %**. At 9600
baud either value is within 0.1 %, so this only matters at high speeds. On a
different board, measure the HRC and override `HT5023_FSYS_HZ`.


**Configuration** — every parameter is a `#define` at the top of `main.c`:

| Define | Default | Meaning |
|--------|---------|---------|
| `UART_AT_PORT` | `1` | `1` = UART1 on `PC0`/`PC1`, `2` = UART2 on `PC11`/`PC12`, `0` = UART0 on `PC3`/`PC2` |
| `UART_AT_BAUD` | `115200u` | Line speed (divider is `Fsys / (2 * (SREL + 1))`) |
| `UART_AT_DATA_BITS` | `8` | Data bits (driver programs 8) |
| `UART_AT_PARITY` | `'N'` | Parity (driver programs none) |
| `UART_AT_STOP_BITS` | `1` | Stop bits (driver programs 1) |
| `UART_AT_SYSCLK_HZ` | `HT5023_FSYS_HZ` | Fsys feeding the divider (9.196 MHz measured) |
| `UART_AT_ENABLE_TX` / `_RX` | `true` | Direction enables |
| `UART_AT_ECHO` | `1` | Echo received characters (also `AT+ECHO=0|1`) |
| `UART_AT_EOL` | `"
"` | Terminator on the target's replies |
| `UART_AT_LINE_MAX` | `128u` | Longest accepted command line |
| `UART_AT_SHOW_RX` | `1` | Print `RX[n]: "..."` for each line |
| `UART_AT_SHOW_HEX` | `0` | Also dump each line as hex |
| `UART_AT_LED_ON_RX` | `1` | Toggle LEDs per received byte |
| `UART_AT_UARTCON_SET` / `_CLR` | `0` | Raw UARTCON frame bits (see below) |

**Frame format:** the driver configures 8N1. The hardware also has `STOPSEL`
(bit 8), `LENSEL` (bit 7), `PARITYSEL` (bits 6:5) and `PARITYEN` (bit 4) in
`UARTCON`, but this SDK does not name their encodings, so they are exposed as
raw bits via `UART_AT_UARTCON_SET` / `UART_AT_UARTCON_CLR`, applied right after
init. **Check the UART chapter of `HT5023.pdf` for the encodings** — they are not
verified here.

If `ht5023_uart_init()` rejects the baud/sysclk pair the divider is out of
range, and the example lights both LEDs and stops, so the failure is visible
without a working serial link.

---

## uart_ansi

**Purpose:** ANSI colour animation over UART (loader/progress bar style).

Sends ANSI escape sequences to draw and animate a coloured ASCII pattern in
a terminal. Demonstrates cursor control, colour codes, and CLS sequences.
Requires a terminal emulator that supports ANSI/VT100 (e.g. PuTTY, minicom,
screen, picocom).

```bash
make EXAMPLE=uart_ansi
```

**Wiring:** Same as `uart` — PC11 TX, PC12 RX, 9600 8N1.

---

## ansi_art

**Purpose:** Full-screen ANSI art animation — rainbow logo and comet field.

Displays a large multi-line `HT5023` ASCII-art logo where each row cycles
through a 6-hue rainbow palette independently, producing a shifting colour
wave. Below the logo, a comet field shows horizontal particles flying
left across four rows, each with a bright `>` head and a coloured `-` trail
that fades with distance. Comets spawn, cross the screen, and respawn with
randomised speed, colour, and timing.

The entire 13-row scene is overwritten in place each frame (cursor-home,
no full clear) to avoid flicker at 9600 baud.

```bash
make EXAMPLE=ansi_art
```

Override clock or baud at build time:

```bash
# EXTRA_CFLAGS, not CFLAGS: a command-line CFLAGS replaces every compiler flag.
make EXAMPLE=ansi_art EXTRA_CFLAGS="-DHT5023_FSYS_HZ=9196000 -DANSI_ART_BAUD=115200"
```

**Wiring:** PC11 TX only (RX not used). Connect to an ANSI/VT100 terminal
at 9600 8N1 with a dark background for best results.

---

## ansi_art_game

**Purpose:** ANSI terminal platformer mini-game (UART) with coloured ASCII tiles.

Renders a side-scrolling platformer level over UART2 with ground, platforms,
spikes, coins, and a finish flag. Move with `a`/`d`, jump with `w` (or space),
and restart with `r`. Uses only UART polling + ANSI escape sequences, so it
runs in common serial terminals (PuTTY, minicom, picocom, screen).

```bash
make EXAMPLE=ansi_art_game
```

**Wiring:** Same as `uart` — PC11 TX, PC12 RX, 9600 8N1. Use an ANSI-capable
terminal and send raw keystrokes (line buffering disabled if your terminal has
that option).

---

## uart_ringbuf

**Purpose:** Non-blocking UART receive with a ring buffer.

Drains the UART hardware FIFO into an `ht5023_ringbuf_t` each main loop
iteration, then processes buffered bytes. Prints receive statistics (byte
count, overrun count) periodically. Demonstrates the recommended ISR-safe
ring buffer pattern for bursty serial input.

```bash
make EXAMPLE=uart_ringbuf
```

**Wiring:** PC11 TX, PC12 RX, 9600 8N1.

---

## boot_banner

**Purpose:** Colored chip profile + live runtime status line.

Prints a full ANSI-coloured boot banner (chip name, memory, clock, peripherals,
features) on startup using `ht5023_boot_banner_print`, then refreshes a
single-line status display (`ht5023_boot_banner_print_runtime_line`) each
main loop iteration showing uptime, frame count, and received bytes.

```bash
make EXAMPLE=boot_banner
```

**Wiring:** PC11 TX, PC12 RX, 9600 8N1. Connect with an ANSI-capable terminal
for coloured output.

---

## wdt_monitor

**Purpose:** Watchdog timer configuration and feed loop.

Configures the WDT for a ~1-second timeout, prints the SET value and actual
timeout, then feeds the watchdog in the main loop while printing the live
counter value. Demonstrates `ht5023_wdt_configure_and_feed` and
`ht5023_wdt_feed`.

```bash
make EXAMPLE=wdt_monitor
```

**Wiring:** PC11 TX, PC12 RX, 9600 8N1.

---

## timer_poll

**Purpose:** Hardware timer polling without interrupts.

Initialises TMR0 as a periodic counter and polls the overflow flag in the main
loop to drive a 10 Hz heartbeat. Demonstrates `ht5023_timer_init` and
`ht5023_timer_poll_tick` without requiring an ISR.

```bash
make EXAMPLE=timer_poll
```

**Wiring:** PC11 TX, PC12 RX, 9600 8N1.

---

## reset_info

**Purpose:** Decode and log the reset cause on every boot.

Reads the RSTSTA register via `ht5023_reset_read`, prints which reset source
bits are set (POR, WDT, EXT, SOFT, etc.) over UART, then clears all flags.
Useful for diagnosing unexpected resets in production firmware.

```bash
make EXAMPLE=reset_info
```

**Wiring:** PC11 TX, PC12 RX, 9600 8N1.

---

## spi_loopback

**Purpose:** SPI master self-test with a MOSI→MISO loopback wire.

Transmits incrementing bytes and compares them against the received data.
Prints pass/fail results over UART. Requires a physical jumper connecting
**PC4 (MOSI) to PC5 (MISO)**.

```bash
make EXAMPLE=spi_loopback
```

**Wiring:**

| Signal | Pin | Note |
|--------|-----|------|
| MOSI | PC4 | Jumper to PC5 |
| MISO | PC5 | Jumper to PC4 |
| SCLK | PC6 | |
| CS | PC7 | |
| UART TX | PC11 | |
| UART RX | PC12 | |

---

## i2c_scan

**Purpose:** I2C bus scanner — detect all connected devices.

Attempts an I2C START + address byte for every 7-bit address 0x00–0x7F,
reporting each ACK over UART. Useful for identifying unknown device addresses
or verifying bus wiring.

```bash
make EXAMPLE=i2c_scan
```

**Wiring:**

| Signal | Pin |
|--------|-----|
| SCL | PC13 |
| SDA | PC14 |
| UART TX | PC11 |
| UART RX | PC12 |

Add 4.7 kΩ pull-up resistors on SCL and SDA to 3.3 V.

---

## systick_irq

**Purpose:** SysTick interrupt — tick counter and UART printout.

Configures SysTick for a 1 kHz interrupt, increments a `volatile` tick
counter in `SysTick_Handler`, and prints the counter value over UART every
second. Demonstrates `ht5023_systick_start` and the minimal ISR integration
pattern.

```bash
make EXAMPLE=systick_irq
```

**Wiring:** PC11 TX, PC12 RX, 9600 8N1.

---

## st7789_demo

**Purpose:** ST7789 LCD colour bars and animated box.

Initialises the 240×240 IPS panel and draws colour bars, then runs a
bouncing-box animation using direct `ht5023_st7789_fill_rect` calls.
Demonstrates the full LCD init sequence and pixel streaming.

```bash
make EXAMPLE=st7789_demo
```

**Wiring (3.3 V logic):**

| Signal | Pin |
|--------|-----|
| MOSI (SDA) | PC4 |
| MISO | PC5 (optional) |
| SCLK (SCL) | PC6 |
| CS | PC7 |
| DC | PA0 |
| RST | PA2 |

---

## st7789_ui_demo

**Purpose:** GFX layer showcase — primitives, text, sprites, framebuffer.

Uses the full display stack:
- GFX primitives: lines, rectangles, circles, triangles
- Text renderer: multi-scale coloured strings with the 5×7 bitmap font
- RGB565 sprite blit: a small sprite copied to the display
- Framebuffer with dirty-rect flush: off-screen draw then upload only the
  changed region

Uses the same wiring as `st7789_demo`.

```bash
make EXAMPLE=st7789_ui_demo
```

---

## st7789_ui_dma_demo

**Purpose:** Classy full-screen 320x240 ST7789 UI demo with DMA-backed SPI TX.

Draws a full-screen dashboard layout (animated main scene, telemetry card,
bottom analyzer panel, and visual test/status cards) while using the ST7789
driver's built-in HT5023 DMA transport. The default build targets a common
**240x320 ST7789 panel rotated to logical 320x240**. The driver falls back to
polling SPI if a DMA transfer times out.

```bash
make EXAMPLE=st7789_ui_dma_demo
```

Uses the same wiring as `st7789_demo`.

If your ST7789 panel needs different geometry/offsets, override the build
defines (for example `ST7789_DEMO_WIDTH`, `ST7789_DEMO_HEIGHT`,
`ST7789_DEMO_X_OFFSET`, `ST7789_DEMO_Y_OFFSET`) and keep the example's
rotation at 90° for the 320x240 layout.

---

## energy_meter_dashboard

**Purpose:** Dark 320x240 smart energy meter dashboard with DMA-backed ST7789 updates.

Drives a common 240x320 ST7789 panel rotated to logical 320x240 with simulated
smart-meter telemetry: live voltage, current, and power tiles; grid/solar
source chips; relay state; a load-history trend; small power-factor/frequency
dials; and demand/export status bars. The dynamic panels are drawn into the
RGB565 framebuffer and flushed through the ST7789 driver's DMA transport.

```bash
make EXAMPLE=energy_meter_dashboard
```

Uses the same wiring as `st7789_demo`.

---

## xpt2046_touch_demo

**Purpose:** XPT2046 resistive touch controller over SPI.

Polls the XPT2046 using `ht5023_xpt2046_read_point`, printing mapped screen
coordinates and raw ADC values over UART whenever a touch is detected.
Demonstrates the calibration fields and the optional PENIRQ fast-path.

```bash
make EXAMPLE=xpt2046_touch_demo
```

Override calibration at build time:

```bash
make EXAMPLE=xpt2046_touch_demo \
     CFLAGS="-DTOUCH_RAW_X_MIN=300 -DTOUCH_RAW_X_MAX=3800"
```

**Wiring:**

| Signal | Pin | Note |
|--------|-----|------|
| DIN (MOSI) | PC4 | |
| DOUT (MISO) | PC5 | |
| CLK | PC6 | |
| CS | PA3 | |
| PENIRQ | PA4 | Optional; active-low |
| UART TX | PC11 | |
| UART RX | PC12 | |

---

## random_sentence

**Purpose:** Fill the debug log with readable, always-different text — a quick
way to see that live logging works end to end.

Every second it assembles a sentence from small word tables (adjectives, nouns,
verbs, adverbs, moods) using one of four templates, picked with an xorshift32
PRNG seeded from the free-running SysTick counter, so each reset produces a
different run. The LEDs toggle once per sentence. No `printf`: the sentence is
built by appending into a fixed buffer, which keeps the image around 2.7 KB.

```bash
make EXAMPLE=random_sentence
```

**Wiring:** none beyond the LEDs for `Semihosting`; `PC11` = TX2 at `9600 8N1`
for `Serial`.

**Where the output appears:** both debug channels carry it, so the Flasher's
debug log shows it on either source (semihosting is gated behind
`g_semihost_enable` exactly as in `button_press`, so a plain flash-and-run logs
over UART2 only). The Flasher arms that flag — and the older examples'
`g_force_semihost` — automatically when its monitor attaches.

Expected console:

```
HT5023 random sentences -- a new one every second.
[1] Every reset, the GPIO pin reboots the boot loader while the LEDs blink.
[2] The quiet watchdog counts the polite GPIO pin.
[3] Somehow, the linker script counts the GPIO pin while the LEDs blink.
[4] The oscillator reboots in half a clock cycle, says the rusty ring buffer.
```

**Tuning:** `SENTENCE_PERIOD_MS` (default `1000`) sets the interval; add words to
any of the tables and the templates pick them up automatically.

---

## semihosting

ARM **semihosting** demo: prints a banner and ten `tick=N` lines over the
debugger channel using `bkpt 0xAB` / `SYS_WRITE0` — **no UART wiring required**.
Output travels through the J-Link to the host, so this is handy when you have a
debug probe but no USB-serial adapter.

```bash
make EXAMPLE=semihosting
```

**How to view the output:**

- Easiest: the GUI flasher (`HT5023 Flasher.bat`) → set the **DEBUG LOG** source
  to **Semihosting** and press **Compile & Upload**; the output streams live in
  the panel at the bottom (or press **Start Log** / `F6` on its own).
- Manual, with J-Link GDB Server + GDB:

  ```bash
  JLinkGDBServerCL -device Cortex-M0 -if SWD -speed 1000 -port 2331
  arm-none-eabi-gdb build/app.elf \
      -ex "target remote :2331" \
      -ex "monitor reset" \
      -ex "monitor semihosting enable" \
      -ex "monitor semihosting IOClient 3" \
      -ex "continue"
  ```

Expected console:

```
HT5023 semihosting: hello from the target!
HT5023 semihosting tick=0
...
HT5023 semihosting tick=9
HT5023 semihosting: done.
```

**Notes:**

- Semihosting works **only while a debugger is attached** — a `bkpt 0xAB` with no
  debugger present halts into a HardFault. This example guards every semihosting
  call behind a debugger-attached check (`DHCSR.C_DEBUGEN`), so flashing it
  standalone is safe: with no debugger it simply blinks the LEDs.
- No wiring beyond the SWD debug connection (`SWDIO=PB13`, `SWCLK=PB15`, `NRST`,
  `VTref=3.3V`, `GND`).

---

## semihost_rng

ARM **semihosting** random-number demo: an `xorshift32` PRNG (seeded from the
free-running SysTick counter) **continuously** prints pseudo-random values over
the debugger channel — no UART wiring. Same safe debugger-attached guard as
`semihosting` (blinks the LEDs when no debugger is present).

```bash
make EXAMPLE=semihost_rng
```

View it the same way as `semihosting` (GUI flasher debug log, or
JLinkGDBServer + GDB). It streams forever — press **Stop Monitor** (or Ctrl-C in
GDB) to end. Expected console:

```
HT5023 semihosting RNG: continuous (xorshift32) -- press Stop to end
rand[0] = 2899336751  (1..100: 52)
rand[1] = 333808711   (1..100: 12)
rand[2] = ...          (1..100: ..)
... (continues until stopped)
```

Note: it is a deterministic PRNG (no hardware RNG on this part). The SysTick
seed varies the sequence somewhat; for a repeatable sequence, set `rng_state` to
a fixed value in `main()`.

---

## Build options summary

| Variable | Default | Description |
|----------|---------|-------------|
| `EXAMPLE` | `blinky` | Example to build |
| `CFLAGS` | *(empty)* | Extra compiler flags (e.g. calibration defines) |

Flash and monitor (adjust port as needed):

```bash
make EXAMPLE=uart
# flash with your programmer, then:
picocom -b 9600 /dev/ttyUSB0
```
