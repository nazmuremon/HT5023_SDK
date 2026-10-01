# SPI API

**Header:** `#include "ht5023/spi.h"`
**Source:** `lib/ht5023_spi/src/ht5023_spi.c`

---

## Overview

SPI master driver. Full-duplex, polling-based with a finite timeout.
Only one hardware SPI peripheral is present; the pin mapping is fixed.

| Signal | Pin |
|--------|-----|
| MOSI | PC4 |
| MISO | PC5 |
| CLK | PC6 |
| CS (SSN0) | PC7 |

---

## Types

### `ht5023_spi_master_cfg_t`

```c
typedef struct {
    uint8_t spr;   // clock rate: 0=Fsys/2  1=Fsys/4  2=Fsys/8  3=Fsys/16
                   //             4=Fsys/32 5=Fsys/64 6=Fsys/128 7=no clock
    bool cpol;     // idle clock polarity (false = low, true = high)
    bool cpha;     // clock phase (false = sample on leading edge)
} ht5023_spi_master_cfg_t;
```

**`spr` values:**

| spr | Divisor | Rate at Fsys = 9.196 MHz |
|-----|---------|-----------------|
| 0 | /2 | 4.598 MHz |
| 1 | /4 | 2.299 MHz |
| 2 | /8 | 1.149 MHz |
| 3 | /16 | 575 kHz |
| 4 | /32 | 287 kHz |
| 5 | /64 | 144 kHz |
| 6 | /128 | 72 kHz |

---

## Functions

### `ht5023_spi_pins_init_pc4_pc5_pc6_pc7`

```c
void ht5023_spi_pins_init_pc4_pc5_pc6_pc7(void);
```

Configure PC4–PC7 for the SPI alternate function. Called automatically by
`ht5023_spi_master_init()`.

---

### `ht5023_spi_master_init`

```c
bool ht5023_spi_master_init(const ht5023_spi_master_cfg_t* cfg);
```

Enable the SPI clock gate, configure the pins, and program SPICON.
Returns `false` if `cfg` is NULL. Leaves CS high on entry.

---

### `ht5023_spi_set_cs`

```c
void ht5023_spi_set_cs(bool high);
```

Drive the hardware SSN0 line. Pass `false` to assert CS before a
transaction, `true` to de-assert afterwards.

When the XPT2046 touch controller is connected to a separate CS pin
(e.g. PA3) via a GPIO callback, keep SSN0 high with
`ht5023_spi_set_cs(true)` so it does not interfere.

---

### `ht5023_spi_transfer_byte`

```c
uint8_t ht5023_spi_transfer_byte(uint8_t tx);
```

Clock out `tx` on MOSI while simultaneously clocking in a byte on MISO.
Polls the SPIF flag for up to 60 000 iterations.

Returns the received byte, or `0x00` on timeout (hardware not responding).

---

### `ht5023_spi_transfer`

```c
void ht5023_spi_transfer(const uint8_t* tx, uint8_t* rx, size_t len);
```

Transfer a block of `len` bytes.

- `tx` may be NULL — sends `0xFF` bytes (read-only transfer).
- `rx` may be NULL — discards incoming bytes (write-only transfer).

---

## Example — SPI loopback

```c
#include "ht5023/spi.h"

const ht5023_spi_master_cfg_t cfg = { .spr = 3, .cpol = false, .cpha = false };
ht5023_spi_master_init(&cfg);

ht5023_spi_set_cs(false);
uint8_t rx = ht5023_spi_transfer_byte(0xA5);
ht5023_spi_set_cs(true);
// if PC4 is jumpered to PC5: rx == 0xA5
```

## Example — ST7789 write-only

```c
static void st7789_write_cb(const uint8_t* data, size_t len, void* user) {
    (void)user;
    ht5023_spi_transfer(data, NULL, len);
}
```

See also: `examples/spi_loopback/`, `examples/st7789_demo/`
