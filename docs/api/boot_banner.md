# Boot Banner API

**Header:** `#include "ht5023/boot_banner.h"`
**Source:** `lib/ht5023_boot_banner/src/ht5023_boot_banner.c`

---

## Overview

Prints a formatted, optionally ANSI-coloured chip profile and runtime status
to any character output stream. The library has no I/O dependency of its own —
output is routed through a caller-supplied `putc` callback, making it usable
with UART, USB-CDC, a framebuffer terminal, or any other sink.

Two functions are provided:

- `ht5023_boot_banner_print` — full chip profile + initial runtime snapshot
  (typically called once at startup).
- `ht5023_boot_banner_print_runtime_line` — compact one-line status update with
  a spinning progress indicator (typically called in the main loop to show
  liveness).

---

## Callback

```c
typedef void (*ht5023_banner_putc_fn)(char c, void* ctx);
```

The driver calls this once per character. `ctx` is an opaque pointer forwarded
unchanged from the call site.

---

## Types

### `ht5023_banner_chip_info_t`

Static chip description — fill once at startup and pass by pointer.

```c
typedef struct {
    const char* chip_name;          // e.g. "HT5023"
    const char* core_name;          // e.g. "Cortex-M0"
    uint32_t    flash_kib;          // main flash in KiB (e.g. 256)
    uint32_t    info_flash_kib;     // info/data flash in KiB (e.g. 1)
    uint32_t    sram_kib;           // SRAM in KiB (e.g. 32)
    uint32_t    sysclk_hz_nominal;  // configured system clock in Hz
    uint8_t     uart_ports;         // number of UART peripherals (e.g. 6)
    bool        has_spi;
    bool        has_i2c;
    bool        has_rtc;
    bool        has_lcd;
    bool        has_dma;
} ht5023_banner_chip_info_t;
```

### `ht5023_banner_runtime_t`

Live application state — update fields before each call.

```c
typedef struct {
    const char* app_name;     // application identifier string
    const char* version;      // version string, e.g. "v1.2"
    const char* uart_path;    // console description, e.g. "UART2 PC11/PC12 @ 9600"
    uint32_t    uptime_ms;    // milliseconds since boot
    uint32_t    frames;       // main-loop iteration count (used for spinner)
    uint32_t    rx_bytes;     // total bytes received on the console
    uint32_t    resets_seen;  // count of detected resets since boot
    bool        ansi_enabled; // emit ANSI escape sequences when true
} ht5023_banner_runtime_t;
```

---

## Functions

### `ht5023_boot_banner_print`

```c
void ht5023_boot_banner_print(
    ht5023_banner_putc_fn            putc_fn,
    void*                            ctx,
    const ht5023_banner_chip_info_t* chip,
    const ht5023_banner_runtime_t*   rt);
```

Emit the full boot banner:

```
========================================
  HT5023 BOOT BANNER / SYSTEM PROFILE
========================================
app: boot_banner
version: v1
chip: HT5023
core: Cortex-M0

[MEMORY]
flash: 256 KiB
info_flash: 1 KiB
sram: 32 KiB

[CLOCK / IO]
sysclk_nominal: 9196000 Hz
uart_ports: 6
console: UART2 PC11/PC12 @ 9600

[FEATURES]
spi: yes  i2c: yes  rtc: no
lcd: yes  dma: no

[RUNTIME]
uptime: 0 ms
frames: 0
rx_bytes: 0
resets_seen: 0
```

When `ansi_enabled` is `true`, headings and values are coloured with ANSI
escape codes (cyan keys, white values, green section headers). Returns
immediately if any required pointer is NULL.

---

### `ht5023_boot_banner_print_runtime_line`

```c
void ht5023_boot_banner_print_runtime_line(
    ht5023_banner_putc_fn          putc_fn,
    void*                          ctx,
    const ht5023_banner_runtime_t* rt);
```

Print a single status line terminated with `\r` (overwrites the current
terminal line). The `frames` field drives a four-state spinner (`| / - \`).

```
[|] uptime=1024ms  frames=512  rx=0B  uart=UART2 PC11/PC12 @ 9600
```

Call this at the end of every main-loop iteration to produce a live updating
display without scrolling.

---

## Example

```c
#include "ht5023/boot_banner.h"
#include "ht5023/uart.h"

static void putc_uart(char c, void* ctx) {
    (void)ctx;
    ht5023_uart_write_byte(HT5023_UART_ID_2, (uint8_t)c);
}

static const ht5023_banner_chip_info_t chip = {
    .chip_name          = "HT5023",
    .core_name          = "Cortex-M0",
    .flash_kib          = 256,
    .info_flash_kib     = 1,
    .sram_kib           = 32,
    .sysclk_hz_nominal  = HT5023_FSYS_HZ,
    .uart_ports         = 6,
    .has_spi = true, .has_i2c = true, .has_lcd = true,
};

ht5023_banner_runtime_t rt = {
    .app_name      = "my_app",
    .version       = "v1.0",
    .uart_path     = "UART2 PC11/PC12 @ 9600",
    .ansi_enabled  = true,
};

// On startup — print full profile
ht5023_boot_banner_print(putc_uart, NULL, &chip, &rt);

// In main loop — update live status line
while (1) {
    rt.uptime_ms = get_uptime_ms();
    rt.frames++;
    ht5023_boot_banner_print_runtime_line(putc_uart, NULL, &rt);
}
```

See also: `examples/boot_banner/`
