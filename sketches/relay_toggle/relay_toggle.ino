// relay_toggle -- switches the energy-meter PCB's relay on, then off, changing
// state every 5 seconds.
//
// Open this in the Flasher (or the Arduino IDE) and press Compile & Upload.
// Each switch is printed on Serial (115200).
//
// Wiring, from ht5023_energy_meter_v1 ("CIRCUIT CONTEXT"):
//
//   PC4 (net JDQH) -> U6 input A   75k pull-down (R65)
//   PC5 (net JDQL) -> U6 input B   75k pull-down (R64)
//   U6 = BL8023F bi-direction relay driver, VIN from +12V via R86 100R/D4
//        into a 1000uF reservoir (C18); outputs OA/OB -> J10 -> relay coil
//
// It is a *latching* relay: the coil is only pulsed, one direction to switch
// on and the other to switch off, and the contacts stay where they were left
// with the coil unpowered -- even across a reset. BL8023 logic table:
//
//   A B   OA/OB        relay
//   1 0   drive +/-    ON
//   0 1   drive -/+    OFF
//   0 0   high-Z       hold     <- where both pins rest between pulses
//   1 1   high-Z       hold     (never driven here)
//
// The BL8023 datasheet recommends 100 ms pulses at least 100 ms apart, which
// also leaves C18 (fed through 100R, so tau = 0.1 s) time to recharge.
//
// If the contacts do the opposite of what is printed, the relay's coil is
// wired the other way round on J10: swap RELAY_ON_PIN and RELAY_OFF_PIN.

// U6 input A: pulsing it switches the relay on ("trigger").
static const uint8_t RELAY_ON_PIN = PIN_PC4;
// U6 input B: pulsing it switches the relay off ("untrigger").
static const uint8_t RELAY_OFF_PIN = PIN_PC5;

// Time between switches: 5 s on, 5 s off.
static const unsigned long SWITCH_PERIOD_MS = 5000UL;

// Coil pulse length, as recommended by the BL8023 datasheet.
static const unsigned long PULSE_MS = 100UL;

static unsigned long g_next_switch_ms;
static bool g_relay_on;

// Drives one BL8023 input high for PULSE_MS, then returns both to 0 0 (hold).
// The other input is forced low first so the driver never sees 1 1.
static void pulseRelay(bool on) {
  const uint8_t drive = on ? RELAY_ON_PIN : RELAY_OFF_PIN;
  const uint8_t idle = on ? RELAY_OFF_PIN : RELAY_ON_PIN;

  digitalWrite(idle, LOW);
  digitalWrite(drive, HIGH);
  delay(PULSE_MS);
  digitalWrite(drive, LOW);

  g_relay_on = on;
}

void setup() {
  pinMode(RELAY_ON_PIN, OUTPUT);
  pinMode(RELAY_OFF_PIN, OUTPUT);
  digitalWrite(RELAY_ON_PIN, LOW);
  digitalWrite(RELAY_OFF_PIN, LOW);

  Serial.begin(115200);
  Serial.println();
  Serial.println("HT5023 relay_toggle: relay on J10 via BL8023 (PC4 on, PC5 off)");

  // A latching relay keeps its last position through a reset, so start from a
  // known state.
  pulseRelay(false);
  Serial.println("relay OFF (start-up)");

  g_next_switch_ms = millis() + SWITCH_PERIOD_MS;
}

void loop() {
  // Signed difference, so the schedule survives millis() wrapping at 49 days.
  if ((long)(millis() - g_next_switch_ms) < 0) return;
  // Step the deadline rather than restarting it, so the 5 s cadence does not
  // drift by the pulse and print time on every switch.
  g_next_switch_ms += SWITCH_PERIOD_MS;

  pulseRelay(!g_relay_on);
  Serial.print("relay ");
  Serial.print(g_relay_on ? "ON" : "OFF");
  Serial.print(" at ");
  Serial.print(millis());
  Serial.println(" ms");
}
