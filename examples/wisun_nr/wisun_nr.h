// wisun_nr.h — Wi-SUN node (NR) link manager and application interface.
//
// Call nr_init() once, then nr_poll() from the main loop as often as possible.
// Nothing in here blocks; every wait is a timestamp compared on a later poll.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wisun_config.h"

typedef enum {
  NR_UART_SYNC = 0,
  NR_PROBE,
  NR_CONFIG_CHECK,
  NR_APPLY_CONFIG,
  NR_CLEAN_SOCKETS,
  NR_PREPARE_JOIN,
  NR_JOIN,
  NR_WAIT_OPERATIONAL,
  NR_GET_ADDRESSES,
  NR_VERIFY_PING,
  NR_OPEN_SERVER,
  NR_CONNECT_UPLINK,
  NR_WAIT_DOWNLINK,
  NR_READY,
  NR_CLOSE_SOCKETS,
  NR_ERROR,
  NR_STATE_COUNT
} nr_state_t;

typedef struct {
  nr_state_t state;
  const char* state_name;
  int32_t join_state;             // -1 unknown, else 0..5
  uint32_t state_elapsed_s;       // time in the current state (join time while joining)
  char nr_ipv6[WISUN_IPV6_MAX];
  char br_ipv6[WISUN_IPV6_MAX];
  int32_t listen_sock;
  int32_t tx_sock;
  int32_t rx_sock;
  uint32_t join_attempts;
  uint32_t frames_tx;
  uint32_t frames_rx;
  uint32_t retries;
  uint32_t crc_errors;
  uint32_t link_losses;
  uint32_t msgs_delivered;
  uint32_t msgs_failed;
  uint32_t msgs_received;
  uint32_t queued;
  uint32_t uart_rx_overflows;
  uint32_t cli_line_overflows;
} nr_status_t;

typedef void (*nr_receive_fn)(const uint8_t* data, size_t len);
// The payload is passed back so the application can report what was (not) delivered.
typedef void (*nr_delivery_fn)(uint32_t msg_id, bool success, const uint8_t* data, size_t len);

void nr_init(void);
void nr_poll(void);

bool nr_is_ready(void);

// Queues a message of 1..WISUN_MSG_MAX bytes. False if not READY or the queue
// (WISUN_APP_QUEUE_DEPTH messages) is full. *msg_id identifies it in the
// delivery callback.
bool nr_send(const uint8_t* data, size_t len, uint32_t* msg_id);

void nr_on_receive(nr_receive_fn cb);
void nr_on_delivery_result(nr_delivery_fn cb);

void nr_status(nr_status_t* out);
const char* nr_state_name(nr_state_t s);

// Debug console hooks.
bool nr_raw(const char* cli_command);   // response lines are logged as RAW<
void nr_rejoin(void);                   // force disconnect, then PREPARE_JOIN
