// wisun_events.c — see wisun_events.h. Formats are the verified ones from real
// E51 sessions (spec 3.2-3.7 and 5.1).
#include "wisun_events.h"

#include <string.h>

#include "wisun_util.h"

// Reads a signed decimal, allowing leading spaces.
static bool read_int(const char** p, int32_t* out) {
  while (**p == ' ') (*p)++;
  bool neg = false;
  if (**p == '-') {
    neg = true;
    (*p)++;
  }
  uint32_t v;
  if (!parse_u32(p, &v)) return false;
  *out = neg ? -(int32_t)v : (int32_t)v;
  return true;
}

// Copies an IP that runs up to (not including) `stop`. False if stop is absent.
static bool read_ip_until(const char** p, const char* stop, char* ip) {
  const char* end = strstr(*p, stop);
  if (!end) return false;
  str_copyn(ip, WISUN_IPV6_MAX, *p, (size_t)(end - *p));
  *p = end + strlen(stop);
  return true;
}

// "IP (PORT): N"  and  "IP (PORT): N,LEN"
static bool read_ip_port_id(const char** p, wisun_event_t* ev, bool with_len) {
  if (!read_ip_until(p, " (", ev->ip)) return false;
  int32_t port;
  if (!read_int(p, &port) || **p != ')') return false;
  (*p)++;
  if (**p != ':') return false;
  (*p)++;
  if (!read_int(p, &ev->a)) return false;
  if (with_len) {
    if (**p != ',') return false;
    (*p)++;
    if (!read_int(p, &ev->b)) return false;
    ev->c = port;
  } else {
    ev->b = port;
  }
  return true;
}

ev_type_t ev_parse(const char* line, wisun_event_t* ev) {
  memset(ev, 0, sizeof(*ev));
  ev->a = ev->b = ev->c = -1;
  const char* p;

  if (str_contains(line, "Wi-SUN CLI Application")) return ev->type = EV_MODULE_BOOT;

  // "40 bytes from fd12::7369: icmp_seq=1 time=84 ms"
  p = strstr(line, " bytes from ");
  if (p && line[0] >= '0' && line[0] <= '9') {
    p += strlen(" bytes from ");
    if (read_ip_until(&p, ": icmp_seq", ev->ip)) return ev->type = EV_PING_REPLY;
  }

  if (line[0] != '[') return EV_NONE;

  // Header of a socket_read reply; deliberately has no closing bracket.
  if (str_starts(line, "[Data from ")) {
    p = line + strlen("[Data from ");
    if (read_ip_port_id(&p, ev, true)) return ev->type = EV_DATA_FROM;
    return ev->type = EV_UNKNOWN;
  }

  size_t len = strlen(line);
  if (line[len - 1u] != ']') return ev->type = EV_UNKNOWN;

  if (str_starts(line, "[Join state ")) {
    p = line + strlen("[Join state ");
    if (read_int(&p, &ev->a) && *p == ':') {
      str_copyn(ev->text, sizeof(ev->text), p + 2, len - (size_t)(p + 2 - line) - 1u);
      return ev->type = EV_JOIN_STATE;
    }
  } else if (str_starts(line, "[Connecting to ")) {
    str_copyn(ev->text, sizeof(ev->text), line + 15, len - 16u);
    return ev->type = EV_CONNECTING;
  } else if (str_starts(line, "[Connected: ")) {
    p = line + strlen("[Connected: ");
    if (read_int(&p, &ev->a)) return ev->type = EV_CONNECTED;
  } else if (str_starts(line, "[IPv6 address: ")) {
    str_copyn(ev->ip, sizeof(ev->ip), line + 15, len - 16u);
    return ev->type = EV_IPV6;
  } else if (strcmp(line, "[Disconnected]") == 0) {
    return ev->type = EV_DISCONNECTED;
  } else if (str_starts(line, "[Listening: ")) {
    p = line + strlen("[Listening: ");
    if (read_int(&p, &ev->a)) return ev->type = EV_LISTENING;
  } else if (str_starts(line, "[Opening: ")) {
    p = line + strlen("[Opening: ");
    if (read_ip_port_id(&p, ev, false)) return ev->type = EV_OPENING;
  } else if (str_starts(line, "[Opened: ")) {
    p = line + strlen("[Opened: ");
    if (read_int(&p, &ev->a)) return ev->type = EV_OPENED;
  } else if (str_starts(line, "[Socket connection available: ")) {
    p = line + strlen("[Socket connection available: ");
    if (read_int(&p, &ev->a)) return ev->type = EV_CONN_AVAILABLE;
  } else if (str_starts(line, "[Accepted ")) {
    p = line + strlen("[Accepted ");
    if (read_ip_port_id(&p, ev, false)) return ev->type = EV_ACCEPTED;
  } else if (str_starts(line, "[Data available: ")) {
    p = line + strlen("[Data available: ");
    if (read_int(&p, &ev->a) && *p == ',' && (p++, read_int(&p, &ev->b)))
      return ev->type = EV_DATA_AVAILABLE;
  } else if (str_starts(line, "[Data sent: ")) {
    p = line + strlen("[Data sent: ");
    if (read_int(&p, &ev->a) && *p == ',' && (p++, read_int(&p, &ev->b)))
      return ev->type = EV_DATA_SENT;
  } else if (str_starts(line, "[Closed: ")) {
    p = line + strlen("[Closed: ");
    if (read_int(&p, &ev->a)) return ev->type = EV_CLOSED;
  } else if (str_starts(line, "[Failed: ")) {
    str_copyn(ev->text, sizeof(ev->text), line + 9, len - 10u);
    return ev->type = EV_FAILED;
  } else if (str_starts(line, "[Wrote ")) {
    p = line + strlen("[Wrote ");
    if (read_int(&p, &ev->a)) return ev->type = EV_WROTE;
  } else if (strcmp(line, "[Settings saved]") == 0) {
    return ev->type = EV_SETTINGS_SAVED;
  } else if (strcmp(line, "[Credential cache cleared]") == 0) {
    return ev->type = EV_CCC;
  } else if (str_starts(line, "[Using ") || strcmp(line, "[Settings reset]") == 0) {
    return ev->type = EV_INFO;
  }
  return ev->type = EV_UNKNOWN;
}

const char* ev_name(ev_type_t t) {
  static const char* const k[] = {
    "none", "connecting", "join_state", "connected", "ipv6", "disconnected",
    "listening", "opening", "opened", "conn_available", "accepted",
    "data_available", "data_sent", "data_from", "closed", "ping_reply",
    "module_boot", "failed", "wrote", "settings_saved", "ccc", "info", "unknown",
  };
  return (unsigned)t < sizeof(k) / sizeof(k[0]) ? k[t] : "?";
}
