# I2C API

**Header:** `#include "ht5023/i2c.h"`
**Source:** `lib/ht5023_i2c/src/ht5023_i2c.c`

---

## Overview

I2C master driver. Polling-based with a 60 000-iteration timeout per SI-flag
wait. 7-bit addressing only. Supports write, read, and combined
write-then-read (repeated START) transactions.

| Signal | Pin |
|--------|-----|
| SCL | PC13 |
| SDA | PC14 |

> **Pull-ups required.** The I2C bus requires external pull-up resistors
> (typically 4.7 kΩ to 3.3 V) on SCL and SDA for reliable operation.
> The driver enables the internal weak pull-ups as a fallback, but external
> pull-ups are strongly recommended.

---

## Types

### `ht5023_i2c_master_cfg_t`

```c
typedef struct {
    uint32_t sysclk_hz;  // system clock in Hz
    uint32_t bus_hz;     // I2C clock rate, e.g. 100000 or 400000
} ht5023_i2c_master_cfg_t;
```

---

## Functions

### `ht5023_i2c_pins_init_pc13_pc14`

```c
void ht5023_i2c_pins_init_pc13_pc14(void);
```

Configure PC13/PC14 as SCL/SDA with internal pull-ups. Called automatically
by `ht5023_i2c_master_init()`.

---

### `ht5023_i2c_master_init`

```c
bool ht5023_i2c_master_init(const ht5023_i2c_master_cfg_t* cfg);
```

Enable the I2C clock gate, configure pins, and program the CR (clock rate)
divider. Returns `false` if `cfg` is NULL or either frequency is zero.

---

### `ht5023_i2c_probe_7bit`

```c
bool ht5023_i2c_probe_7bit(uint8_t addr7);
```

Send START + SLA+W to `addr7`. Returns `true` if the device ACKs its
address (device present). Always sends STOP afterwards.

Useful for scanning the bus to discover connected devices.

```c
for (uint8_t a = 0x08; a < 0x78; ++a) {
    if (ht5023_i2c_probe_7bit(a)) {
        // device at address a
    }
}
```

---

### `ht5023_i2c_write`

```c
bool ht5023_i2c_write(uint8_t addr7, const uint8_t* data, size_t len);
```

Write `len` bytes to device `addr7`. Issues START, SLA+W, data bytes, STOP.
Returns `false` if the device NACKs, a timeout occurs, or `data` is NULL
with `len > 0`.

---

### `ht5023_i2c_read`

```c
bool ht5023_i2c_read(uint8_t addr7, uint8_t* data, size_t len);
```

Read `len` bytes from device `addr7` into `data`. Issues START, SLA+R,
clock `len` bytes (ACK all but last), STOP.
Returns `false` on NACK, timeout, or invalid arguments.

---

### `ht5023_i2c_write_read`

```c
bool ht5023_i2c_write_read(uint8_t addr7,
                           const uint8_t* wr_data, size_t wr_len,
                           uint8_t*       rd_data, size_t rd_len);
```

Combined write-then-read with a **repeated START** between the two phases
(no bus release). This is the standard pattern for register-addressed
reads used by almost all sensors and EEPROMs.

- `wr_len` may be 0 (degenerates to a plain read with START instead of
  repeated START).
- `rd_len` may be 0 (plain write).
- Returns `false` if either pointer is NULL when the corresponding length
  is non-zero, or on any NACK/timeout.

---

## Example — write one register

```c
#include "ht5023/i2c.h"

const ht5023_i2c_master_cfg_t cfg = { .sysclk_hz = HT5023_FSYS_HZ, .bus_hz = 100000 };
ht5023_i2c_master_init(&cfg);

// Write 0x01 to register 0x20 of device at address 0x48
uint8_t buf[2] = { 0x20, 0x01 };
ht5023_i2c_write(0x48, buf, sizeof(buf));
```

## Example — read sensor register (repeated START)

```c
uint8_t reg = 0x00;
uint8_t value;

// Write register address, then repeated-START read 1 byte
bool ok = ht5023_i2c_write_read(0x48, &reg, 1, &value, 1);
if (ok) {
    // value contains the sensor reading
}
```

See also: `examples/i2c_scan/`
