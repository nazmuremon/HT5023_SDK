// wisun_frame.c — see wisun_frame.h.
#include "wisun_frame.h"

#include <string.h>

static const char k_hex[] = "0123456789ABCDEF";

uint16_t crc16_ccitt_false(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)((uint16_t)data[i] << 8);
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

size_t frame_encode(char type, uint16_t seq, const uint8_t* payload, size_t len,
                    char* out, size_t cap) {
  // Worst case: "$" + T + "," + 5 + "," + 128 + "," + 4 + ";" + NUL = 143.
  if (len > WISUN_FRAME_MAX_PAYLOAD || cap < 16u + 2u * len) return 0;

  size_t n = 0;
  out[n++] = '$';
  out[n++] = type;
  out[n++] = ',';

  char digits[5];
  size_t d = 0;
  uint16_t s = seq;
  do {
    digits[d++] = (char)('0' + (s % 10u));
    s = (uint16_t)(s / 10u);
  } while (s);
  while (d) out[n++] = digits[--d];
  out[n++] = ',';

  for (size_t i = 0; i < len; i++) {
    out[n++] = k_hex[payload[i] >> 4];
    out[n++] = k_hex[payload[i] & 15u];
  }
  out[n++] = ',';

  // CRC covers everything between '$' and the last ','.
  const uint16_t crc = crc16_ccitt_false((const uint8_t*)out + 1, n - 2u);
  out[n++] = k_hex[(crc >> 12) & 15u];
  out[n++] = k_hex[(crc >> 8) & 15u];
  out[n++] = k_hex[(crc >> 4) & 15u];
  out[n++] = k_hex[crc & 15u];
  out[n++] = ';';
  out[n] = '\0';
  return n;
}

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

frame_status_t frame_decode(const char* body, size_t len, frame_t* f) {
  // Locate the three commas: T , SEQ , HEX , CRC
  size_t c[3];
  size_t found = 0;
  for (size_t i = 0; i < len; i++) {
    if (body[i] == ',') {
      if (found == 3u) return FRAME_MALFORMED;
      c[found++] = i;
    }
  }
  if (found != 3u || c[0] != 1u) return FRAME_MALFORMED;

  const size_t seq_len = c[1] - c[0] - 1u;
  const size_t hex_len = c[2] - c[1] - 1u;
  const size_t crc_len = len - c[2] - 1u;
  if (seq_len == 0u || seq_len > 5u || crc_len != 4u) return FRAME_MALFORMED;
  if (hex_len % 2u || hex_len > 2u * WISUN_FRAME_MAX_PAYLOAD) return FRAME_MALFORMED;

  uint16_t crc_rx = 0;
  for (size_t i = 0; i < 4u; i++) {
    const int v = hexval(body[c[2] + 1u + i]);
    if (v < 0) return FRAME_MALFORMED;
    crc_rx = (uint16_t)((crc_rx << 4) | (uint16_t)v);
  }
  if (crc16_ccitt_false((const uint8_t*)body, c[2]) != crc_rx) return FRAME_BAD_CRC;

  uint32_t seq = 0;
  for (size_t i = 0; i < seq_len; i++) {
    const char ch = body[c[0] + 1u + i];
    if (ch < '0' || ch > '9') return FRAME_MALFORMED;
    seq = seq * 10u + (uint32_t)(ch - '0');
  }
  if (seq > 65535u) return FRAME_MALFORMED;

  f->type = body[0];
  f->seq = (uint16_t)seq;
  f->len = (uint8_t)(hex_len / 2u);
  for (size_t i = 0; i < f->len; i++) {
    const int hi = hexval(body[c[1] + 1u + 2u * i]);
    const int lo = hexval(body[c[1] + 2u + 2u * i]);
    if (hi < 0 || lo < 0) return FRAME_MALFORMED;
    f->payload[i] = (uint8_t)((hi << 4) | lo);
  }
  return FRAME_OK;
}

// ---------------------------------------------------------- stream splitter --
void frame_stream_reset(frame_stream_t* s) {
  s->len = 0;
  s->in_frame = false;
}

void frame_stream_feed(frame_stream_t* s, const char* text, size_t n,
                       frame_body_fn cb, void* ctx) {
  for (size_t i = 0; i < n; i++) {
    const char ch = text[i];
    if (ch == '$') {                 // a new start always resynchronises
      s->in_frame = true;
      s->len = 0;
      continue;
    }
    if (!s->in_frame) continue;      // bytes before '$' are discarded
    if (ch == ';') {
      s->in_frame = false;
      s->buf[s->len] = '\0';
      cb(ctx, s->buf, s->len);
      s->len = 0;
      continue;
    }
    if (s->len >= WISUN_FRAME_MAX_CHARS) {   // no ';' within 200 chars: drop
      s->in_frame = false;
      s->len = 0;
      s->dropped_overlong++;
      continue;
    }
    s->buf[s->len++] = ch;
  }
}

// -------------------------------------------------------------- reassembly --
void reasm_reset(reasm_t* r) {
  r->len = 0;
  r->discarding = false;
  r->complete = false;
  r->have_last = false;
}

reasm_result_t reasm_accept(reasm_t* r, const frame_t* f) {
  // A retransmission carries the seq already accepted: ack again, keep nothing.
  if (r->have_last && f->seq == r->last_seq) return REASM_DUPLICATE;
  r->have_last = true;
  r->last_seq = f->seq;

  const bool last = (f->type == FRAME_DATA);
  if (r->complete) {                 // the previous message was handed over
    r->len = 0;
    r->complete = false;
  }

  if (r->discarding) {
    if (last) {
      r->discarding = false;
      r->len = 0;
    }
    return REASM_TOO_LONG;
  }

  if ((size_t)r->len + f->len > WISUN_MSG_MAX) {
    r->len = 0;
    r->discarding = !last;           // swallow the rest of this message
    return REASM_TOO_LONG;
  }

  memcpy(r->msg + r->len, f->payload, f->len);
  r->len = (uint16_t)(r->len + f->len);
  r->complete = last;
  return last ? REASM_COMPLETE : REASM_STORED;
}
