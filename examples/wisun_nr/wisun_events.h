// wisun_events.h — turns E51 CLI output lines into typed events.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "wisun_config.h"

typedef enum {
  EV_NONE = 0,          // not an event (a key = value line, an echo, ...)
  EV_CONNECTING,        // [Connecting to "NAME"]
  EV_JOIN_STATE,        // [Join state N: TEXT]              a = N
  EV_CONNECTED,         // [Connected: S s]                  a = S
  EV_IPV6,              // [IPv6 address: IP]                ip
  EV_DISCONNECTED,      // [Disconnected]
  EV_LISTENING,         // [Listening: N]                    a = N
  EV_OPENING,           // [Opening: IP (PORT): N]           ip, a = N, b = PORT
  EV_OPENED,            // [Opened: N]                       a = N
  EV_CONN_AVAILABLE,    // [Socket connection available: L]  a = L
  EV_ACCEPTED,          // [Accepted IP (PORT): N]           ip, a = N, b = PORT
  EV_DATA_AVAILABLE,    // [Data available: N,LEN]           a = N, b = LEN
  EV_DATA_SENT,         // [Data sent: N,FREE]               a = N, b = FREE
  EV_DATA_FROM,         // [Data from IP (PORT): N,LEN       ip, a = N, b = LEN, c = PORT
  EV_CLOSED,            // [Closed: N]                       a = N
  EV_PING_REPLY,        // <n> bytes from IP: icmp_seq=...   ip
  EV_MODULE_BOOT,       // Wi-SUN CLI Application
  EV_FAILED,            // [Failed: REASON]                  text
  EV_WROTE,             // [Wrote N bytes]                   a = N
  EV_SETTINGS_SAVED,    // [Settings saved]
  EV_CCC,               // [Credential cache cleared]
  EV_INFO,              // other known response lines: [Using ...], [Settings reset]
  EV_UNKNOWN,           // any other [...] line -- logged, never fatal
} ev_type_t;

typedef struct {
  ev_type_t type;
  int32_t a;
  int32_t b;
  int32_t c;
  char ip[WISUN_IPV6_MAX];
  char text[64];
} wisun_event_t;

// Parses one complete line (no CR/LF). Always fills ev; returns type != EV_NONE.
ev_type_t ev_parse(const char* line, wisun_event_t* ev);

const char* ev_name(ev_type_t t);
