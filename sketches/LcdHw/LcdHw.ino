// LcdHw -- two-digit TN LCD, 00-99, driven by the HT5023's own LCD controller.
//
// No bit-banging and no external resistors: the peripheral generates the bias
// levels itself, so two DIFFERENT digits finally work. All the sketch does is
// write a segment map into the display buffer; hardware produces the waveform.
//
// Wiring (LCD peripheral function, not GPIO):
//
//   segment  pin    function        segment  pin    function
//   a        PC8    NONE  <-- !     e        PF4    SEG39
//   b        PC13   NONE  <-- !     f        PA0    SEG28  (10k series R)
//   c        PF3    SEG38           g        PE3    SEG53  (AFCFG=1)
//   d        PB11   SEG11
//
//   ! PC8 and PC13 have no LCD SEG function -- port C has none at all -- so
//     this sketch cannot light segments a and b with the current wiring. The
//     GPIO sketches can. See docs/lcd_pinout.md.
//
//   digit 1 common  PD8   COM0
//   digit 2 common  PD14  COM6
//
// TWO THINGS THIS PART MAKES YOU DO, both easy to miss:
//
// 1. THE LCD IS CLOCKED FROM THE 32 kHz LOW-FREQUENCY RC, AND THIS CORE'S
//    SystemInit() SHUTS IT DOWN (it is also the watchdog's clock). Worse, the
//    shutdown is a latch: writing LRC_CTRL back to 1 does NOT restart the
//    oscillator. So this sketch must be built with the shutdown suppressed:
//
//      arduino-cli compile -b ht5023:ht5023:ht5023:layout=bootloader //        --build-property "build.extra_flags=-DHT5023_ARDUINO_VARIANT_BLINKY //                          -DHT5023_DISABLE_WDT_BY_DEFAULT=0" sketches/LcdHw
//
//    Build it without that and every register reads back perfectly while the
//    glass stays blank. setup() says so on the serial log rather than leaving
//    you to guess. The watchdog runs off the same clock, so the loop feeds it.
//
// 2. The LCD block is disabled at reset (CLKCTRL0.LCD_EN = 0) and that register
//    is write-protected. Miss it and everything looks right and nothing lights.
//
// 3. COM6 forces 1/8 duty. The peripheral only offers 1/4 (COM0-3), 1/6
//    (COM0-5) and 1/8 (COM0-7) duty, so a common on COM6 drags the whole panel
//    to eight time slots for the two it actually uses. Contrast at 1/8 duty is
//    about 1.45:1 against 1.73:1 at 1/4 -- move digit 2's common from PD14 to
//    PD9 (COM1) and set DIGIT2_COM to 1 below, and it gets noticeably better.
//
// Serial (115200):
//   <number> Enter  hold it      + / -  contrast up / down
//   r  count 00-99               x      swap which digit is the tens
//   b  blank                     t      all segments on

#include <stdint.h>

// --------------------------------------------------------------- registers --
#define REG16(a) (*(volatile uint16_t*)(a))
#define REG8(a)  (*(volatile uint8_t*)(a))

#define LCD_BASE     0x4000D000u
#define LCD_CLK      (LCD_BASE + 0x00u)   // BIASCTL | DUTY[1:0] | LCLK[2:0]
#define LCD_CON      (LCD_BASE + 0x04u)   // FCSET | FC | LC | VRSEL[3:0]
#define LCD_OUT      (LCD_BASE + 0x0Cu)   // OUTEN
// One 32-bit slot per SEG, holding that segment's COM bits in the low byte.
//
// The datasheet gets this wrong twice: it lists the buffer as one BYTE per SEG
// ("0x10+i x 1") and stops at i=42. On silicon the slots are WORD spaced and
// run to SEG53 -- verified by writing 0x55 to 0x4000D050 and 0xAA to
// 0x4000D0D8 and reading both back, while 0x4000D0E8 (SEG54) refuses to store.
// Byte writes are worse than useless: they lose their lane and land in the low
// byte of the containing word, which is why segment d (SEG35) first showed up
// in SEG32 and b (SEG38) in SEG36.
#define LCD_BUF(seg) (LCD_BASE + 0x10u + ((uint32_t)(seg) * 4u))
#define LCD_SEG_MAX 53u

// The low-frequency RC gate, behind a write sequence so a stray store cannot
// change it.
#define CTRLBYFLASH  0x4000C100u
#define LRCCOMMAND   0x4000C104u
#define LRC_CTRL_BIT (1u << 1)

// The LCD block is powered down out of reset: CLKCTRL0.LCD_EN is 0. Every LCD
// register writes and reads back perfectly with the block disabled, and the
// panel stays completely blank -- this one bit was the whole difference.
// CLKCTRL0 is write-protected, so WPREG has to be unlocked first.
#define CMU_BASE     0x4000F000u
#define CMU_WPREG    (CMU_BASE + 0x00u)
#define CMU_CLKCTRL0 (CMU_BASE + 0x2Cu)
#define CMU_WP_KEY   0xA55Au
#define LCD_EN_BIT   (1u << 1)

#define WDT_CLR      0x40010004u
#define WDT_CNT      0x40010008u
#define WDT_FEED_KEY 0xAAu

// GPIO, for handing the pins over to the LCD peripheral.
#define GPIOA_BASE 0x40011000u
#define GPIOB_BASE 0x40011100u
#define GPIOC_BASE 0x40011200u
#define GPIOD_BASE 0x40011300u
#define GPIOE_BASE 0x40011400u
// Port F is at 0x600, not 0x500 -- the ports are not evenly spaced past E.
#define GPIOF_BASE 0x40011600u
#define IOCFG 0x00u
#define AFCFG 0x04u

// ----------------------------------------------------------------- wiring --
struct LcdPin {
  uint32_t port;   // GPIO base
  uint8_t bit;     // pin within the port
  uint8_t af;      // 0 = first multiplex function, 1 = second
};

// A segment with no SEG function on its pad. The peripheral has no way to
// reach it, so it is skipped rather than pointed at the wrong slot.
#define NO_SEG 0xFFu

// a, b, c, d, e, f, g -- order must match the bit order in DIGITS.
//
// Segments a and b sit on PC8 and PC13, and PORT C CARRIES NO SEG FUNCTION
// ANYWHERE ON THIS PART -- PC.8 is TOUT1/INT7, PC.13 is SCL, and that is all
// they are. This sketch drives the panel through the LCD peripheral, so those
// two segments CANNOT light here however the registers are set. The GPIO
// sketches (LcdTwoDigit, LcdStatic) bit-bang the pins as plain outputs and are
// unaffected -- use one of those with this wiring, or move a and b onto pads
// that have a SEG function. docs/lcd_pinout.md lists which pads those are.
static const LcdPin SEG_PIN[7] = {
    {0u,          0u, 0u},  // a  PC8  -- no SEG on port C
    {0u,          0u, 0u},  // b  PC13 -- no SEG on port C
    {GPIOF_BASE,  3u, 0u},  // c  PF3  SEG38
    {GPIOB_BASE, 11u, 0u},  // d  PB11 SEG11
    {GPIOF_BASE,  4u, 0u},  // e  PF4  SEG39
    {GPIOA_BASE,  0u, 0u},  // f  PA0  SEG28 -- via a 10 kOhm series resistor
    {GPIOE_BASE,  3u, 1u},  // g  PE3  SEG53 -- second function; first is CLKOUT
};
static const uint8_t SEG_NUM[7] = { NO_SEG, NO_SEG, 38u, 11u, 39u, 28u, 53u };

#if !defined(HT5023_LCDHW_SUPPRESS_PIN_WARNING)
#warning "LcdHw: segments a (PC8) and b (PC13) have no LCD SEG function and will stay dark. See docs/lcd_pinout.md."
#endif

// The commons. PD8 is COM0; PD14 is COM6 (its first function -- SEG26 is the
// second), and using COM6 is what forces 1/8 duty.
static const LcdPin COM_PIN[2] = {
    {GPIOD_BASE, 8u, 0u},    // digit 1  COM0
    {GPIOD_BASE, 14u, 0u},   // digit 2  COM6
};
// The bias rails VLCD0..VLCD3 come out on PB2..PB5, as those pins' SECOND
// function. Leave them as GPIO and the internal divider has nowhere to develop
// its voltages, so the panel gets no drive at all -- registers all read back
// perfectly and the glass stays blank.
//
// PB13 and PB15 are SWDIO and SWCLK, so these bits are OR-ed in, never assigned.
static const LcdPin VLCD_PIN[4] = {
    {GPIOB_BASE, 2u, 1u},   // VLCD0
    {GPIOB_BASE, 3u, 1u},   // VLCD1
    {GPIOB_BASE, 4u, 1u},   // VLCD2
    {GPIOB_BASE, 5u, 1u},   // VLCD3
};

#define DIGIT1_COM 0u
#ifndef DIGIT2_COM
#define DIGIT2_COM 6u        // set to 1 after moving that wire to PD9
#endif

// ------------------------------------------------------------------ setup --
// 1/8 duty because a common sits on COM6. The datasheet asks for 1/4 bias at
// 1/6 and 1/8 duty, and LCLK=100 puts the frame rate at 64 Hz there.
#if DIGIT2_COM > 5
#define LCD_DUTY_BITS (0x2u << 3)   // 1/8 duty, COM0..COM7
#define LCD_BIAS_BIT  0u            // 1/4 bias
#define LCD_LCLK_BITS 0x4u          // frame 64 Hz at 1/8 duty
#elif DIGIT2_COM > 3
#define LCD_DUTY_BITS (0x1u << 3)   // 1/6 duty, COM0..COM5
#define LCD_BIAS_BIT  0u
#define LCD_LCLK_BITS 0x0u          // frame 42.7 Hz -- low, but the only option
#else
#define LCD_DUTY_BITS (0x0u << 3)   // 1/4 duty, COM0..COM3
#define LCD_BIAS_BIT  (1u << 7)     // 1/3 bias
#define LCD_LCLK_BITS 0x0u          // frame 64 Hz at 1/4 duty
#endif

// VRSEL[3:0] sets the drive as a percentage of VLCD, and LOWER means darker:
// 0 gives 97.4% (1/3 bias) or 99.1% (1/4 bias), 15 only 46%. Start at maximum --
// 1/8 duty needs all the help it can get -- and trim with '+' / '-'.
#ifndef LCD_CONTRAST
#define LCD_CONTRAST 0u
#endif

// -------------------------------------------------------------- patterns --
// Bit 0 = a, bit 1 = b, ... bit 6 = g.
static const uint8_t DIGITS[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};
static const uint8_t BLANK = 0x00;
static const uint8_t ALL_ON = 0x7F;

static uint8_t value = 0;
static uint8_t contrast = LCD_CONTRAST;
static bool counting = true;
static bool tens_first = true;     // digit 1 (COM0) carries the tens
static unsigned long last_step = 0;
static String entry;

// Hand a pin to the LCD peripheral: IOCFG selects peripheral over GPIO, AFCFG
// picks which of the pin's two functions.
static void claimPin(const LcdPin& p) {
  const uint16_t mask = (uint16_t)(1u << p.bit);
  REG16(p.port + IOCFG) |= mask;
  if (p.af) {
    REG16(p.port + AFCFG) |= mask;
  } else {
    REG16(p.port + AFCFG) &= (uint16_t)~mask;
  }
}

// The 32 kHz RC clocks the LCD. Setting LRC_CTRL is necessary but NOT
// sufficient: once SystemInit() has run the 0x5555/0xAAAA shutdown sequence the
// oscillator stays stopped, and no amount of setting the bit brings it back.
// That is why this sketch has to be built with the shutdown suppressed --
// see the note at the top of the file.
static void lrcEnable(void) {
  REG16(CTRLBYFLASH) |= (uint16_t)LRC_CTRL_BIT;
}

// Is a low-frequency clock actually ticking? The watchdog counter runs off it,
// so watching that answers the question without configuring anything. (The
// auxiliary RTC second register looks like the obvious choice and is not: it
// needs the RTC enabled first, and reads a steady zero until then.)
static bool lowFreqClockRunning(void) {
  const uint16_t first = REG16(WDT_CNT);
  const unsigned long start = millis();
  while (millis() - start < 400UL) {
    if (REG16(WDT_CNT) != first) return true;
  }
  return false;
}

static void lcdSet(uint8_t seg, uint8_t bits) {
  *(volatile uint32_t*)LCD_BUF(seg) = bits;
}

static void lcdWrite(uint8_t p1, uint8_t p2) {
  // Clear every slot first: the buffer powers up with junk, and a SEG this
  // panel does not use would otherwise drive whatever it happened to hold.
  for (uint8_t s = 0; s <= LCD_SEG_MAX; s++) lcdSet(s, 0u);

  for (uint8_t i = 0; i < 7; i++) {
    if (SEG_NUM[i] == NO_SEG) continue;   // no pad to drive; see SEG_PIN above
    uint8_t bits = 0u;
    if ((p1 >> i) & 1u) bits |= (uint8_t)(1u << DIGIT1_COM);
    if ((p2 >> i) & 1u) bits |= (uint8_t)(1u << DIGIT2_COM);
    lcdSet(SEG_NUM[i], bits);
  }
}

static void setContrast(uint8_t vrsel) {
  contrast = (uint8_t)(vrsel & 0x0Fu);
  REG16(LCD_CON) = (uint16_t)((REG16(LCD_CON) & ~0x000Fu) | contrast);
}

static void showValue(uint8_t v) {
  value = (uint8_t)(v % 100u);
  const uint8_t tens = DIGITS[value / 10u];
  const uint8_t units = DIGITS[value % 10u];
  lcdWrite(tens_first ? tens : units, tens_first ? units : tens);
  Serial.print("showing ");
  if (value < 10) Serial.print('0');
  Serial.println(value);
}

void setup() {
  Serial.begin(115200);
  entry.reserve(8);

  lrcEnable();

  // Power up the LCD block. Read-modify-write: the boot code has already
  // configured other bits in here and they must survive.
  REG16(CMU_WPREG) = CMU_WP_KEY;
  *(volatile uint32_t*)CMU_CLKCTRL0 |= LCD_EN_BIT;

  for (uint8_t i = 0; i < 7; i++) {
    if (SEG_NUM[i] != NO_SEG) claimPin(SEG_PIN[i]);
  }
  claimPin(COM_PIN[0]);
  claimPin(COM_PIN[1]);
  for (uint8_t i = 0; i < 4; i++) claimPin(VLCD_PIN[i]);

  REG16(LCD_CLK) = (uint16_t)(LCD_BIAS_BIT | LCD_DUTY_BITS | LCD_LCLK_BITS);
  setContrast(contrast);
  REG16(LCD_OUT) = 0x0001u;        // OUTEN

  Serial.println();
  Serial.println("HT5023 hardware LCD controller -- 00 to 99");
  Serial.print("LCDCLK=0x"); Serial.print(REG16(LCD_CLK), HEX);
  Serial.print("  LCDCON=0x"); Serial.print(REG16(LCD_CON), HEX);
  Serial.print("  LCDOUT=0x"); Serial.println(REG16(LCD_OUT), HEX);
  Serial.print("duty 1/");
  Serial.print(DIGIT2_COM > 5 ? 8 : (DIGIT2_COM > 3 ? 6 : 4));
  Serial.print(", digit 2 on COM"); Serial.println(DIGIT2_COM);
#if DIGIT2_COM > 5
  Serial.println("NOTE: COM6 forces 1/8 duty. Moving that wire to PD9 (COM1) "
                 "and building with -DDIGIT2_COM=1 gives better contrast.");
#endif
  if (!lowFreqClockRunning()) {
    Serial.println("*** no low-frequency clock: the LCD has none either, so the "
                   "panel will stay blank ***");
    Serial.println("*** rebuild with -DHT5023_DISABLE_WDT_BY_DEFAULT=0 (see the "
                   "top of this sketch) ***");
  } else {
    Serial.println("low-frequency clock running -- the LCD has a clock");
  }
  {
    uint8_t dark = 0u;
    for (uint8_t i = 0; i < 7; i++) if (SEG_NUM[i] == NO_SEG) dark++;
    if (dark) {
      Serial.print("*** ");
      Serial.print(dark);
      Serial.println(" segment(s) are on pads with no LCD SEG function and will");
      Serial.println("*** stay dark: a=PC8, b=PC13 (port C has no SEG at all). The");
      Serial.println("*** digits will be unreadable. Use LcdTwoDigit (GPIO drive)");
      Serial.println("*** with this wiring, or move a and b to SEG-capable pads --");
      Serial.println("*** docs/lcd_pinout.md lists them. ***");
    }
  }
  Serial.println("keys: <number> Enter | r count | b blank | t all | +/- contrast | x swap");

  showValue(0);
  last_step = millis();
}

void loop() {
  const unsigned long now = millis();

  // The watchdog runs off the same low-frequency clock that was just switched
  // back on, so keep it fed.
  REG8(WDT_CLR) = WDT_FEED_KEY;

  if (counting && (now - last_step >= 500UL)) {
    last_step = now;
    showValue((uint8_t)(value + 1u));
  }

  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c >= '0' && c <= '9') {
      if (entry.length() < 3) entry += c;
    } else if (c == '\r' || c == '\n') {
      if (entry.length() > 0) {
        counting = false;
        showValue((uint8_t)entry.toInt());
        entry = "";
      }
    } else if (c == 'r' || c == 'R') {
      counting = true; entry = ""; Serial.println("counting");
    } else if (c == 'b' || c == 'B') {
      counting = false; lcdWrite(BLANK, BLANK); Serial.println("blank");
    } else if (c == 't' || c == 'T') {
      counting = false; lcdWrite(ALL_ON, ALL_ON); Serial.println("all segments on");
    } else if (c == '+') {
      if (contrast > 0u) setContrast((uint8_t)(contrast - 1u));   // lower VRSEL = darker
      Serial.print("contrast VRSEL="); Serial.println(contrast);
    } else if (c == '-') {
      if (contrast < 15u) setContrast((uint8_t)(contrast + 1u));
      Serial.print("contrast VRSEL="); Serial.println(contrast);
    } else if (c == 'x' || c == 'X') {
      tens_first = !tens_first;
      showValue(value);
      Serial.println("digits swapped");
    }
  }
}
