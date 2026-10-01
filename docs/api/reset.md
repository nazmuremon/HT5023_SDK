# Reset Status API

**Header:** `#include "ht5023/reset.h"`
**Source:** `lib/ht5023_reset/src/ht5023_reset.c`

---

## Overview

Read and clear the RSTSTA register to determine what caused the last reset.
This is useful for diagnostics: log the reset cause on startup, then clear
the flags so the next boot can distinguish fresh resets.

---

## Reset sources

| Flag | Meaning |
|------|---------|
| `por_reset` | Power-on reset |
| `lbor_reset` | Low-voltage brownout reset |
| `wdt_reset` | Watchdog timeout |
| `wakeup_reset` | Wakeup from deep sleep triggered a reset |
| `ext_reset` | External reset pin asserted |
| `soft_reset` | Software-triggered reset |
| `debug_reset` | Reset via debug interface |
| `bor_reset` | Brownout reset |
| `sleep_wakeup` | Wakeup from sleep mode (flag, not a reset) |
| `hold_wakeup` | Wakeup from hold mode (flag, not a reset) |
| `flashwr_lbor_reset` | Flash write / LBOR combined flag |

---

## Types

### `ht5023_reset_status_t`

```c
typedef struct {
    uint16_t raw;             // raw RSTSTA register value
    bool por_reset;
    bool lbor_reset;
    bool wdt_reset;
    bool wakeup_reset;
    bool ext_reset;
    bool soft_reset;
    bool debug_reset;
    bool bor_reset;
    bool sleep_wakeup;
    bool hold_wakeup;
    bool flashwr_lbor_reset;
} ht5023_reset_status_t;
```

---

## Functions

### `ht5023_reset_read_raw`

```c
uint16_t ht5023_reset_read_raw(void);
```

Read the raw RSTSTA register value.

---

### `ht5023_reset_read`

```c
ht5023_reset_status_t ht5023_reset_read(void);
```

Read and parse RSTSTA into a `ht5023_reset_status_t` struct with individual
boolean flags for each source.

---

### `ht5023_reset_clear_flags`

```c
void ht5023_reset_clear_flags(uint16_t clear_mask);
```

Clear the flags specified by `clear_mask`. Bits in RSTSTA are cleared by
writing 0 (write 1 to preserve a flag).

To clear all known flags:

```c
ht5023_reset_clear_flags(HT5023_RSTSTA_KNOWN_FLAGS_MASK);
```

To clear only the WDT flag:

```c
ht5023_reset_clear_flags(HT5023_RSTSTA_WDTRST_BIT);
```

---

## Example — log reset cause on boot

```c
#include "ht5023/reset.h"
#include "ht5023/uart.h"

void log_reset_cause(void) {
    ht5023_reset_status_t s = ht5023_reset_read();
    if (s.por_reset)   ht5023_uart_write_str(HT5023_UART_ID_2, "POR\r\n");
    if (s.wdt_reset)   ht5023_uart_write_str(HT5023_UART_ID_2, "WDT\r\n");
    if (s.soft_reset)  ht5023_uart_write_str(HT5023_UART_ID_2, "SOFT\r\n");
    if (s.ext_reset)   ht5023_uart_write_str(HT5023_UART_ID_2, "EXT\r\n");
    ht5023_reset_clear_flags(HT5023_RSTSTA_KNOWN_FLAGS_MASK);
}
```

See also: `examples/reset_info/`
