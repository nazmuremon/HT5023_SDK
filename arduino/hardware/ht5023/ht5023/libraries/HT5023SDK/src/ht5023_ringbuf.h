// ht5023_ringbuf.h — byte ring buffer (ISR-safe with critical section support)
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Ring buffer state. Declare as volatile when sharing between an ISR and main context.
// The caller is responsible for disabling/enabling interrupts around multi-step
// operations (e.g., check-then-push) when the buffer is used in ISR context.
typedef struct {
  uint8_t*          data;
  uint16_t          capacity;
  volatile uint16_t head;    // next write index
  volatile uint16_t tail;    // next read index
  volatile uint16_t count;   // number of bytes currently stored
} ht5023_ringbuf_t;

void ht5023_ringbuf_init(ht5023_ringbuf_t* rb, uint8_t* storage, uint16_t capacity);
void ht5023_ringbuf_clear(ht5023_ringbuf_t* rb);

// Push one byte. Returns false if the buffer is full.
bool ht5023_ringbuf_push(ht5023_ringbuf_t* rb, uint8_t v);

// Push one byte, dropping the oldest byte if the buffer is full.
// Always succeeds for valid, non-zero-capacity buffers. Returns false only on
// invalid arguments. Useful in RX ISR handlers where the oldest data may be discarded.
bool ht5023_ringbuf_push_overwrite(ht5023_ringbuf_t* rb, uint8_t v);

// Pop one byte into *out. Returns false if the buffer is empty.
bool ht5023_ringbuf_pop(ht5023_ringbuf_t* rb, uint8_t* out);

// Peek at the next byte without removing it. Returns false if empty or out is NULL.
bool ht5023_ringbuf_peek(const ht5023_ringbuf_t* rb, uint8_t* out);

uint16_t ht5023_ringbuf_count(const ht5023_ringbuf_t* rb);
uint16_t ht5023_ringbuf_capacity(const ht5023_ringbuf_t* rb);
bool ht5023_ringbuf_is_empty(const ht5023_ringbuf_t* rb);
bool ht5023_ringbuf_is_full(const ht5023_ringbuf_t* rb);

#ifdef __cplusplus
}
#endif
