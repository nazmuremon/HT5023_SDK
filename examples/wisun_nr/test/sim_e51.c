// test/sim_e51.c — see sim_e51.h.
#include "sim_e51.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../wisun_config.h"
#include "../wisun_frame.h"

bool hal_host_rx_push(uint8_t b);

#define BYTES_PER_MS 11u               // 115200 baud, 10 bits a byte
#define MAX_SOCKS 24
#define MAX_ACTIONS 128
#define OUT_CAP (1u << 20)
#define BR_IP "fd12:3456::3610:f4ff:fea8:7369"
#define NR_IP "fd12:3456::3610:f4ff:fea8:7a1d"
#define NR_LL "fe80::3610:f4ff:fea8:7a1d"

typedef enum { K_SERVER, K_CLIENT, K_ACCEPTED } sock_kind_t;

typedef struct {
  bool used;
  int id;
  sock_kind_t kind;
  int port;                          // local port for a server, remote otherwise
  char rip[64];
  bool active;
  bool peer_closed;
  char rx[16384];
  size_t rx_len;
} sock_t;

typedef enum {
  A_TEXT,            // emit a line
  A_OPENED,          // client socket connected
  A_CONNECT_FAIL,    // client socket refused
  A_BR_CONNECT_BACK, // BR connects to the NR listener
  A_BR_RX,           // bytes written by the NR reach the BR
  A_TO_NR,           // bytes written by the BR reach an NR socket
  A_PEER_CLOSE,      // BR closed its end of an NR socket
  A_DISCONNECTED,
} act_kind_t;

typedef struct {
  bool used;
  uint32_t at;
  act_kind_t kind;
  int sock;
  char text[400];
} action_t;

static struct {
  sim_opts_t o;
  uint32_t now;
  char name[64];
  char domain[8];
  int chan, phy;
  char s_name[64];
  char s_domain[8];
  int s_chan, s_phy;

  int join_state;
  bool joining;
  uint32_t phase_start;
  bool phase_valid;
  uint32_t lost_at;                  // BR went away while Operational

  sock_t socks[MAX_SOCKS];
  int next_id;

  char in[512];
  size_t in_len;

  char out[OUT_CAP];
  size_t out_len;
  size_t out_pos;                    // bytes already on the wire to the MCU
  size_t prompt_end;                 // the MCU must read past this before the next command

  action_t acts[MAX_ACTIONS];
  sim_stats_t stats;

  // Seen on the hardware (and by the BR application): after a listener is
  // closed, the E51 refuses to bind that port again for about 4 minutes.
  int blocked_port;
  uint32_t blocked_until;
} m;

#define SIM_BIND_BLOCK_MS 240000u

static struct {
  bool powered;
  int uplink;                        // NR socket id of the NR -> BR connection
  int downlink;                      // NR socket id of the BR -> NR connection
  frame_stream_t stream;
  reasm_t reasm;
  uint16_t seq;
  int drop_acks;
  // outgoing BR -> NR message (stop-and-wait)
  uint8_t msg[1100];
  size_t msg_len, msg_off;
  bool sending;
  bool awaiting;
  uint16_t out_seq;
  uint8_t tries;
  uint32_t sent_at;
  bool lose_first_ack;
  bool corrupt_next;
  bool app_down;                     // the BR application is not running
} br;

#define SIM_READ_WRAP 64u     // socket_read payload characters per line, as on the module

// ------------------------------------------------------------------ output --
static bool inside_data_header(size_t pos) {
  // True if the line ending at pos is a "[Data from ..." header awaiting payload.
  size_t start = pos;
  while (start > 0 && m.out[start - 1] != '\n') start--;
  return strncmp(m.out + start, "[Data from ", 11) == 0 && m.out[pos - 1] != ']';
}

static void out_raw(const char* s) {
  size_t n = strlen(s);
  if (m.out_len + n >= OUT_CAP) {
    fprintf(stderr, "simulator output overflow\n");
    exit(2);
  }
  memcpy(m.out + m.out_len, s, n);
  m.out_len += n;
}

static void out_line(const char* s) {
  out_raw(s);
  out_raw("\r\n");
}

// Async lines are spliced in at the next line boundary still ahead of the
// wire, so they can land in the middle of a long response -- as they do on the
// real module.
static void out_async(const char* s) {
  size_t at = m.out_pos;
  while (at < m.out_len && m.out[at] != '\n') at++;
  if (at >= m.out_len || inside_data_header(at + 1)) {
    out_line(s);
    return;
  }
  at++;
  size_t n = strlen(s) + 2u;
  memmove(m.out + at + n, m.out + at, m.out_len - at);
  memcpy(m.out + at, s, n - 2u);
  m.out[at + n - 2u] = '\r';
  m.out[at + n - 1u] = '\n';
  m.out_len += n;
  if (m.prompt_end > at) m.prompt_end += n;
}

static void prompt(void) {
  out_raw("> ");
  m.prompt_end = m.out_len;
}

static void outf(const char* fmt, ...) {
  char buf[600];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  out_line(buf);
}

static void schedule(uint32_t delay, act_kind_t kind, int sock, const char* text) {
  for (int i = 0; i < MAX_ACTIONS; i++) {
    if (!m.acts[i].used) {
      m.acts[i].used = true;
      m.acts[i].at = m.now + delay;
      m.acts[i].kind = kind;
      m.acts[i].sock = sock;
      snprintf(m.acts[i].text, sizeof(m.acts[i].text), "%s", text ? text : "");
      return;
    }
  }
  fprintf(stderr, "simulator action table full\n");
  exit(2);
}

// ----------------------------------------------------------------- sockets --
static sock_t* sock_find(int id) {
  for (int i = 0; i < MAX_SOCKS; i++) {
    if (m.socks[i].used && m.socks[i].id == id) return &m.socks[i];
  }
  return NULL;
}

static sock_t* sock_new(sock_kind_t kind, int port, const char* rip) {
  for (int i = 0; i < MAX_SOCKS; i++) {
    if (!m.socks[i].used) {
      memset(&m.socks[i], 0, sizeof(m.socks[i]));
      m.socks[i].used = true;
      m.socks[i].id = m.next_id++;
      m.socks[i].kind = kind;
      m.socks[i].port = port;
      snprintf(m.socks[i].rip, sizeof(m.socks[i].rip), "%s", rip);
      return &m.socks[i];
    }
  }
  return NULL;
}

static sock_t* listener(int port) {
  for (int i = 0; i < MAX_SOCKS; i++) {
    if (m.socks[i].used && m.socks[i].kind == K_SERVER && m.socks[i].port == port) return &m.socks[i];
  }
  return NULL;
}

static bool network_up(void) { return m.join_state == 5 && br.powered; }

// --------------------------------------------------------- border router --
static void br_write_to_nr(const char* text) {
  if (br.downlink < 0) return;                       // no connection back: lost
  schedule(80, A_TO_NR, br.downlink, text);
}

static void br_send_frame(char type, uint16_t seq, const uint8_t* p, size_t n) {
  char f[200];
  frame_encode(type, seq, p, n, f, sizeof(f));
  if (br.corrupt_next) {                             // flip a CRC digit
    br.corrupt_next = false;
    size_t len = strlen(f);
    f[len - 2] = (f[len - 2] == '0') ? '1' : '0';
  }
  br_write_to_nr(f);
}

static void br_send_next_fragment(void) {
  size_t chunk = br.msg_len - br.msg_off;
  bool more = chunk > WISUN_FRAME_MAX_PAYLOAD;
  if (more) chunk = WISUN_FRAME_MAX_PAYLOAD;
  br_send_frame(more ? 'F' : 'D', br.out_seq, br.msg + br.msg_off, chunk);
  br.awaiting = true;
  br.sent_at = m.now;
}

static void br_on_frame(void* ctx, const char* body, size_t len) {
  (void)ctx;
  frame_t f;
  frame_status_t s = frame_decode(body, len, &f);
  if (s == FRAME_BAD_CRC) {
    m.stats.br_crc_errors++;
    return;
  }
  if (s != FRAME_OK) return;
  m.stats.br_frames_rx++;
  switch (f.type) {
    case 'D':
    case 'F': {
      if (br.drop_acks > 0) {
        br.drop_acks--;
      } else {
        br_send_frame('A', f.seq, NULL, 0);
        m.stats.br_acks_sent++;
      }
      reasm_result_t r = reasm_accept(&br.reasm, &f);
      if (r == REASM_COMPLETE) {
        m.stats.br_msgs++;
        m.stats.br_last_len = br.reasm.len;
        memcpy(m.stats.br_last_msg, br.reasm.msg, br.reasm.len);
      }
      break;
    }
    case 'H':
      m.stats.br_hb_rx++;
      br_send_frame('R', f.seq, NULL, 0);
      break;
    case 'A':
      if (br.sending && br.awaiting && f.seq == br.out_seq) {
        if (br.lose_first_ack) {                     // pretend it never arrived
          br.lose_first_ack = false;
          break;
        }
        br.awaiting = false;
        size_t chunk = br.msg_len - br.msg_off;
        if (chunk > WISUN_FRAME_MAX_PAYLOAD) chunk = WISUN_FRAME_MAX_PAYLOAD;
        br.msg_off += chunk;
        if (br.msg_off >= br.msg_len) {
          br.sending = false;
          m.stats.br_send_ok++;
        } else {
          br.out_seq = br.seq++;
          br.tries = 0;
          br_send_next_fragment();
        }
      }
      break;
    default:
      break;
  }
}

static void br_tick(void) {
  if (br.sending && br.awaiting && m.now - br.sent_at >= WISUN_ACK_TIMEOUT_MS) {
    if (br.tries < WISUN_ACK_RETRIES) {
      br.tries++;
      br_send_next_fragment();
    } else {
      br.sending = false;
      m.stats.br_send_failed++;
    }
  }
  m.stats.br_connected_both = br.powered && br.uplink >= 0 && br.downlink >= 0;
}

// --------------------------------------------------------------- join model --
static const char* k_state_text[] = {
  "Disconnected", "Join state 1: Select PAN", "Join state 2: Authenticate",
  "Join state 3: Acquire PAN Config", "Join state 4: Configure Routing - DAO registration",
  "Join state 5: Operational",
};

static bool settings_match_br(void) {
  return strcmp(m.name, WISUN_NETWORK_NAME) == 0 && strcmp(m.domain, WISUN_REGULATORY_DOMAIN) == 0 &&
         m.chan == WISUN_CHAN_PLAN_ID && m.phy == WISUN_PHY_MODE_ID;
}

static void set_join(int n) {
  if (n == m.join_state) return;
  m.join_state = n;
  if (n == 4) {
    out_async("[Join state 4: Configure Routing - parent selection]");
    out_async("[Join state 4: Configure Routing - DHCP]");
    out_async("[Join state 4: Configure Routing - address registration]");
    out_async("[Join state 4: Configure Routing - DAO registration]");
  } else if (n >= 1) {
    char b[80];
    snprintf(b, sizeof(b), "[%s]", k_state_text[n]);
    out_async(b);
  }
  if (n == 5) {
    char b[120];
    snprintf(b, sizeof(b), "[Connected: %u s]", (unsigned)((m.now - m.phase_start) / 1000u));
    out_async(b);
    snprintf(b, sizeof(b), "[IPv6 address: %s]", NR_IP);
    out_async(b);
    m.joining = false;
  }
}

static void join_tick(void) {
  // Operational but the BR vanished: the stack notices after 45 s and falls
  // back to searching, recovering on its own when the BR returns.
  if (m.join_state == 5 && !br.powered) {
    if (!m.lost_at) m.lost_at = m.now;
    if (m.now - m.lost_at >= 45000u) {
      m.lost_at = 0;
      m.joining = true;
      m.phase_valid = false;
      set_join(1);
    }
    return;
  }
  m.lost_at = 0;
  if (!m.joining) return;

  if (!br.powered || !settings_match_br()) {
    m.phase_valid = false;
    if (m.join_state != 1) set_join(1);
    return;
  }
  if (!m.phase_valid) {
    m.phase_valid = true;
    m.phase_start = m.now;
  }
  const uint32_t t = m.now - m.phase_start;
  if (t >= 150000u) set_join(5);
  else if (t >= 140000u) set_join(4);
  else if (t >= 50000u) set_join(3);
  else if (t >= 40000u) set_join(2);
  else set_join(1);
}

// --------------------------------------------------------------- commands --
static void dump_get_wisun(void) {
  m.stats.get_wisun++;
  if (m.o.br_firmware) {
    outf("wisun.state = Operational (5)");
    outf("wisun.network_name = \"%s\"", m.name);
    return;
  }
  char js[80];
  if (m.join_state == 0) snprintf(js, sizeof(js), "Disconnected (0)");
  else snprintf(js, sizeof(js), "%s (%d)", k_state_text[m.join_state], m.join_state);
  outf("wisun.join_state = %s", js);
  outf("wisun.network_name = \"%s\"", m.name);
  outf("wisun.network_size = small");
  outf("wisun.regulatory_domain = %s (1)", m.domain);
  outf("wisun.operating_class = 1");
  outf("wisun.operating_mode = 0x1b (1b)");
  outf("wisun.chan_plan_id = %d", m.chan);
  outf("wisun.phy_mode_id = %d", m.phy);
  outf("wisun.unicast_dwell_interval = 255");
  outf("wisun.broadcast_dwell_interval = 255");
  outf("wisun.broadcast_interval = 1020");
  outf("wisun.lfn_broadcast_interval = 60000");
  outf("wisun.lfn_broadcast_sync_period = 5");
  outf("wisun.allowed_channels = 0-255");
  outf("wisun.tx_power = 20");
  outf("wisun.tx_power_ddbm = 200");
  outf("wisun.device_type = FFN (0)");
  outf("wisun.lfn_profile = test (0)");
  outf("wisun.mac_address = 34:10:f4:ff:fe:a8:7a:1d");
  if (m.join_state == 5) outf("wisun.ip_addresses = [%s, %s]", NR_IP, NR_LL);
  else outf("wisun.ip_addresses = [%s]", NR_LL);
  if (m.join_state == 5) outf("wisun.border_router = %s", BR_IP);
  for (int i = 0; i < 40; i++) outf("wisun.stat_counter_%02d = %d", i, i * 37);
}

static int split(char* s, char** argv, int max) {
  int n = 0;
  char* p = s;
  while (*p && n < max) {
    while (*p == ' ') p++;
    if (!*p) break;
    if (*p == '"') {
      argv[n++] = ++p;
      while (*p && *p != '"') p++;
    } else {
      argv[n++] = p;
      while (*p && *p != ' ') p++;
    }
    if (*p) *p++ = '\0';
  }
  return n;
}

static void cmd(char* line) {
  char copy[512];
  snprintf(copy, sizeof(copy), "%s", line);
  out_line(copy);                                    // echo

  if (!line[0]) {
    out_raw("> ");            // a UART-sync blank line: not a command, no single-flight check
    return;
  }

  m.stats.commands++;
  if (strlen(copy) > m.stats.max_cmd_len) m.stats.max_cmd_len = (uint32_t)strlen(copy);
  if (m.out_pos < m.prompt_end) m.stats.single_flight_violations++;

  char* argv[8];
  int argc = split(line, argv, 8);

  if (argc == 1 && strcmp(argv[0], "reboot") == 0) {
    m.stats.reboot_cmds++;
    out_line("No command found");
  } else if (argc < 2 || strcmp(argv[0], "wisun") != 0) {
    out_line("No command found");
  } else if (strcmp(argv[1], "get") == 0 && argc == 3 && strcmp(argv[2], "wisun") == 0) {
    dump_get_wisun();
  } else if (strcmp(argv[1], "set") == 0 && argc == 4) {
    if (strcmp(argv[2], "wisun.network_name") == 0) snprintf(m.name, sizeof(m.name), "%s", argv[3]);
    else if (strcmp(argv[2], "wisun.regulatory_domain") == 0) snprintf(m.domain, sizeof(m.domain), "%s", argv[3]);
    else if (strcmp(argv[2], "wisun.chan_plan_id") == 0) m.chan = atoi(argv[3]);
    else if (strcmp(argv[2], "wisun.phy_mode_id") == 0) m.phy = atoi(argv[3]);
    else if (strncmp(argv[2], "app.", 4) == 0) {
      out_line("[Failed: unable to set the key: app]");
      prompt();
      return;
    }
    outf("%s = %s", argv[2], argv[3]);
  } else if (strcmp(argv[1], "save") == 0) {
    m.stats.saves++;
    snprintf(m.s_name, sizeof(m.s_name), "%s", m.name);
    snprintf(m.s_domain, sizeof(m.s_domain), "%s", m.domain);
    m.s_chan = m.chan;
    m.s_phy = m.phy;
    out_line("[Settings saved]");
  } else if (strcmp(argv[1], "ccc") == 0) {
    out_line("[Credential cache cleared]");
  } else if (strcmp(argv[1], "join_fan11") == 0) {
    m.stats.join_fan11++;
    out_line("[Using built-in trusted CA #0]");
    out_line("[Using built-in device credentials]");
    outf("[Connecting to \"%s\"]", m.name);
    m.joining = true;
    m.phase_valid = false;
    m.join_state = 0;
  } else if (strcmp(argv[1], "disconnect") == 0) {
    if (m.join_state == 0 && !m.joining) {
      out_line("[Failed: already disconnected]");
    } else {
      m.joining = false;
      m.join_state = 0;
      br.uplink = br.downlink = -1;                  // the BR's TCP state goes with the link
      schedule(300, A_DISCONNECTED, -1, NULL);
    }
  } else if (strcmp(argv[1], "ping") == 0 && argc == 3) {
    m.stats.pings++;
    outf("PING %s: 40 data bytes", argv[2]);
    if (strcmp(argv[2], BR_IP) == 0 && network_up()) {
      char b[160];
      snprintf(b, sizeof(b), "40 bytes from %s: icmp_seq=1 time=84 ms", BR_IP);
      schedule(84, A_TEXT, -1, b);
    }
  } else if (strcmp(argv[1], "tcp_server") == 0 && argc == 3) {
    int port = atoi(argv[2]);
    if (listener(port) || (port == m.blocked_port && m.now < m.blocked_until)) {
      out_line("[Failed: unable to bind a socket: 4294967295]");
    } else {
      sock_t* s = sock_new(K_SERVER, port, "::");
      s->active = true;
      outf("[Listening: %d]", s->id);
    }
  } else if (strcmp(argv[1], "tcp_client") == 0 && argc == 4) {
    m.stats.tcp_clients++;
    sock_t* s = sock_new(K_CLIENT, atoi(argv[3]), argv[2]);
    outf("[Opening: %s (%s): %d]", argv[2], argv[3], s->id);
    if (strcmp(argv[2], BR_IP) == 0 && atoi(argv[3]) == (int)WISUN_APP_PORT && network_up()) {
      schedule(2000, A_OPENED, s->id, NULL);
    } else {
      schedule(8000, A_CONNECT_FAIL, s->id, NULL);
    }
  } else if (strcmp(argv[1], "socket_list") == 0) {
    out_line("!  ##  Type        Info");
    for (int i = 0; i < MAX_SOCKS; i++) {
      sock_t* s = &m.socks[i];
      if (!s->used) continue;
      if (s->kind == K_SERVER) outf("#  %2d  TCP server  :: (%d) (LISTENING)", s->id, s->port);
      else outf("#  %2d  TCP client  %s (%d) (%s)", s->id, s->rip, s->port, s->active ? "ACTIVE" : "OPENING");
    }
    out_line("!");
  } else if (strcmp(argv[1], "socket_close") == 0 && argc == 3) {
    int id = atoi(argv[2]);
    sock_t* s = sock_find(id);
    if (!s) {
      out_line("[Failed: unable to close a socket: -1]");
    } else {
      m.stats.socket_closes++;
      if (s->kind == K_SERVER) {
        m.blocked_port = s->port;
        m.blocked_until = m.now + SIM_BIND_BLOCK_MS;
      }
      s->used = false;
      outf("[Closed: %d]", id);
      if (id == br.uplink) {
        br.uplink = -1;
        if (br.downlink >= 0) {                      // BR drops its connection back too
          schedule(500, A_PEER_CLOSE, br.downlink, NULL);
        }
      }
      if (id == br.downlink) br.downlink = -1;
    }
  } else if (strcmp(argv[1], "socket_write") == 0) {
    if (argc != 4) {
      m.stats.bad_socket_writes++;
      out_line("Incorrect number of arguments");
    } else {
      sock_t* s = sock_find(atoi(argv[2]));
      if (!s || !s->active) {
        out_line("[Failed: unable to write to a socket: -1]");
      } else {
        outf("[Wrote %zu bytes]", strlen(argv[3]));
        char b[64];
        snprintf(b, sizeof(b), "[Data sent: %d,2048]", s->id);
        schedule(50, A_TEXT, -1, b);
        if (s->id == br.uplink && br.powered) schedule(100, A_BR_RX, s->id, argv[3]);
      }
    }
  } else if (strcmp(argv[1], "socket_read") == 0 && argc == 4) {
    sock_t* s = sock_find(atoi(argv[2]));
    size_t max = (size_t)atoi(argv[3]);
    if (s && s->rx_len) {
      size_t n = s->rx_len < max ? s->rx_len : max;
      char hdr[160];
      snprintf(hdr, sizeof(hdr), "[Data from %s (%d): %d,%zu", s->rip, s->port, s->id, s->rx_len);
      out_line(hdr);
      // The real module wraps the payload every 64 characters (seen on the
      // hardware: a 79-byte read came back as a 64-char line and a 15-char line
      // ending in ']'), so the payload spans several lines.
      for (size_t off = 0; off < n; off += SIM_READ_WRAP) {
        size_t chunk = (n - off) < SIM_READ_WRAP ? (n - off) : SIM_READ_WRAP;
        const bool last = off + chunk >= n;
        char line[SIM_READ_WRAP + 2];
        memcpy(line, s->rx + off, chunk);
        if (last) line[chunk++] = ']';
        line[chunk] = '\0';
        out_line(line);
      }
      memmove(s->rx, s->rx + n, s->rx_len - n);
      s->rx_len -= n;
    } else if (s && s->peer_closed) {
      outf("[Data from %s (%d): %d,0]", s->rip, s->port, s->id);
    } else {
      out_line("[Failed: unable to read from a socket: -1]");
    }
  } else {
    out_line("Incorrect number of arguments");
  }
  prompt();
}

// -------------------------------------------------------------- lifecycle --
static void load_saved(void) {
  snprintf(m.name, sizeof(m.name), "%s", m.s_name);
  snprintf(m.domain, sizeof(m.domain), "%s", m.s_domain);
  m.chan = m.s_chan;
  m.phy = m.s_phy;
}

void sim_init(const sim_opts_t* opts) {
  memset(&m, 0, sizeof(m));
  memset(&br, 0, sizeof(br));
  m.o = *opts;
  m.next_id = 7;
  if (opts->factory_settings) {
    snprintf(m.s_name, sizeof(m.s_name), "Wi-SUN FAN");
    snprintf(m.s_domain, sizeof(m.s_domain), "EU");
    m.s_chan = 32;
    m.s_phy = 3;
  } else {
    snprintf(m.s_name, sizeof(m.s_name), "%s", WISUN_NETWORK_NAME);
    snprintf(m.s_domain, sizeof(m.s_domain), "%s", WISUN_REGULATORY_DOMAIN);
    m.s_chan = WISUN_CHAN_PLAN_ID;
    m.s_phy = WISUN_PHY_MODE_ID;
  }
  load_saved();
  br.powered = opts->br_powered;
  br.uplink = br.downlink = -1;
  br.seq = 100;
  frame_stream_reset(&br.stream);
  reasm_reset(&br.reasm);
  if (opts->start_operational) m.join_state = 5;
  out_raw("\r\nWi-SUN CLI Application\r\n");
  prompt();
  m.prompt_end = 0;
}

void sim_from_mcu(const uint8_t* bytes, size_t len) {
  for (size_t i = 0; i < len; i++) {
    char c = (char)bytes[i];
    if (c == '\r') continue;
    if (c == '\n') {
      m.in[m.in_len] = '\0';
      m.in_len = 0;
      cmd(m.in);
    } else if (m.in_len < sizeof(m.in) - 1u) {
      m.in[m.in_len++] = c;
    }
  }
}

static void run_action(action_t* a) {
  char b[200];
  sock_t* s = sock_find(a->sock);
  switch (a->kind) {
    case A_TEXT:
      out_async(a->text);
      break;
    case A_DISCONNECTED:
      out_async("[Disconnected]");
      break;
    case A_OPENED:
      if (!s) break;
      if (!network_up()) {
        schedule(0, A_CONNECT_FAIL, a->sock, NULL);
        break;
      }
      s->active = true;
      snprintf(b, sizeof(b), "[Opened: %d]", s->id);
      out_async(b);
      // The BR accepts, drops any previous NR connection, and connects back.
      if (br.uplink >= 0 && br.uplink != s->id) br.uplink = -1;
      br.uplink = s->id;
      reasm_reset(&br.reasm);
      frame_stream_reset(&br.stream);
      if (!br.app_down) schedule(3000, A_BR_CONNECT_BACK, -1, NULL);
      break;
    case A_CONNECT_FAIL:
      if (!s) break;
      s->used = false;
      snprintf(b, sizeof(b), "[Closed: %d]", a->sock);
      out_async(b);
      break;
    case A_BR_CONNECT_BACK: {
      sock_t* l = listener((int)WISUN_APP_PORT);
      if (!l || !network_up() || br.uplink < 0) break;
      if (br.downlink >= 0 && sock_find(br.downlink)) {   // replaces its old connection back
        schedule(0, A_PEER_CLOSE, br.downlink, NULL);
      }
      sock_t* n = sock_new(K_ACCEPTED, 54321, BR_IP);
      n->active = true;
      br.downlink = n->id;
      snprintf(b, sizeof(b), "[Socket connection available: %d]", l->id);
      out_async(b);
      snprintf(b, sizeof(b), "[Accepted %s (54321): %d]", BR_IP, n->id);
      out_async(b);
      break;
    }
    case A_BR_RX:
      if (br.powered && a->sock == br.uplink) {
        frame_stream_feed(&br.stream, a->text, strlen(a->text), br_on_frame, NULL);
      }
      break;
    case A_TO_NR:
      if (!s || !br.powered) break;
      memcpy(s->rx + s->rx_len, a->text, strlen(a->text));
      s->rx_len += strlen(a->text);
      snprintf(b, sizeof(b), "[Data available: %d,%zu]", s->id, s->rx_len);
      out_async(b);
      break;
    case A_PEER_CLOSE:
      if (!s) break;
      s->peer_closed = true;
      s->active = false;
      snprintf(b, sizeof(b), "[Data available: %d,0]", s->id);
      out_async(b);
      if (a->sock == br.downlink) br.downlink = -1;
      break;
  }
}

void sim_tick(uint32_t now_ms) {
  m.now = now_ms;
  for (int i = 0; i < MAX_ACTIONS; i++) {
    if (m.acts[i].used && m.acts[i].at <= now_ms) {
      m.acts[i].used = false;
      run_action(&m.acts[i]);
    }
  }
  join_tick();
  br_tick();
  for (unsigned i = 0; i < BYTES_PER_MS && m.out_pos < m.out_len; i++) {
    hal_host_rx_push((uint8_t)m.out[m.out_pos++]);
  }
  if (m.out_pos == m.out_len && m.out_len > OUT_CAP / 2u) {  // reclaim delivered bytes
    m.prompt_end = m.prompt_end > m.out_pos ? m.prompt_end - m.out_pos : 0;
    m.out_len = m.out_pos = 0;
  }
}

// ---------------------------------------------------------------- controls --
void sim_br_power(bool on) {
  br.powered = on;
  if (!on) {
    br.uplink = br.downlink = -1;                    // its TCP state is gone
    br.sending = false;
  }
}

void sim_module_reboot(void) {
  memset(m.socks, 0, sizeof(m.socks));
  memset(m.acts, 0, sizeof(m.acts));
  m.join_state = 0;
  m.joining = false;
  m.next_id = 7;
  br.uplink = br.downlink = -1;
  load_saved();
  out_raw("\r\nWi-SUN CLI Application\r\n");
  prompt();
}

void sim_br_drop_acks(int count) { br.drop_acks = count; }

void sim_br_app_down(bool down) { br.app_down = down; }

void sim_br_drop_connections(void) {
  if (br.downlink >= 0) schedule(0, A_PEER_CLOSE, br.downlink, NULL);
  if (br.uplink >= 0) schedule(0, A_PEER_CLOSE, br.uplink, NULL);
  br.uplink = br.downlink = -1;
}

void sim_br_send(const uint8_t* msg, size_t len, bool lose_first_ack) {
  memcpy(br.msg, msg, len);
  br.msg_len = len;
  br.msg_off = 0;
  br.out_seq = br.seq++;
  br.tries = 0;
  br.sending = true;
  br.lose_first_ack = lose_first_ack;
  br_send_next_fragment();
}

void sim_br_send_corrupt_frame(void) {
  br.corrupt_next = true;
  static const uint8_t k[] = "corrupted";
  br_send_frame('D', br.seq++, k, sizeof(k) - 1u);
}

const sim_stats_t* sim_stats(void) { return &m.stats; }
int sim_join_state(void) { return m.join_state; }
const char* sim_br_ip(void) { return BR_IP; }
const char* sim_nr_ip(void) { return NR_IP; }

size_t sim_socket_count(void) {
  size_t n = 0;
  for (int i = 0; i < MAX_SOCKS; i++) n += m.socks[i].used;
  return n;
}
