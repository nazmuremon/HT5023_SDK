// ht5023_log.h — tiny sink-based logger with minimal printf-style formatting
#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HT5023_LOG_LEVEL_TRACE = 0,
  HT5023_LOG_LEVEL_DEBUG = 1,
  HT5023_LOG_LEVEL_INFO  = 2,
  HT5023_LOG_LEVEL_WARN  = 3,
  HT5023_LOG_LEVEL_ERROR = 4,
  HT5023_LOG_LEVEL_NONE  = 5,
} ht5023_log_level_t;

typedef void (*ht5023_log_sink_fn)(const char* bytes, size_t len, void* user);

void ht5023_log_set_sink(ht5023_log_sink_fn sink, void* user);
void ht5023_log_set_level(ht5023_log_level_t level);
ht5023_log_level_t ht5023_log_get_level(void);
int ht5023_log_is_enabled(ht5023_log_level_t level);

void ht5023_log_vwrite(ht5023_log_level_t level, const char* tag, const char* fmt, va_list ap);
void ht5023_log_write(ht5023_log_level_t level, const char* tag, const char* fmt, ...);

#ifdef __cplusplus
}
#endif
