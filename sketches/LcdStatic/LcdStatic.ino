// LcdStatic -- maximum-brightness drive for the two-digit TN panel.
//
// Measured on this panel: a 50% square wave at the full 3 V rail is as bright
// as it gets. That is static drive, and no multiplexed scheme can match it --
// the arithmetic is unforgiving:
//
//   drive                                on-pixel RMS at 3.3 V   different digits?
//   static, 50% square wave              3.30 V                  no
//   GPIO 1/2 duty, 1/2 bias (resistors)  2.60 V                  yes
//   hardware controller, 1/4 duty        1.90 V                  yes
//   hardware controller, 1/8 duty        1.50 V                  yes
//
// The chip's LCD peripheral cannot do better than 1/4 duty -- its duty options
// are 1/4, 1/6 and 1/8 -- so it is the DIMMEST way to drive this panel, however
// convenient it is. This sketch goes back to GPIO and gives the glass the whole
// rail.
//
// The catch is the one that has been there all along: both digits share the
// segment lines, so at full brightness they can only ever show the SAME thing.
// Two different digits needs bias, and bias means less than the full rail.
//
// Wiring (the rewired pin-out, driven as plain GPIO here):
//
//   a  PC8     e  PF4        digit 1 common  PD8
//   b  PC13    f  PA0  *     digit 2 common  PD14
//   c  PF3     g  PE3
//   d  PB11
//
//   * segment f goes through a 10 kOhm series resistor.
//
// The previous pin map is kept in docs/lcd_pinout.md.
//
// Serial (115200):
//   0..9  hold that digit    d  cycle: both -> digit 1 -> digit 2
//   r     count 0-9          b  blank            t  all segments

// ---------------------------------------------------------------- wiring --
static const uint8_t SEGMENT_PIN[7] = {
    PIN_PC8,   // a  top
    PIN_PC13,  // b  top right
    PIN_PF3,   // c  bottom right
    PIN_PB11,  // d  bottom
    PIN_PF4,   // e  bottom left
    PIN_PA0,   // f  top left   -- through a 10 kOhm series resistor
    PIN_PE3,   // g  middle
};
static const uint8_t COM_PIN[2] = { PIN_PD8, PIN_PD14 };

// 1 = digit 1 only, 2 = digit 2 only, 3 = both showing the same number.
#ifndef ACTIVE_DIGITS
#define ACTIVE_DIGITS 3
#endif

// 100 Hz square wave (5 ms per half), the frequency this panel was measured at.
//
// Worth being clear about the amplitude: an ON pixel sees the segment and the
// common in ANTIPHASE, so it swings +3.3 V to -3.3 V -- 6.6 V peak to peak,
// 3.3 V RMS. The 4 V p-p that measured brightest is 2 V RMS, so static drive
// clears the requirement with room to spare.
#ifndef HALF_PERIOD_MS
#define HALF_PERIOD_MS 5UL
#endif
#ifndef DIGIT_MS
#define DIGIT_MS 1000UL
#endif

// Bit 0 = a, bit 1 = b, ... bit 6 = g.
static const uint8_t DIGITS[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};
static const uint8_t BLANK = 0x00;
static const uint8_t ALL_ON = 0x7F;

static uint8_t active = ACTIVE_DIGITS;   // bit0 = digit 1, bit1 = digit 2
static uint8_t pattern = 0;
static bool phase = false;
static bool counting = true;
static uint8_t digit = 0;
static unsigned long last_tick = 0;
static unsigned long last_digit = 0;

// The bootloader leaves the low-frequency RC running (the LCD peripheral needs
// it), and the watchdog runs off that same clock. So the watchdog is live from
// reset and every sketch on this board has to feed it -- miss this and the part
// resets in a loop, which looks exactly like a sketch that never started.
#define WDT_CLR      0x40010004u
#define WDT_FEED_KEY 0xAAu

static void wdtFeed(void) {
  *(volatile uint8_t*)WDT_CLR = WDT_FEED_KEY;
}

// The LCD peripheral may still own these pins from another sketch, and a
// powered-up LCD block would fight the GPIO drive. Hand everything back.
static void releaseLcdBlock(void) {
  *(volatile uint16_t*)0x4000D00Cu = 0u;          // LCDOUT: output off
  *(volatile uint16_t*)0x4000F000u = 0xA55Au;     // WPREG unlock
  *(volatile uint32_t*)0x4000F02Cu &= ~(1u << 1); // CLKCTRL0.LCD_EN = 0
}

static void driveHalfCycle(void) {
  phase = !phase;

  // Commons in use carry the square wave. A common NOT in use is tri-stated
  // rather than driven or left to its reset pull-up: floating it puts about
  // half the rail across that digit, where the pull-up would put all of it.
  for (uint8_t i = 0; i < 2; i++) {
    if (active & (1u << i)) {
      pinMode(COM_PIN[i], OUTPUT);
      digitalWrite(COM_PIN[i], phase ? HIGH : LOW);
    } else {
      pinMode(COM_PIN[i], INPUT);
    }
  }

  for (uint8_t i = 0; i < 7; i++) {
    const bool on = (pattern >> i) & 1u;
    // on  -> opposite the common: the full rail across the pixel
    // off -> the same level: nothing across it
    digitalWrite(SEGMENT_PIN[i], (on ? !phase : phase) ? HIGH : LOW);
  }
}

static void showDigit(uint8_t d) {
  digit = (uint8_t)(d % 10u);
  pattern = DIGITS[digit];
  Serial.print("digit ");
  Serial.println(digit);
}

void setup() {
  Serial.begin(115200);
  wdtFeed();
  releaseLcdBlock();

  for (uint8_t i = 0; i < 7; i++) pinMode(SEGMENT_PIN[i], OUTPUT);
  for (uint8_t i = 0; i < 2; i++) pinMode(COM_PIN[i], OUTPUT);

  Serial.println();
  Serial.println("HT5023 static LCD drive -- the full rail across every lit pixel");
  Serial.print("~");
  Serial.print(500UL / HALF_PERIOD_MS);
  Serial.println(" Hz square wave, 50% duty, no bias -- 6.6 V p-p across a lit "
                 "pixel (3.3 V RMS)");
  Serial.println("both digits show the same number: at full brightness that is "
                 "the only thing shared segment lines allow");
  Serial.println("keys: 0-9 hold | r count | d which digits | b blank | t all");

  showDigit(0);
  last_tick = last_digit = millis();
}

void loop() {
  const unsigned long now = millis();

  wdtFeed();

  if (now - last_tick >= HALF_PERIOD_MS) {
    last_tick += HALF_PERIOD_MS;
    driveHalfCycle();
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
      counting = true; last_digit = now; Serial.println("counting");
    } else if (c == 'b' || c == 'B') {
      counting = false; pattern = BLANK; Serial.println("blank");
    } else if (c == 't' || c == 'T') {
      counting = false; pattern = ALL_ON; Serial.println("all segments on");
    } else if (c == 'd' || c == 'D') {
      // 3 = both, 1 = digit 1, 2 = digit 2
      active = (uint8_t)(active == 3u ? 1u : (active == 1u ? 2u : 3u));
      Serial.print("driving ");
      Serial.println(active == 3u ? "both digits (same number)"
                                  : (active == 1u ? "digit 1 only" : "digit 2 only"));
    }
  }
}
