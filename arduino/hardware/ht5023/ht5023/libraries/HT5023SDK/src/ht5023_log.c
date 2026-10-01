// ht5023_log.c — tiny sink-based logger with minimal printf-style formatting
#include "ht5023_log.h"

#include <stdbool.h>

typedef struct {
  ht5023_log_sink_fn sink;
  void* user;
  ht5023_log_level_t level;
} ht5023_log_state_t;

static ht5023_log_state_t g_log = {
  0,
  0,
  HT5023_LOG_LEVEL_INFO,
};

static char level_char(ht5023_log_level_t level) {
  switch (level) {
    case HT5023_LOG_LEVEL_TRACE: return 'T';
    case HT5023_LOG_LEVEL_DEBUG: return 'D';
    case HT5023_LOG_LEVEL_INFO:  return 'I';
    case HT5023_LOG_LEVEL_WARN:  return 'W';
    case HT5023_LOG_LEVEL_ERROR: return 'E';
    default: return '?';
  }
}

static void buf_putc(char* buf, size_t cap, size_t* n, char c) {
  if (!buf || !n || cap == 0u) return;
  if (*n + 1u < cap) buf[*n] = c;
  (*n)++;
}

static void buf_puts(char* buf, size_t cap, size_t* n, const char* s) {
  if (!s) s = "(null)";
  while (*s) buf_putc(buf, cap, n, *s++);
}

static void buf_putu(char* buf, size_t cap, size_t* n, unsigned long v, unsigned base, bool upper) {
  char tmp[16];
  size_t t = 0u;
  const char a = upper ? 'A' : 'a';
  if (base < 2u || base > 16u) return;
  if (v == 0u) {
    buf_putc(buf, cap, n, '0');
    return;
  }
  while (v != 0u && t < sizeof(tmp)) {
    const unsigned digit = (unsigned)(v % base);
    tmp[t++] = (char)((digit < 10u) ? ('0' + digit) : (a + (digit - 10u)));
    v /= base;
  }
  while (t != 0u) buf_putc(buf, cap, n, tmp[--t]);
}

static void buf_putd(char* buf, size_t cap, size_t* n, long v) {
  if (v < 0) {
    buf_putc(buf, cap, n, '-');
    buf_putu(buf, cap, n, (unsigned long)(0u - (unsigned long)v), 10u, false);
    return;
  }
  buf_putu(buf, cap, n, (unsigned long)v, 10u, false);
}

void ht5023_log_set_sink(ht5023_log_sink_fn sink, void* user) {
  g_log.sink = sink;
  g_log.user = user;
}

void ht5023_log_set_level(ht5023_log_level_t level) {
  g_log.level = level;
}

ht5023_log_level_t ht5023_log_get_level(void) {
  return g_log.level;
}

int ht5023_log_is_enabled(ht5023_log_level_t level) {
  if (!g_log.sink) return 0;
  if (level < g_log.level) return 0;
  if (level >= HT5023_LOG_LEVEL_NONE) return 0;
  return 1;
}

void ht5023_log_vwrite(ht5023_log_level_t level, const char* tag, const char* fmt, va_list ap) {
  char buf[192];
  size_t n = 0u;
  if (!ht5023_log_is_enabled(level) || !fmt) return;

  buf_putc(buf, sizeof(buf), &n, '[');
  buf_putc(buf, sizeof(buf), &n, level_char(level));
  buf_putc(buf, sizeof(buf), &n, ']');
  if (tag && *tag) {
    buf_putc(buf, sizeof(buf), &n, '[');
    buf_puts(buf, sizeof(buf), &n, tag);
    buf_putc(buf, sizeof(buf), &n, ']');
  }
  buf_putc(buf, sizeof(buf), &n, ' ');

  while (*fmt) {
    if (*fmt != '%') {
      buf_putc(buf, sizeof(buf), &n, *fmt++);
      continue;
    }

    ++fmt;
    if (*fmt == '%') {
      buf_putc(buf, sizeof(buf), &n, *fmt++);
      continue;
    }

    {
      bool long_arg = false;
      if (*fmt == 'l') {
        long_arg = true;
        ++fmt;
      }

      switch (*fmt) {
        case 'c':
          buf_putc(buf, sizeof(buf), &n, (char)va_arg(ap, int));
          ++fmt;
          break;
        case 's':
          buf_puts(buf, sizeof(buf), &n, va_arg(ap, const char*));
          ++fmt;
          break;
        case 'u':
          if (long_arg) buf_putu(buf, sizeof(buf), &n, va_arg(ap, unsigned long), 10u, false);
          else buf_putu(buf, sizeof(buf), &n, (unsigned long)va_arg(ap, unsigned int), 10u, false);
          ++fmt;
          break;
        case 'd':
          if (long_arg) buf_putd(buf, sizeof(buf), &n, va_arg(ap, long));
          else buf_putd(buf, sizeof(buf), &n, (long)va_arg(ap, int));
          ++fmt;
          break;
        case 'x':
          if (long_arg) buf_putu(buf, sizeof(buf), &n, va_arg(ap, unsigned long), 16u, false);
          else buf_putu(buf, sizeof(buf), &n, (unsigned long)va_arg(ap, unsigned int), 16u, false);
          ++fmt;
          break;
        case 'X':
          if (long_arg) buf_putu(buf, sizeof(buf), &n, va_arg(ap, unsigned long), 16u, true);
          else buf_putu(buf, sizeof(buf), &n, (unsigned long)va_arg(ap, unsigned int), 16u, true);
          ++fmt;
          break;
        case 'p':
          buf_puts(buf, sizeof(buf), &n, "0x");
          buf_putu(buf, sizeof(buf), &n, (unsigned long)va_arg(ap, uintptr_t), 16u, false);
          ++fmt;
          break;
        default:
          buf_putc(buf, sizeof(buf), &n, '%');
          if (long_arg) buf_putc(buf, sizeof(buf), &n, 'l');
          if (*fmt) buf_putc(buf, sizeof(buf), &n, *fmt++);
          break;
      }
    }
  }

  buf_putc(buf, sizeof(buf), &n, '\r');
  buf_putc(buf, sizeof(buf), &n, '\n');
  if (n >= sizeof(buf)) n = sizeof(buf) - 1u;
  g_log.sink(buf, n, g_log.user);
}

void ht5023_log_write(ht5023_log_level_t level, const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  ht5023_log_vwrite(level, tag, fmt, ap);
  va_end(ap);
}
