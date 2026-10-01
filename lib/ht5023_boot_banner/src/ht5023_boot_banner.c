// ht5023_boot_banner.c — simple colored boot banner/status printer
#include "ht5023_boot_banner.h"

#include <stddef.h>
#include <stdint.h>

static void outc(ht5023_banner_putc_fn fn, void* ctx, char c) {
  if (fn) fn(c, ctx);
}

static void outs(ht5023_banner_putc_fn fn, void* ctx, const char* s) {
  if (!fn || !s) return;
  while (*s) fn(*s++, ctx);
}

static void out_u32(ht5023_banner_putc_fn fn, void* ctx, uint32_t v) {
  char buf[10];
  uint8_t n = 0;
  if (v == 0u) {
    outc(fn, ctx, '0');
    return;
  }
  while (v != 0u && n < sizeof(buf)) {
    buf[n++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  while (n) outc(fn, ctx, buf[--n]);
}

static void out_bool(ht5023_banner_putc_fn fn, void* ctx, bool v) {
  outs(fn, ctx, v ? "yes" : "no");
}

static void ansi(ht5023_banner_putc_fn fn, void* ctx, bool en, const char* seq) {
  if (en) outs(fn, ctx, seq);
}

static void kv_str(ht5023_banner_putc_fn fn, void* ctx, bool ansi_en,
                   const char* k, const char* v) {
  ansi(fn, ctx, ansi_en, "\x1b[36m");
  outs(fn, ctx, k);
  ansi(fn, ctx, ansi_en, "\x1b[0m");
  outs(fn, ctx, ": ");
  ansi(fn, ctx, ansi_en, "\x1b[37m");
  outs(fn, ctx, v ? v : "-");
  ansi(fn, ctx, ansi_en, "\x1b[0m");
  outs(fn, ctx, "\r\n");
}

static void kv_u32(ht5023_banner_putc_fn fn, void* ctx, bool ansi_en,
                   const char* k, uint32_t v, const char* suffix) {
  ansi(fn, ctx, ansi_en, "\x1b[36m");
  outs(fn, ctx, k);
  ansi(fn, ctx, ansi_en, "\x1b[0m");
  outs(fn, ctx, ": ");
  ansi(fn, ctx, ansi_en, "\x1b[37m");
  out_u32(fn, ctx, v);
  if (suffix) outs(fn, ctx, suffix);
  ansi(fn, ctx, ansi_en, "\x1b[0m");
  outs(fn, ctx, "\r\n");
}

void ht5023_boot_banner_print(
    ht5023_banner_putc_fn putc_fn, void* ctx,
    const ht5023_banner_chip_info_t* chip,
    const ht5023_banner_runtime_t* rt) {
  const bool ansi_en = (rt && rt->ansi_enabled);

  if (!putc_fn || !chip || !rt) return;

  ansi(putc_fn, ctx, ansi_en, "\x1b[2J\x1b[H\x1b[?25l");
  ansi(putc_fn, ctx, ansi_en, "\x1b[1;35m");
  outs(putc_fn, ctx, "========================================\r\n");
  ansi(putc_fn, ctx, ansi_en, "\x1b[1;33m");
  outs(putc_fn, ctx, "  HT5023 BOOT BANNER / SYSTEM PROFILE   \r\n");
  ansi(putc_fn, ctx, ansi_en, "\x1b[1;35m");
  outs(putc_fn, ctx, "========================================\r\n");
  ansi(putc_fn, ctx, ansi_en, "\x1b[0m");

  kv_str(putc_fn, ctx, ansi_en, "app", rt->app_name);
  kv_str(putc_fn, ctx, ansi_en, "version", rt->version);
  kv_str(putc_fn, ctx, ansi_en, "chip", chip->chip_name);
  kv_str(putc_fn, ctx, ansi_en, "core", chip->core_name);

  ansi(putc_fn, ctx, ansi_en, "\x1b[32m");
  outs(putc_fn, ctx, "\r\n[MEMORY]\r\n");
  ansi(putc_fn, ctx, ansi_en, "\x1b[0m");
  kv_u32(putc_fn, ctx, ansi_en, "flash", chip->flash_kib, " KiB");
  kv_u32(putc_fn, ctx, ansi_en, "info_flash", chip->info_flash_kib, " KiB");
  kv_u32(putc_fn, ctx, ansi_en, "sram", chip->sram_kib, " KiB");

  ansi(putc_fn, ctx, ansi_en, "\x1b[32m");
  outs(putc_fn, ctx, "\r\n[CLOCK / IO]\r\n");
  ansi(putc_fn, ctx, ansi_en, "\x1b[0m");
  kv_u32(putc_fn, ctx, ansi_en, "sysclk_nominal", chip->sysclk_hz_nominal, " Hz");
  kv_u32(putc_fn, ctx, ansi_en, "uart_ports", chip->uart_ports, "");
  kv_str(putc_fn, ctx, ansi_en, "console", rt->uart_path);

  ansi(putc_fn, ctx, ansi_en, "\x1b[32m");
  outs(putc_fn, ctx, "\r\n[FEATURES]\r\n");
  ansi(putc_fn, ctx, ansi_en, "\x1b[0m");
  ansi(putc_fn, ctx, ansi_en, "\x1b[36m");
  outs(putc_fn, ctx, "spi");
  ansi(putc_fn, ctx, ansi_en, "\x1b[0m");
  outs(putc_fn, ctx, ": "); out_bool(putc_fn, ctx, chip->has_spi); outs(putc_fn, ctx, "  ");
  ansi(putc_fn, ctx, ansi_en, "\x1b[36m");
  outs(putc_fn, ctx, "i2c");
  ansi(putc_fn, ctx, ansi_en, "\x1b[0m");
  outs(putc_fn, ctx, ": "); out_bool(putc_fn, ctx, chip->has_i2c); outs(putc_fn, ctx, "  ");
  ansi(putc_fn, ctx, ansi_en, "\x1b[36m");
  outs(putc_fn, ctx, "rtc");
  ansi(putc_fn, ctx, ansi_en, "\x1b[0m");
  outs(putc_fn, ctx, ": "); out_bool(putc_fn, ctx, chip->has_rtc); outs(putc_fn, ctx, "\r\n");

  ansi(putc_fn, ctx, ansi_en, "\x1b[36m");
  outs(putc_fn, ctx, "lcd");
  ansi(putc_fn, ctx, ansi_en, "\x1b[0m");
  outs(putc_fn, ctx, ": "); out_bool(putc_fn, ctx, chip->has_lcd); outs(putc_fn, ctx, "  ");
  ansi(putc_fn, ctx, ansi_en, "\x1b[36m");
  outs(putc_fn, ctx, "dma");
  ansi(putc_fn, ctx, ansi_en, "\x1b[0m");
  outs(putc_fn, ctx, ": "); out_bool(putc_fn, ctx, chip->has_dma); outs(putc_fn, ctx, "\r\n");

  ansi(putc_fn, ctx, ansi_en, "\x1b[32m");
  outs(putc_fn, ctx, "\r\n[RUNTIME]\r\n");
  ansi(putc_fn, ctx, ansi_en, "\x1b[0m");
  kv_u32(putc_fn, ctx, ansi_en, "uptime", rt->uptime_ms, " ms");
  kv_u32(putc_fn, ctx, ansi_en, "frames", rt->frames, "");
  kv_u32(putc_fn, ctx, ansi_en, "rx_bytes", rt->rx_bytes, "");
  kv_u32(putc_fn, ctx, ansi_en, "resets_seen", rt->resets_seen, "");

  ansi(putc_fn, ctx, ansi_en, "\x1b[90m");
  outs(putc_fn, ctx, "\r\n(status line updates below)\r\n");
  ansi(putc_fn, ctx, ansi_en, "\x1b[0m");
}

void ht5023_boot_banner_print_runtime_line(
    ht5023_banner_putc_fn putc_fn, void* ctx,
    const ht5023_banner_runtime_t* rt) {
  static const char spinner[] = { '|', '/', '-', '\\' };
  if (!putc_fn || !rt) return;

  const bool ansi_en = rt->ansi_enabled;
  ansi(putc_fn, ctx, ansi_en, "\x1b[2K");
  ansi(putc_fn, ctx, ansi_en, "\x1b[33m");
  outs(putc_fn, ctx, "[");
  outc(putc_fn, ctx, spinner[rt->frames & 3u]);
  outs(putc_fn, ctx, "] ");
  ansi(putc_fn, ctx, ansi_en, "\x1b[0m");

  outs(putc_fn, ctx, "uptime=");
  out_u32(putc_fn, ctx, rt->uptime_ms);
  outs(putc_fn, ctx, "ms  frames=");
  out_u32(putc_fn, ctx, rt->frames);
  outs(putc_fn, ctx, "  rx=");
  out_u32(putc_fn, ctx, rt->rx_bytes);
  outs(putc_fn, ctx, "B  uart=");
  outs(putc_fn, ctx, rt->uart_path ? rt->uart_path : "-");
  outs(putc_fn, ctx, "\r");
}

