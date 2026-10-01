// jingle_bells -- plays "Jingle Bells" (verse and chorus) on the energy-meter
// PCB's buzzer and blinks its four LEDs with the notes.
//
// Open this in the Flasher (or the Arduino IDE) and press Compile & Upload.
// The song repeats forever; a line is printed on Serial (115200) at the start
// of each pass, so a silent buzzer can be told apart from a sketch that is not
// running.
//
// Wiring, from ht5023_energy_meter_v1 ("CIRCUIT CONTEXT"):
//
//   BUZZER1  PA7 -> 1k -> AO3400A gate (Q1, 100k pull-down), buzzer to +5V
//   D12      PF0 -> 1k -> LED -> GND     leftmost LED, lit by HIGH
//   D11      PF1 -> 1k -> LED -> GND                   lit by HIGH
//   D15      PC6 -> 1k -> LED -> +3V3                  lit by LOW
//   D16      PC7 -> 1k -> LED -> +3V3    rightmost LED, lit by LOW
//
// Each note lights one LED, walking left to right as the pitch rises, and all
// four go dark in the gap between notes, so every note is one blink.
//
// PF0 and PF1 are normally the metering pulse outputs (PFOUT/QFOUT). Taking
// them as GPIO here also drives the EL817 optocouplers U20/U15 on the same
// nets, so the isolated pulse outputs on the terminals blink along too.
// LED_BUILTIN is not used: it is PE3, which on this PCB switches IR_POW.
//
// BUZZER1 on the BOM (SUN-12095-5VPA7.6, LCSC C360615) is an *active* buzzer:
// its own oscillator sounds 2.7 kHz whenever PA7 is high. So by default each
// note is a plain beep, in rhythm (BUZZER_BEEP_ONLY). With a passive 12 mm
// magnetic buzzer fitted instead, set it to false to play the actual pitches;
// on the active part that only gives a rough, buzzy version of the tune.
//
// Why not tone(): PA7 has no timer output on this part (the pad is
// PA.7/INT2/SEG33), so the wave is toggled by the CPU either way. The core's
// tone() waits out each half cycle with delayMicroseconds(), and each micros()
// call behind that does a 64-bit division in software -- at Fcpu = 4.6 MHz
// that adds enough to every half cycle to pull high notes flatter than low
// ones. playNote() below instead reads the SysTick counter that millis()
// already runs and toggles on absolute deadlines, so loop overhead becomes a
// few microseconds of jitter on each edge, never a pitch error.

static const uint8_t BUZZER_PIN = PIN_PA7;

// true:  hold PA7 high for each note -- clean beeps, rhythm only, which is all
//        the fitted active buzzer can do cleanly.
// false: square wave at each note's pitch (for a passive buzzer).
static const bool BUZZER_BEEP_ONLY = true;

// Quarter notes per minute.
static const unsigned long TEMPO_BPM = 160UL;

// Silence (and LEDs off) cut from the end of every note, so the repeated
// "bells" are heard -- and seen -- as separate notes rather than one long one.
static const unsigned long NOTE_GAP_MS = 40UL;

// Pause between passes of the song.
static const unsigned long REPEAT_PAUSE_MS = 2000UL;

struct Led {
  uint8_t pin;
  bool lit_level_high;  // true: anode side on the pin; false: cathode side
};

// Left to right as fitted on the PCB.
static const Led kLeds[] = {
  {PIN_PF0, true},   // D12
  {PIN_PF1, true},   // D11
  {PIN_PC6, false},  // D15
  {PIN_PC7, false},  // D16
};
static const size_t LED_COUNT = sizeof(kLeds) / sizeof(kLeds[0]);

// SysTick counts down at Fcpu (HT5023_SYSTICK_HZ) and reloads once per
// millisecond; the core starts it before setup() for millis().
#define SYSTICK_BASE    0xE000E000u
#define SYST_RVR_OFFSET 0x14u
#define SYST_CVR_OFFSET 0x18u

// Equal temperament, A4 = 440 Hz. The tune sits an octave above where it is
// usually written: small buzzers are loudest towards their 2-4 kHz resonance
// and get weak below about 500 Hz.
static const uint16_t NOTE_G4 = 392u;
static const uint16_t NOTE_A4 = 440u;
static const uint16_t NOTE_B4 = 494u;
static const uint16_t NOTE_C5 = 523u;
static const uint16_t NOTE_D5 = 587u;
static const uint16_t NOTE_E5 = 659u;
static const uint16_t NOTE_F5 = 698u;
static const uint16_t NOTE_G5 = 784u;
static const uint16_t NOTE_A5 = 880u;

struct Note {
  uint16_t freq_hz;  // 0 = rest
  uint8_t eighths;   // length in eighth notes: 2 = quarter, 8 = whole
};

// "Jingle Bells", James Lord Pierpont (1857) -- public domain. One bar per
// line; each bar adds up to 8 eighths.
static const Note kVerse[] = {
  // Dash-ing through the snow, in a one-horse o-pen sleigh,
  {NOTE_G4, 2}, {NOTE_E5, 2}, {NOTE_D5, 2}, {NOTE_C5, 2},
  {NOTE_G4, 6}, {NOTE_G4, 1}, {NOTE_G4, 1},
  {NOTE_G4, 2}, {NOTE_E5, 2}, {NOTE_D5, 2}, {NOTE_C5, 2},
  {NOTE_A4, 8},
  // o'er the fields we go, laugh-ing all the way.
  {NOTE_A4, 2}, {NOTE_F5, 2}, {NOTE_E5, 2}, {NOTE_D5, 2},
  {NOTE_B4, 8},
  {NOTE_G5, 2}, {NOTE_G5, 2}, {NOTE_F5, 2}, {NOTE_D5, 2},
  {NOTE_E5, 8},
  // Bells on bob-tail ring, mak-ing spir-its bright; what
  {NOTE_G4, 2}, {NOTE_E5, 2}, {NOTE_D5, 2}, {NOTE_C5, 2},
  {NOTE_G4, 8},
  {NOTE_G4, 2}, {NOTE_E5, 2}, {NOTE_D5, 2}, {NOTE_C5, 2},
  {NOTE_A4, 6}, {NOTE_A4, 2},
  // fun it is to ride and sing a sleigh-ing song to-night!
  {NOTE_A4, 2}, {NOTE_F5, 2}, {NOTE_E5, 2}, {NOTE_D5, 2},
  {NOTE_G5, 2}, {NOTE_G5, 2}, {NOTE_G5, 2}, {NOTE_G5, 2},
  {NOTE_A5, 2}, {NOTE_G5, 2}, {NOTE_F5, 2}, {NOTE_D5, 2},
  {NOTE_C5, 8},
};

static const Note kChorus[] = {
  // Jin-gle bells, jin-gle bells, jin-gle all the way!
  {NOTE_E5, 2}, {NOTE_E5, 2}, {NOTE_E5, 4},
  {NOTE_E5, 2}, {NOTE_E5, 2}, {NOTE_E5, 4},
  {NOTE_E5, 2}, {NOTE_G5, 2}, {NOTE_C5, 3}, {NOTE_D5, 1},
  {NOTE_E5, 8},
  // Oh what fun it is to ride in a one-horse o-pen sleigh, hey!
  {NOTE_F5, 2}, {NOTE_F5, 2}, {NOTE_F5, 3}, {NOTE_F5, 1},
  {NOTE_F5, 2}, {NOTE_E5, 2}, {NOTE_E5, 2}, {NOTE_E5, 1}, {NOTE_E5, 1},
  {NOTE_E5, 2}, {NOTE_D5, 2}, {NOTE_D5, 2}, {NOTE_E5, 2},
  {NOTE_D5, 4}, {NOTE_G5, 4},
  // Jin-gle bells, jin-gle bells, jin-gle all the way!
  {NOTE_E5, 2}, {NOTE_E5, 2}, {NOTE_E5, 4},
  {NOTE_E5, 2}, {NOTE_E5, 2}, {NOTE_E5, 4},
  {NOTE_E5, 2}, {NOTE_G5, 2}, {NOTE_C5, 3}, {NOTE_D5, 1},
  {NOTE_E5, 8},
  // Oh what fun it is to ride in a one-horse o-pen sleigh!
  {NOTE_F5, 2}, {NOTE_F5, 2}, {NOTE_F5, 3}, {NOTE_F5, 1},
  {NOTE_F5, 2}, {NOTE_E5, 2}, {NOTE_E5, 2}, {NOTE_E5, 1}, {NOTE_E5, 1},
  {NOTE_G5, 2}, {NOTE_G5, 2}, {NOTE_F5, 2}, {NOTE_D5, 2},
  {NOTE_C5, 8},
};

// Sounds freq_hz on the buzzer for ms milliseconds, or stays silent that long
// if freq_hz is 0. Blocks until done and leaves the pin low.
static void playNote(uint16_t freq_hz, unsigned long ms) {
  if (freq_hz == 0u) {
    digitalWrite(BUZZER_PIN, LOW);
    delay(ms);
    return;
  }
  if (BUZZER_BEEP_ONLY) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(ms);
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }

  volatile uint32_t* const cvr = ht5023_reg32(SYSTICK_BASE, SYST_CVR_OFFSET);
  const uint32_t reload = *ht5023_reg32(SYSTICK_BASE, SYST_RVR_OFFSET) + 1u;
  const uint32_t half_period = HT5023_SYSTICK_HZ / (2UL * freq_hz);
  const uint32_t total = (HT5023_SYSTICK_HZ / 1000UL) * ms;

  uint32_t prev = *cvr;
  uint32_t elapsed = 0u;
  uint32_t next_edge = half_period;
  bool high = true;
  digitalWrite(BUZZER_PIN, HIGH);

  while (elapsed < total) {
    // SysTick counts down; a reading above the previous one means it passed
    // zero and reloaded in between.
    const uint32_t now = *cvr;
    elapsed += (now <= prev) ? (prev - now) : (prev + reload - now);
    prev = now;

    if (elapsed >= next_edge) {
      high = !high;
      digitalWrite(BUZZER_PIN, high ? HIGH : LOW);
      next_edge += half_period;
    }
  }

  digitalWrite(BUZZER_PIN, LOW);
}

static void setLed(size_t index, bool on) {
  const Led& led = kLeds[index];
  digitalWrite(led.pin, (on == led.lit_level_high) ? HIGH : LOW);
}

static void allLedsOff() {
  for (size_t i = 0; i < LED_COUNT; i++) setLed(i, false);
}

// The LED a note lights, moving right as the melody climbs. The bands are cut
// around the chorus (C5..G5, mostly E5) so it uses all four LEDs, not one.
static size_t ledForNote(uint16_t freq_hz) {
  if (freq_hz <= NOTE_C5) return 0;  // G4 A4 B4 C5
  if (freq_hz <= NOTE_D5) return 1;  // D5
  if (freq_hz <= NOTE_E5) return 2;  // E5
  return 3;                          // F5 G5 A5
}

static void playTune(const Note* notes, size_t count) {
  const unsigned long eighth_ms = 30000UL / TEMPO_BPM;

  for (size_t i = 0; i < count; i++) {
    const unsigned long ms = notes[i].eighths * eighth_ms;
    // LEDs are switched outside playNote(), so they cost no pitch accuracy.
    if (notes[i].freq_hz != 0u) setLed(ledForNote(notes[i].freq_hz), true);
    playNote(notes[i].freq_hz, ms - NOTE_GAP_MS);
    allLedsOff();
    delay(NOTE_GAP_MS);
  }
}

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // pinMode() also takes PF0/PF1 off their pulse-output function.
  for (size_t i = 0; i < LED_COUNT; i++) pinMode(kLeds[i].pin, OUTPUT);
  allLedsOff();

  Serial.begin(115200);
  Serial.println();
  Serial.println("HT5023 jingle_bells: buzzer on PA7, LEDs D12 D11 D15 D16");
}

void loop() {
  Serial.println("Playing Jingle Bells");
  playTune(kVerse, sizeof(kVerse) / sizeof(kVerse[0]));
  playTune(kChorus, sizeof(kChorus) / sizeof(kChorus[0]));
  delay(REPEAT_PAUSE_MS);
}
