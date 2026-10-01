#include <Arduino.h>
#include <HT5023SDK.h>
#include <math.h>

struct Ht5023EnergyData {
  float voltage = NAN;
  float current = NAN;
  float power = NAN;
  float energy = NAN;
  float frequency = NAN;
  float pf = NAN;
  bool valid = false;
};

static Ht5023EnergyData g_energy;
static unsigned long g_last_sample_ms = 0u;
static bool g_led_state = false;

static void serial_putc(char c, void* ctx) {
  (void)ctx;
  Serial.write((uint8_t)c);
}

static void print_banner(void) {
  const ht5023_banner_chip_info_t chip = {
      .chip_name = "HT5023",
      .core_name = "Cortex-M0",
      .flash_kib = 256u,
      .info_flash_kib = 1u,
      .sram_kib = 8u,
      .sysclk_hz_nominal = (uint32_t)F_CPU,
      .uart_ports = 6u,
      .has_spi = true,
      .has_i2c = true,
      .has_rtc = false,
      .has_lcd = true,
      .has_dma = true,
  };
  const ht5023_banner_runtime_t runtime = {
      .app_name = "Energymeter HT5023",
      .version = "bringup",
      .uart_path = "UART2 PC11/PC12 @ 115200",
      .uptime_ms = millis(),
      .frames = 0u,
      .rx_bytes = 0u,
      .resets_seen = 0u,
      .ansi_enabled = true,
  };
  ht5023_boot_banner_print(serial_putc, 0, &chip, &runtime);
  Serial.println();
  Serial.println("ESP32-only networking, Preferences, and display drivers are disabled in this HT5023 build profile.");
}

static void sample_energy(Ht5023EnergyData& data) {
  const float phase = (float)(millis() % 10000u) / 10000.0f;
  data.voltage = 228.0f + (phase * 4.0f);
  data.current = 1.20f + (phase * 0.15f);
  data.power = data.voltage * data.current;
  data.energy += data.power / 3600000.0f;
  data.frequency = 50.0f;
  data.pf = 0.98f;
  data.valid = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  g_energy.energy = 0.0f;
  print_banner();
}

void loop() {
  const unsigned long now = millis();
  if ((now - g_last_sample_ms) < 1000u) {
    return;
  }

  g_last_sample_ms = now;
  g_led_state = !g_led_state;
  digitalWrite(LED_BUILTIN, g_led_state ? HIGH : LOW);

  sample_energy(g_energy);
  Serial.print("V=");
  Serial.print(g_energy.voltage, 1);
  Serial.print(" I=");
  Serial.print(g_energy.current, 3);
  Serial.print(" P=");
  Serial.print(g_energy.power, 1);
  Serial.print(" E=");
  Serial.print(g_energy.energy, 6);
  Serial.print(" PF=");
  Serial.println(g_energy.pf, 2);
}
