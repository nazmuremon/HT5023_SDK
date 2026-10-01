// wisun_frame.h — the application frame protocol (spec section 7).
//
//   $<TYPE>,<SEQ>,<HEX>,<CRC>;
//
// CRC-16/CCITT-FALSE over the text between '$' and the last ','. The border
// router implements exactly this, so nothing here is negotiable.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wisun_config.h"

#define FRAME_DATA       'D'   // data, last (or only) fragment
#define FRAME_FRAGMENT   'F'   // data fragment, more follow
#define FRAME_ACK        'A'
#define FRAME_HEARTBEAT  'H'
#define FRAME_HB_REPLY   'R'

uint16_t crc16_ccitt_false(const uint8_t* data, size_t len);

// Writes a complete frame ("$...;") and NUL. Returns its length, 0 if it does
// not fit or the payload exceeds WISUN_FRAME_MAX_PAYLOAD.
size_t frame_encode(char type, uint16_t seq, const uint8_t* payload, size_t len,
                    char* out, size_t cap);

typedef struct {
  char type;
  uint16_t seq;
  uint8_t len;
  uint8_t payload[WISUN_FRAME_MAX_PAYLOAD];
} frame_t;

typedef enum { FRAME_OK = 0, FRAME_BAD_CRC, FRAME_MALFORMED } frame_status_t;

// Decodes the text between '$' and ';' (both excluded).
frame_status_t frame_decode(const char* body, size_t len, frame_t* f);

// ---------------------------------------------------------- stream splitter --
// TCP is a byte stream: frames arrive split and merged. Feed whatever text a
// socket_read returned; complete frame bodies come out through the callback.
typedef void (*frame_body_fn)(void* ctx, const char* body, size_t len);

typedef struct {
  char buf[WISUN_FRAME_MAX_CHARS + 1u];
  uint16_t len;
  bool in_frame;
  uint32_t dropped_overlong;
} frame_stream_t;

void frame_stream_reset(frame_stream_t* s);
void frame_stream_feed(frame_stream_t* s, const char* text, size_t n,
                       frame_body_fn cb, void* ctx);

// -------------------------------------------------------------- reassembly --
typedef enum {
  REASM_DUPLICATE = 0,   // seen before: ack it, deliver nothing
  REASM_STORED,          // fragment kept: ack it
  REASM_COMPLETE,        // whole message ready in msg/len: ack and deliver
  REASM_TOO_LONG,        // message over WISUN_MSG_MAX, discarded: ack it
} reasm_result_t;

typedef struct {
  uint8_t msg[WISUN_MSG_MAX];
  uint16_t len;
  bool discarding;       // over-long message: drop fragments until its 'D'
  bool complete;         // msg holds a delivered message; cleared on next frame
  bool have_last;
  uint16_t last_seq;
} reasm_t;

void reasm_reset(reasm_t* r);
reasm_result_t reasm_accept(reasm_t* r, const frame_t* f);
