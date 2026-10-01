// examples/wisun_nr/main.c — Wi-SUN FAN 1.1 node on an EBYTE E51 (NR firmware).
//
// With no human input the firmware syncs with the module, fixes its network
// settings if needed, joins, discovers the border router, opens the two TCP
// connections the BR expects, and exchanges framed, acknowledged messages --
// recovering on its own from module resets, BR restarts and link loss.
//
//   make EXAMPLE=wisun_nr
//
// Wiring and every tunable: wisun_config.h. Design and RAM budget: README.md.
//
// Debug console (type into the log port, Enter to send):
//   send <text>     queue <text> as a message to the BR
//   raw <command>   run a raw E51 CLI command; its reply is logged as RAW<
//   status          state, addresses, sockets and counters
//   rejoin          disconnect and join again
//   log all|payload full diagnostics, or only payload lines (the default)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "wisun_config.h"
#include "wisun_hal.h"
#include "wisun_nr.h"
#include "wisun_util.h"

// ===========================================================================
// Application payload -- replace with real sensor data.
// ===========================================================================
static size_t build_nr_payload(char* out, size_t cap, uint32_t counter) {
  size_t n = fmt_str(out, 0, cap, "NR data #");
  n = fmt_u32(out, n, cap, counter);
  n = fmt_str(out, n, cap, " uptime=");
  n = fmt_u32(out, n, cap, hal_time_ms());
  return n;
}
// ===========================================================================

static bool printable(const uint8_t* d, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (d[i] < 0x20u || d[i] > 0x7Eu) return false;
  }
  return true;
}

// "<label><payload>" -- text as-is, anything unprintable as hex.
static void log_payload(const char* label, const uint8_t* data, size_t len) {
  wlog_t l;
  wlog_begin_user(&l);
  wlog_str(&l, label);
  if (printable(data, len)) {
    wlog_strn(&l, (const char*)data, len);
  } else {
    wlog_str(&l, "hex:");
    wlog_hex(&l, data, len);
  }
  wlog_end(&l);
}

static void on_receive(const uint8_t* data, size_t len) {
  log_payload("RX: ", data, len);
}

static void on_delivery(uint32_t msg_id, bool success, const uint8_t* data, size_t len) {
  (void)msg_id;
  log_payload(success ? "TX: " : "TX FAILED: ", data, len);
}

static void print_status(void) {
  nr_status_t s;
  nr_status(&s);
  wlog_t l;

  wlog_begin_user(&l);
  wlog_str(&l, "status: ");
  wlog_str(&l, s.state_name);
  wlog_str(&l, " for ");
  wlog_u32(&l, s.state_elapsed_s);
  wlog_str(&l, " s, join state ");
  wlog_i32(&l, s.join_state);
  wlog_end(&l);

  wlog_begin_user(&l);
  wlog_str(&l, "        NR ");
  wlog_str(&l, s.nr_ipv6[0] ? s.nr_ipv6 : "-");
  wlog_str(&l, "  BR ");
  wlog_str(&l, s.br_ipv6[0] ? s.br_ipv6 : "-");
  wlog_str(&l, "  sockets listen/tx/rx ");
  wlog_i32(&l, s.listen_sock);
  wlog_str(&l, "/");
  wlog_i32(&l, s.tx_sock);
  wlog_str(&l, "/");
  wlog_i32(&l, s.rx_sock);
  wlog_end(&l);

  wlog_begin_user(&l);
  wlog_str(&l, "        joins ");
  wlog_u32(&l, s.join_attempts);
  wlog_str(&l, "  frames tx/rx ");
  wlog_u32(&l, s.frames_tx);
  wlog_str(&l, "/");
  wlog_u32(&l, s.frames_rx);
  wlog_str(&l, "  retries ");
  wlog_u32(&l, s.retries);
  wlog_str(&l, "  crc errors ");
  wlog_u32(&l, s.crc_errors);
  wlog_str(&l, "  link losses ");
  wlog_u32(&l, s.link_losses);
  wlog_end(&l);

  wlog_begin_user(&l);
  wlog_str(&l, "        messages ok/failed/rx ");
  wlog_u32(&l, s.msgs_delivered);
  wlog_str(&l, "/");
  wlog_u32(&l, s.msgs_failed);
  wlog_str(&l, "/");
  wlog_u32(&l, s.msgs_received);
  wlog_str(&l, "  queued ");
  wlog_u32(&l, s.queued);
  wlog_str(&l, "  uart overflows ");
  wlog_u32(&l, s.uart_rx_overflows);
  wlog_str(&l, "  line overflows ");
  wlog_u32(&l, s.cli_line_overflows);
  wlog_end(&l);
}

static void console_poll(void) {
  char line[WISUN_CONSOLE_LINE_MAX];
  if (!hal_console_read_line(line, sizeof(line))) return;

  if (str_starts(line, "send ")) {
    const char* text = line + 5;
    uint32_t id;
    if (nr_send((const uint8_t*)text, strlen(text), &id)) wlog_line("queued: ", text);
    else wlog_user_line("send refused (not READY, or the queue is full)", NULL);
  } else if (str_starts(line, "raw ")) {
    if (!nr_raw(line + 4)) wlog_user_line("raw refused: command queue full", NULL);
  } else if (strcmp(line, "status") == 0) {
    print_status();
  } else if (str_starts(line, "log ")) {
    const bool payload_only = !str_starts(line + 4, "all");
    wlog_set_payload_only(payload_only);
    wlog_user_line("log: ", payload_only ? "payloads only" : "full diagnostics");
  } else if (strcmp(line, "rejoin") == 0) {
    nr_rejoin();
  } else {
    wlog_user_line("commands: send <text> | raw <cli command> | status | rejoin | log all|payload", NULL);
  }
}

int main(void) {
  hal_init();

  wlog_line("Wi-SUN NR firmware for the EBYTE E51 -- network \"" WISUN_NETWORK_NAME
            "\", domain " WISUN_REGULATORY_DOMAIN, NULL);
  nr_on_receive(on_receive);
  nr_on_delivery_result(on_delivery);
  nr_init();

  uint32_t counter = 0;
  uint32_t last_demo = hal_time_ms();

  for (;;) {
    hal_poll();
    nr_poll();
    console_poll();

    if (nr_is_ready() && (uint32_t)(hal_time_ms() - last_demo) >= WISUN_DEMO_PERIOD_MS) {
      last_demo = hal_time_ms();
      char payload[64];
      const size_t n = build_nr_payload(payload, sizeof(payload), ++counter);
      if (!nr_send((const uint8_t*)payload, n, NULL)) {
        wlog_line("demo: send queue full, skipping this message", NULL);
      }
    }
  }
}
