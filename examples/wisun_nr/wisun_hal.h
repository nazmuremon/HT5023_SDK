// wisun_hal.h — the only thing the Wi-SUN logic knows about the hardware.
//
// Everything outside hal_*.c is portable C99. To move to another MCU, write a
// new hal_<platform>.c implementing these functions; the CLI driver, frame
// codec and state machine do not change. test/hal_host.c is a second
// implementation that drives the same code against a simulated E51.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Platform bring-up: tick source, interrupt controller, log port.
void hal_init(void);

// Configure the module UART (8N1, no flow control). Receive must be buffered
// (>= 4 KB) by an interrupt, not polled.
bool hal_uart_init(uint32_t baud);

// Queue bytes for the module. Non-blocking: returns false, having queued
// nothing, if the whole buffer does not fit -- the caller retries later.
bool hal_uart_write(const uint8_t* bytes, size_t len);

// Non-blocking read of whatever has arrived. Returns the number of bytes copied.
size_t hal_uart_read(uint8_t* buf, size_t max);

// Bytes dropped because the receive buffer was full (diagnostics).
uint32_t hal_uart_rx_overflows(void);

// Monotonic milliseconds since hal_init(). Wraps after ~49 days; callers only
// ever subtract two readings, which stays correct across the wrap.
uint32_t hal_time_ms(void);

// Debug output. Non-blocking: text that does not fit is dropped and counted.
void hal_log(const char* text);

// Optional debug console. Returns true and a NUL-terminated line (without the
// line ending) once a whole line has been typed. Never blocks.
bool hal_console_read_line(char* buf, size_t max);

// Optional hardware reset of the module. Starts a pulse and returns at once;
// hal_poll() ends it. A no-op when no reset pin is wired.
void hal_module_reset_pulse(void);
bool hal_module_reset_available(void);

// Housekeeping for the HAL itself (finishing a reset pulse). Call every loop.
void hal_poll(void);
