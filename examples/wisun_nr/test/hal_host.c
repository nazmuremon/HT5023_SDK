// test/hal_host.c — wisun_hal.h on a PC, wired to the E51 simulator.
//
// Time is virtual: the test advances it one millisecond per loop. Bytes from
// the simulated module reach the firmware at 115200 baud (~11 bytes per ms)
// through a 4 KB ring like the target's, so responses split mid-line and a slow
// reader overflows exactly as it would on the board.
#include <stdio.h>
#include <string.h>

#include "../wisun_config.h"
#include "../wisun_hal.h"
#include "sim_e51.h"

uint32_t g_now_ms;
int g_verbose;

static uint8_t rx_ring[WISUN_UART_RX_RING];
static uint32_t rx_head, rx_tail, rx_overflows;

static char log_buf[1u << 20];
static size_t log_len;

static char console_q[256];
static size_t console_len;

void hal_host_reset(void) {
  rx_head = rx_tail = rx_overflows = 0;
  console_len = 0;
}

// Called by the simulator for every byte "on the wire" towards the MCU.
bool hal_host_rx_push(uint8_t b) {
  uint32_t next = (rx_head + 1u) % WISUN_UART_RX_RING;
  if (next == rx_tail) {
    rx_overflows++;
    return false;
  }
  rx_ring[rx_head] = b;
  rx_head = next;
  return true;
}

void hal_host_console(const char* line) {
  size_t n = strlen(line);
  if (console_len + n + 1u < sizeof(console_q)) {
    memcpy(console_q + console_len, line, n);
    console_len += n;
    console_q[console_len++] = '\n';
  }
}

const char* hal_host_log(void) { return log_buf; }
void hal_host_log_clear(void) {
  log_len = 0;
  log_buf[0] = '\0';
}

void hal_init(void) {}

bool hal_uart_init(uint32_t baud) {
  (void)baud;
  return true;
}

bool hal_uart_write(const uint8_t* bytes, size_t len) {
  sim_from_mcu(bytes, len);
  return true;
}

size_t hal_uart_read(uint8_t* buf, size_t max) {
  size_t n = 0;
  while (n < max && rx_tail != rx_head) {
    buf[n++] = rx_ring[rx_tail];
    rx_tail = (rx_tail + 1u) % WISUN_UART_RX_RING;
  }
  return n;
}

uint32_t hal_uart_rx_overflows(void) { return rx_overflows; }

uint32_t hal_time_ms(void) { return g_now_ms; }

void hal_log(const char* text) {
  if (g_verbose) fputs(text, stdout);
  size_t n = strlen(text);
  if (log_len + n + 1u >= sizeof(log_buf)) log_len = 0;   // keep the most recent MB
  memcpy(log_buf + log_len, text, n);
  log_len += n;
  log_buf[log_len] = '\0';
}

bool hal_console_read_line(char* buf, size_t max) {
  char* nl = memchr(console_q, '\n', console_len);
  if (!nl) return false;
  size_t n = (size_t)(nl - console_q);
  size_t c = n < max - 1u ? n : max - 1u;
  memcpy(buf, console_q, c);
  buf[c] = '\0';
  memmove(console_q, nl + 1, console_len - n - 1u);
  console_len -= n + 1u;
  return true;
}

void hal_module_reset_pulse(void) {}
bool hal_module_reset_available(void) { return false; }
void hal_poll(void) {}
