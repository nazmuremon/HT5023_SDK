// wisun_nr.c — Wi-SUN node link manager: the spec section 6 state machine,
// socket bookkeeping, and the section 7 stop-and-wait message transport.
//
// Structure
//   * One state machine. Each state is a function called on every poll; a
//     `step` counter sequences the commands inside it. Every wait is a
//     timestamp compared later -- nothing blocks.
//   * The state machine owns at most one CLI command at a time (sm_run).
//     A generation counter, bumped on every state or step change, discards the
//     result of a command that belonged to a step we have already left.
//   * Async events (join progress, sockets, data) are handled as they arrive,
//     in any state, and only mutate fields or request a transition.
//   * In READY, writes (data/ack/heartbeat), reads, closes and the health check
//     are independent streams multiplexed through the single-flight CLI queue.
#include "wisun_nr.h"

#include <string.h>

#include "wisun_cli.h"
#include "wisun_events.h"
#include "wisun_frame.h"
#include "wisun_hal.h"
#include "wisun_util.h"

// CLI tags: who a command's lines and result belong to.
enum { TAG_SM = 1, TAG_WRITE, TAG_READ, TAG_CLOSE, TAG_HEALTH, TAG_RAW };

enum { ERR_NONE = 0, ERR_BR_FIRMWARE, ERR_BIND, ERR_CONFIG_LOCKED };

enum { WK_CTRL = 0, WK_DATA };     // what the in-flight socket_write carried

#define AFTER_CLEAN_DECIDE NR_STATE_COUNT   // resume or rejoin, decided after cleaning

static const char* const k_state_names[NR_STATE_COUNT] = {
  "UART_SYNC", "PROBE", "CONFIG_CHECK", "APPLY_CONFIG", "CLEAN_SOCKETS",
  "PREPARE_JOIN", "JOIN", "WAIT_OPERATIONAL", "GET_ADDRESSES", "VERIFY_PING",
  "OPEN_SERVER", "CONNECT_UPLINK", "WAIT_DOWNLINK", "READY", "CLOSE_SOCKETS",
  "ERROR",
};

// ------------------------------------------------------------------- data --
// "wisun get wisun", reduced to the keys the state machine needs (spec 3.4).
typedef struct {
  bool have_join_state;
  int32_t join_state;
  bool br_firmware;                 // "wisun.state =" -> BR firmware answered
  bool have_name, have_domain, have_chan, have_phy;
  char name[48];
  char domain[12];
  int32_t chan;
  int32_t phy;
  char ip_global[WISUN_IPV6_MAX];
  char br[WISUN_IPV6_MAX];
} snapshot_t;

typedef struct {
  uint32_t id;
  uint16_t len;
  uint8_t data[WISUN_MSG_MAX];
} app_msg_t;

typedef struct {
  char type;                        // 'A' or 'R'
  uint16_t seq;
} ctrl_t;

static struct {
  // state machine
  nr_state_t state;
  uint8_t step;
  uint32_t t_state;
  uint32_t t_step;
  uint32_t t_aux;
  uint32_t gen;
  uint8_t tries;
  bool cmd_pending;
  bool cmd_done;
  uint32_t cmd_gen;
  cli_result_t res;
  char reason[64];
  const char* expect;
  bool expect_seen;
  bool polling;

  // module / network
  int32_t join_state;
  char nr_ipv6[WISUN_IPV6_MAX];
  char br_ipv6[WISUN_IPV6_MAX];
  bool cfg_matched;
  bool disconnected_seen;
  bool ping_ok;
  uint32_t prompt_base;
  uint32_t sync_cycles;
  uint32_t hold_ms;                 // delay before PREPARE_JOIN proceeds
  bool force_disconnect;            // PREPARE_JOIN disconnects even if join state is 0
  uint32_t backoff_ms;
  uint32_t join_attempts;
  uint32_t t_join;                  // this join attempt started (join_fan11 sent)
  uint32_t t_progress;              // the join state last reached a new high
  int32_t best_join_state;          // highest join state seen in this attempt
  nr_state_t after_clean;
  nr_state_t close_next;
  int error_kind;
  bool bind_retried;
  uint8_t connect_fails;

  // sockets
  int32_t listen_sock;
  int32_t tx_sock;
  int32_t rx_sock;
  int32_t pending_client;
  int32_t opened_id;
  bool client_closed;
  int32_t listed[WISUN_MAX_SOCKETS_LISTED];     // sockets to close
  uint8_t listed_count;
  int32_t listed_listener;          // a LISTENING server on our port, -1 if none
  int32_t close_q[WISUN_MAX_SOCKETS_LISTED];
  uint8_t close_count;
  bool close_busy;

  // snapshot fed by TAG_SM / TAG_HEALTH "get wisun"
  snapshot_t snap;

  // READY traffic
  bool write_busy;
  int write_kind;
  bool read_busy;
  bool read_want_rx;
  bool read_want_tx;
  bool health_busy;
  uint32_t t_hb;
  uint32_t t_health;
  uint32_t t_read_poll;
  uint8_t hb_outstanding;
  bool hb_due;
  uint16_t next_seq;
  ctrl_t ctrl_q[WISUN_ACK_QUEUE_DEPTH];
  uint8_t ctrl_count;

  frame_stream_t stream_rx;
  frame_stream_t stream_tx;
  reasm_t reasm;

  app_msg_t app_q[WISUN_APP_QUEUE_DEPTH];
  uint8_t app_head;
  uint8_t app_count;
  uint32_t next_msg_id;
  struct {
    bool active;
    bool need_send;
    bool awaiting_ack;
    uint16_t offset;
    uint16_t seq;
    uint8_t tries;
    uint32_t sent_at;
  } out;
  uint8_t ack_fail_streak;

  nr_receive_fn on_receive;
  nr_delivery_fn on_delivery;

  // counters
  uint32_t frames_tx, frames_rx, retries, crc_errors, link_losses;
  uint32_t msgs_ok, msgs_failed, msgs_rx;
} st;

// ---------------------------------------------------------------- helpers --
static uint32_t now(void) { return hal_time_ms(); }
static uint32_t since(uint32_t t) { return (uint32_t)(now() - t); }

static void log_state(nr_state_t from, nr_state_t to, const char* why) {
  wlog_t l;
  wlog_begin(&l);
  wlog_str(&l, "STATE ");
  wlog_str(&l, k_state_names[from]);
  wlog_str(&l, " -> ");
  wlog_str(&l, k_state_names[to]);
  wlog_str(&l, " (");
  wlog_str(&l, why);
  wlog_str(&l, ")");
  wlog_end(&l);
}

static void on_ready_entry(void);

static void go(nr_state_t next, const char* why) {
  log_state(st.state, next, why);
  // A join attempt starts when join_fan11 goes out -- join-state events arrive
  // before its prompt -- or when waiting for a lost network to recover.
  if (next == NR_JOIN || (next == NR_WAIT_OPERATIONAL && st.state != NR_JOIN)) {
    st.t_join = st.t_progress = now();
    st.best_join_state = 0;
  }
  st.state = next;
  st.step = 0;
  st.t_state = st.t_step = st.t_aux = now();
  st.gen++;
  st.cmd_done = false;
  st.tries = 0;
  st.polling = false;
  if (next == NR_READY) on_ready_entry();
}

static void next_step(void) {
  st.step++;
  st.t_step = now();
  st.gen++;
  st.cmd_done = false;
}

static void set_step(uint8_t step) {
  st.step = step;
  st.t_step = now();
  st.gen++;
  st.cmd_done = false;
}

// Retry delay after a failed uplink: `base` doubled per consecutive failure.
static uint32_t retry_delay(uint32_t base) {
  const uint8_t shift = st.connect_fails < 4u ? st.connect_fails : 4u;
  const uint32_t delay = base << shift;
  return delay > WISUN_CONNECT_BACKOFF_MAX_MS ? WISUN_CONNECT_BACKOFF_MAX_MS : delay;
}

static bool online_state(void) {
  return st.state == NR_GET_ADDRESSES || st.state == NR_VERIFY_PING ||
         st.state == NR_OPEN_SERVER || st.state == NR_CONNECT_UPLINK ||
         st.state == NR_WAIT_DOWNLINK || st.state == NR_READY;
}

// Runs one state-machine command. Returns true once, when it has finished;
// st.res / st.reason / st.expect_seen then describe the outcome.
static bool sm_run(const char* cmd, uint32_t timeout_ms, const char* expect) {
  if (st.cmd_done) {
    st.cmd_done = false;
    return true;
  }
  if (!st.cmd_pending && cli_enqueue(cmd, timeout_ms, TAG_SM)) {
    st.cmd_pending = true;
    st.cmd_gen = st.gen;
    st.expect = expect;
    st.expect_seen = false;
    if (str_starts(cmd, "wisun get wisun")) memset(&st.snap, 0, sizeof(st.snap));
    if (str_starts(cmd, "wisun socket_list")) {
      st.listed_count = 0;
      st.listed_listener = -1;
    }
  }
  return false;
}

static void cmd_with_int(char* out, size_t cap, const char* prefix, int32_t v, const char* suffix) {
  size_t n = fmt_str(out, 0, cap, prefix);
  n = fmt_u32(out, n, cap, (uint32_t)v);
  fmt_str(out, n, cap, suffix ? suffix : "");
}

static void log_kv(const char* a, const char* b, const char* c, const char* d) {
  wlog_t l;
  wlog_begin(&l);
  if (a) wlog_str(&l, a);
  if (b) wlog_str(&l, b);
  if (c) wlog_str(&l, c);
  if (d) wlog_str(&l, d);
  wlog_end(&l);
}

// ----------------------------------------------------------- snapshot parse --
// Value normalisation: quoted strings lose their quotes; otherwise a trailing
// " (N)" suffix is removed ("NA (1)" -> "NA").
static void norm_value(const char* v, char* out, size_t cap) {
  while (*v == ' ') v++;
  if (*v == '"') {
    const char* end = strchr(v + 1, '"');
    str_copyn(out, cap, v + 1, end ? (size_t)(end - v - 1) : strlen(v + 1));
    return;
  }
  str_copy(out, cap, v);
  size_t n = strlen(out);
  while (n && out[n - 1u] == ' ') out[--n] = '\0';
  if (n >= 3u && out[n - 1u] == ')') {
    size_t i = n - 1u;
    while (i > 0u && out[i - 1u] >= '0' && out[i - 1u] <= '9') i--;
    if (i >= 2u && out[i - 1u] == '(' && out[i - 2u] == ' ' && i < n - 1u) {
      out[i - 2u] = '\0';
    }
  }
}

static void snap_line(const char* line) {
  const char* eq = strstr(line, " = ");
  if (!eq) return;
  const size_t klen = (size_t)(eq - line);
  const char* v = eq + 3;
  #define KEY(k) (klen == sizeof(k) - 1u && strncmp(line, k, klen) == 0)

  if (KEY("wisun.state")) {
    st.snap.br_firmware = true;
  } else if (KEY("wisun.join_state")) {
    const char* open = strrchr(v, '(');
    uint32_t n;
    if (open && (open++, parse_u32(&open, &n))) {
      st.snap.have_join_state = true;
      st.snap.join_state = (int32_t)n;
    }
  } else if (KEY("wisun.network_name")) {
    norm_value(v, st.snap.name, sizeof(st.snap.name));
    st.snap.have_name = true;
  } else if (KEY("wisun.regulatory_domain")) {
    norm_value(v, st.snap.domain, sizeof(st.snap.domain));
    st.snap.have_domain = true;
  } else if (KEY("wisun.chan_plan_id")) {
    uint32_t n;
    if (parse_u32(&v, &n)) { st.snap.chan = (int32_t)n; st.snap.have_chan = true; }
  } else if (KEY("wisun.phy_mode_id")) {
    uint32_t n;
    if (parse_u32(&v, &n)) { st.snap.phy = (int32_t)n; st.snap.have_phy = true; }
  } else if (KEY("wisun.ip_addresses")) {
    // [fd12:3456::7a1d, fe80::7a1d] -- the first non-fe80 entry is global.
    const char* p = strchr(v, '[');
    p = p ? p + 1 : v;
    while (*p && !st.snap.ip_global[0]) {
      while (*p == ' ' || *p == ',') p++;
      const char* end = p;
      while (*end && *end != ',' && *end != ']' && *end != ' ') end++;
      char ip[WISUN_IPV6_MAX];
      str_copyn(ip, sizeof(ip), p, (size_t)(end - p));
      if (ipv6_is_global(ip)) str_copy(st.snap.ip_global, sizeof(st.snap.ip_global), ip);
      if (!*end || *end == ']') break;
      p = end;
    }
  } else if (KEY("wisun.border_router")) {
    char br[WISUN_IPV6_MAX];
    norm_value(v, br, sizeof(br));
    if (ipv6_is_global(br)) str_copy(st.snap.br, sizeof(st.snap.br), br);
  }
  #undef KEY
}

// "#   7  TCP server  :: (9000) (LISTENING)"
// A listener on our own port is kept apart rather than closed: on the E51 a
// closed listener's port cannot be bound again for about 4 minutes (seen on the
// hardware after an MCU reset), so it is reused instead.
static void socket_list_line(const char* line) {
  if (line[0] != '#') return;
  const char* p = line + 1;
  while (*p == ' ') p++;
  uint32_t id;
  if (!parse_u32(&p, &id)) return;
  char our_port[12];
  cmd_with_int(our_port, sizeof(our_port), "(", (int32_t)WISUN_APP_PORT, ")");
  if (st.listed_listener < 0 && str_contains(p, "TCP server") && str_contains(p, our_port) &&
      str_contains(p, "(LISTENING)")) {
    st.listed_listener = (int32_t)id;
  } else if (st.listed_count < WISUN_MAX_SOCKETS_LISTED) {
    st.listed[st.listed_count++] = (int32_t)id;
  }
}

// ----------------------------------------------------------------- closing --
static bool close_pending(int32_t id) {
  for (uint8_t i = 0; i < st.close_count; i++) {
    if (st.close_q[i] == id) return true;
  }
  return false;
}

static void request_close(int32_t id) {
  if (id < 0 || close_pending(id) || st.close_count >= WISUN_MAX_SOCKETS_LISTED) return;
  st.close_q[st.close_count++] = id;
}

static void close_poll(void) {
  if (st.close_busy || st.close_count == 0u) return;
  char cmd[40];
  cmd_with_int(cmd, sizeof(cmd), "wisun socket_close ", st.close_q[0], NULL);
  if (cli_enqueue(cmd, WISUN_CMD_SOCKET_TIMEOUT_MS, TAG_CLOSE)) st.close_busy = true;
}

static void close_done(void) {
  st.close_busy = false;
  if (st.close_count) {
    memmove(st.close_q, st.close_q + 1, (st.close_count - 1u) * sizeof(st.close_q[0]));
    st.close_count--;
  }
}

// --------------------------------------------------------- app delivery --
static app_msg_t* app_head(void) {
  return st.app_count ? &st.app_q[st.app_head] : NULL;
}

static void deliver_result(bool ok) {
  const app_msg_t* m = app_head();
  if (!m) return;
  const uint32_t id = m->id;
  if (ok) st.msgs_ok++;
  else st.msgs_failed++;

  wlog_t l;
  wlog_begin(&l);
  wlog_str(&l, ok ? "delivered message #" : "DELIVERY FAILED message #");
  wlog_u32(&l, id);
  wlog_end(&l);
  // Reported while the message still owns its queue slot, so a callback that
  // queues a reply cannot overwrite the payload it is reading.
  if (st.on_delivery) st.on_delivery(id, ok, m->data, m->len);

  st.app_head = (uint8_t)((st.app_head + 1u) % WISUN_APP_QUEUE_DEPTH);
  st.app_count--;
  st.out.active = false;
}

static uint16_t take_seq(void) {
  const uint16_t s = st.next_seq;
  st.next_seq = (uint16_t)(st.next_seq + 1u);   // wraps 65535 -> 0
  return s;
}

// Forgets everything tied to the current pair of TCP connections.
static void reset_connection_state(void) {
  if (st.out.active) deliver_result(false);   // in-flight message is lost with the link
  st.out.active = false;
  st.ctrl_count = 0;
  st.hb_due = false;
  st.hb_outstanding = 0;
  st.read_want_rx = st.read_want_tx = false;
  st.ack_fail_streak = 0;
  frame_stream_reset(&st.stream_rx);
  frame_stream_reset(&st.stream_tx);
  reasm_reset(&st.reasm);
}

// Closes the two data sockets, then moves on. The listener is never closed --
// its port would stay unbindable for minutes. With `forget_listener` its id is
// dropped and OPEN_SERVER finds it again with socket_list.
static void begin_close(bool forget_listener, nr_state_t next, const char* why) {
  request_close(st.tx_sock);
  request_close(st.rx_sock);
  st.tx_sock = st.rx_sock = -1;
  if (forget_listener) st.listen_sock = -1;
  st.pending_client = -1;
  reset_connection_state();
  st.close_next = next;
  go(NR_CLOSE_SOCKETS, why);
}

static void sockets_lost(const char* why) {
  if (!(st.state == NR_READY || st.state == NR_WAIT_DOWNLINK || st.state == NR_CONNECT_UPLINK)) return;
  st.link_losses++;
  begin_close(false, NR_VERIFY_PING, why);
}

static void network_lost(const char* why) {
  if (!online_state()) return;
  st.link_losses++;
  st.nr_ipv6[0] = '\0';
  begin_close(true, NR_WAIT_OPERATIONAL, why);
}

static void schedule_rejoin(const char* why) {
  st.backoff_ms = st.backoff_ms ? st.backoff_ms * 2u : WISUN_BACKOFF_FIRST_MS;
  if (st.backoff_ms > WISUN_BACKOFF_MAX_MS) st.backoff_ms = WISUN_BACKOFF_MAX_MS;
  st.hold_ms = st.backoff_ms;
  go(NR_PREPARE_JOIN, why);

  wlog_t l;
  wlog_begin(&l);
  wlog_str(&l, "next join attempt in ");
  wlog_u32(&l, st.hold_ms / 1000u);
  wlog_str(&l, " s (attempts so far: ");
  wlog_u32(&l, st.join_attempts);
  wlog_str(&l, ")");
  wlog_end(&l);
}

// Module rebooted (banner) or first start: forget everything about it.
static void restart(const char* why) {
  cli_reset();
  st.cmd_pending = false;
  st.join_state = -1;
  st.nr_ipv6[0] = st.br_ipv6[0] = '\0';
  st.listen_sock = st.tx_sock = st.rx_sock = st.pending_client = -1;
  st.close_count = 0;
  st.close_busy = st.write_busy = st.read_busy = st.health_busy = false;
  st.force_disconnect = false;
  reset_connection_state();
  go(NR_UART_SYNC, why);
}

// ---------------------------------------------------------- frame handling --
static void ctrl_push(char type, uint16_t seq) {
  if (st.ctrl_count >= WISUN_ACK_QUEUE_DEPTH) {
    wlog_line("WARN: control frame queue full, dropping ", type == FRAME_ACK ? "ACK" : "R");
    return;
  }
  st.ctrl_q[st.ctrl_count].type = type;
  st.ctrl_q[st.ctrl_count].seq = seq;
  st.ctrl_count++;
}

static void on_ack(void) {
  const app_msg_t* m = app_head();
  if (!m) return;
  st.out.awaiting_ack = false;
  uint16_t chunk = (uint16_t)(m->len - st.out.offset);
  if (chunk > WISUN_FRAME_MAX_PAYLOAD) chunk = WISUN_FRAME_MAX_PAYLOAD;
  st.out.offset = (uint16_t)(st.out.offset + chunk);
  if (st.out.offset >= m->len) {
    st.ack_fail_streak = 0;
    deliver_result(true);
  } else {
    st.out.seq = take_seq();
    st.out.tries = 0;
    st.out.need_send = true;
  }
}

static void on_frame_body(void* ctx, const char* body, size_t len) {
  (void)ctx;
  frame_t f;
  const frame_status_t fs = frame_decode(body, len, &f);
  if (fs == FRAME_BAD_CRC) {
    st.crc_errors++;
    wlog_line("CRC error, frame dropped: $", body);
    return;
  }
  if (fs != FRAME_OK) {
    wlog_line("malformed frame dropped: $", body);
    return;
  }
  st.frames_rx++;

  switch (f.type) {
    case FRAME_DATA:
    case FRAME_FRAGMENT: {
      ctrl_push(FRAME_ACK, f.seq);                   // always ack, even duplicates
      const reasm_result_t r = reasm_accept(&st.reasm, &f);
      if (r == REASM_COMPLETE) {
        st.msgs_rx++;
        if (st.on_receive) st.on_receive(st.reasm.msg, st.reasm.len);
      } else if (r == REASM_TOO_LONG) {
        wlog_line("WARN: incoming message over 1024 bytes discarded", NULL);
      }
      break;
    }
    case FRAME_ACK:
      if (st.out.active && st.out.awaiting_ack && f.seq == st.out.seq) on_ack();
      break;
    case FRAME_HB_REPLY:
      st.hb_outstanding = 0;
      break;
    case FRAME_HEARTBEAT:
      ctrl_push(FRAME_HB_REPLY, f.seq);
      break;
    default:
      break;                                          // unknown TYPE: ignore, no ack
  }
}

// ----------------------------------------------------------------- traffic --
static bool write_frame(char type, uint16_t seq, const uint8_t* payload, size_t len, int kind) {
  char frame[16u + 2u * WISUN_FRAME_MAX_PAYLOAD];
  if (!frame_encode(type, seq, payload, len, frame, sizeof(frame))) return false;
  char cmd[WISUN_CMD_MAX];
  cmd_with_int(cmd, sizeof(cmd), "wisun socket_write ", st.tx_sock, " ");
  fmt_str(cmd, strlen(cmd), sizeof(cmd), frame);
  if (!cli_enqueue(cmd, WISUN_CMD_SOCKET_TIMEOUT_MS, TAG_WRITE)) return false;
  st.write_busy = true;
  st.write_kind = kind;
  st.frames_tx++;
  return true;
}

static void tx_poll(void) {
  if (st.write_busy || st.tx_sock < 0) return;

  if (st.ctrl_count) {                               // acks and replies first
    if (write_frame(st.ctrl_q[0].type, st.ctrl_q[0].seq, NULL, 0, WK_CTRL)) {
      memmove(st.ctrl_q, st.ctrl_q + 1, (st.ctrl_count - 1u) * sizeof(st.ctrl_q[0]));
      st.ctrl_count--;
    }
    return;
  }

  if (st.hb_due) {
    if (write_frame(FRAME_HEARTBEAT, st.next_seq, NULL, 0, WK_CTRL)) {
      (void)take_seq();
      st.hb_due = false;
      st.hb_outstanding++;
    }
    return;
  }

  const app_msg_t* m = app_head();
  if (st.out.active && st.out.need_send && m) {
    uint16_t chunk = (uint16_t)(m->len - st.out.offset);
    const bool more = chunk > WISUN_FRAME_MAX_PAYLOAD;
    if (more) chunk = WISUN_FRAME_MAX_PAYLOAD;
    if (write_frame(more ? FRAME_FRAGMENT : FRAME_DATA, st.out.seq,
                    m->data + st.out.offset, chunk, WK_DATA)) {
      if (st.out.tries) st.retries++;
      st.out.need_send = false;
      st.out.awaiting_ack = true;
      st.out.sent_at = now();
    }
  }
}

static void app_poll(void) {
  if (!st.out.active && st.app_count) {
    st.out.active = true;
    st.out.offset = 0;
    st.out.seq = take_seq();
    st.out.tries = 0;
    st.out.need_send = true;
    st.out.awaiting_ack = false;
  }

  if (st.out.active && st.out.awaiting_ack && !st.write_busy &&
      since(st.out.sent_at) >= WISUN_ACK_TIMEOUT_MS) {
    st.out.awaiting_ack = false;
    if (st.out.tries < WISUN_ACK_RETRIES) {
      st.out.tries++;
      st.out.need_send = true;
      wlog_t l;
      wlog_begin(&l);
      wlog_str(&l, "no ACK for seq ");
      wlog_u32(&l, st.out.seq);
      wlog_str(&l, ", retry ");
      wlog_u32(&l, st.out.tries);
      wlog_str(&l, "/3");
      wlog_end(&l);
    } else {
      deliver_result(false);
      if (++st.ack_fail_streak >= WISUN_ACK_FAILS_LINK_LOST) {
        sockets_lost("ACK retries exhausted twice in a row");
      }
    }
  }
}

static void read_poll(void) {
  if (st.read_busy) return;
  int32_t id = -1;
  bool* flag = NULL;
  if (st.read_want_rx && st.rx_sock >= 0) {
    id = st.rx_sock;
    flag = &st.read_want_rx;
  } else if (st.read_want_tx && st.tx_sock >= 0) {
    id = st.tx_sock;
    flag = &st.read_want_tx;
  } else if (st.state == NR_READY && st.rx_sock >= 0 &&
             since(st.t_read_poll) >= WISUN_READ_SAFETY_POLL_MS) {
    id = st.rx_sock;                                 // slow safety poll, spec 3.6
    st.t_read_poll = now();
  }
  if (id < 0) return;

  char cmd[48];
  cmd_with_int(cmd, sizeof(cmd), "wisun socket_read ", id, " ");
  fmt_u32(cmd, strlen(cmd), sizeof(cmd), WISUN_SOCKET_READ_MAX);
  if (cli_enqueue(cmd, WISUN_CMD_SOCKET_TIMEOUT_MS, TAG_READ)) {
    st.read_busy = true;
    if (flag) *flag = false;
  }
}

static void on_ready_entry(void) {
  st.t_hb = st.t_health = st.t_read_poll = now();
  st.hb_outstanding = 0;
  st.hb_due = false;
  st.backoff_ms = 0;
  st.connect_fails = 0;
  st.bind_retried = false;
  st.ack_fail_streak = 0;
  // Not reset_connection_state(): frames the BR sent right after connecting
  // back may already be in the stream buffer. Every loss path resets it.

  wlog_t l;
  wlog_begin(&l);
  wlog_str(&l, "READY  NR ");
  wlog_str(&l, st.nr_ipv6);
  wlog_str(&l, "  BR ");
  wlog_str(&l, st.br_ipv6);
  wlog_str(&l, "  listen ");
  wlog_i32(&l, st.listen_sock);
  wlog_str(&l, "  tx ");
  wlog_i32(&l, st.tx_sock);
  wlog_str(&l, "  rx ");
  wlog_i32(&l, st.rx_sock);
  wlog_end(&l);
}

static void ready_poll(void) {
  if (since(st.t_hb) >= WISUN_HEARTBEAT_MS) {
    st.t_hb = now();
    if (st.hb_outstanding >= WISUN_HEARTBEAT_MISSES) {
      sockets_lost("3 heartbeats without a reply");
      return;
    }
    st.hb_due = true;
  }

  if (!st.health_busy && since(st.t_health) >= WISUN_HEALTH_PERIOD_MS &&
      cli_enqueue("wisun get wisun", WISUN_CMD_GET_TIMEOUT_MS, TAG_HEALTH)) {
    memset(&st.snap, 0, sizeof(st.snap));
    st.health_busy = true;
    st.t_health = now();
  }

  app_poll();
  tx_poll();
}

// ------------------------------------------------------------ CLI callbacks --
static void cli_line_cb(void* ctx, uint8_t tag, const char* line) {
  (void)ctx;
  if (tag == TAG_SM || tag == TAG_HEALTH) {
    snap_line(line);
    socket_list_line(line);
    if (tag == TAG_SM && st.expect && str_contains(line, st.expect)) st.expect_seen = true;
  } else if (tag == TAG_RAW) {
    wlog_user_line("RAW< ", line);
  }
}

static void health_done(cli_result_t r) {
  st.health_busy = false;
  if (st.state != NR_READY) return;
  if (r != CLI_OK || !st.snap.have_join_state) {
    wlog_line("WARN: health check got no usable reply", NULL);
    return;
  }
  if (st.snap.join_state != 5) {
    network_lost("health check: not Operational");
    return;
  }
  if (st.snap.br[0] && !str_ieq(st.snap.br, st.br_ipv6)) {
    log_kv("border router changed: ", st.br_ipv6, " -> ", st.snap.br);
    str_copy(st.br_ipv6, sizeof(st.br_ipv6), st.snap.br);
    sockets_lost("border router address changed");
  }
}

static void cli_done_cb(void* ctx, uint8_t tag, cli_result_t result, const char* reason) {
  (void)ctx;
  switch (tag) {
    case TAG_SM:
      st.cmd_pending = false;
      if (st.cmd_gen == st.gen && result != CLI_ABORTED) {
        st.cmd_done = true;
        st.res = result;
        str_copy(st.reason, sizeof(st.reason), reason);
      }
      break;
    case TAG_WRITE:
      st.write_busy = false;
      if (result != CLI_OK && result != CLI_ABORTED) {
        wlog_line("socket_write failed: ", reason);
        sockets_lost("socket_write failed");
      } else if (result == CLI_OK && st.write_kind == WK_DATA) {
        st.out.sent_at = now();                      // ACK timer runs from the write
      }
      break;
    case TAG_READ:
      st.read_busy = false;
      // "[Failed: unable to read from a socket: -1]" just means no data.
      if (result == CLI_TIMEOUT) wlog_line("WARN: socket_read timed out", NULL);
      break;
    case TAG_CLOSE:
      close_done();
      break;
    case TAG_HEALTH:
      health_done(result);
      break;
    case TAG_RAW: {
      static const char* const k[] = { "ok", "failed", "timeout", "aborted" };
      wlog_t l;
      wlog_begin_user(&l);
      wlog_str(&l, "RAW done: ");
      wlog_str(&l, k[result]);
      if (*reason) {
        wlog_str(&l, " -- ");
        wlog_str(&l, reason);
      }
      wlog_end(&l);
      break;
    }
    default:
      break;
  }
}

static void cli_data_cb(void* ctx, const char* header, const char* payload, size_t len) {
  (void)ctx;
  wisun_event_t ev;
  if (ev_parse(header, &ev) != EV_DATA_FROM) return;

  const int32_t sock = ev.a;
  if (ev.b == 0) {                                   // len 0: the peer closed
    if (sock == st.rx_sock || sock == st.tx_sock) sockets_lost("peer closed the TCP connection");
    else request_close(sock);
    return;
  }
  if ((size_t)ev.b != len && ev.b <= (int32_t)WISUN_SOCKET_READ_MAX) {
    wlog_t l;
    wlog_begin(&l);
    wlog_str(&l, "WARN: data length mismatch on socket ");
    wlog_i32(&l, sock);
    wlog_str(&l, ": header ");
    wlog_i32(&l, ev.b);
    wlog_str(&l, ", payload ");
    wlog_u32(&l, (uint32_t)len);
    wlog_end(&l);
  }

  if (sock == st.rx_sock) {
    frame_stream_feed(&st.stream_rx, payload, len, on_frame_body, NULL);
    if (ev.b > (int32_t)WISUN_SOCKET_READ_MAX) st.read_want_rx = true;
  } else if (sock == st.tx_sock) {
    frame_stream_feed(&st.stream_tx, payload, len, on_frame_body, NULL);
    if (ev.b > (int32_t)WISUN_SOCKET_READ_MAX) st.read_want_tx = true;
  }
}

static void on_join_state(int32_t n, const char* text) {
  if (n != st.join_state) {
    wlog_t l;
    wlog_begin(&l);
    wlog_str(&l, "Join state ");
    wlog_i32(&l, n);
    wlog_str(&l, ": ");
    wlog_str(&l, text);
    wlog_str(&l, "  (+");
    wlog_u32(&l, since(st.t_join) / 1000u);
    wlog_str(&l, " s)");
    wlog_end(&l);
  }
  const int32_t previous = st.join_state;
  st.join_state = n;
  if (n > st.best_join_state) {                    // progress restarts the join timeout
    st.best_join_state = n;
    st.t_progress = now();
  }
  if (previous == 5 && n >= 1 && n <= 4) network_lost("join state regressed");
}

static void cli_async_cb(void* ctx, const char* line) {
  (void)ctx;
  wisun_event_t ev;
  switch (ev_parse(line, &ev)) {
    case EV_MODULE_BOOT:
      if (st.state != NR_UART_SYNC || st.step > 1) restart("module banner: the E51 rebooted");
      break;
    case EV_JOIN_STATE:
      on_join_state(ev.a, ev.text);
      break;
    case EV_CONNECTING:
      wlog_line("connecting to ", ev.text);
      break;
    case EV_CONNECTED: {
      wlog_t l;
      wlog_begin(&l);
      wlog_str(&l, "connected after ");
      wlog_i32(&l, ev.a);
      wlog_str(&l, " s");
      wlog_end(&l);
      break;
    }
    case EV_IPV6:
      if (ipv6_is_global(ev.ip)) {
        str_copy(st.nr_ipv6, sizeof(st.nr_ipv6), ev.ip);
        wlog_line("NR global IPv6: ", ev.ip);
      }
      break;
    case EV_DISCONNECTED:
      st.disconnected_seen = true;
      if (st.join_state != 0) wlog_line("module reports Disconnected", NULL);
      st.join_state = 0;
      network_lost("[Disconnected]");
      break;
    case EV_LISTENING:
      st.listen_sock = ev.a;
      break;
    case EV_OPENING:
      st.pending_client = ev.a;
      break;
    case EV_OPENED:
      st.opened_id = ev.a;
      break;
    case EV_ACCEPTED:
      if (st.br_ipv6[0] && str_ieq(ev.ip, st.br_ipv6)) {
        if (st.rx_sock >= 0 && st.rx_sock != ev.a) request_close(st.rx_sock);  // BR reconnected
        st.rx_sock = ev.a;
        frame_stream_reset(&st.stream_rx);
        reasm_reset(&st.reasm);
        log_kv("downlink accepted from BR ", ev.ip, NULL, NULL);
      } else {
        log_kv("WARN: closing connection from unexpected address ", ev.ip, NULL, NULL);
        request_close(ev.a);
      }
      break;
    case EV_DATA_AVAILABLE:
      if (ev.b == 0) {
        if (ev.a == st.rx_sock || ev.a == st.tx_sock) sockets_lost("peer closed (Data available len 0)");
        else if (ev.a != st.listen_sock) request_close(ev.a);
      } else if (ev.a == st.rx_sock) {
        st.read_want_rx = true;
      } else if (ev.a == st.tx_sock) {
        st.read_want_tx = true;
      }
      break;
    case EV_CLOSED:
      if (close_pending(ev.a)) break;                // one we asked for
      if (ev.a == st.pending_client) st.client_closed = true;
      if (ev.a == st.rx_sock || ev.a == st.tx_sock) {
        sockets_lost("unrequested [Closed] on a data socket");
      } else if (ev.a == st.listen_sock) {
        wlog_line("WARN: listener closed unexpectedly; it will be re-created", NULL);
        st.listen_sock = -1;
      }
      break;
    case EV_PING_REPLY:
      if (st.br_ipv6[0] && str_ieq(ev.ip, st.br_ipv6)) st.ping_ok = true;
      break;
    case EV_UNKNOWN:
      wlog_line("unknown module line: ", line);
      break;
    default:
      break;                                         // responses handled by their command
  }
}

// ------------------------------------------------------------------ states --
static void st_uart_sync(void) {
  if (st.step == 0u) {
    cli_reset();
    st.cmd_pending = false;
    st.prompt_base = cli_prompt_count();
    st.t_aux = now() - WISUN_SYNC_PERIOD_MS;          // first "\r\n" right away
    next_step();
  }
  if (cli_prompt_count() != st.prompt_base) {
    go(NR_PROBE, "prompt seen");
    return;
  }
  if (since(st.t_aux) >= WISUN_SYNC_PERIOD_MS && cli_send_raw("\r\n")) st.t_aux = now();
  if (since(st.t_step) >= WISUN_SYNC_TIMEOUT_MS) {
    st.sync_cycles++;
    wlog_t l;
    wlog_begin(&l);
    wlog_str(&l, "ERROR: no \"> \" prompt from the E51 in 15 s (cycle ");
    wlog_u32(&l, st.sync_cycles);
    wlog_str(&l, ") -- check TX/RX wiring, 115200 8N1, 3.3 V and GND");
    wlog_end(&l);
    if (hal_module_reset_available()) {
      wlog_line("pulsing the module reset pin", NULL);
      hal_module_reset_pulse();
    }
    st.t_step = now();
  }
}

static void st_probe(void) {
  if (!sm_run("wisun get wisun", WISUN_CMD_GET_TIMEOUT_MS, NULL)) return;
  if (st.res == CLI_TIMEOUT) {
    go(NR_UART_SYNC, "probe timed out");
    return;
  }
  if (st.snap.br_firmware) {
    st.error_kind = ERR_BR_FIRMWARE;
    go(NR_ERROR, "wisun.state present: BR firmware on the NR side");
    return;
  }
  if (!st.snap.have_join_state) {
    if (++st.tries < 3u) {
      wlog_line("WARN: no wisun.join_state in the reply, probing again", NULL);
      return;
    }
    go(NR_UART_SYNC, "no wisun.join_state after 3 probes");
    return;
  }
  st.join_state = st.snap.join_state;
  if (st.snap.ip_global[0]) str_copy(st.nr_ipv6, sizeof(st.nr_ipv6), st.snap.ip_global);
  if (st.snap.br[0]) str_copy(st.br_ipv6, sizeof(st.br_ipv6), st.snap.br);

  wlog_t l;
  wlog_begin(&l);
  wlog_str(&l, "E51 is running NR firmware, join state ");
  wlog_i32(&l, st.join_state);
  wlog_end(&l);
  go(NR_CONFIG_CHECK, "NR firmware confirmed");
}

static void clean_then(nr_state_t next, const char* why) {
  st.after_clean = next;
  go(NR_CLEAN_SOCKETS, why);
}

static void st_config_check(void) {
  const snapshot_t* s = &st.snap;
  bool ok = true;
  char want[16];

  if (!s->have_name || strcmp(s->name, WISUN_NETWORK_NAME) != 0) {
    log_kv("config: network_name is \"", s->name, "\", want \"" WISUN_NETWORK_NAME, "\"");
    ok = false;
  }
  if (!s->have_domain || !str_ieq(s->domain, WISUN_REGULATORY_DOMAIN)) {
    log_kv("config: regulatory_domain is ", s->domain, ", want ", WISUN_REGULATORY_DOMAIN);
    ok = false;
  }
  if (!s->have_chan || s->chan != WISUN_CHAN_PLAN_ID) {
    cmd_with_int(want, sizeof(want), "", WISUN_CHAN_PLAN_ID, NULL);
    log_kv("config: chan_plan_id differs, want ", want, NULL, NULL);
    ok = false;
  }
  if (!s->have_phy || s->phy != WISUN_PHY_MODE_ID) {
    cmd_with_int(want, sizeof(want), "", WISUN_PHY_MODE_ID, NULL);
    log_kv("config: phy_mode_id differs, want ", want, NULL, NULL);
    ok = false;
  }

  st.cfg_matched = ok;
  if (ok) clean_then(AFTER_CLEAN_DECIDE, "settings match");
#if WISUN_APPLY_CONFIG
  else go(NR_APPLY_CONFIG, "settings differ");
#else
  else {
    st.error_kind = ERR_CONFIG_LOCKED;
    go(NR_ERROR, "settings differ and WISUN_APPLY_CONFIG is 0");
  }
#endif
}

static void apply_failed(const char* what) {
  log_kv("ERROR: ", what, " -- ", st.reason[0] ? st.reason : "no confirmation");
  if (++st.tries >= 3u) go(NR_UART_SYNC, "could not apply the network settings");
  else set_step(st.step);                            // try the same command again
}

static void st_apply_config(void) {
  char cmd[WISUN_CMD_MAX];
  switch (st.step) {
    case 0:
      st.disconnected_seen = false;
      next_step();
      break;
    case 1:
      if (!sm_run("wisun disconnect", WISUN_CMD_TIMEOUT_MS, NULL)) return;
      if (str_contains(st.reason, "already")) set_step(3);
      else next_step();
      break;
    case 2:
      if (st.disconnected_seen || since(st.t_step) >= WISUN_DISCONNECT_WAIT_MS) next_step();
      break;
    case 3:
      if (!sm_run("wisun set wisun.network_name \"" WISUN_NETWORK_NAME "\"",
                  WISUN_CMD_TIMEOUT_MS, "wisun.network_name = ")) return;
      if (st.res == CLI_OK && st.expect_seen) next_step();
      else apply_failed("set wisun.network_name");
      break;
    case 4:
      if (!sm_run("wisun set wisun.regulatory_domain " WISUN_REGULATORY_DOMAIN,
                  WISUN_CMD_TIMEOUT_MS, "wisun.regulatory_domain = ")) return;
      if (st.res == CLI_OK && st.expect_seen) next_step();
      else apply_failed("set wisun.regulatory_domain");
      break;
    case 5:
      cmd_with_int(cmd, sizeof(cmd), "wisun set wisun.chan_plan_id ", WISUN_CHAN_PLAN_ID, NULL);
      if (!sm_run(cmd, WISUN_CMD_TIMEOUT_MS, "wisun.chan_plan_id = ")) return;
      if (st.res == CLI_OK && st.expect_seen) next_step();
      else apply_failed("set wisun.chan_plan_id");
      break;
    case 6:
      cmd_with_int(cmd, sizeof(cmd), "wisun set wisun.phy_mode_id ", WISUN_PHY_MODE_ID, NULL);
      if (!sm_run(cmd, WISUN_CMD_TIMEOUT_MS, "wisun.phy_mode_id = ")) return;
      if (st.res == CLI_OK && st.expect_seen) next_step();
      else apply_failed("set wisun.phy_mode_id");
      break;
    case 7:
      // "[Failed: unable to set the key: app]" is expected on some builds.
      if (!sm_run("wisun set app.autoconnect 0", WISUN_CMD_TIMEOUT_MS, NULL)) return;
      next_step();
      break;
    case 8:
      if (!sm_run("wisun save", WISUN_CMD_TIMEOUT_MS, "[Settings saved]")) return;
      if (!st.expect_seen) {
        apply_failed("wisun save");
        return;
      }
      wlog_line("network settings applied and saved (no reboot needed)", NULL);
      st.join_state = 0;
      st.nr_ipv6[0] = st.br_ipv6[0] = '\0';
      st.cfg_matched = false;
      clean_then(NR_PREPARE_JOIN, "settings saved");
      break;
    default:
      break;
  }
}

static void st_clean_sockets(void) {
  if (st.step == 0u) {
    if (!sm_run("wisun socket_list", WISUN_CMD_SOCKET_TIMEOUT_MS, NULL)) return;
    if (st.res != CLI_OK) wlog_line("WARN: socket_list failed: ", st.reason);
    // Highest id first, as the verified sessions did.
    for (uint8_t i = 0; i < st.listed_count; i++) {
      for (uint8_t j = (uint8_t)(i + 1u); j < st.listed_count; j++) {
        if (st.listed[j] > st.listed[i]) {
          const int32_t t = st.listed[i];
          st.listed[i] = st.listed[j];
          st.listed[j] = t;
        }
      }
    }
    for (uint8_t i = 0; i < st.listed_count; i++) request_close(st.listed[i]);
    st.tx_sock = st.rx_sock = -1;
    st.listen_sock = st.listed_listener;

    wlog_t l;
    wlog_begin(&l);
    wlog_str(&l, "closing ");
    wlog_u32(&l, st.listed_count);
    wlog_str(&l, " stale socket(s)");
    if (st.listen_sock >= 0) {
      wlog_str(&l, ", keeping listener ");
      wlog_i32(&l, st.listen_sock);
    }
    wlog_end(&l);
    next_step();
    return;
  }

  if (st.close_count && since(st.t_step) < WISUN_CMD_SOCKET_TIMEOUT_MS * 4u) return;

  nr_state_t next = st.after_clean;
  if (next == AFTER_CLEAN_DECIDE) {
    if (st.join_state == 5 && st.cfg_matched) {
      go(NR_GET_ADDRESSES, "already Operational: no rejoin");
      return;
    }
    next = NR_PREPARE_JOIN;
  }
  go(next, "sockets clean");
}

static void st_prepare_join(void) {
  switch (st.step) {
    case 0:
      if (since(st.t_state) < st.hold_ms) return;   // join back-off
      st.hold_ms = 0;
      st.nr_ipv6[0] = st.br_ipv6[0] = '\0';
      st.disconnected_seen = false;
      // join_state 0 can be stale: right after join_fan11 the module is joining
      // before its first [Join state] line, so a requested rejoin always disconnects.
      set_step((st.join_state == 0 && !st.force_disconnect) ? 3 : 1);
      st.force_disconnect = false;
      break;
    case 1:
      if (!sm_run("wisun disconnect", WISUN_CMD_TIMEOUT_MS, NULL)) return;
      if (str_contains(st.reason, "already")) set_step(3);
      else next_step();
      break;
    case 2:
      if (st.disconnected_seen || since(st.t_step) >= WISUN_DISCONNECT_WAIT_MS) next_step();
      break;
    case 3:
      if (!sm_run("wisun ccc", WISUN_CMD_TIMEOUT_MS, "[Credential cache cleared]")) return;
      if (!st.expect_seen) wlog_line("WARN: no [Credential cache cleared]; joining anyway", NULL);
      go(NR_JOIN, "credential cache cleared");
      break;
    default:
      break;
  }
}

static void st_join(void) {
  if (!sm_run("wisun join_fan11", WISUN_CMD_JOIN_TIMEOUT_MS, NULL)) return;
  if (st.res == CLI_OK || str_contains(st.reason, "already")) {
    st.join_attempts++;
    wlog_t l;
    wlog_begin(&l);
    wlog_str(&l, "join attempt ");
    wlog_u32(&l, st.join_attempts);
    wlog_str(&l, " started (timeout 360 s)");
    wlog_end(&l);
    go(NR_WAIT_OPERATIONAL, "join_fan11 accepted");
  } else {
    wlog_line("join_fan11 failed: ", st.reason);
    schedule_rejoin("join_fan11 failed");
  }
}

static void st_wait_operational(void) {
  if (st.step == 0u) {
    if (st.join_state == 5 && ipv6_is_global(st.nr_ipv6)) {
      st.backoff_ms = 0;
      go(NR_GET_ADDRESSES, "Operational with a global IPv6");
      return;
    }
    // The timeout counts from the last progress, not the start of the attempt:
    // a BR that comes up part-way through must not cost a join that is nearly
    // done. WISUN_JOIN_MAX_MS still bounds an attempt that keeps regressing.
    const bool stalled = since(st.t_progress) >= WISUN_JOIN_TIMEOUT_MS;
    if (stalled || since(st.t_join) >= WISUN_JOIN_MAX_MS) {
      wlog_t l;
      wlog_begin(&l);
      wlog_str(&l, stalled ? "WARN: no join progress for 360 s, best join state " : "WARN: join still not done after 900 s, best join state ");
      wlog_i32(&l, st.best_join_state);
      wlog_str(&l, " (join attempts: ");
      wlog_u32(&l, st.join_attempts);
      wlog_str(&l, ")");
      wlog_end(&l);
      next_step();
      return;
    }
    // Backup poll -- the async join events are the primary signal (spec 3.4).
    if (st.polling || since(st.t_aux) >= WISUN_JOIN_POLL_MS) {
      st.polling = true;
      if (!sm_run("wisun get wisun", WISUN_CMD_GET_TIMEOUT_MS, NULL)) return;
      st.polling = false;
      st.t_aux = now();
      if (st.res == CLI_OK && st.snap.have_join_state) {
        if (st.snap.join_state != st.join_state) on_join_state(st.snap.join_state, "(from get wisun)");
        if (st.snap.ip_global[0]) str_copy(st.nr_ipv6, sizeof(st.nr_ipv6), st.snap.ip_global);
      }
    }
    return;
  }
  // step 1: give up on this attempt and back off.
  if (!sm_run("wisun disconnect", WISUN_CMD_TIMEOUT_MS, NULL)) return;
  st.join_state = 0;
  schedule_rejoin("join timeout");
}

static void st_get_addresses(void) {
  if (st.step == 0u) {
    if (!sm_run("wisun get wisun", WISUN_CMD_GET_TIMEOUT_MS, NULL)) return;
    if (st.res == CLI_OK && st.snap.have_join_state && st.snap.join_state != 5) {
      st.join_state = st.snap.join_state;
      go(NR_WAIT_OPERATIONAL, "not Operational any more");
      return;
    }
    if (st.snap.ip_global[0]) str_copy(st.nr_ipv6, sizeof(st.nr_ipv6), st.snap.ip_global);
    if (st.res == CLI_OK && st.snap.br[0]) {
      str_copy(st.br_ipv6, sizeof(st.br_ipv6), st.snap.br);
      log_kv("NR IPv6 ", st.nr_ipv6, "   BR IPv6 ", st.br_ipv6);
      go(NR_VERIFY_PING, "border router address discovered");
      return;
    }
    if (since(st.t_state) >= WISUN_BR_ADDR_GIVEUP_MS) {
      schedule_rejoin("no border router address after 60 s");
      return;
    }
    wlog_line("border router address not known yet, retrying in 5 s", NULL);
    next_step();
    return;
  }
  if (since(st.t_step) >= WISUN_BR_ADDR_RETRY_MS) set_step(0);
}

static void after_ping(const char* why) {
  go(st.listen_sock >= 0 ? NR_CONNECT_UPLINK : NR_OPEN_SERVER, why);
}

static void st_verify_ping(void) {
  char cmd[WISUN_CMD_MAX];
  switch (st.step) {
    case 0:
      st.ping_ok = false;
      next_step();
      break;
    case 1:
      fmt_str(cmd, fmt_str(cmd, 0, sizeof(cmd), "wisun ping "), sizeof(cmd), st.br_ipv6);
      if (!sm_run(cmd, WISUN_CMD_TIMEOUT_MS, NULL)) return;
      set_step(st.res == CLI_OK ? 2 : 3);
      break;
    case 2:
      if (st.ping_ok) {
        after_ping("BR answered ping");
        return;
      }
      if (since(st.t_step) >= WISUN_PING_WAIT_MS) set_step(3);
      break;
    case 3:
      if (++st.tries >= WISUN_PING_TRIES) {
        wlog_line("WARN: no ping reply from the BR after 3 tries; trying TCP anyway", NULL);
        after_ping("ping unanswered, continuing");
        return;
      }
      next_step();
      break;
    case 4:
      if (since(st.t_step) >= WISUN_PING_GAP_MS) set_step(0);
      break;
    default:
      break;
  }
}

static void st_open_server(void) {
  char cmd[40];
  switch (st.step) {
    case 0:
      if (st.listen_sock >= 0) {
        go(NR_CONNECT_UPLINK, "listener already open");
        return;
      }
      next_step();
      break;
    case 1:
      // A listener may already exist (kept across a network loss or rejoin):
      // reuse it rather than fail to bind a second one.
      if (!sm_run("wisun socket_list", WISUN_CMD_SOCKET_TIMEOUT_MS, NULL)) return;
      if (st.res == CLI_OK && st.listed_listener >= 0) {
        st.listen_sock = st.listed_listener;
        st.bind_retried = false;
        go(NR_CONNECT_UPLINK, "reusing the listener already open on the port");
        return;
      }
      next_step();
      break;
    case 2:
      cmd_with_int(cmd, sizeof(cmd), "wisun tcp_server ", (int32_t)WISUN_APP_PORT, NULL);
      if (!sm_run(cmd, WISUN_CMD_SOCKET_TIMEOUT_MS, NULL)) return;
      if (st.listen_sock >= 0) {
        st.bind_retried = false;
        go(NR_CONNECT_UPLINK, "listening");
        return;
      }
      if (st.res == CLI_OK) {
        next_step();                                 // [Listening] may trail the prompt
        return;
      }
      set_step(4);
      break;
    case 3:
      if (st.listen_sock >= 0) {
        st.bind_retried = false;
        go(NR_CONNECT_UPLINK, "listening");
      } else if (since(st.t_step) >= 2000u) {
        set_step(4);
      }
      break;
    case 4:
      wlog_line("tcp_server failed: ", st.reason[0] ? st.reason : "no [Listening]");
      if (!st.bind_retried) {
        st.bind_retried = true;
        clean_then(NR_OPEN_SERVER, "bind failed: cleaning sockets once");
      } else {
        st.error_kind = ERR_BIND;
        go(NR_ERROR, "tcp_server failed twice");
      }
      break;
    default:
      break;
  }
}

static void st_connect_uplink(void) {
  char cmd[WISUN_CMD_MAX];
  switch (st.step) {
    case 0:
      if (st.listen_sock < 0) {
        go(NR_OPEN_SERVER, "listener missing");
        return;
      }
      st.pending_client = -1;
      st.opened_id = -1;
      st.client_closed = false;
      next_step();
      break;
    case 1: {
      size_t n = fmt_str(cmd, 0, sizeof(cmd), "wisun tcp_client ");
      n = fmt_str(cmd, n, sizeof(cmd), st.br_ipv6);
      n = fmt_str(cmd, n, sizeof(cmd), " ");
      fmt_u32(cmd, n, sizeof(cmd), WISUN_APP_PORT);
      if (!sm_run(cmd, WISUN_CMD_SOCKET_TIMEOUT_MS, NULL)) return;
      set_step(st.res == CLI_OK ? 2 : 3);
      break;
    }
    case 2:
      if (st.pending_client >= 0 && st.opened_id == st.pending_client) {
        st.tx_sock = st.pending_client;
        st.pending_client = -1;
        // connect_fails is cleared in READY, not here: an uplink that opens but
        // is never answered is still a failed attempt, and the next one waits longer.
        go(NR_WAIT_DOWNLINK, "uplink opened");
        return;
      }
      if (st.client_closed || since(st.t_step) >= WISUN_OPENED_WAIT_MS) set_step(3);
      break;
    case 3:
      wlog_line("WARN: uplink to the BR did not open", NULL);
      request_close(st.pending_client);
      st.pending_client = -1;
      if (++st.connect_fails >= WISUN_CONNECT_FAILS_RECHECK) {
        st.connect_fails = 0;
        set_step(5);
      } else {
        next_step();
      }
      break;
    case 4:
      if (since(st.t_step) >= retry_delay(WISUN_CONNECT_RETRY_MS)) set_step(0);
      break;
    case 5:
      if (!sm_run("wisun get wisun", WISUN_CMD_GET_TIMEOUT_MS, NULL)) return;
      if (st.res == CLI_OK && st.snap.have_join_state && st.snap.join_state != 5) {
        st.join_state = st.snap.join_state;
        st.listen_sock = -1;                         // kept open on the module; found again by OPEN_SERVER
        schedule_rejoin("5 uplink failures and not Operational");
        return;
      }
      set_step(4);
      break;
    default:
      break;
  }
}

static void st_wait_downlink(void) {
  if (st.step == 0u) {
    if (st.rx_sock >= 0) {
      go(NR_READY, "BR connected back");
      return;
    }
    if (since(st.t_state) >= WISUN_ACCEPT_WAIT_MS) {
      wlog_line("WARN: BR did not connect back within 30 s; reconnecting uplink", NULL);
      request_close(st.tx_sock);
      st.tx_sock = -1;
      if (st.connect_fails < 255u) st.connect_fails++;   // backs off the next attempt
      next_step();
    }
    return;
  }
  if (since(st.t_step) >= retry_delay(WISUN_ACCEPT_RETRY_MS)) go(NR_CONNECT_UPLINK, "retry after no downlink");
}

static void st_close_sockets(void) {
  if (st.close_count && since(st.t_state) < WISUN_CMD_SOCKET_TIMEOUT_MS * 4u) return;
  go(st.close_next, st.close_next == NR_WAIT_OPERATIONAL ? "waiting for the stack to recover"
                                                         : "sockets closed");
}

static void st_error(void) {
  if (since(st.t_aux) >= WISUN_ERROR_LOG_PERIOD_MS) {
    st.t_aux = now();
    wlog_line("ERROR: ", st.error_kind == ERR_BR_FIRMWARE
                             ? "BR firmware on the NR side -- flash the E51 with node (NR) firmware"
                         : st.error_kind == ERR_CONFIG_LOCKED
                             ? "module network settings differ from wisun_config.h; not changing "
                               "them because WISUN_APPLY_CONFIG is 0"
                             : "cannot open the TCP server socket");
  }
  if (since(st.t_state) >= WISUN_ERROR_RETRY_MS) go(NR_UART_SYNC, "retrying after error");
}

// --------------------------------------------------------------- interface --
void nr_init(void) {
  // Callbacks survive: an application registers them once, and may do so
  // before nr_init() -- or call nr_init() again after an MCU-side restart.
  const nr_receive_fn on_receive = st.on_receive;
  const nr_delivery_fn on_delivery = st.on_delivery;
  memset(&st, 0, sizeof(st));
  st.on_receive = on_receive;
  st.on_delivery = on_delivery;
  st.state = NR_UART_SYNC;
  const cli_handlers_t h = {
    .ctx = NULL, .line = cli_line_cb, .done = cli_done_cb,
    .async = cli_async_cb, .data = cli_data_cb,
  };
  cli_init(&h);
  (void)hal_uart_init(WISUN_MODULE_BAUD);
  restart("boot");
}

void nr_poll(void) {
  cli_poll();

  switch (st.state) {
    case NR_UART_SYNC: st_uart_sync(); break;
    case NR_PROBE: st_probe(); break;
    case NR_CONFIG_CHECK: st_config_check(); break;
    case NR_APPLY_CONFIG: st_apply_config(); break;
    case NR_CLEAN_SOCKETS: st_clean_sockets(); break;
    case NR_PREPARE_JOIN: st_prepare_join(); break;
    case NR_JOIN: st_join(); break;
    case NR_WAIT_OPERATIONAL: st_wait_operational(); break;
    case NR_GET_ADDRESSES: st_get_addresses(); break;
    case NR_VERIFY_PING: st_verify_ping(); break;
    case NR_OPEN_SERVER: st_open_server(); break;
    case NR_CONNECT_UPLINK: st_connect_uplink(); break;
    case NR_WAIT_DOWNLINK: st_wait_downlink(); break;
    case NR_READY: ready_poll(); break;
    case NR_CLOSE_SOCKETS: st_close_sockets(); break;
    case NR_ERROR: st_error(); break;
    default: break;
  }

  // Acks for frames that arrived before READY still have to go out.
  if (st.state != NR_READY && st.tx_sock >= 0 && st.ctrl_count) tx_poll();
  read_poll();
  close_poll();
}

bool nr_is_ready(void) { return st.state == NR_READY; }

bool nr_send(const uint8_t* data, size_t len, uint32_t* msg_id) {
  if (st.state != NR_READY || !data || len == 0u || len > WISUN_MSG_MAX ||
      st.app_count >= WISUN_APP_QUEUE_DEPTH) {
    return false;
  }
  app_msg_t* m = &st.app_q[(st.app_head + st.app_count) % WISUN_APP_QUEUE_DEPTH];
  m->id = ++st.next_msg_id;
  m->len = (uint16_t)len;
  memcpy(m->data, data, len);
  st.app_count++;
  if (msg_id) *msg_id = m->id;
  return true;
}

void nr_on_receive(nr_receive_fn cb) { st.on_receive = cb; }
void nr_on_delivery_result(nr_delivery_fn cb) { st.on_delivery = cb; }

const char* nr_state_name(nr_state_t s) {
  return (unsigned)s < NR_STATE_COUNT ? k_state_names[s] : "?";
}

void nr_status(nr_status_t* o) {
  memset(o, 0, sizeof(*o));
  o->state = st.state;
  o->state_name = k_state_names[st.state];
  o->join_state = st.join_state;
  o->state_elapsed_s = since(st.state == NR_WAIT_OPERATIONAL ? st.t_join : st.t_state) / 1000u;
  str_copy(o->nr_ipv6, sizeof(o->nr_ipv6), st.nr_ipv6);
  str_copy(o->br_ipv6, sizeof(o->br_ipv6), st.br_ipv6);
  o->listen_sock = st.listen_sock;
  o->tx_sock = st.tx_sock;
  o->rx_sock = st.rx_sock;
  o->join_attempts = st.join_attempts;
  o->frames_tx = st.frames_tx;
  o->frames_rx = st.frames_rx;
  o->retries = st.retries;
  o->crc_errors = st.crc_errors;
  o->link_losses = st.link_losses;
  o->msgs_delivered = st.msgs_ok;
  o->msgs_failed = st.msgs_failed;
  o->msgs_received = st.msgs_rx;
  o->queued = st.app_count;
  o->uart_rx_overflows = hal_uart_rx_overflows();
  o->cli_line_overflows = cli_line_overflows();
}

bool nr_raw(const char* cli_command) {
  return cli_enqueue(cli_command, WISUN_CMD_SOCKET_TIMEOUT_MS, TAG_RAW);
}

void nr_rejoin(void) {
  st.backoff_ms = 0;
  st.hold_ms = 0;
  st.force_disconnect = true;
  if (online_state()) {
    st.link_losses++;
    begin_close(true, NR_PREPARE_JOIN, "rejoin requested");
  } else {
    go(NR_PREPARE_JOIN, "rejoin requested");
  }
}
