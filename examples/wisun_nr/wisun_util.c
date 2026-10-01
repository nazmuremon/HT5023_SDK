// wisun_util.c — see wisun_util.h.
#include "wisun_util.h"

#include <string.h>

#include "wisun_config.h"
#include "wisun_hal.h"

// ------------------------------------------------------------------ logging --
static void wlog_flush(wlog_t* l) {
  if (l->n == 0u) return;
  l->buf[l->n] = '\0';
  if (l->show) hal_log(l->buf);
  l->n = 0;
}

void wlog_strn(wlog_t* l, const char* s, size_t n) {
  for (size_t i = 0; i < n && s[i]; i++) {
    if (l->n >= sizeof(l->buf) - 3u) wlog_flush(l);   // room for "\r\n\0"
    const char c = s[i];
    l->buf[l->n++] = (c == '\r' || c == '\n') ? ' ' : c;
  }
}

void wlog_str(wlog_t* l, const char* s) { wlog_strn(l, s, (size_t)-1); }

void wlog_u32(wlog_t* l, uint32_t v) {
  char tmp[11];
  size_t n = fmt_u32(tmp, 0, sizeof(tmp), v);
  tmp[n] = '\0';
  wlog_str(l, tmp);
}

void wlog_i32(wlog_t* l, int32_t v) {
  if (v < 0) {
    wlog_str(l, "-");
    wlog_u32(l, (uint32_t)(-(int64_t)v));
  } else {
    wlog_u32(l, (uint32_t)v);
  }
}

void wlog_hex(wlog_t* l, const uint8_t* bytes, size_t n) {
  static const char k[] = "0123456789ABCDEF";
  for (size_t i = 0; i < n; i++) {
    char two[3] = { k[bytes[i] >> 4], k[bytes[i] & 15u], '\0' };
    wlog_str(l, two);
  }
}

static bool s_payload_only = WISUN_LOG_PAYLOAD_ONLY != 0;

void wlog_set_payload_only(bool on) { s_payload_only = on; }
bool wlog_payload_only(void) { return s_payload_only; }

void wlog_begin_user(wlog_t* l) {
  l->n = 0;
  l->show = true;
}

void wlog_begin(wlog_t* l) {
  l->n = 0;
  l->show = !s_payload_only;
  wlog_str(l, "[NR] ");
  wlog_u32(l, hal_time_ms());
  wlog_str(l, " ");
}

void wlog_end(wlog_t* l) {
  l->buf[l->n++] = '\r';
  l->buf[l->n++] = '\n';
  wlog_flush(l);
}

void wlog_line(const char* a, const char* b) {
  wlog_t l;
  wlog_begin(&l);
  if (a) wlog_str(&l, a);
  if (b) wlog_str(&l, b);
  wlog_end(&l);
}

void wlog_user_line(const char* a, const char* b) {
  wlog_t l;
  wlog_begin_user(&l);
  if (a) wlog_str(&l, a);
  if (b) wlog_str(&l, b);
  wlog_end(&l);
}

// ------------------------------------------------------------------ strings --
bool str_starts(const char* s, const char* prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

bool str_contains(const char* s, const char* needle) {
  return strstr(s, needle) != NULL;
}

size_t str_copyn(char* dst, size_t cap, const char* src, size_t n) {
  if (cap == 0u) return 0;
  size_t i = 0;
  while (i < n && i + 1u < cap && src[i]) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
  return i;
}

size_t str_copy(char* dst, size_t cap, const char* src) {
  return str_copyn(dst, cap, src, (size_t)-1);
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

bool str_ieq(const char* a, const char* b) {
  while (*a && *b) {
    if (lower(*a) != lower(*b)) return false;
    a++;
    b++;
  }
  return *a == *b;
}

bool parse_u32(const char** p, uint32_t* out) {
  const char* s = *p;
  if (*s < '0' || *s > '9') return false;
  uint32_t v = 0;
  while (*s >= '0' && *s <= '9') {
    v = v * 10u + (uint32_t)(*s - '0');
    s++;
  }
  *out = v;
  *p = s;
  return true;
}

size_t fmt_str(char* dst, size_t pos, size_t cap, const char* s) {
  while (*s && pos + 1u < cap) dst[pos++] = *s++;
  dst[pos] = '\0';
  return pos;
}

size_t fmt_u32(char* dst, size_t pos, size_t cap, uint32_t v) {
  char tmp[10];
  size_t n = 0;
  do {
    tmp[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  } while (v && n < sizeof(tmp));
  while (n && pos + 1u < cap) dst[pos++] = tmp[--n];
  dst[pos] = '\0';
  return pos;
}

bool ipv6_is_global(const char* ip) {
  if (!ip || !ip[0]) return false;
  if (strcmp(ip, "::") == 0) return false;
  return !(lower(ip[0]) == 'f' && lower(ip[1]) == 'e' && ip[2] == '8' && ip[3] == '0');
}
