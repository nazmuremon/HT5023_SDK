// wisun_cli.h — E51 CLI driver: line assembly, prompt detection and a
// single-flight command queue (spec section 5).
//
// One command is on the wire at a time. It completes when the "> " prompt
// returns after its echo (or after any response line), fails when a failure
// line was seen, and times out otherwise. Every line -- in flight or not -- is
// also offered to the async handler, because events interleave with responses.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  CLI_OK = 0,
  CLI_FAILED,     // "[Failed: ...]", "No command found", "Incorrect number of arguments"
  CLI_TIMEOUT,
  CLI_ABORTED,    // dropped by cli_reset()
} cli_result_t;

typedef struct {
  void* ctx;
  // A response line of the command identified by `tag`.
  void (*line)(void* ctx, uint8_t tag, const char* line);
  // The command identified by `tag` finished. `reason` is the failure text or "".
  void (*done)(void* ctx, uint8_t tag, cli_result_t result, const char* reason);
  // Every non-echo line, whether or not a command is in flight.
  void (*async)(void* ctx, const char* line);
  // A socket_read reply: its header line and the payload between it and ']'.
  void (*data)(void* ctx, const char* header, const char* payload, size_t len);
} cli_handlers_t;

void cli_init(const cli_handlers_t* handlers);

// Drives receive, completion and timeouts, and starts the next queued command.
void cli_poll(void);

// Queues a command (without the line ending). False if the queue is full or
// the command is too long.
bool cli_enqueue(const char* cmd, uint32_t timeout_ms, uint8_t tag);

// Writes raw bytes straight to the module, bypassing the queue. Used only for
// the "\r\n" of UART sync, and only when nothing is in flight.
bool cli_send_raw(const char* text);

// Drops everything queued or in flight (each reported as CLI_ABORTED) and the
// partial receive line.
void cli_reset(void);

bool cli_idle(void);                    // nothing in flight and nothing queued
uint32_t cli_prompt_count(void);        // increments on every "> " seen
uint32_t cli_queue_free(void);
uint32_t cli_line_overflows(void);
