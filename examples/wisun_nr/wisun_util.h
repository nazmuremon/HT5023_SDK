// wisun_util.h — small string and logging helpers (no printf, no heap).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ------------------------------------------------------------------ logging --
// Builds one log line and hands it to hal_log(). Text longer than the buffer is
// flushed in pieces rather than truncated, so a 1 KB payload still logs whole.
//
// Two kinds of line:
//   wlog_begin       diagnostic, prefixed "[NR] <ms> " as the spec asks; dropped
//                    when WISUN_LOG_PAYLOAD_ONLY is 1.
//   wlog_begin_user  payloads and console replies, no prefix, always printed.
typedef struct {
  char buf[200];
  size_t n;
  bool show;
} wlog_t;

void wlog_begin(wlog_t* l);
void wlog_begin_user(wlog_t* l);

// Payload-only logging can be switched at runtime (console: "log all" /
// "log payload"); it starts as WISUN_LOG_PAYLOAD_ONLY.
void wlog_set_payload_only(bool on);
bool wlog_payload_only(void);
void wlog_str(wlog_t* l, const char* s);
void wlog_strn(wlog_t* l, const char* s, size_t n);
void wlog_u32(wlog_t* l, uint32_t v);
void wlog_i32(wlog_t* l, int32_t v);
void wlog_hex(wlog_t* l, const uint8_t* bytes, size_t n);
void wlog_end(wlog_t* l);

// One-shot "[NR] <ms> <a><b>" (diagnostic) and "<a><b>" (always shown).
void wlog_line(const char* a, const char* b);
void wlog_user_line(const char* a, const char* b);

// ------------------------------------------------------------------ strings --
bool str_starts(const char* s, const char* prefix);
bool str_contains(const char* s, const char* needle);
size_t str_copy(char* dst, size_t cap, const char* src);
size_t str_copyn(char* dst, size_t cap, const char* src, size_t n);
bool str_ieq(const char* a, const char* b);

// Parse an unsigned decimal at *p, advancing *p past it. False if no digit.
bool parse_u32(const char** p, uint32_t* out);

// Appends decimal / uppercase hex into a buffer, returning the new length.
size_t fmt_u32(char* dst, size_t pos, size_t cap, uint32_t v);
size_t fmt_str(char* dst, size_t pos, size_t cap, const char* s);

// A global (routable) IPv6 address as the module prints it: non-empty, not
// link-local fe80::, not the unspecified "::".
bool ipv6_is_global(const char* ip);
