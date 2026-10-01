// LcdTwoDigit -- two-digit TN LCD, 00-99, on a shared set of segment lines.
//
// Two backplanes with ONE set of segment pins is a multiplexed panel, not a
// static one, and it cannot be driven with two levels. Every pixel sees
// Vsegment - Vbackplane; with only 0 V and VDD available, an "on" pixel and an
// "off" pixel on the other digit end up with the same RMS voltage and the
// display is uniformly grey. Multiplexing needs a third level.
//
// So this uses the standard 1/2-duty, 1/2-bias scheme, with the third level
// made by tri-stating the pin: an input with no pull-up floats to about mid
// rail. A frame is four sub-frames -- each backplane active once per polarity:
//
//   sub-frame   COM A      COM B      segment line
//   1           VDD        Hi-Z(V/2)  0 V if that pixel of A is on, else VDD
//   2           Hi-Z(V/2)  VDD        ... same, for B
//   3           0 V        Hi-Z(V/2)  inverted
//   4           Hi-Z(V/2)  0 V        inverted
//
// Sub-frames 3 and 4 are 1 and 2 with the polarity flipped, so no pixel carries
// any DC -- the thing that permanently stains this glass.
//
// Over a frame each pixel sees:
//
//   on   Vrms = V * sqrt(5)/2 / sqrt(2) ~= 0.79 * VDD   (2.6 V at 3.3 V)
//   off  Vrms = V / (2*sqrt(2))         ~= 0.35 * VDD   (1.2 V at 3.3 V)
//
// a contrast ratio of sqrt(5) ~= 2.24. That is inherently weaker than static
// drive, and it is the best two-backplane multiplexing can do.
//
// ABOUT THE MID LEVEL -- THIS PART IS NOT OPTIONAL:
//
// A tri-stated pin does NOT sit at half rail on its own. It capacitively
// divides against the seven segment lines, so its voltage moves with whatever
// is being displayed and drifts with leakage; measured on this board it reads
// as a definite logic level that shifts with the pattern. That is why two
// different digits looked wrong while "both digits, same number" -- the case
// that needs no bias at all -- looked fine.
//
// Fit a divider on EACH common: 100 kOhm to VDD and 100 kOhm to GND on PD8 and
// on PD14. Four resistors. The GPIO drives straight through the 50 kOhm they
// present when it is not tri-stated (~66 uA), and the RC against the panel's
// capacitance settles in well under one 2.5 ms sub-frame.
//
// With that in place a lit pixel sees 0.79 x 3.3 V = 2.6 V RMS, against the
// 4 V p-p (2 V RMS) this panel measured brightest at -- so it clears the bar,
// where the chip's own LCD peripheral cannot: its best duty is 1/4, worth only
// 1.9 V RMS.
// Wiring:
//
//   a  PD0      e  PF2         digit 1 common  PD8
//   b  PF3      f  PA11        digit 2 common  PD14
//   c  PF7      g  PF4
//   d  PA12
//
// Serial (115200): a number + Enter holds it | r count | b blank | t all on |
//                  s cycle a single-digit static test (see below)
//
// If digit 2 turns out to have its OWN seven segment pins rather than sharing
// these, it is two independent static displays instead -- say so and this
// becomes much simpler, and brighter.

// ---------------------------------------------------------------- wiring --
static const uint8_t SEGMENT_PIN[7] = {
    PIN_PD0,   // a  top
    PIN_PF3,   // b  top right
    PIN_PF7,   // c  bottom right
    PIN_PA12,  // d  bottom
    PIN_PF2,   // e  bottom left
    PIN_PA11,  // f  top left
    PIN_PF4,   // g  middle
};

// Index 0 and 1 are the two backplanes.
static const uint8_t COM_PIN[2] = { PIN_PD8, PIN_PD14 };

// Which backplane carries the first (left) digit. COM_PIN[0] is PA13, so out of
// the box PA13 shows the tens and PE8 the units. If the number reads backwards
// on the glass, press 'x' -- no rebuild needed.
#ifndef COM_TENS_DEFAULT
#define COM_TENS_DEFAULT 0
#endif
static uint8_t com_tens = COM_TENS_DEFAULT;

// Any other pin that reaches the panel. Held at the "off" level with the rest,
// because an undriven pin sits at 3.3 V through its reset pull-up and ghosts.
// 0xFF means "no pin" -- a C++ array cannot be empty, and a real pin used as a
// placeholder gets driven like an idle one, which quietly holds that segment off.
#define NO_PIN 0xFFu

static const uint8_t IDLE_PIN[] = {
    NO_PIN,
    // PIN_PD1, PIN_PF3, ... add the rest of the panel here
};
static const uint8_t IDLE_COUNT = (uint8_t)(sizeof(IDLE_PIN) / sizeof(IDLE_PIN[0]));

// ---------------------------------------------------------------- timing --
// Four sub-frames make a frame, so 2500 us each gives a 100 Hz frame -- the
// frequency this panel measured brightest at. Timed in microseconds because
// 100 Hz does not land on a whole number of milliseconds.
#ifndef SUBFRAME_US
#define SUBFRAME_US 2500UL
#endif

// How long each number stays up while counting.
#ifndef STEP_MS
#define STEP_MS 500UL
#endif

// -------------------------------------------------------------- patterns --
// Bit 0 = a, bit 1 = b, ... bit 6 = g.
static const uint8_t DIGITS[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};
static const uint8_t BLANK = 0x00;
static const uint8_t ALL_ON = 0x7F;

static uint8_t pattern[2] = {0, 0};   // segments lit, per backplane
static uint8_t subframe = 0;          // 0..3
static bool counting = true;
static uint8_t value = 0;             // 0..99
static uint8_t single = 0;            // 0 = multiplexed, 1/2 = static test

static bool startup_hold = true;      // showing "12" so the wiring is obvious
#define STARTUP_HOLD_MS 3000UL

static unsigned long last_subframe = 0;
static unsigned long last_subframe_start = 0;   // when setup() finished
static unsigned long last_step = 0;
static String entry;

static void setLevel(uint8_t pin, bool high) {
  digitalWrite(pin, high ? HIGH : LOW);
}

// Park a backplane at the mid level by letting go of it. pinMode(INPUT) leaves
// the pull-up off on this part, which is what makes this work at all -- with
// the pull-up on it would sit at VDD and light the other digit.
static void floatPin(uint8_t pin) {
  pinMode(pin, INPUT);
}

static void driveSubframe(void) {
  const uint8_t active = subframe & 1u;          // which backplane is driven
  const bool high = ((subframe >> 1) & 1u) == 0; // polarity of this half-frame

  if (single == 0) {
    floatPin(COM_PIN[1u - active]);              // the other one goes to V/2
  } else {
    // Static test: one digit only, the other backplane parked at the off level
    // so its pixels see nothing at all.
    pinMode(COM_PIN[1u - active], OUTPUT);
    setLevel(COM_PIN[1u - active], high);
  }

  pinMode(COM_PIN[active], OUTPUT);
  setLevel(COM_PIN[active], high);

  const uint8_t bits = pattern[active];
  for (uint8_t i = 0; i < 7; i++) {
    const bool on = (bits >> i) & 1u;
    // on  -> opposite the active backplane (full swing across the pixel)
    // off -> the same level (nothing across it)
    setLevel(SEGMENT_PIN[i], on ? !high : high);
  }
  for (uint8_t i = 0; i < IDLE_COUNT; i++) {
    if (IDLE_PIN[i] != NO_PIN) setLevel(IDLE_PIN[i], high);
  }
}

static void setValue(uint8_t v) {
  value = (uint8_t)(v % 100u);
  pattern[com_tens] = DIGITS[value / 10u];
  pattern[1u - com_tens] = DIGITS[value % 10u];
  Serial.print("showing ");
  if (value < 10) Serial.print('0');
  Serial.print(value);
  Serial.print("   (");
  Serial.print(com_tens == 0 ? "PA13" : "PE8");
  Serial.print('=');
  Serial.print(value / 10u);
  Serial.print("  ");
  Serial.print(com_tens == 0 ? "PE8" : "PA13");
  Serial.print('=');
  Serial.print(value % 10u);
  Serial.println(')');
}

void setup() {
  Serial.begin(115200);
  entry.reserve(8);

  for (uint8_t i = 0; i < 2; i++) {
    pinMode(COM_PIN[i], OUTPUT);
  }
  for (uint8_t i = 0; i < 7; i++) {
    pinMode(SEGMENT_PIN[i], OUTPUT);
  }
  for (uint8_t i = 0; i < IDLE_COUNT; i++) {
    if (IDLE_PIN[i] != NO_PIN) pinMode(IDLE_PIN[i], OUTPUT);
  }

  Serial.println();
  Serial.println("HT5023 two-digit TN LCD -- 1/2 duty, 1/2 bias, tri-state mid level");
  Serial.print("frame ");
  Serial.print(1000000UL / (4UL * SUBFRAME_US));
  Serial.println(" Hz; contrast ratio is sqrt(5), the most two backplanes allow");
  Serial.println("keys: <number> Enter hold | r count | b blank | t all on | "
                 "s single-digit static test | x swap the digits");

  // Start on 12 rather than 00: two different digits show at a glance which
  // backplane is which, and whether the number reads the right way round.
  counting = false;
  setValue(12);
  Serial.println("holding 12 for a moment -- if the glass reads 21, press x");
  last_subframe = micros();
  last_subframe_start = last_step = millis();
}

void loop() {
  const unsigned long now = millis();

  if (micros() - last_subframe >= SUBFRAME_US) {
    last_subframe += SUBFRAME_US;      // keep the four sub-frames even
    subframe = (uint8_t)((subframe + 1u) & 3u);
    driveSubframe();
  }

  if (startup_hold && (now - last_subframe_start >= STARTUP_HOLD_MS)) {
    startup_hold = false;            // the "12" has been up long enough
    counting = true;
    last_step = now;
  }

  if (counting && (now - last_step >= STEP_MS)) {
    last_step = now;
    setValue((uint8_t)(value + 1u));
  }

  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c >= '0' && c <= '9') {
      if (entry.length() < 3) entry += c;
    } else if (c == '\r' || c == '\n') {
      if (entry.length() > 0) {
        counting = false;
        setValue((uint8_t)entry.toInt());
      startup_hold = false;
        entry = "";
      }
    } else if (c == 'r' || c == 'R') {
      counting = true;
      entry = "";
      Serial.println("counting");
      startup_hold = false;
    } else if (c == 'b' || c == 'B') {
      counting = false;
      pattern[0] = pattern[1] = BLANK;
      Serial.println("blank");
      startup_hold = false;
    } else if (c == 't' || c == 'T') {
      counting = false;
      pattern[0] = pattern[1] = ALL_ON;
      Serial.println("all segments on, both digits");
      startup_hold = false;
    } else if (c == 'x' || c == 'X') {
      com_tens = (uint8_t)(1u - com_tens);
      setValue(value);
      Serial.println("digits swapped");
    } else if (c == 's' || c == 'S') {
      single = (uint8_t)((single + 1u) % 3u);
      Serial.print("drive mode: ");
      Serial.println(single == 0 ? "multiplexed (both digits)"
                                 : "static test (one digit at a time)");
    }
  }
}
