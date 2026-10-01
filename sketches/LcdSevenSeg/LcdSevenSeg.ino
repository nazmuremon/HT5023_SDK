// LcdSevenSeg -- one digit of a two-digit TN LCD, 0-9.
//
// The panel shares its seven segment lines between both digits and picks one
// with a digit-select (backplane) pin: PA13 for the first digit, PE8 for the
// second. That sharing is what makes the unused digit awkward -- a segment line
// carries the same voltage to both digits, so "blank the one I am not using" is
// always a question about the OTHER backplane, never about the segments.
//
// A TN panel must also never see DC: a steady voltage drags ions to one side of
// the cell and permanently stains the segment. So the backplane runs a square
// wave and segments switch by PHASE, not by level -- in phase means 0 V across
// the pixel (off), inverted means the full swing (on).
//
// Never leave the unused backplane alone. Out of reset every pin here is an
// input with its pull-up ON, so it sits at 3.3 V DC while the segments swing
// underneath: the idle digit then sees ~0.71 x VDD -- clearly visible -- and the
// DC works on the glass the whole time.
//
// Two ways to do better, and which wins depends on your panel's threshold
// voltage, so both are here. Press 'm' and look at the glass:
//
//   mode      active digit           idle digit    how
//   STATIC    1.00 x VDD on          0.50 x VDD    idle backplane tri-stated,
//             0.00      off                        floating to mid rail
//   MUX       0.79 x VDD on          0.35 x VDD    1/2 duty, 1/2 bias: the idle
//             0.35      off                        digit driven, but blank
//
// STATIC gives the digit you care about perfect contrast, and is the one to use
// if the idle digit stays invisible at half rail. MUX blanks the idle digit
// harder, at the cost of contrast on the active one. Nothing gives you both --
// that is the price of sharing the segment lines.
//
// Wiring:
//
//   a  PC8      e  PF4         digit 1 select  PA13
//   b  PC13     f  PA0  *      digit 2 select  PE8
//   c  PF3      g  PE3
//   d  PB11
//
//   * segment f goes through a 10 kOhm series resistor.
//
// NOTE: the segment pins match the other LCD sketches, but the digit-select
// pins here are still PA13/PE8 -- the other sketches use PD8/PD14. Wired for
// those, this sketch selects nothing. See docs/lcd_pinout.md.
//
// Serial (115200):
//   0..9  hold that digit      m  switch STATIC <-> MUX
//   r     count 0-9            d  cycle PA13 -> PE8 -> both digits
//   b     blank                t  light every segment (a wiring test)

// ---------------------------------------------------------------- wiring --
// Order is a,b,c,d,e,f,g and must match the bit order in DIGITS below.
static const uint8_t SEGMENT_PIN[7] = {
    PIN_PC8,   // a  top
    PIN_PC13,  // b  top right
    PIN_PF3,   // c  bottom right
    PIN_PB11,  // d  bottom
    PIN_PF4,   // e  bottom left
    PIN_PA0,   // f  top left   -- through a 10 kOhm series resistor
    PIN_PE3,   // g  middle
};

// The digit-select pins. Index 0 is the first digit.
static const uint8_t COM_PIN[2] = { PIN_PA13, PIN_PE8 };

// Which digit to drive: 0 = PA13, 1 = PE8, 2 = BOTH showing the same number.
//
// BOTH is worth knowing about. The segment lines carry the same voltage to both
// digits, so as long as the two are meant to show the SAME thing, both
// backplanes can carry the same square wave: every pixel then sees the full
// swing and both digits look exactly as good as one on its own. Bias and
// multiplexing are only needed when the digits must differ.
//
// 'd' cycles this at runtime; -DACTIVE_DIGIT=n picks the power-on value, which
// is what to use when no serial adapter is plugged in to press a key with.
#define DIGIT_BOTH 2u
#ifndef ACTIVE_DIGIT
#define ACTIVE_DIGIT 0
#endif

// What to show at power-on. -DSTART_COUNTING=0 holds START_DIGIT instead of
// counting, again for when there is no keyboard on the other end.
#ifndef START_DIGIT
#define START_DIGIT 0
#endif
#ifndef START_COUNTING
#define START_COUNTING 1
#endif

// 0xFF means "no pin": a C++ array cannot be empty, and a real pin used as a
// placeholder would quietly hold that segment off.
#define NO_PIN 0xFFu

// Anything else that reaches the panel -- decimal points, colons. Driven to the
// off level, for the same reason the idle backplane is never left floating.
static const uint8_t IDLE_PIN[] = {
    NO_PIN,
    // PIN_PD1, PIN_PF3, ... add the rest of the panel here
};
static const uint8_t IDLE_COUNT = (uint8_t)(sizeof(IDLE_PIN) / sizeof(IDLE_PIN[0]));

// ---------------------------------------------------------------- timing --
// STATIC toggles every HALF_PERIOD_MS  -> ~62 Hz square wave.
// MUX runs four sub-frames of SUBFRAME_MS -> ~62 Hz frame rate.
#ifndef HALF_PERIOD_MS
#define HALF_PERIOD_MS 8UL
#endif
#ifndef SUBFRAME_MS
#define SUBFRAME_MS 4UL
#endif

// How long each digit stays up while counting.
#ifndef DIGIT_MS
#define DIGIT_MS 1000UL
#endif

// -------------------------------------------------------------- patterns --
// Bit 0 = a, bit 1 = b, ... bit 6 = g.
static const uint8_t DIGITS[10] = {
    0x3F,  // 0  a b c d e f
    0x06,  // 1  b c
    0x5B,  // 2  a b d e g
    0x4F,  // 3  a b c d g
    0x66,  // 4  b c f g
    0x6D,  // 5  a c d f g
    0x7D,  // 6  a c d e f g
    0x07,  // 7  a b c
    0x7F,  // 8  all
    0x6F,  // 9  a b c d f g
};
static const uint8_t BLANK = 0x00;
static const uint8_t ALL_ON = 0x7F;

enum Mode { MODE_STATIC = 0, MODE_MUX = 1 };

// Which mode to come up in. Set -DSTART_MODE=MODE_MUX if MUX turns out to suit
// your panel better, and it will be the default from power-on.
#ifndef START_MODE
#define START_MODE MODE_STATIC
#endif

static uint8_t mode = START_MODE;
static uint8_t active = ACTIVE_DIGIT;  // which backplane carries the digit
static uint8_t pattern = 0;            // segments lit on the active digit
static uint8_t subframe = 0;           // 0..1 in STATIC, 0..3 in MUX
static bool counting = (START_COUNTING != 0);
static uint8_t digit = 0;

static unsigned long last_tick = 0;
static unsigned long last_digit = 0;

static void setLevel(uint8_t pin, bool high) {
  digitalWrite(pin, high ? HIGH : LOW);
}

// Let go of a backplane so it floats to about mid rail. pinMode(INPUT) leaves
// the pull-up off on this part, which is the whole trick -- with the pull-up on
// it would sit at VDD and light the idle digit.
static void floatPin(uint8_t pin) {
  pinMode(pin, INPUT);
}

// Put the segment lines where `bits` says, measured against a backplane sitting
// at `com_high`: an on segment goes to the opposite rail (full swing across the
// pixel), an off one to the same rail (nothing across it).
static void driveSegments(uint8_t bits, bool com_high) {
  for (uint8_t i = 0; i < 7; i++) {
    const bool on = (bits >> i) & 1u;
    setLevel(SEGMENT_PIN[i], on ? !com_high : com_high);
  }
  for (uint8_t i = 0; i < IDLE_COUNT; i++) {
    if (IDLE_PIN[i] != NO_PIN) setLevel(IDLE_PIN[i], com_high);
  }
}

static void tick(void) {
  if (mode == MODE_STATIC) {
    // Square wave on the chosen backplane; the other floats to mid rail, the
    // quietest an unused digit can be made without bias.
    subframe = (uint8_t)(subframe ^ 1u);
    const bool high = (subframe == 0);

    if (active == DIGIT_BOTH) {
      // Same waveform on both backplanes: both digits show the same number at
      // full contrast, and neither is left floating to ghost.
      for (uint8_t i = 0; i < 2; i++) {
        pinMode(COM_PIN[i], OUTPUT);
        setLevel(COM_PIN[i], high);
      }
    } else {
      floatPin(COM_PIN[1u - active]);
      pinMode(COM_PIN[active], OUTPUT);
      setLevel(COM_PIN[active], high);
    }
    driveSegments(pattern, high);
    return;
  }

  // MUX: four sub-frames -- each backplane driven once per polarity, the other
  // at mid rail. The idle digit is driven with a blank pattern instead of being
  // left to float, and that is what pulls it down to 0.35 x VDD.
  subframe = (uint8_t)((subframe + 1u) & 3u);
  const uint8_t driven = subframe & 1u;
  const bool high = ((subframe >> 1) & 1u) == 0;

  floatPin(COM_PIN[1u - driven]);
  pinMode(COM_PIN[driven], OUTPUT);
  setLevel(COM_PIN[driven], high);
  driveSegments((active == DIGIT_BOTH || driven == active) ? pattern : BLANK, high);
}

static unsigned long tickInterval(void) {
  return (mode == MODE_STATIC) ? HALF_PERIOD_MS : SUBFRAME_MS;
}

static void showDigit(uint8_t d) {
  digit = (uint8_t)(d % 10u);
  pattern = DIGITS[digit];
  Serial.print("digit ");
  Serial.print(digit);
  Serial.print("  on ");
  Serial.println(active == 0 ? "PA13" : (active == 1 ? "PE8" : "both (PA13+PE8)"));
}

void setup() {
  Serial.begin(115200);

  for (uint8_t i = 0; i < 2; i++) pinMode(COM_PIN[i], OUTPUT);
  for (uint8_t i = 0; i < 7; i++) pinMode(SEGMENT_PIN[i], OUTPUT);
  for (uint8_t i = 0; i < IDLE_COUNT; i++) {
    if (IDLE_PIN[i] != NO_PIN) pinMode(IDLE_PIN[i], OUTPUT);
  }

  Serial.println();
  Serial.println("HT5023 seven-segment TN LCD -- one digit of a shared-segment panel");
  Serial.println("mode STATIC: best contrast here, idle digit at 0.50 x VDD");
  Serial.println("press m for MUX: idle digit at 0.35 x VDD, this one at 0.79");
  Serial.println("keys: 0-9 hold | r count | b blank | t all on | d other digit "
                 "| m mode");

  showDigit(START_DIGIT);
  last_tick = last_digit = millis();
}

void loop() {
  const unsigned long now = millis();

  if (now - last_tick >= tickInterval()) {
    last_tick += tickInterval();     // stay on the grid instead of drifting
    tick();
  }

  if (counting && (now - last_digit >= DIGIT_MS)) {
    last_digit = now;
    showDigit((uint8_t)(digit + 1u));
  }

  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c >= '0' && c <= '9') {
      counting = false;
      showDigit((uint8_t)(c - '0'));
    } else if (c == 'r' || c == 'R') {
      counting = true;
      last_digit = now;
      Serial.println("counting");
    } else if (c == 'b' || c == 'B') {
      counting = false;
      pattern = BLANK;
      Serial.println("blank");
    } else if (c == 't' || c == 'T') {
      counting = false;
      pattern = ALL_ON;
      Serial.println("all segments on");
    } else if (c == 'd' || c == 'D') {
      active = (uint8_t)((active + 1u) % 3u);   // PA13 -> PE8 -> both
      Serial.print("driving ");
      Serial.println(active == 0 ? "PA13" : (active == 1 ? "PE8"
                                                         : "both, same number"));
    } else if (c == 'm' || c == 'M') {
      mode = (uint8_t)(mode == MODE_STATIC ? MODE_MUX : MODE_STATIC);
      subframe = 0;
      last_tick = now;
      Serial.println(mode == MODE_STATIC
                         ? "mode STATIC -- idle digit floats at 0.50 x VDD"
                         : "mode MUX -- idle digit driven blank at 0.35 x VDD");
    }
  }
}
