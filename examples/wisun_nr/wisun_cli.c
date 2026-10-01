// wisun_cli.c — see wisun_cli.h.
#include "wisun_cli.h"

#include <string.h>

#include "wisun_config.h"
#include "wisun_hal.h"
#include "wisun_util.h"

typedef struct {
  char text[WISUN_CMD_MAX];
  uint32_t timeout_ms;
  uint8_t tag;
} cmd_t;

static struct {
  cli_handlers_t h;

  cmd_t queue[WISUN_CMD_QUEUE_DEPTH];
  uint8_t q_head;
  uint8_t q_count;

  bool busy;                  // queue[q_head] is on the wire
  uint32_t sent_at;
  bool echo_seen;
  bool response_seen;
  cli_result_t pending_result;
  char reason[64];

  char line[WISUN_LINE_MAX];
  size_t line_len;
  bool line_overflow;
  uint32_t overflows;

  // A socket_read reply in progress. The module wraps the payload every 64
  // characters, so it spans lines until the one ending in ']' (the frame
  // format never contains ']'). `expect` is the length from the header,
  // capped at what one read can return.
  bool capturing;
  char header[96];
  char payload[WISUN_SOCKET_READ_MAX + 1u];
  size_t payload_len;
  size_t expect;

  uint32_t prompts;
} s;

void cli_init(const cli_handlers_t* handlers) {
  memset(&s, 0, sizeof(s));
  s.h = *handlers;
}

static void trim_right(char* t) {
  size_t n = strlen(t);
  while (n && (t[n - 1u] == ' ' || t[n - 1u] == '\t')) t[--n] = '\0';
}

static bool is_echo(const char* line) {
  if (!s.busy || s.echo_seen) return false;
  char a[WISUN_CMD_MAX];
  char b[WISUN_CMD_MAX];
  str_copy(a, sizeof(a), line);
  str_copy(b, sizeof(b), s.queue[s.q_head].text);
  trim_right(a);
  trim_right(b);
  return strcmp(a, b) == 0;
}

static void finish(cli_result_t result, const char* reason) {
  if (!s.busy) return;
  const uint8_t tag = s.queue[s.q_head].tag;
  s.busy = false;
  s.q_head = (uint8_t)((s.q_head + 1u) % WISUN_CMD_QUEUE_DEPTH);
  s.q_count--;
  s.h.done(s.h.ctx, tag, result, reason ? reason : "");
}

static void note_failure(const char* line) {
  if (s.pending_result == CLI_FAILED) return;         // keep the first reason
  if (str_starts(line, "[Failed: ")) {
    size_t n = strlen(line);
    str_copyn(s.reason, sizeof(s.reason), line + 9, n > 10u ? n - 10u : 0u);
    s.pending_result = CLI_FAILED;
  } else if (str_starts(line, "No command found") ||
             str_starts(line, "Incorrect number of arguments")) {
    str_copy(s.reason, sizeof(s.reason), line);
    s.pending_result = CLI_FAILED;
  }
}

static void payload_done(void) {
  s.capturing = false;
  s.payload[s.payload_len] = '\0';
  s.h.data(s.h.ctx, s.header, s.payload, s.payload_len);
}

// Unread-byte count at the end of "[Data from IP (PORT): id,len".
static size_t header_len(const char* header) {
  const char* comma = strrchr(header, ',');
  uint32_t n = 0;
  if (!comma || (++comma, !parse_u32(&comma, &n))) return WISUN_SOCKET_READ_MAX;
  return n < WISUN_SOCKET_READ_MAX ? n : WISUN_SOCKET_READ_MAX;
}

static void handle_line(char* line) {
#if WISUN_LOG_UART_TRAFFIC
  if (line[0]) {
    wlog_t l;
    wlog_begin(&l);
    wlog_str(&l, "RX< ");
    wlog_str(&l, line);
    wlog_end(&l);
  }
#endif

  if (line[0] == '\0') return;

  if (is_echo(line)) {
    s.echo_seen = true;
    return;
  }

  // Payload of a socket_read: every line after "[Data from ..." up to the one
  // ending in ']'. An async event cannot be payload -- those start with '[' --
  // so one landing between payload lines is handled normally below.
  if (s.capturing && line[0] != '[') {
    size_t n = strlen(line);
    const bool last = n && line[n - 1u] == ']';
    if (last) n--;
    const size_t room = sizeof(s.payload) - 1u - s.payload_len;
    if (n > room) n = room;
    memcpy(s.payload + s.payload_len, line, n);
    s.payload_len += n;
    // Stop at ']' -- or once the header's length is in, so a lost ']' cannot
    // swallow the next command's response.
    if (last || s.payload_len >= s.expect) payload_done();
    if (s.busy) s.response_seen = true;
    return;
  }

  if (str_starts(line, "[Data from ")) {
    if (s.capturing) payload_done();           // previous reply never closed: keep what came
    const size_t n = strlen(line);
    if (line[n - 1u] == ']') {                 // header with nothing after it
      s.h.data(s.h.ctx, line, "", 0);
    } else {
      str_copy(s.header, sizeof(s.header), line);
      s.payload_len = 0;
      s.expect = header_len(line);
      s.capturing = true;
    }
  }

  if (s.busy) {
    s.response_seen = true;
    note_failure(line);
    s.h.line(s.h.ctx, s.queue[s.q_head].tag, line);
  }
  s.h.async(s.h.ctx, line);
}

static void handle_prompt(void) {
  s.prompts++;
  // The reply ends before the prompt; a capture still open lost its ']'.
  if (s.capturing) payload_done();
  // A prompt only closes the command once the module has answered it -- its
  // echo or a response line. Otherwise a stale prompt, say one that trails a
  // command that already timed out, would complete the next command instantly.
  if (s.busy && (s.echo_seen || s.response_seen)) {
    finish(s.pending_result, s.reason);
  }
}

static void rx_byte(uint8_t b) {
  if (b == '\r') return;
  if (b == '\n') {
    s.line[s.line_len] = '\0';
    if (s.line_overflow) s.overflows++;
    s.line_overflow = false;
    s.line_len = 0;
    handle_line(s.line);
    return;
  }
  if (s.line_len < sizeof(s.line) - 1u) {
    s.line[s.line_len++] = (char)b;
  } else {
    s.line_overflow = true;
  }
  // "> " at the start of a line is the prompt even with no newline after it;
  // whatever follows on the same line ("> [Data available: 8,2]") is parsed
  // as a fresh line.
  if (s.line_len == 2u && s.line[0] == '>' && s.line[1] == ' ') {
    s.line_len = 0;
    handle_prompt();
  }
}

static void start_next(void) {
  if (s.busy || s.q_count == 0u) return;
  const cmd_t* c = &s.queue[s.q_head];

  char wire[WISUN_CMD_MAX + 2u];
  size_t n = str_copy(wire, sizeof(wire), c->text);
  wire[n++] = '\r';
  wire[n++] = '\n';
  if (!hal_uart_write((const uint8_t*)wire, n)) return;   // TX ring full: retry next poll

#if WISUN_LOG_UART_TRAFFIC
  wlog_line("TX> ", c->text);
#endif
  s.busy = true;
  s.sent_at = hal_time_ms();
  s.echo_seen = false;
  s.response_seen = false;
  s.pending_result = CLI_OK;
  s.reason[0] = '\0';
}

void cli_poll(void) {
  uint8_t chunk[64];
  size_t n;
  while ((n = hal_uart_read(chunk, sizeof(chunk))) > 0u) {
    for (size_t i = 0; i < n; i++) rx_byte(chunk[i]);
  }

  if (s.busy && (uint32_t)(hal_time_ms() - s.sent_at) >= s.queue[s.q_head].timeout_ms) {
    if (s.capturing) payload_done();
    // A reply that already reported failure is still a failure, not a timeout.
    if (s.pending_result == CLI_FAILED) finish(CLI_FAILED, s.reason);
    else finish(CLI_TIMEOUT, "timeout");
  }
  start_next();
}

bool cli_enqueue(const char* cmd, uint32_t timeout_ms, uint8_t tag) {
  if (s.q_count >= WISUN_CMD_QUEUE_DEPTH) return false;
  if (strlen(cmd) > 180u) return false;                // spec 3.1: lines <= 180 chars
  cmd_t* c = &s.queue[(s.q_head + s.q_count) % WISUN_CMD_QUEUE_DEPTH];
  str_copy(c->text, sizeof(c->text), cmd);
  c->timeout_ms = timeout_ms;
  c->tag = tag;
  s.q_count++;
  start_next();
  return true;
}

bool cli_send_raw(const char* text) {
  if (s.busy) return false;
  return hal_uart_write((const uint8_t*)text, strlen(text));
}

void cli_reset(void) {
  if (s.busy) finish(CLI_ABORTED, "aborted");
  while (s.q_count) {
    s.busy = true;                                     // so finish() reports it
    finish(CLI_ABORTED, "aborted");
  }
  s.line_len = 0;
  s.line_overflow = false;
  s.capturing = false;
}

bool cli_idle(void) { return !s.busy && s.q_count == 0u; }
uint32_t cli_prompt_count(void) { return s.prompts; }
uint32_t cli_queue_free(void) { return WISUN_CMD_QUEUE_DEPTH - s.q_count; }
uint32_t cli_line_overflows(void) { return s.overflows; }
