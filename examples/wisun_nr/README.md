# wisun_nr — Wi-SUN FAN 1.1 node on an EBYTE E51

Firmware for the HT5023 that drives an **EBYTE E51 running the factory "Wi-SUN
CLI Application" (router/node firmware)** over UART. With no human input it:

1. syncs with the module and confirms it runs NR (node) firmware;
2. checks the network settings and fixes them only if they differ;
3. joins the FAN 1.1 network and waits for Operational with a global IPv6;
4. discovers the border router's address and pings it;
5. opens a TCP server for the BR's return connection, then connects to the BR;
6. exchanges framed messages (CRC, ACK, retry, fragmentation, heartbeat);
7. detects link loss and reconnects or rejoins on its own.

```bash
make EXAMPLE=wisun_nr                                  # build
python3 tools/ht5023_flash_seq.py build/app.bin        # program over SWD
make -C examples/wisun_nr/test                         # host test suite
```

From VS Code: **HT5023: Select SDK Example → wisun_nr**, then **Upload**.

---

## Wiring

| HT5023 | | E51 (NR) | |
|---|---|---|---|
| `PC5` (TX5) | → | RXD | 3.3 V logic on both sides — no level shifter needed |
| `PC4` (RX5) | ← | TXD | |
| GND | — | GND | common ground is mandatory |
| *(optional GPIO)* | → | reset | disabled by default; see below |

Module UART: UART5, 115200 8N1, no flow control.

`PC4`/`PC5` are also `SPI_MOSI`/`SPI_MISO`; UART5 is their second mux function
(`IOCFG=1`, `AFCFG=1`), so the SPI bus cannot be used alongside this firmware.
To wire the module to UART1 (`PC0` TX1 / `PC1` RX1) instead, build with
`EXTRA_CFLAGS="-DWISUN_MODULE_UART=1"`.

**Debug log and console:** UART2, `PC11` (TX) / `PC12` (RX), 115200 8N1 — the
same USB-UART adapter the Flasher uses.

**Reset pin:** `WISUN_RESET_ENABLED` is `0`. Before enabling it, check the
reset pin name and its active level in the EBYTE E51 manual and set
`WISUN_RESET_PORT`, `_PIN` and `_ACTIVE_LOW` to match. Nothing here assumes
them. The firmware never needs the pin — it is only used to recover a module
that stops answering entirely.

---

## Configuration

Every tunable is in [`wisun_config.h`](wisun_config.h): UARTs, clock, reset pin,
the four network parameters and the TCP port, every timeout, and every buffer
size. All of them can be overridden without editing the file:

```bash
make EXAMPLE=wisun_nr EXTRA_CFLAGS="-DWISUN_LOG_UART_TRAFFIC=0"
```

| Network parameter | Default | Must equal the BR's |
|---|---|---|
| `WISUN_NETWORK_NAME` | `Wi-SUN Network` | yes |
| `WISUN_REGULATORY_DOMAIN` | `NA` — must be legal where deployed | yes |
| `WISUN_CHAN_PLAN_ID` | `1` | yes |
| `WISUN_PHY_MODE_ID` | `2` | yes |
| `WISUN_APP_PORT` | `9000` | yes |

`WISUN_APPLY_CONFIG` (default `1`) lets the firmware set and save module
settings that differ from these. Build with `0` to inspect a module that may
belong to another network: it logs the difference and stays in `ERROR` without
sending any `wisun set` or `wisun save`.

### The clock matters — `WISUN_SYSCLK_HZ`

The datasheet gives the HRC as 9.5 MHz. **On this board it measures
9.19 MHz**, and at 115200 baud that is the difference between −2.65 % error and
a measured −0.4 … −1.1 %. `WISUN_SYSCLK_HZ` defaults to the SDK-wide
`HT5023_FSYS_HZ` (`ht5023/core/system.h`), which every example now shares.

How it was measured: SysTick's reload was read back over SWD and the firmware's
millisecond counter timed against wall-clock time in a single J-Link session.
Two independent firmwares agree (Fcpu 4.5968 and 4.5974 MHz), and so does the
4.598 MHz the Arduino core's `HT5023_SYSTICK_HZ` already records. After the
change the firmware's clock tracks wall time to −0.03 %.

The UART side was then measured directly, independent of SysTick: the time to
transmit 645 120 bytes in 8N1, 8N2 and 7N1 differs by exactly one bit per byte,
giving the true bit period. See `HT5023_FSYS_HZ` for how to override it on a
board that measures differently.

---

## Design

```
main.c            demo application and debug console
wisun_nr.c/.h     link manager: state machine, sockets, transport, app interface
wisun_cli.c/.h    CLI driver: line assembly, prompt detection, single-flight queue
wisun_events.c/.h async line -> typed event parser
wisun_frame.c/.h  frame codec, CRC, stream splitter, reassembly
wisun_util.c/.h   string and log helpers (no printf, no heap)
wisun_hal.h       the platform interface
hal_ht5023.c      the HT5023 implementation of it
wisun_config.h    every tunable
test/             host tests: simulated E51 + border router
```

Everything except `hal_ht5023.c` is portable C99 with no platform headers.

### Platform abstraction

| Spec name | Here | HT5023 implementation |
|---|---|---|
| `uart_init(baud)` | `hal_uart_init` | UART5 (IRQ 14) with RX and TX interrupts |
| `uart_write(bytes, len)` | `hal_uart_write` | queues into a 512-byte TX ring; never blocks |
| `uart_read(buf, max)` | `hal_uart_read` | drains the 4 KB interrupt-fed RX ring |
| `time_ms()` | `hal_time_ms` | SysTick at 1 kHz |
| `log(text)` | `hal_log` | 2 KB interrupt-driven TX ring on UART2; overflow is marked, not silent |
| `debug_console_read_line()` | `hal_console_read_line` | UART2 RX interrupt, 128-byte ring |
| `module_reset_pulse()` | `hal_module_reset_pulse` | starts the pulse; `hal_poll()` ends it |

To port to another MCU, write `hal_<platform>.c` implementing `wisun_hal.h`.
`test/hal_host.c` is a second implementation, which is a good template.

### Non-blocking

Nothing waits. Every state is a function called on each pass of the main loop;
every delay is a timestamp compared on a later pass. The only blocking code is
the SDK's UART init at start-up. Worst-case stack is about 2.1 KB, measured with
`-fstack-usage`.

### CLI driver

One command on the wire at a time. It completes when `> ` returns **after the
module echoed it or sent any response line** — so a stale prompt, such as one
trailing a command that already timed out, cannot complete the next command
instantly. `> ` at the start of a line counts even with no newline after it,
and whatever follows on that line (`> [Data available: 8,2]`) is parsed as a
fresh line.

Every line, whether or not a command is in flight, is offered to the event
parser, because async events arrive in the middle of other responses.

**`socket_read` payloads wrap every 64 characters.** Found on the hardware — the
specification assumed one line. A 79-byte read arrives as:

```
[Data from fd12:3456::3610:f4ff:fea8:7369 (36580): 11,79
$D,24320,42522D41434B3A4E52206461746120233120757074696D653D32313
833333739,C560;]
```

The driver joins payload lines up to the one ending in `]` (frames never
contain `]`), stopping early at the header's length, at the prompt or at the
next `[Data from` header, so a lost `]` cannot swallow the next reply. A peer
that keeps only the first line never sees a complete frame longer than 63
characters and never acknowledges it — the border router application had exactly
that bug. The simulator wraps the same way.

Command lines are not limited to the CLI's usual 128 bytes on the NR firmware:
a 162-character `socket_write` was echoed intact and answered
`[Wrote 140 bytes]`.

### State machine

Implements section 6 of the specification as written:

```
UART_SYNC -> PROBE -> CONFIG_CHECK -> [APPLY_CONFIG] -> CLEAN_SOCKETS
          -> (already Operational? -> GET_ADDRESSES)
          -> PREPARE_JOIN -> JOIN -> WAIT_OPERATIONAL -> GET_ADDRESSES -> VERIFY_PING
          -> OPEN_SERVER -> CONNECT_UPLINK -> WAIT_DOWNLINK -> READY
READY --(sockets lost, still Operational)--> CLOSE_SOCKETS -> VERIFY_PING
READY --(network lost)--> CLOSE_SOCKETS -> WAIT_OPERATIONAL (grace) -> PREPARE_JOIN
any   --(module banner)--> UART_SYNC
```

A generation counter, bumped on every state or step change, discards the result
of a command that belonged to a step already left. Unrecoverable-looking
conditions (BR firmware on the NR side, a listener that cannot bind twice)
enter `ERROR`, log every 5 s, and re-probe after 60 s rather than stopping.

**One deliberate deviation — the join timeout.** The specification times a join
out 360 s after it starts. On the hardware that threw away a join that had
reached state 4: the border router came up part-way through the attempt. The
360 s (`WISUN_JOIN_TIMEOUT_MS`) now counts from the last time the join state
reached a new high, and `WISUN_JOIN_MAX_MS` (900 s) caps an attempt that keeps
regressing. A join with no progress still gives up after 360 s.

**A second one — the listener is never closed.** The specification closes every
stale socket after an MCU reset and every socket on network loss. On the E51 a
closed listener's port cannot be bound again for about 4 minutes, so a reset
left the node in `ERROR` ("cannot bind TCP server") for 4 minutes. Clean-up now
closes the data connections only and adopts a `TCP server … (9000) (LISTENING)`
row; `OPEN_SERVER` runs `socket_list` first and reuses a listener it finds. After
an MCU reset the node is `READY` again in about 6 s.

**A third — the uplink retries back off.** The specification retries a failed
uplink after 10 s, and a border router that never connects back after 5 s. Each
attempt leaves a socket on the BR's module, which has room for about nine: with
the BR application closed, the node filled it in minutes and the module then
accepted nothing at all. Successive failures now wait twice as long, capped by
`WISUN_CONNECT_BACKOFF_MAX_MS` (120 s) — 13 attempts in 20 minutes instead of 34.

### Application interface

```c
bool nr_is_ready(void);
bool nr_send(const uint8_t* data, size_t len, uint32_t* msg_id);   // 1..1024 bytes
void nr_on_receive(nr_receive_fn cb);
void nr_on_delivery_result(nr_delivery_fn cb);   // (id, success, payload, len)
void nr_status(nr_status_t* out);
```

The delivery callback also receives the message payload, so the application can
report what was delivered.

Put real sensor data in `build_nr_payload()` in `main.c`.

### Debug console

Type into the log port and press Enter:

| Command | |
|---|---|
| `send <text>` | queue `<text>` as a message |
| `raw <cli command>` | run a raw E51 command; its reply is logged as `RAW<` |
| `status` | state, addresses, sockets, counters |
| `rejoin` | disconnect, then join again |
| `log all` / `log payload` | full diagnostics, or payloads only (the default at boot) |

### What the log port prints

By default (`WISUN_LOG_PAYLOAD_ONLY=1`) only application payloads — and replies
to commands you type — reach the log port:

```
TX: NR data #5 uptime=50004          sent, and acknowledged by the BR
RX: BR-ACK:NR data #5 uptime=50004   received from the BR
TX FAILED: hello                     no ACK after the retries
```

Binary payloads print as `hex:…`. Nothing is printed while the node syncs,
joins or connects, so use `status` to see where it is. For the full
`[NR] <ms>` diagnostic log — every state change, event and (with
`WISUN_LOG_UART_TRAFFIC`) module line — type `log all` on the console, or build
with `EXTRA_CFLAGS="-DWISUN_LOG_PAYLOAD_ONLY=0"` to start that way. `log payload`
switches back.

---

## Memory

Measured from the linked image:

| | Used | Of |
|---|---|---|
| Flash | 24.3 KB (24 870 B) | 256 KB (10 %) |
| Static RAM | 20.5 KB (21 012 B) | 32 KB (64 %) |
| Stack, worst case | ≈ 2.1 KB | 12.8 KB left |

The largest static buffers: application queue 8 × 1024 B, module RX ring 4 KB,
log TX ring 2 KB, CLI line 1.1 KB, socket_read payload 1 KB, reassembly 1 KB. No heap is used.

---

## Verification

### Host test suite — 138 checks, plus 8 with `WISUN_APPLY_CONFIG=0` and 8 with payload-only logging, all passing

`make -C examples/wisun_nr/test` compiles the portable sources for the PC, under
AddressSanitizer and UBSan, and drives them against `test/sim_e51.c`: a
simulated E51 and border router. The simulator delivers bytes at 115200 baud
through a 4 KB ring like the target's, echoes commands, prints `> ` without a
newline, splices async events into the middle of long responses, follows the
join timing from the specification, and implements the BR side of the frame
protocol using the firmware's own codec.

| Spec 9 criterion | Scenario | Result |
|---|---|---|
| 1 cold boot | factory settings on the module, BR running | READY in 155 s, one save, no BR address configured |
| 2 delivery | "hello BR" | arrives; success reported |
| 3 fragmentation | 300-byte message | 5 frames, intact |
| 4 MCU reset | re-init the firmware, module untouched | READY in 5 s, no rejoin, no save, stale sockets closed |
| 5 BR restart | BR off 2 minutes, back on | loss detected at 45 s, READY 155 s after return |
| 6 BR off at boot | 1 hour with no BR | 9 join attempts, back-off capped at 120 s, then joins |
| 7 settings | matching settings | never saved |

Plus: BR→NR delivery with a lost ACK (delivered once), a corrupted CRC
(dropped), a missing ACK (resent), ACKs exhausted twice (link reset), the BR
dropping both TCP connections (reconnect without rejoin), a module reboot
(banner → resync), BR firmware on the NR side (`ERROR`), a console `rejoin`
issued before the first join event (still disconnects), a 30-minute soak at the
demo's 10 s rate (180/180 delivered), and on every scenario: no `reboot`, every
command ≤ 180 characters, no spaces in any `socket_write`, and single-flight
never violated.

### On the HT5023

Programmed over SWD and inspected by reading RAM through the J-Link while the
firmware ran:

- the firmware starts, the state machine runs, and the extended vector table
  dispatches SysTick and UART interrupts;
- SysTick is correct to −0.03 % against wall-clock time;
- with no module answering, it stays in `UART_SYNC` and logs one sync failure
  every 15 s, exactly as configured;
- UART1 and UART2 have both interrupts enabled with clean status flags, and
  there is no interrupt storm.

Those checks were made when the module port was UART1. On UART5, with the log
read out of RAM over SWD:

- **UART5 loopback** (`PC5` jumpered to `PC4`, `examples/e51_scan`): all 256
  byte values plus text returned intact at 9600, 57600 and 115200.
- **A real E51 on `PC5`/`PC4`**: the firmware synced, received the complete
  ~2 KB `wisun get wisun` with no receive or line overflows, confirmed NR
  firmware, found the settings already matching (so saved nothing), closed no
  stale sockets, cleared the credential cache and started `join_fan11`. With no
  border router in range it stayed in Select PAN, timed out at 360 s,
  disconnected, backed off and started join attempt 2, as specified.

- **With a border router in range** (join attempt 6): join states 3 → 4 → 5
  in 110 s, NR and BR global addresses discovered from the module, ping answered,
  `Listening` → `Opened` → `Accepted <BR>` → `READY`. NR messages delivered and
  acknowledged; BR messages — including ones wrapped over two lines — delivered,
  de-duplicated and acknowledged; heartbeat answered.
- **MCU reset while Operational** (re-flash): no rejoin, no save, the listener
  kept, the data connections re-made — `READY` 6 s after flashing.
- **Long messages both ways** with the border router application: 120 bytes
  NR → BR (a 64-byte, 140-character fragment that the module wraps over three
  read lines, plus a final frame) and 300 bytes BR → NR, both delivered and
  acknowledged, with only `RX:`/`TX:` lines printed on either side.
- **Link loss**: `[Data available: N,0]`, and ACK retries exhausted twice,
  each closed both data sockets, re-pinged and reconnected without rejoining.
- **Border router application closed for half an hour**: the node kept its
  listener, retried the uplink with the back-off above, and returned to `READY`
  by itself once the application came back.

**Not verified on hardware:** BR power loss (criterion 5). The border router
application originally joined wrapped `socket_read` lines with a newline and so
rejected every NR frame longer than one line; it now joins them the same way as
this driver.

If the board's `PC4` header pin is not reaching the chip, the symptom is a
module that never answers: the sync failure repeats every 15 s, and
`e51_scan`'s pin probe reports `PC4` as floating.

---

## Changes this example made to the SDK

- **Peripheral interrupt vectors.** `sdk/cores/ht5023/src/startup_ht5023.s`
  only had core exceptions. It now has IRQ 0–31 as numbered in HT502X User
  Manual §8.1, each a weak alias for `Default_Handler`, so existing examples are
  unaffected.
- **Default handlers had no Thumb bit.** The core-exception defaults were plain
  labels, so their vectors held even addresses; taking one would lock the core
  up instead of reaching `Default_Handler`. They are now `.thumb_set` aliases.
- **Multi-file examples.** The Makefile builds every `.c` in the example folder,
  not only `main.c`.
- **Header dependencies.** The Makefile now passes `-MMD -MP`. Previously,
  editing a header such as `wisun_config.h` rebuilt nothing and relinked the old
  objects.

---

## Known limitations

- **UDP is not used.** The firmware offers it but it has not been tested on
  these modules; the specification requires TCP.
- **A message abandoned mid-transfer can corrupt the next one at the receiver.**
  If a sender gives up on a multi-fragment message after its fragments were
  received but every ACK was lost, the receiver still holds those fragments and
  the next message is appended to them. The frame format has no
  start-of-message marker to detect this, so it is inherent to the section 7
  protocol, which the BR implements as-is. It needs the same ACK to be lost four
  times in a row.
- **Accepted sockets are never written to.** Replies go out on the NR's own
  client connection, the only pattern verified on these modules.
