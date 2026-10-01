# UART API

**Header:** `#include "ht5023/uart.h"`
**Source:** `lib/ht5023_uart/src/ht5023_uart.c`

---

## Overview

Polling-based UART driver supporting all six UARTs (UART0–UART5).
All transmit and blocking-receive operations spin on status flags — there are
no interrupts or DMA.

**Default console:** UART2 on PC11 (TX) / PC12 (RX), 9600 baud.

---

## Pin initialisation helpers

```c
void ht5023_uart0_pins_init_pc2_pc3(void);
void ht5023_uart1_pins_init_pc0_pc1(void);
void ht5023_uart2_pins_init_pc11_pc12(void);
void ht5023_uart5_pins_init_pc4_pc5(void);
```

Configure the GPIO alternate-function mux for the respective UART.
Call before `ht5023_uart_init()`.

| UART | TX pin | RX pin | Mux |
|------|--------|--------|-----|
| UART0 | PC3 | PC2 | first function (`AFCFG=0`) |
| UART1 | PC0 | PC1 | first function (`AFCFG=0`); second is `SEG43`/`SEG44` |
| UART2 | PC11 | PC12 | both functions |
| UART5 | PC5 | PC4 | **second** function (`AFCFG=1`); first is `SPI_MISO`/`SPI_MOSI`, so SPI is unavailable while UART5 is in use |

For other UARTs, configure the pin mux manually using the datasheet pin table.

---

## Types

### `ht5023_uart_id_t`

```c
typedef enum {
    HT5023_UART_ID_0 = 0,
    HT5023_UART_ID_1 = 1,
    HT5023_UART_ID_2 = 2,
    HT5023_UART_ID_3 = 3,
    HT5023_UART_ID_4 = 4,
    HT5023_UART_ID_5 = 5,
} ht5023_uart_id_t;
```

### `ht5023_uart_config_t`

```c
typedef struct {
    uint32_t sysclk_hz;  // Fsys in Hz: pass HT5023_FSYS_HZ
    uint32_t baud;       // baud rate (e.g. 9600, 115200)
    bool enable_tx;
    bool enable_rx;
} ht5023_uart_config_t;
```

---

## Functions

### `ht5023_uart_init`

```c
bool ht5023_uart_init(ht5023_uart_id_t id, const ht5023_uart_config_t* cfg);
```

Initialise the UART: enable its clock gate, compute the SREL baud divider,
and configure the control register.

Returns `false` if `cfg` is NULL, the UART id is invalid, or the baud rate
cannot be achieved from `sysclk_hz` (divider overflow or zero).

> **8N1 fixed.** The driver always programs 8 data bits, 1 stop bit, no parity.
> Interrupts are not enabled.

---

### `ht5023_uart_write_byte`

```c
void ht5023_uart_write_byte(ht5023_uart_id_t id, uint8_t byte);
```

Transmit one byte. Blocks until the hardware TXIF flag is set (previous byte
shifted out), then loads the new byte and waits for it to complete.

---

### `ht5023_uart_write`

```c
void ht5023_uart_write(ht5023_uart_id_t id, const void* data, size_t len);
```

Transmit `len` bytes from `data`. No-op if `data` is NULL or `len` is 0.

---

### `ht5023_uart_write_str`

```c
void ht5023_uart_write_str(ht5023_uart_id_t id, const char* s);
```

Transmit a null-terminated string. No-op if `s` is NULL.

---

### `ht5023_uart_rx_ready`

```c
bool ht5023_uart_rx_ready(ht5023_uart_id_t id);
```

Returns `true` if the RXIF flag is set (a received byte is waiting in SBUF).

---

### `ht5023_uart_try_read_byte`

```c
bool ht5023_uart_try_read_byte(ht5023_uart_id_t id, uint8_t* out);
```

Non-blocking receive. Reads the byte into `*out` and clears RXIF.
Returns `false` if no byte is available or `out` is NULL.

---

### `ht5023_uart_read_byte_blocking`

```c
uint8_t ht5023_uart_read_byte_blocking(ht5023_uart_id_t id);
```

Spin-wait until a byte arrives, then return it.
**Caution:** never returns if RX is disabled or no sender is connected.

---

## Example

```c
#include "ht5023/uart.h"

const ht5023_uart_config_t cfg = {
    .sysclk_hz = HT5023_FSYS_HZ,   // from ht5023/core/system.h
    .baud      = 9600,
    .enable_tx = true,
    .enable_rx = true,
};

ht5023_uart2_pins_init_pc11_pc12();
ht5023_uart_init(HT5023_UART_ID_2, &cfg);

ht5023_uart_write_str(HT5023_UART_ID_2, "Hello\r\n");

// Non-blocking echo loop
uint8_t b;
while (1) {
    if (ht5023_uart_try_read_byte(HT5023_UART_ID_2, &b)) {
        ht5023_uart_write_byte(HT5023_UART_ID_2, b);
    }
}
```

See also: `examples/uart/`, `examples/uart_ringbuf/`
