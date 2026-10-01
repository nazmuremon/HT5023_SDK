# Watchdog Timer API

**Header:** `#include "ht5023/wdt.h"`
**Source:** `lib/ht5023_wdt/src/ht5023_wdt.c`

---

## Overview

Helper for the HT5023 hardware watchdog. `SystemInit()` disables the WDT by
default — call `ht5023_wdt_configure_and_feed()` to enable it and set the
timeout.

The WDT resets the device if not fed before the timeout expires.

---

## Timeout formula

```
timeout_ms = 64 × (1 + SET[7:0])
```

| SET value | Timeout |
|-----------|---------|
| 0 | 64 ms |
| 15 | 1024 ms (~1 s) |
| 63 | 4096 ms (~4 s) |
| 127 | 8192 ms (~8 s) |
| 255 | 16 384 ms (~16.4 s) |

---

## Functions

### `ht5023_wdt_set_from_timeout_ms`

```c
uint8_t ht5023_wdt_set_from_timeout_ms(uint32_t timeout_ms);
```

Convert a desired timeout in milliseconds to the nearest achievable SET
value. Values below 64 ms return 0 (minimum timeout).

---

### `ht5023_wdt_timeout_ms_from_set`

```c
uint32_t ht5023_wdt_timeout_ms_from_set(uint8_t set_value);
```

Convert a SET register value back to the actual timeout in milliseconds.
Use to log or verify the configured timeout.

---

### `ht5023_wdt_configure_and_feed`

```c
void ht5023_wdt_configure_and_feed(uint8_t set_value);
```

Write the SET value and the feed key (`0xAA`) to the WDTCLR register in one
operation, simultaneously configuring the timeout and resetting the counter.
This **enables** the watchdog if it was previously disabled.

---

### `ht5023_wdt_feed`

```c
void ht5023_wdt_feed(void);
```

Feed the watchdog without changing the timeout. Reads the current SET value
and calls `ht5023_wdt_configure_and_feed()`. Call this regularly — more
frequently than your timeout period.

---

### `ht5023_wdt_get_set_value`

```c
uint8_t ht5023_wdt_get_set_value(void);
```

Read the currently configured SET value from hardware.

---

### `ht5023_wdt_get_counter`

```c
uint16_t ht5023_wdt_get_counter(void);
```

Read the current WDT counter value from WDTCNT.

---

## Example

```c
#include "ht5023/wdt.h"

// Configure for ~1 second timeout and enable
uint8_t setv = ht5023_wdt_set_from_timeout_ms(1024);
ht5023_wdt_configure_and_feed(setv);

while (1) {
    do_work();           // must complete before timeout
    ht5023_wdt_feed();   // reset countdown
}
```

> **Hardware strap.** On some HT5023 boards the JTAGWDTEN pin controls
> whether the WDT can be re-enabled after `SystemInit()` disables it.
> Check your hardware schematics if the WDT appears to have no effect.

See also: `examples/wdt_monitor/`
