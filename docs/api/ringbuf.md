# Ring Buffer API

**Header:** `#include "ht5023/ringbuf.h"`
**Source:** `lib/ht5023_ringbuf/src/ht5023_ringbuf.c`

---

## Overview

A fixed-capacity byte FIFO backed by a caller-supplied array.
The `head`, `tail`, and `count` fields are `volatile`, making the buffer
safe for single-producer / single-consumer use between an ISR and main
context on Cortex-M0 — without additional locking, provided each
read-modify-write sequence on a shared index is only ever performed by
one context at a time.

**Typical usage:** receive ISR writes with `push_overwrite`; main loop
reads with `pop`.

---

## Type

```c
typedef struct {
    uint8_t*          data;      // backing storage (caller-allocated)
    uint16_t          capacity;  // size of storage in bytes
    volatile uint16_t head;      // next write position
    volatile uint16_t tail;      // next read position
    volatile uint16_t count;     // number of bytes currently stored
} ht5023_ringbuf_t;
```

---

## Functions

### `ht5023_ringbuf_init`

```c
void ht5023_ringbuf_init(ht5023_ringbuf_t* rb, uint8_t* storage, uint16_t capacity);
```

Attach `storage` to `rb` and reset head/tail/count to zero. Does nothing
if `rb` is NULL.

---

### `ht5023_ringbuf_clear`

```c
void ht5023_ringbuf_clear(ht5023_ringbuf_t* rb);
```

Reset the buffer to empty (does not zero the backing array).

---

### `ht5023_ringbuf_push`

```c
bool ht5023_ringbuf_push(ht5023_ringbuf_t* rb, uint8_t v);
```

Append one byte. Returns `false` (and discards `v`) if the buffer is full.

---

### `ht5023_ringbuf_push_overwrite`

```c
bool ht5023_ringbuf_push_overwrite(ht5023_ringbuf_t* rb, uint8_t v);
```

Append one byte, discarding the **oldest** byte if the buffer is already
full. Returns `false` only on invalid arguments (NULL or zero capacity).

Use this in a UART RX ISR so new bytes are never silently dropped during
a burst — older bytes are sacrificed instead.

---

### `ht5023_ringbuf_pop`

```c
bool ht5023_ringbuf_pop(ht5023_ringbuf_t* rb, uint8_t* out);
```

Remove and return the oldest byte into `*out`. Returns `false` if the
buffer is empty or `out` is NULL.

---

### `ht5023_ringbuf_peek`

```c
bool ht5023_ringbuf_peek(const ht5023_ringbuf_t* rb, uint8_t* out);
```

Copy the oldest byte into `*out` without removing it. Returns `false` if
empty or `out` is NULL.

---

### Query functions

```c
uint16_t ht5023_ringbuf_count(const ht5023_ringbuf_t* rb);
uint16_t ht5023_ringbuf_capacity(const ht5023_ringbuf_t* rb);
bool     ht5023_ringbuf_is_empty(const ht5023_ringbuf_t* rb);
bool     ht5023_ringbuf_is_full(const ht5023_ringbuf_t* rb);
```

---

## Example — polling UART with ring buffer

```c
#include "ht5023/ringbuf.h"
#include "ht5023/uart.h"

static uint8_t rx_storage[128];
static ht5023_ringbuf_t rx_buf;

int main(void) {
    ht5023_ringbuf_init(&rx_buf, rx_storage, sizeof(rx_storage));
    // ... init UART ...

    while (1) {
        // drain hardware FIFO into ring buffer
        uint8_t b;
        while (ht5023_uart_try_read_byte(HT5023_UART_ID_2, &b)) {
            ht5023_ringbuf_push_overwrite(&rx_buf, b);  // never blocks
        }

        // process buffered bytes
        while (ht5023_ringbuf_pop(&rx_buf, &b)) {
            process_byte(b);
        }
    }
}
```

See also: `examples/uart_ringbuf/`
