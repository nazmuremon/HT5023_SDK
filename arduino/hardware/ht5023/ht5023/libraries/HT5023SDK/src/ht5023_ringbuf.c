// ht5023_ringbuf.c — tiny byte ring buffer utility
#include "ht5023_ringbuf.h"

#include <stddef.h>
#include <stdint.h>

void ht5023_ringbuf_init(ht5023_ringbuf_t* rb, uint8_t* storage, uint16_t capacity) {
  if (!rb) return;
  rb->data = storage;
  rb->capacity = capacity;
  rb->head = 0u;
  rb->tail = 0u;
  rb->count = 0u;
}

void ht5023_ringbuf_clear(ht5023_ringbuf_t* rb) {
  if (!rb) return;
  rb->head = 0u;
  rb->tail = 0u;
  rb->count = 0u;
}

bool ht5023_ringbuf_push(ht5023_ringbuf_t* rb, uint8_t v) {
  if (!rb || !rb->data || rb->capacity == 0u) return false;
  if (rb->count >= rb->capacity) return false;
  rb->data[rb->head] = v;
  rb->head = (uint16_t)((rb->head + 1u) % rb->capacity);
  rb->count++;
  return true;
}

bool ht5023_ringbuf_push_overwrite(ht5023_ringbuf_t* rb, uint8_t v) {
  if (!rb || !rb->data || rb->capacity == 0u) return false;
  if (rb->count >= rb->capacity) {
    // Drop the oldest byte by advancing tail before writing the new one.
    rb->tail = (uint16_t)((rb->tail + 1u) % rb->capacity);
    rb->count--;
  }
  rb->data[rb->head] = v;
  rb->head = (uint16_t)((rb->head + 1u) % rb->capacity);
  rb->count++;
  return true;
}

bool ht5023_ringbuf_pop(ht5023_ringbuf_t* rb, uint8_t* out) {
  if (!rb || !rb->data || rb->capacity == 0u || rb->count == 0u) return false;
  if (out) *out = rb->data[rb->tail];
  rb->tail = (uint16_t)((rb->tail + 1u) % rb->capacity);
  rb->count--;
  return true;
}

bool ht5023_ringbuf_peek(const ht5023_ringbuf_t* rb, uint8_t* out) {
  if (!rb || !rb->data || rb->capacity == 0u || rb->count == 0u || !out) return false;
  *out = rb->data[rb->tail];
  return true;
}

uint16_t ht5023_ringbuf_count(const ht5023_ringbuf_t* rb) {
  return rb ? rb->count : 0u;
}

uint16_t ht5023_ringbuf_capacity(const ht5023_ringbuf_t* rb) {
  return rb ? rb->capacity : 0u;
}

bool ht5023_ringbuf_is_empty(const ht5023_ringbuf_t* rb) {
  return ht5023_ringbuf_count(rb) == 0u;
}

bool ht5023_ringbuf_is_full(const ht5023_ringbuf_t* rb) {
  return rb && rb->capacity != 0u && rb->count >= rb->capacity;
}

