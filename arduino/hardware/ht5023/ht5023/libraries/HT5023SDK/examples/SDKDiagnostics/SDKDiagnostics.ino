#include <HT5023SDK.h>

static ht5023_ringbuf_t g_log_buffer;
static uint8_t g_log_storage[128];

static void serial_putc(char c, void* ctx) {
  (void)ctx;
  Serial.write((uint8_t)c);
}

static void log_to_serial_and_buffer(const char* bytes, size_t len, void* user) {
  ht5023_ringbuf_t* rb = (ht5023_ringbuf_t*)user;
  if (bytes && len != 0u) {
    Serial.write((const uint8_t*)bytes, len);
    if (rb) {
      for (size_t i = 0; i < len; ++i) {
        (void)ht5023_ringbuf_push(rb, (uint8_t)bytes[i]);
      }
    }
  }
}

static void print_boot_banner(void) {
  const ht5023_banner_chip_info_t chip = {
      .chip_name = "HT5023",
      .core_name = "Cortex-M0",
      .flash_kib = 256u,
      .info_flash_kib = 1u,
      .sram_kib = 32u,
      .sysclk_hz_nominal = (uint32_t)F_CPU,
      .uart_ports = 6u,
      .has_spi = true,
      .has_i2c = true,
      .has_rtc = false,
      .has_lcd = true,
      .has_dma = true,
  };
  const ht5023_banner_runtime_t runtime = {
      .app_name = "SDKDiagnostics",
      .version = "v1",
      .uart_path = "UART2 PC11/PC12 @ 115200",
      .uptime_ms = millis(),
      .frames = 0u,
      .rx_bytes = 0u,
      .resets_seen = 0u,
      .ansi_enabled = true,
  };
  ht5023_boot_banner_print(serial_putc, 0, &chip, &runtime);
  Serial.println();
}

static void print_reset_status(void) {
  const ht5023_reset_status_t status = ht5023_reset_read();
  Serial.print("RSTSTA=0x");
  Serial.println(status.raw, HEX);
  Serial.print("POR=");
  Serial.print(status.por_reset ? 1 : 0);
  Serial.print(" WDT=");
  Serial.print(status.wdt_reset ? 1 : 0);
  Serial.print(" EXT=");
  Serial.println(status.ext_reset ? 1 : 0);
}

void setup() {
  const ht5023_timer_periodic_cfg_t timer_cfg = {
      .prescaler = 9999u,
      .period = 999u,
  };

  Serial.begin(115200);

  ht5023_ringbuf_init(&g_log_buffer, g_log_storage, sizeof(g_log_storage));
  ht5023_log_set_sink(log_to_serial_and_buffer, &g_log_buffer);
  ht5023_log_set_level(HT5023_LOG_LEVEL_INFO);

  print_boot_banner();
  print_reset_status();

  if (ht5023_timer_periodic_start(HT5023_TMR0, &timer_cfg)) {
    ht5023_log_write(HT5023_LOG_LEVEL_INFO, "sdk", "timer0 started");
  } else {
    ht5023_log_write(HT5023_LOG_LEVEL_ERROR, "sdk", "timer0 start failed");
  }
}

void loop() {
  static uint32_t ticks = 0u;
  if (ht5023_timer_period_elapsed(HT5023_TMR0)) {
    ++ticks;
    ht5023_log_write(HT5023_LOG_LEVEL_INFO,
                     "sdk",
                     "tick=%lu ringbuf=%u wdt_set=%u",
                     (unsigned long)ticks,
                     (unsigned)ht5023_ringbuf_count(&g_log_buffer),
                     (unsigned)ht5023_wdt_get_set_value());
  }
}
