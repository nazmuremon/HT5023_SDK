// test/test_main.c — unit tests plus the spec section 9 acceptance scenarios,
// run against the simulated E51 and border router in virtual time.
//
//   make -C examples/wisun_nr/test            # build and run everything
//   ./examples/wisun_nr/test/wisun_nr_test -v # with the firmware's log
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../wisun_cli.h"
#include "../wisun_events.h"
#include "../wisun_frame.h"
#include "../wisun_nr.h"
#include "../wisun_util.h"
#include "sim_e51.h"

extern uint32_t g_now_ms;
extern int g_verbose;
void hal_host_reset(void);
void hal_host_console(const char* line);
const char* hal_host_log(void);
void hal_host_log_clear(void);

static int g_failures;
static int g_checks;

#define CHECK(cond, ...)                                    \
  do {                                                      \
    g_checks++;                                             \
    if (cond) {                                             \
      printf("    ok    ");                                 \
    } else {                                                \
      printf("    FAIL  ");                                 \
      g_failures++;                                         \
    }                                                       \
    printf(__VA_ARGS__);                                    \
    printf("\n");                                           \
  } while (0)

// ----------------------------------------------------------- app callbacks --
static uint32_t g_rx_count;
static char g_rx_last[1100];
static size_t g_rx_last_len;
static uint32_t g_delivered_ok;
static uint32_t g_delivered_fail;

static void on_rx(const uint8_t* d, size_t n) {
  g_rx_count++;
  memcpy(g_rx_last, d, n);
  g_rx_last_len = n;
}

static void on_delivery(uint32_t id, bool ok, const uint8_t* data, size_t len) {
  (void)id;
  (void)data;
  (void)len;
  if (ok) g_delivered_ok++;
  else g_delivered_fail++;
}

// --------------------------------------------------------------- stepping --
static void step(void) {
  g_now_ms++;
  sim_tick(g_now_ms);
  nr_poll();
}

static void run_for(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) step();
}

static bool run_until(bool (*cond)(void), uint32_t max_ms) {
  for (uint32_t i = 0; i < max_ms; i++) {
    if (cond()) return true;
    step();
  }
  return cond();
}

static bool is_ready(void) { return nr_is_ready(); }
static bool not_ready(void) { return !nr_is_ready(); }
static bool delivered_something(void) { return g_delivered_ok + g_delivered_fail > 0; }

static void boot(const sim_opts_t* o) {
  g_now_ms = 1000;
  hal_host_reset();
  hal_host_log_clear();
  sim_init(o);
  g_rx_count = g_delivered_ok = g_delivered_fail = 0;
  nr_on_receive(on_rx);
  nr_on_delivery_result(on_delivery);
  nr_init();
}

static int count_in_log(const char* needle) {
  int n = 0;
  for (const char* p = hal_host_log(); (p = strstr(p, needle)) != NULL; p++) n++;
  return n;
}

static void protocol_invariants(void) {
  const sim_stats_t* s = sim_stats();
  CHECK(s->reboot_cmds == 0, "never sent 'reboot'");
  CHECK(s->max_cmd_len <= 180, "longest command line %u chars (<= 180)", s->max_cmd_len);
  CHECK(s->bad_socket_writes == 0, "no socket_write with spaces or wrong argument count");
  CHECK(s->single_flight_violations == 0, "single-flight: never sent before the previous prompt");
}

// ================================================================== units ==
static void test_frames(void) {
  printf("\n[unit] frame codec\n");
  CHECK(crc16_ccitt_false((const uint8_t*)"123456789", 9) == 0x29B1, "CRC-16/CCITT-FALSE(\"123456789\") = 29B1");

  char f[160];
  size_t n = frame_encode('D', 12, (const uint8_t*)"Hello", 5, f, sizeof(f));
  const uint16_t crc = crc16_ccitt_false((const uint8_t*)"D,12,48656C6C6F", 15);
  char want[40];
  snprintf(want, sizeof(want), "$D,12,48656C6C6F,%04X;", crc);
  CHECK(n == strlen(want) && strcmp(f, want) == 0, "encode Hello/12 -> %s", f);

  frame_t fr;
  CHECK(frame_decode(f + 1, n - 2, &fr) == FRAME_OK && fr.seq == 12 && fr.len == 5 &&
            memcmp(fr.payload, "Hello", 5) == 0, "decode round-trips");
  f[n - 2] = (f[n - 2] == '0') ? '1' : '0';
  CHECK(frame_decode(f + 1, n - 2, &fr) == FRAME_BAD_CRC, "a corrupted CRC is rejected");

  n = frame_encode('A', 65535, NULL, 0, f, sizeof(f));
  CHECK(frame_decode(f + 1, n - 2, &fr) == FRAME_OK && fr.type == 'A' && fr.seq == 65535 && fr.len == 0,
        "empty-payload ACK with seq 65535: %s", f);

  uint8_t big[64];
  memset(big, 0xAB, sizeof(big));
  n = frame_encode('F', 65535, big, 64, f, sizeof(f));
  CHECK(n == 143 && strlen("wisun socket_write 99 ") + n <= 180, "largest frame is %zu chars; its command line fits 180", n);
}

static int g_bodies;
static char g_body_last[256];
static void body_cb(void* ctx, const char* body, size_t len) {
  (void)ctx;
  g_bodies++;
  memcpy(g_body_last, body, len);
  g_body_last[len] = '\0';
}

static void test_stream_and_reasm(void) {
  printf("\n[unit] stream splitter and reassembly\n");
  frame_stream_t s = { 0 };
  frame_stream_reset(&s);
  g_bodies = 0;
  frame_stream_feed(&s, "junk$D,1,41", 11, body_cb, NULL);
  frame_stream_feed(&s, ",ABCD;$A,2,,1234;", 17, body_cb, NULL);
  CHECK(g_bodies == 2, "split and merged frames: %d bodies (want 2), garbage before '$' ignored", g_bodies);

  char longtxt[260];
  memset(longtxt, 'X', sizeof(longtxt));
  longtxt[0] = '$';
  g_bodies = 0;
  frame_stream_feed(&s, longtxt, sizeof(longtxt), body_cb, NULL);
  frame_stream_feed(&s, "$A,3,,1234;", 11, body_cb, NULL);
  CHECK(g_bodies == 1 && s.dropped_overlong == 1, "200+ chars without ';' dropped, then resynchronised");

  reasm_t r;
  reasm_reset(&r);
  frame_t f = { .type = 'F', .seq = 5, .len = 2, .payload = { 'a', 'b' } };
  CHECK(reasm_accept(&r, &f) == REASM_STORED, "fragment stored");
  CHECK(reasm_accept(&r, &f) == REASM_DUPLICATE, "retransmitted fragment is a duplicate");
  f.type = 'D';
  f.seq = 6;
  f.payload[0] = 'c';
  f.len = 1;
  CHECK(reasm_accept(&r, &f) == REASM_COMPLETE && r.len == 3 && memcmp(r.msg, "abc", 3) == 0, "final fragment completes \"abc\"");
  CHECK(reasm_accept(&r, &f) == REASM_DUPLICATE, "retransmitted final frame is not delivered twice");
  f.seq = 7;
  CHECK(reasm_accept(&r, &f) == REASM_COMPLETE && r.len == 1, "next message starts clean");
}

static void test_events(void) {
  printf("\n[unit] event parser (verified formats)\n");
  wisun_event_t e;
  CHECK(ev_parse("[Join state 3: Acquire PAN Config]", &e) == EV_JOIN_STATE && e.a == 3, "join state 3");
  CHECK(ev_parse("[Join state 4: Configure Routing - DHCP]", &e) == EV_JOIN_STATE && e.a == 4, "join state 4 sub-step");
  CHECK(ev_parse("[Connected: 166 s]", &e) == EV_CONNECTED && e.a == 166, "connected 166 s");
  CHECK(ev_parse("[IPv6 address: fd12:3456::3610:f4ff:fea8:7a1d]", &e) == EV_IPV6 &&
            strcmp(e.ip, "fd12:3456::3610:f4ff:fea8:7a1d") == 0, "IPv6 address");
  CHECK(ev_parse("[Opening: fd12::1 (9000): 8]", &e) == EV_OPENING && e.a == 8 && e.b == 9000 && strcmp(e.ip, "fd12::1") == 0, "opening");
  CHECK(ev_parse("[Accepted fd12::1 (2297): 9]", &e) == EV_ACCEPTED && e.a == 9 && e.b == 2297, "accepted");
  CHECK(ev_parse("[Data available: 8,23]", &e) == EV_DATA_AVAILABLE && e.a == 8 && e.b == 23, "data available");
  CHECK(ev_parse("[Data available: 8,0]", &e) == EV_DATA_AVAILABLE && e.b == 0, "data available len 0 (peer closed)");
  CHECK(ev_parse("[Data from fd12::1 (2297): 8,23", &e) == EV_DATA_FROM && e.a == 8 && e.b == 23 && e.c == 2297, "data-from header");
  CHECK(ev_parse("40 bytes from fd12::7369: icmp_seq=1 time=84 ms", &e) == EV_PING_REPLY && strcmp(e.ip, "fd12::7369") == 0, "ping reply");
  CHECK(ev_parse("Wi-SUN CLI Application", &e) == EV_MODULE_BOOT, "boot banner");
  CHECK(ev_parse("[Failed: unable to bind a socket: 4294967295]", &e) == EV_FAILED &&
            strcmp(e.text, "unable to bind a socket: 4294967295") == 0, "failure reason");
  CHECK(ev_parse("[Socket connection available: 7]", &e) == EV_CONN_AVAILABLE && e.a == 7, "connection available");
  CHECK(ev_parse("[Something new]", &e) == EV_UNKNOWN, "unknown bracket line is EV_UNKNOWN, not a crash");
  CHECK(ev_parse("wisun.join_state = Disconnected (0)", &e) == EV_NONE, "key = value is not an event");
}

// ---- CLI driver against scripted module output (no simulator) --------------
static char s_script_async[16][128];
static int s_async_n;
static cli_result_t s_done_res;
static int s_done_n;
static char s_data[1100];
static int s_line_n;
static char s_script_line[128];
static void t_line(void* c, uint8_t t, const char* l) { (void)c; (void)t; s_line_n++; snprintf(s_script_line, sizeof(s_script_line), "%s", l); }
static void t_done(void* c, uint8_t t, cli_result_t r, const char* why) { (void)c; (void)t; (void)why; s_done_res = r; s_done_n++; }
static void t_async(void* c, const char* l) { (void)c; if (s_async_n < 16) snprintf(s_script_async[s_async_n++], 128, "%s", l); }
static void t_data(void* c, const char* h, const char* p, size_t n) { (void)c; (void)h; memcpy(s_data, p, n); s_data[n] = '\0'; }
bool hal_host_rx_push(uint8_t b);
static void feed(const char* s) { while (*s) hal_host_rx_push((uint8_t)*s++); cli_poll(); }

static void test_cli(void) {
  printf("\n[unit] CLI driver: echo, prompt, interleaving, timeouts\n");
  g_now_ms = 0;
  hal_host_reset();
  sim_opts_t o = { 0 };
  sim_init(&o);                       // absorbs the bytes the CLI writes
  hal_host_reset();                   // but its banner is not wanted here
  const cli_handlers_t h = { NULL, t_line, t_done, t_async, t_data };
  cli_init(&h);

  s_done_n = 0;
  cli_enqueue("wisun ping fd12::1", 3000, 1);
  feed("> ");                         // stale prompt before the echo: must not complete
  CHECK(s_done_n == 0, "a prompt before the echo does not complete the command");
  feed("wisun ping fd12::1\r\nPING fd12::1: 40 data bytes\r\n> [Data available: 8,2]\r\n");
  CHECK(s_done_n == 1 && s_done_res == CLI_OK, "echo + response + prompt completes it");
  CHECK(s_async_n >= 1 && strcmp(s_script_async[s_async_n - 1], "[Data available: 8,2]") == 0,
        "\"> [Data available: 8,2]\" parsed as prompt + event");

  s_done_n = 0;
  cli_enqueue("wisun socket_read 8 1024", 5000, 1);
  feed("wisun socket_rea");            // split mid-line
  feed("d 8 1024\r\n[Data from fd12::1 (2297): 8,11\r\n$A,1,,1234;]\r\n> ");
  CHECK(s_done_n == 1 && strcmp(s_data, "$A,1,,1234;") == 0, "socket_read payload captured up to ']': %s", s_data);

  // Captured on the hardware: the module wraps the payload every 64 characters.
  s_done_n = 0;
  s_data[0] = '\0';
  cli_enqueue("wisun socket_read 11 1024", 5000, 1);
  feed("wisun socket_read 11 1024\r\n[Data from fd12:3456::3610:f4ff:fea8:7369 (36580): 11,90\r\n"
       "$D,24321,42522D41434B3A4E52206461746120233220757074696D653D32313\r\n"
       "933333832,7DDC;$A,4,,43F0;]\r\n> ");
  CHECK(s_done_n == 1 && strcmp(s_data, "$D,24321,42522D41434B3A4E52206461746120233220757074696D653D32313"
                                       "933333832,7DDC;$A,4,,43F0;") == 0,
        "payload wrapped at 64 chars is joined across lines (%zu chars)", strlen(s_data));

  s_done_n = 0;
  s_data[0] = '\0';
  s_async_n = 0;
  cli_enqueue("wisun socket_read 11 1024", 5000, 1);
  feed("wisun socket_read 11 1024\r\n[Data from fd12::1 (2297): 11,71\r\n"
       "$D,7,0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF012345678\r\n"
       "[Data sent: 10,2048]\r\n"
       "9AB,1234;]\r\n> ");
  CHECK(s_done_n == 1 && strcmp(s_data, "$D,7,0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789AB,1234;") == 0 &&
            s_async_n == 2 && strcmp(s_script_async[1], "[Data sent: 10,2048]") == 0,
        "an event between payload lines is handled and not mixed into the data");

  s_done_n = 0;
  s_data[0] = '\0';
  cli_enqueue("wisun socket_read 8 1024", 5000, 1);
  feed("wisun socket_read 8 1024\r\n[Data from fd12::1 (2297): 8,64\r\n"
       "$D,9,0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF01234,ABCD;\r\n]\r\n> ");
  CHECK(s_done_n == 1 && strlen(s_data) == 64, "a closing ']' alone on the next line ends the payload");

  // A reply whose ']' never comes must not swallow the next command's lines.
  s_done_n = 0;
  s_data[0] = '\0';
  s_line_n = 0;
  cli_enqueue("wisun socket_read 8 1024", 5000, 1);
  feed("wisun socket_read 8 1024\r\n[Data from fd12::1 (2297): 8,20\r\n$D,1,414243444546,AB\r\n> ");
  cli_enqueue("wisun get wisun", 4000, 1);
  feed("wisun get wisun\r\nwisun.join_state = Operational (5)\r\n> ");
  CHECK(strcmp(s_data, "$D,1,414243444546,AB") == 0 && s_line_n >= 1 &&
            strcmp(s_script_line, "wisun.join_state = Operational (5)") == 0,
        "a capture missing its ']' ends at the prompt; the next reply is not swallowed");

  s_done_n = 0;
  cli_enqueue("wisun tcp_server 9000", 5000, 1);
  feed("wisun tcp_server 9000\r\n[Failed: unable to bind a socket: 4294967295]\r\n> ");
  CHECK(s_done_n == 1 && s_done_res == CLI_FAILED, "[Failed: ...] reports CLI_FAILED");

  s_done_n = 0;
  cli_enqueue("wisun get wisun", 4000, 1);
  g_now_ms = 5000;
  cli_poll();
  cli_poll();
  CHECK(s_done_n == 1 && s_done_res == CLI_TIMEOUT, "silence times out");
}

// ============================================================= scenarios ==
static void scenario_cold_boot(void) {
  printf("\n[9.1] cold boot, factory settings on the module, BR running\n");
  sim_opts_t o = { .factory_settings = true, .br_powered = true };
  boot(&o);
  const uint32_t t0 = g_now_ms;
  bool ok = run_until(is_ready, 420000);
  nr_status_t s;
  nr_status(&s);
  CHECK(ok, "reached READY after %u s", (unsigned)((g_now_ms - t0) / 1000u));
  CHECK(sim_stats()->saves == 1, "settings differed: saved exactly once (%u)", sim_stats()->saves);
  CHECK(sim_stats()->join_fan11 == 1, "joined once (%u join_fan11)", sim_stats()->join_fan11);
  CHECK(count_in_log("Join state 3: Acquire PAN Config") >= 1 && count_in_log("Join state 5: Operational") >= 1,
        "join states logged with elapsed time");
  CHECK(strcmp(s.nr_ipv6, sim_nr_ip()) == 0, "NR IPv6 %s", s.nr_ipv6);
  CHECK(strcmp(s.br_ipv6, sim_br_ip()) == 0, "BR IPv6 discovered, not configured: %s", s.br_ipv6);
  CHECK(sim_stats()->pings >= 1 && count_in_log("BR answered ping") == 1, "ping reply from the BR");
  CHECK(s.listen_sock >= 0 && s.tx_sock >= 0 && s.rx_sock >= 0, "listen %d / tx %d / rx %d", s.listen_sock, s.tx_sock, s.rx_sock);
  CHECK(sim_stats()->br_connected_both, "BR sees both TCP connections");
  CHECK(count_in_log("STATE ") >= 12, "state transitions logged");
  protocol_invariants();
}

static void scenario_messages(void) {
  printf("\n[9.2/9.3] message delivery, fragmentation, receive, dedup, CRC, retry\n");
  const sim_stats_t* st = sim_stats();

  uint32_t before = st->br_msgs;
  g_delivered_ok = g_delivered_fail = 0;
  CHECK(nr_send((const uint8_t*)"hello BR", 8, NULL), "send accepted in READY");
  run_until(delivered_something, 20000);
  CHECK(g_delivered_ok == 1 && st->br_msgs == before + 1 && st->br_last_len == 8 &&
            memcmp(st->br_last_msg, "hello BR", 8) == 0, "BR received \"hello BR\"; NR reports success");

  uint8_t big[300];
  for (int i = 0; i < 300; i++) big[i] = (uint8_t)(i * 7 + 3);
  g_delivered_ok = g_delivered_fail = 0;
  uint32_t frames_before = st->br_frames_rx;
  CHECK(nr_send(big, sizeof(big), NULL), "300-byte message queued");
  run_until(delivered_something, 60000);
  CHECK(g_delivered_ok == 1 && st->br_last_len == 300 && memcmp(st->br_last_msg, big, 300) == 0,
        "300 bytes arrived intact in %u frames", st->br_frames_rx - frames_before);

  g_rx_count = 0;
  sim_br_send((const uint8_t*)"hello NR", 8, true);  // the BR loses the first ACK and resends
  run_for(15000);
  CHECK(g_rx_count == 1 && g_rx_last_len == 8 && memcmp(g_rx_last, "hello NR", 8) == 0,
        "BR->NR message delivered once despite a retransmission (%u deliveries)", g_rx_count);
  CHECK(st->br_send_ok >= 1, "BR's stop-and-wait completed");

  // Seen on the hardware: a 32-byte reply is a 79-char frame, which the module
  // prints over two lines. It must still be delivered and acknowledged.
  static const char k_hw[] = "BR-ACK:NR data #1 uptime=2183379";
  uint32_t ok_before = st->br_send_ok;
  g_rx_count = 0;
  sim_br_send((const uint8_t*)k_hw, sizeof(k_hw) - 1u, false);
  run_for(15000);
  CHECK(g_rx_count == 1 && g_rx_last_len == sizeof(k_hw) - 1u && memcmp(g_rx_last, k_hw, sizeof(k_hw) - 1u) == 0 &&
            st->br_send_ok == ok_before + 1, "79-char frame wrapped over two lines: delivered and ACKed");

  uint8_t from_br[300];
  for (int i = 0; i < 300; i++) from_br[i] = (uint8_t)(i * 13 + 5);
  ok_before = st->br_send_ok;
  g_rx_count = 0;
  sim_br_send(from_br, sizeof(from_br), false);
  run_for(40000);
  CHECK(g_rx_count == 1 && g_rx_last_len == 300 && memcmp(g_rx_last, from_br, 300) == 0 &&
            st->br_send_ok == ok_before + 1, "300-byte BR->NR message in 143-char frames arrives intact");

  nr_status_t s;
  nr_status(&s);
  const uint32_t crc_before = s.crc_errors;
  g_rx_count = 0;
  sim_br_send_corrupt_frame();
  run_for(3000);
  nr_status(&s);
  CHECK(s.crc_errors == crc_before + 1 && g_rx_count == 0, "corrupted frame dropped, not delivered");

  g_delivered_ok = g_delivered_fail = 0;
  sim_br_drop_acks(1);
  nr_send((const uint8_t*)"retry me", 8, NULL);
  run_until(delivered_something, 30000);
  nr_status(&s);
  CHECK(g_delivered_ok == 1 && s.retries >= 1, "missing ACK: resent and delivered (%u retries)", s.retries);

  CHECK(!nr_send(big, 1025, NULL), "a 1025-byte message is refused");
  run_for(95000);
  CHECK(nr_is_ready() && st->br_hb_rx >= 3, "heartbeats every 30 s answered (%u sent), still READY", st->br_hb_rx);
  protocol_invariants();
}

static void scenario_console(void) {
  printf("\n[8] debug console\n");
  hal_host_log_clear();
  // The console parser lives in main.c, which the host build does not link;
  // this exercises the link-manager entry point its "raw" command calls.
  CHECK(nr_raw("wisun socket_list"), "raw command accepted");
  run_for(2000);
  CHECK(count_in_log("RAW< #") >= 3 && count_in_log("RAW done: ok") == 1, "raw reply logged line by line");
}

static void scenario_mcu_reset(void) {
  printf("\n[9.4] reset only the MCU while the module stays Operational\n");
  const uint32_t joins = sim_stats()->join_fan11;
  const uint32_t saves = sim_stats()->saves;
  const uint32_t closes = sim_stats()->socket_closes;
  hal_host_reset();
  hal_host_log_clear();
  nr_init();
  const uint32_t t0 = g_now_ms;
  bool ok = run_until(is_ready, 120000);
  CHECK(ok, "READY again after %u s (target: about 30 s)", (unsigned)((g_now_ms - t0) / 1000u));
  CHECK(g_now_ms - t0 <= 40000u, "within 40 s");
  CHECK(sim_stats()->join_fan11 == joins, "no rejoin");
  CHECK(sim_stats()->saves == saves, "settings not saved again");
  CHECK(sim_stats()->socket_closes == closes + 2, "stale connections closed, the listener kept (%u closed)",
        sim_stats()->socket_closes - closes);
  CHECK(count_in_log("already Operational: no rejoin") == 1, "logged the resume decision");
  g_delivered_ok = g_delivered_fail = 0;
  nr_send((const uint8_t*)"after reset", 11, NULL);
  run_until(delivered_something, 20000);
  CHECK(g_delivered_ok == 1, "traffic flows after the MCU reset");
  protocol_invariants();
}

static void scenario_peer_close(void) {
  printf("\n[6.16] BR application drops both TCP connections (network stays up)\n");
  const uint32_t joins = sim_stats()->join_fan11;
  sim_br_drop_connections();
  bool lost = run_until(not_ready, 10000);
  const uint32_t t0 = g_now_ms;
  bool back = run_until(is_ready, 120000);
  CHECK(lost, "len-0 close detected");
  CHECK(back, "reconnected in %u s without rejoining", (unsigned)((g_now_ms - t0) / 1000u));
  CHECK(sim_stats()->join_fan11 == joins, "no rejoin for a socket-level loss");
}

static void scenario_br_restart(void) {
  printf("\n[9.5] border router power loss and return\n");
  sim_br_power(false);
  const uint32_t t0 = g_now_ms;
  bool lost = run_until(not_ready, 200000);
  CHECK(lost, "loss detected after %u s", (unsigned)((g_now_ms - t0) / 1000u));
  run_for(120000);                                   // BR stays off for a while
  CHECK(!nr_is_ready(), "not READY while the BR is off");
  sim_br_power(true);
  const uint32_t t1 = g_now_ms;
  bool back = run_until(is_ready, 1200000);
  CHECK(back, "READY again %u s after the BR returned", (unsigned)((g_now_ms - t1) / 1000u));
  g_delivered_ok = g_delivered_fail = 0;
  nr_send((const uint8_t*)"BR is back", 10, NULL);
  run_until(delivered_something, 20000);
  CHECK(g_delivered_ok == 1, "traffic flows again");
  protocol_invariants();
}

static void scenario_module_reboot(void) {
  printf("\n[5.1] E51 reboots (banner) while READY\n");
  const uint32_t saves = sim_stats()->saves;
  sim_module_reboot();
  bool lost = run_until(not_ready, 5000);
  CHECK(lost && count_in_log("module banner") >= 1, "banner resets the state machine to UART_SYNC");
  bool back = run_until(is_ready, 420000);
  CHECK(back, "rejoined and READY again");
  CHECK(sim_stats()->saves == saves, "saved settings survived the reboot: no second save");
}

static void scenario_ack_exhaustion(void) {
  printf("\n[6.16] ACK retries exhausted twice in a row -> sockets lost\n");
  g_delivered_ok = g_delivered_fail = 0;
  sim_br_drop_acks(1000);
  nr_send((const uint8_t*)"lost-1", 6, NULL);
  nr_send((const uint8_t*)"lost-2", 6, NULL);
  bool lost = run_until(not_ready, 90000);
  sim_br_drop_acks(0);
  CHECK(lost && g_delivered_fail >= 2, "two delivery failures (%u) then link reset", g_delivered_fail);
  CHECK(run_until(is_ready, 120000), "recovered to READY");
}

static void scenario_br_off_at_boot(void) {
  printf("\n[9.6] BR off when the NR boots: retry forever, then join when it appears\n");
  sim_opts_t o = { .factory_settings = false, .br_powered = false };
  boot(&o);
  run_for(3600000);                                   // one hour
  nr_status_t s;
  nr_status(&s);
  CHECK(!nr_is_ready(), "not READY after an hour with no BR (state %s)", s.state_name);
  CHECK(s.join_attempts >= 6, "kept retrying: %u join attempts", s.join_attempts);
  CHECK(count_in_log("next join attempt in 120 s") >= 1 && count_in_log("next join attempt in 240 s") == 0,
        "back-off capped at 120 s");
  CHECK(sim_stats()->saves == 0, "matching settings were never saved");
  sim_br_power(true);
  const uint32_t t0 = g_now_ms;
  bool ok = run_until(is_ready, 1200000);
  CHECK(ok, "joined and READY %u s after the BR powered on", (unsigned)((g_now_ms - t0) / 1000u));
  protocol_invariants();
}

static void scenario_br_firmware(void) {
  printf("\n[6.2] BR firmware on the NR side\n");
  sim_opts_t o = { .br_firmware = true, .br_powered = true };
  boot(&o);
  run_for(20000);
  nr_status_t s;
  nr_status(&s);
  CHECK(s.state == NR_ERROR && count_in_log("BR firmware on the NR side") >= 3,
        "ERROR state, logged every 5 s (%d times)", count_in_log("BR firmware on the NR side"));
  CHECK(sim_stats()->join_fan11 == 0 && sim_stats()->saves == 0, "nothing sent that would change the module");
}

static bool join_sent(void) { return count_in_log("TX> wisun join_fan11") > 0; }

static void scenario_rejoin_mid_join(void) {
  printf("\n[8] console rejoin just after join_fan11 still forces a disconnect\n");
  sim_opts_t o = { .br_powered = true };
  boot(&o);
  CHECK(run_until(join_sent, 60000), "joining");
  // No [Join state] event has arrived yet, so the last known join state is 0
  // although the module is busy joining.
  nr_status_t s;
  nr_status(&s);
  CHECK(s.join_state == 0, "no join state reported yet (%d)", s.join_state);
  hal_host_log_clear();
  nr_rejoin();
  run_for(10000);
  CHECK(count_in_log("TX> wisun disconnect") == 1, "sent wisun disconnect before joining again");
  CHECK(run_until(is_ready, 420000), "READY after the forced rejoin");
  protocol_invariants();
}

// Seen on the hardware: the BR came up part-way through a join attempt, the node
// reached join state 4, and the 360 s timer -- counted from the start of the
// attempt -- threw the nearly finished join away.
static void scenario_br_up_mid_attempt(void) {
  printf("\n[6.9] BR powered on 250 s into a join attempt: the progressing join is not aborted\n");
  sim_opts_t o = { .br_powered = false };
  boot(&o);
  CHECK(run_until(join_sent, 60000), "join attempt 1 started with no BR");
  run_for(250000);
  sim_br_power(true);
  const uint32_t t0 = g_now_ms;
  bool ok = run_until(is_ready, 400000);
  nr_status_t s;
  nr_status(&s);
  CHECK(ok, "READY %u s after the BR came up", (unsigned)((g_now_ms - t0) / 1000u));
  CHECK(s.join_attempts == 1 && count_in_log("join timeout") == 0,
        "joined on the first attempt, no timeout (%u attempts)", s.join_attempts);
  protocol_invariants();
}

// Hardware: while the BR application was closed, the NR opened a new uplink every
// ~35 s. Each one leaves a socket on the BR's module, which has room for about
// nine, and the module then stopped accepting connections altogether.
static void scenario_br_app_down_backoff(void) {
  printf("\n[6.13] BR application closed: uplink retries back off\n");
  sim_opts_t o = { .br_powered = true };
  boot(&o);
  CHECK(run_until(is_ready, 420000), "READY with the BR application running");
  const uint32_t clients0 = sim_stats()->tcp_clients;
  sim_br_app_down(true);
  sim_br_drop_connections();
  run_for(1200000);                                  // 20 minutes with no BR application
  const uint32_t tries = sim_stats()->tcp_clients - clients0;
  CHECK(tries <= 15u, "%u uplink attempts in 20 min (one every ~35 s would be ~34)", tries);
  CHECK(!nr_is_ready(), "not READY while the BR application is down");
  sim_br_app_down(false);
  CHECK(run_until(is_ready, 300000), "READY again once the BR application returns");
  protocol_invariants();
}

static void scenario_long_run(void) {
  printf("\n[soak] 30 minutes READY with the demo's 10 s traffic\n");
  sim_opts_t o = { .br_powered = true };
  boot(&o);
  run_until(is_ready, 420000);
  nr_status_t s0;
  nr_status(&s0);
  uint32_t sent = 0;
  for (uint32_t t = 0; t < 1800000u; t += 10000u) {
    char msg[64];
    int n = snprintf(msg, sizeof(msg), "NR data #%u uptime=%u", sent + 1u, g_now_ms);
    if (nr_send((const uint8_t*)msg, (size_t)n, NULL)) sent++;
    run_for(10000);
  }
  run_for(20000);
  nr_status_t s;
  nr_status(&s);
  CHECK(sent == 180 && s.msgs_delivered - s0.msgs_delivered == sent && s.msgs_failed == 0,
        "%u sent, %u delivered, %u failed", sent, s.msgs_delivered - s0.msgs_delivered, s.msgs_failed);
  CHECK(s.link_losses == 0 && s.uart_rx_overflows == 0 && s.cli_line_overflows == 0,
        "no link losses, UART overflows or line overflows");
  CHECK(nr_is_ready(), "still READY");
  protocol_invariants();
}

// Built separately with -DWISUN_APPLY_CONFIG=0 (see Makefile).
#if !WISUN_APPLY_CONFIG
static void scenario_config_locked(void) {
  printf("\n[config] WISUN_APPLY_CONFIG=0 with differing settings: report, never change\n");
  sim_opts_t o = { .factory_settings = true, .br_powered = true };
  boot(&o);
  run_for(30000);
  nr_status_t s;
  nr_status(&s);
  CHECK(s.state == NR_ERROR, "stays in ERROR (state %s)", s.state_name);
  CHECK(count_in_log("WISUN_APPLY_CONFIG is 0") >= 3, "says why, repeatedly");
  CHECK(sim_stats()->saves == 0 && count_in_log("TX> wisun set") == 0, "no wisun set, no wisun save");
  CHECK(sim_stats()->join_fan11 == 0, "never joins");
  protocol_invariants();
}
#endif

// Built separately with WISUN_LOG_PAYLOAD_ONLY=1, the firmware default (see Makefile).
#if WISUN_LOG_PAYLOAD_ONLY
static const uint8_t* g_last_tx;
static size_t g_last_tx_len;
static void on_delivery_payload(uint32_t id, bool ok, const uint8_t* data, size_t len) {
  (void)id;
  if (ok) {
    g_delivered_ok++;
    g_last_tx = data;
    g_last_tx_len = len;
  }
}

static void scenario_payload_only_log(void) {
  printf("\n[log] WISUN_LOG_PAYLOAD_ONLY=1: no diagnostics on the log port\n");
  sim_opts_t o = { .factory_settings = true, .br_powered = true };
  boot(&o);
  nr_on_delivery_result(on_delivery_payload);
  CHECK(run_until(is_ready, 420000), "cold boot to READY");
  g_delivered_ok = 0;
  nr_send((const uint8_t*)"hello BR", 8, NULL);
  run_until(delivered_something, 20000);
  CHECK(g_delivered_ok == 1 && g_last_tx_len == 8 && memcmp(g_last_tx, "hello BR", 8) == 0,
        "delivery callback carries the delivered payload");
  sim_br_send((const uint8_t*)"hello NR", 8, false);
  run_for(15000);
  CHECK(g_rx_count == 1, "BR payload received");
  CHECK(hal_host_log()[0] == '\0', "join, sockets and traffic printed nothing (%zu bytes logged)", strlen(hal_host_log()));
  CHECK(nr_raw("wisun socket_list"), "raw command accepted");
  run_for(2000);
  CHECK(count_in_log("RAW< ") >= 1 && count_in_log("RAW done: ok") == 1 && count_in_log("[NR]") == 0,
        "raw replies are still shown, without diagnostics");

  hal_host_log_clear();
  wlog_set_payload_only(false);                      // console: "log all"
  run_for(12000);
  CHECK(count_in_log("[NR]") > 0, "the runtime switch turns diagnostics back on");
  wlog_set_payload_only(true);                       // console: "log payload"
  hal_host_log_clear();
  run_for(12000);
  CHECK(count_in_log("[NR]") == 0, "and off again");
}
#endif

int main(int argc, char** argv) {
  g_verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
  setvbuf(stdout, NULL, _IONBF, 0);

#if WISUN_LOG_PAYLOAD_ONLY
  scenario_payload_only_log();
  printf("\n%d checks, %d failed\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
#endif

#if !WISUN_APPLY_CONFIG
  scenario_config_locked();
  printf("\n%d checks, %d failed\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
#endif

  test_frames();
  test_stream_and_reasm();
  test_events();
  test_cli();

  scenario_cold_boot();
  scenario_messages();
  scenario_console();
  scenario_mcu_reset();
  scenario_peer_close();
  scenario_ack_exhaustion();
  scenario_br_restart();
  scenario_module_reboot();
  scenario_br_off_at_boot();
  scenario_br_firmware();
  scenario_rejoin_mid_join();
  scenario_br_up_mid_attempt();
  scenario_br_app_down_backoff();
  scenario_long_run();

  printf("\n%d checks, %d failed\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
