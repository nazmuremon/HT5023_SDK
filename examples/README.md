# Examples

Build from the repo root with `make EXAMPLE=<name>`.

Available examples:
- `blinky` (default): toggles LEDs on `PE3` + `PE4`
- `led_dance`: LED pattern sequence ("dance") on `PE3` + `PE4`
- `buzzer_music`: passive buzzer melody on `PA1` with dual-LED dance on `PE3` + `PE4`
- `button_press`: user button -> logs `Button pressed` and blinks the LEDs twice; prints over UART2 *and* semihosting, so the Flasher's debug log shows it either way. Button pin is set by `BUTTON_PORT_BASE` / `BUTTON_PIN` / `BUTTON_ACTIVE_LOW` at the top of `main.c` (default `PE5`, active-low); `BUTTON_SCAN` (on by default) reports which pin actually changes when you press, so you can find it without the schematic.
- `st7789_demo`: ST7789 SPI LCD color bars + moving box (`PC4/PC6/PC7` SPI, `PA0=DC`, `PA2=RST`)
- `st7789_ui_demo`: ST7789 UI demo with graphics primitives, text/font, RGB565 sprite blit, and dirty-tile framebuffer flush
- `st7789_ui_dma_demo`: full-screen 320x240 ST7789 "cockpit" dashboard using the library's DMA transport, with animated scene, HUD, and visual test cards
- `dashboard_demo`: ST7789 240x240 animated dashboard using the library's DMA transport, with KPI cards, mode chips, dial gauges, trend graph, sparkline, and status bars
- `energy_meter_dashboard`: dark 320x240 smart energy meter UI using the library's DMA transport, with live VAC/AMP/W tiles, source/relay chips, load-quality trend, and demand/export bars
- `touch_ui_demo`: ST7789 + XPT2046 touch UI demo (draw on canvas, switch brush colors, clear canvas)
- `uart`: UART2 echo/heartbeat on `PC11=TX2`, `PC12=RX2` at `9600 8N1`
- `usb_echo`: **the link test** — no options at all. UART2 (`PC11`/`PC12`) at 115200 8N1, prints a banner and answers every line with `echo: <text>`. Flash this first when you want to know whether the USB serial link works
- `text_back`: simplest round-trip test — type a line, the MCU sends the same text back twice (`TEXT_BACK_REPEAT`). Works over semihosting (J-Link cable only, no wiring) or UART2, selected by `TEXT_BACK_LINK`; port, baud, sysclk, echo, numbering and line length are `#define`s at the top of `main.c`
- `uart1_selftest`: UART loopback self-test — short `PC0` (TX1) to `PC1` (RX1), flash it, and the target checks the wire (GPIO continuity), echoes three strings and sweeps all 256 byte values, reporting PASS/FAIL over semihosting. `SELFTEST_PORT` picks UART1/UART2/UART0
- `wisun_get`: sends `wisun get wisun` to a Wi-SUN module on UART1 (`PC0`=TX1, `PC1`=RX1) at 115200 8N1 and prints the whole reply to the logger as `[wisun]` lines; the command, baud, port and line ending are `#define`s
- `uart_bridge`: relays what you type to a device on UART1 (`PC0`=TX1, `PC1`=RX1) and prints its reply as `[dev]` lines — use it when the MCU sits between you and a module. Host link is semihosting (J-Link only) or UART2
- `uart_at`: receives UART data and answers AT commands (`AT`, `AT+INFO`, `AT+HELP`, `AT+ECHO=0|1`, `AT+BAUD?`, `AT+LED=0|1`, `AT+BLINK`); reports every received line as `RX[n]: "..."`. Defaults to 115200 8N1. Port, baud, sysclk, echo, line length, EOL and raw UARTCON frame bits are all `#define`s at the top of `main.c`. Pair it with the Flasher's send box.
- `uart_ansi`: UART2 ANSI color ASCII animation on `PC11=TX2`, `PC12=RX2` at `9600 8N1`
- `ansi_art`: UART2 ANSI art — rainbow HT5023 logo + animated comet field on `PC11=TX2` at `9600 8N1`
- `ansi_art_game`: UART2 ANSI platformer mini-game with `a/d` move + `w` jump on `PC11=TX2`, `PC12=RX2` at `9600 8N1`
- `boot_banner`: UART2 colored boot banner with chip metrics/features + runtime status line on `PC11=TX2`, `PC12=RX2`
- `wdt_monitor`: UART2 watchdog timeout/counter monitor + feed loop
- `timer_poll`: UART2 TMR0 periodic polling demo (no interrupts)
- `reset_info`: UART2 reset-cause (`RSTSTA`) decoder and clear demo
- `uart_ringbuf`: UART2 ring buffer receive/echo/stats demo
- `spi_loopback`: SPI master (`PC4..PC7`) loopback test + UART2 logs (jumper `PC4` to `PC5`)
- `i2c_scan`: I2C master scanner + UART2 logs (`PC13=SCL`, `PC14=SDA`)
- `systick_irq`: SysTick interrupt demo + UART2 tick printouts
- `xpt2046_touch_demo`: XPT2046 resistive touch polling over SPI + UART2 coordinate logs
- `semihosting`: ARM semihosting (`bkpt 0xAB` / `SYS_WRITE0`) "hello + tick" over the J-Link debug channel — no UART wiring; view in the Flasher's debug log (source `Semihosting`) or via JLinkGDBServer + gdb. Falls back to LED blink when no debugger is attached.
- `random_sentence`: builds a new random sentence every second from word tables (xorshift32 seeded from SysTick) and prints it over UART2 *and* semihosting, so the Flasher's debug log fills with readable, always-different lines; LEDs toggle per sentence
- `semihost_rng`: prints pseudo-random numbers (xorshift32, seeded from SysTick) over semihosting — same debugger-attached guard as `semihosting`.

Examples:
```bash
make
make EXAMPLE=led_dance
make EXAMPLE=buzzer_music
make EXAMPLE=button_press
make EXAMPLE=st7789_demo
make EXAMPLE=st7789_ui_demo
make EXAMPLE=st7789_ui_dma_demo
make EXAMPLE=dashboard_demo
make EXAMPLE=energy_meter_dashboard
make EXAMPLE=touch_ui_demo
make EXAMPLE=uart
make EXAMPLE=uart_at
make EXAMPLE=usb_echo
make EXAMPLE=text_back
make EXAMPLE=uart_bridge
make EXAMPLE=uart1_selftest
make EXAMPLE=wisun_get
make EXAMPLE=uart_ansi
make EXAMPLE=ansi_art
make EXAMPLE=ansi_art_game
make EXAMPLE=boot_banner
make EXAMPLE=wdt_monitor
make EXAMPLE=timer_poll
make EXAMPLE=reset_info
make EXAMPLE=uart_ringbuf
make EXAMPLE=spi_loopback
make EXAMPLE=i2c_scan
make EXAMPLE=systick_irq
make EXAMPLE=xpt2046_touch_demo
make EXAMPLE=semihosting
make EXAMPLE=semihost_rng
make EXAMPLE=random_sentence
```

`st7789_demo` wiring (3.3V logic):
- `PC4` -> `MOSI` (SDA)
- `PC6` -> `SCLK` (SCL)
- `PC7` -> `CS`
- `PA0` -> `DC`
- `PA2` -> `RST`
- `PC5` <- `MISO` (optional/not used by many ST7789 modules)
- `VCC`, `GND`, and panel backlight pin (`BL/LED`) per your module

`st7789_ui_demo` uses the same ST7789 wiring as `st7789_demo`.

`touch_ui_demo` wiring (shared SPI bus, 3.3V logic):
- ST7789 display:
  - `PC4` -> `MOSI` (SDA)
  - `PC6` -> `SCLK` (SCL)
  - `PC7` -> `CS`
  - `PA0` -> `DC`
  - `PA2` -> `RST`
- XPT2046 touch controller:
  - `PC4` -> `DIN` (MOSI)
  - `PC5` <- `DOUT` (MISO)
  - `PC6` -> `CLK`
  - `PA3` -> `CS`
  - `PA4` <- `PENIRQ` (optional; set `TOUCH_USE_PENIRQ=1` to use it)
- If touch coordinates are flipped or mirrored, tune `TOUCH_SWAP_XY`, `TOUCH_INVERT_X`, `TOUCH_INVERT_Y`, and `TOUCH_RAW_*` macros.

`xpt2046_touch_demo` wiring (3.3V logic):
- `PC4` -> `DIN` (MOSI)
- `PC5` <- `DOUT` (MISO)
- `PC6` -> `CLK`
- `PA3` -> `CS`
- `PA4` <- `PENIRQ` (optional; active-low)
- `PC11` -> UART2 TX for logs, `PC12` <- UART2 RX
