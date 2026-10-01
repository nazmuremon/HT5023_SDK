// meter_lcd -- drives the energy-meter PCB's 29-pin custom LCD (U11) with the
// HT5023's own LCD controller, and turns the 3x4 keypad (U4) into a
// calculator.
//
// Open this in the Flasher (or the Arduino IDE) and press Compile & Upload --
// no build flags needed (see ht5023_keep_lrc below).
//
// What it does:
//   start-up  3 s of 12345678 / 12345 with every mapped icon lit -- a quick
//             check that the glass map is right
//   meter     a counter on the big row, seconds since reset on the small row,
//             "kWh" lit
//   calculator   press any key on the keypad
//
// Keypad (phone layout)        1 2 3 / 4 5 6 / 7 8 9 / * 0 #
//   0-9         type a number (up to 8 digits)
//   *           operator; press again to cycle  + - x /
//               (the small character shows A - n d, the big row flashes
//               Add / Sub / nnuLt / dIu)
//   #           equals; # again clears
//   hold *      delete the last digit
//   hold #      contrast: 2 darker, 8 lighter, * or # done
// Whole numbers only: the big row has no decimal points. Division truncates.
//
// Glass map, found with tools/lcd_mapper (sketches/meter_lcd/segment_map.json):
// each 7-segment digit is ONE glass SEG line and its segments sit on commons
// 1*..8*. The big digits use  a=1* f=2* b=3* g=4* e=5* c=6* d=7*,  and big
// digits 1-4 have a triangle below them on 8*. The small character (SEG11)
// uses its own order. The small top row (SEG5-9) was not mapped: it is drawn
// with the big digits' order, which is a guess.
//
// Contrast: the glass needs more drive than 1/4 bias at 3.3 V gives -- that
// is the datasheet's choice for 1/8 duty, and it looked very faint. The LCD
// voltage comes from a resistor divider off VDD, so VDD is the ceiling; what
// software can do is 1/3 bias (11 % more on a lit segment), full VRSEL and the
// stronger divider current (FCSET2). CONTRAST_LEVEL picks a step on that
// ladder; the default is the darkest.
//
// Wiring (from ht5023_energy_meter_v1, "CIRCUIT CONTEXT"):
//   glass SEG1-21  PB0 PB1 PF5 PF6 PF7 PB11 PB6 PB7 PB8 PB9 PB10 PF3 PF4
//                  PD6 PA2 PA1 PA0 PA3 PA4 PD0 PD1
//   COM 1*-8*      PD8-PD15 (COM0-COM7)
//   VLCD0-3        PB2-PB5 -- unconnected on the PCB, but they must be handed
//                  to the LCD block: its bias divider develops its levels there
//   keypad rows    R1-R4 = PD2 PD3 PD4 PD5 (through 220R), driven low one at a time
//   keypad cols    C1-C3 = PA5 PC10 PC9 (through 220R, 10k pull-ups), read
//
// Serial (115200), when a USB adapter is on J6:
//   <number> Enter  hold it on the main row     r  meter (count) again
//   b  blank      t  every segment on           + / -  darker / lighter
//   w  walk every SEG x COM; <n>w starts at glass SEG n
//   <i>m / n / p    light position i (0-167) / next / previous -- for
//                   tools/lcd_mapper, which can also do all this over the J-Link
//   k  check the 29 LCD lines for shorts (also runs at start-up)

#include <stdint.h>

// The LCD runs from the 32 kHz LRC, which SystemInit() otherwise shuts down --
// and that shutdown is a latch, so it has to be skipped rather than undone.
// This tells the core to skip it. The watchdog runs from the same clock, so
// loop() feeds it.
extern "C" const bool ht5023_keep_lrc = true;

// --------------------------------------------------------------- registers --
#define LCD_BASE         0x4000D000u
#define LCD_CLK_OFFSET   0x00u    // BIASCTL | DUTY[1:0] | LCLK[2:0]
#define LCD_CON_OFFSET   0x04u    // FCSET2 | FCSET[1:0] | FC | LC | VRSEL[3:0]
#define LCD_OUT_OFFSET   0x0Cu    // OUTEN
// One 32-bit slot per controller SEG, its COM bits in the low byte. Word
// spaced and running to SEG53, not byte spaced as the datasheet says; byte
// writes land in the wrong slot (LcdHw.ino tells how that was found).
#define LCD_BUF_OFFSET(seg) (0x10u + (uint32_t)(seg) * 4u)
#define LCD_SEG_MAX      53u

#define LCD_BIAS_13      (1u << 7)    // BIASCTL: 1 = 1/3 bias, 0 = 1/4
#define LCD_DUTY_BITS    (0x2u << 3)  // 1/8 duty, COM0-COM7
#define LCD_LCLK_BITS    0x4u         // 64 Hz frame at 1/8 duty
#define LCD_CON_FCSET2   (1u << 8)    // stronger resistor divider
#define LCD_CON_FCSET1   (1u << 7)    // reset value of FCSET[1:0] = 10, kept

#define CTRLBYFLASH_BASE 0x4000C000u
#define CTRLBYFLASH_OFFSET 0x100u
#define LRC_CTRL_BIT     (1u << 1)

// The LCD block is off at reset (CLKCTRL0.LCD_EN), behind the WPREG key.
#define CMU_BASE         0x4000F000u
#define CMU_WPREG_OFFSET 0x00u
#define CMU_CLKCTRL0_OFFSET 0x2Cu
#define CMU_WP_KEY       0xA55Au
#define LCD_EN_BIT       (1u << 1)

// WDTCLR: feed key 0xAA in the HIGH byte, timeout SET[7:0] in the low byte.
#define WDT_BASE         0x40010000u
#define WDT_CLR_OFFSET   0x04u
#define WDT_CNT_OFFSET   0x08u
#define WDT_FEED_KEY     0xAAu

#define GPIOA_BASE 0x40011000u
#define GPIOB_BASE 0x40011100u
#define GPIOD_BASE 0x40011300u
#define GPIOF_BASE 0x40011600u   // F is at 0x600, not 0x500
#define GPIO_IOCFG_OFFSET 0x00u
#define GPIO_AFCFG_OFFSET 0x04u
#define GPIO_PTDIR_OFFSET 0x08u   // 1 = output
#define GPIO_PTUP_OFFSET  0x0Cu   // 0 = pull-up ON (inverted)
#define GPIO_PTDAT_OFFSET 0x10u
#define GPIO_PTCLR_OFFSET 0x18u
#define GPIO_PTOD_OFFSET  0x20u   // 1 = push-pull

// ------------------------------------------------------------ LCD wiring --
struct LcdPin {
  uint32_t port;   // GPIO base
  uint8_t bit;     // pin within the port
  uint8_t af;      // 0 = first multiplex function, 1 = second
};

struct GlassSeg {
  LcdPin pin;
  uint8_t seg;       // controller SEG number = display-buffer slot
  const char* pad;
};

// Index 0 is glass SEG1 (U11 pin 1), as numbered on the drawing.
static const GlassSeg GLASS_SEG[21] = {
    {{GPIOB_BASE,  0u, 0u},  0u, "PB0"},    // SEG1   big digit 1
    {{GPIOB_BASE,  1u, 0u},  1u, "PB1"},    // SEG2   big digit 2
    {{GPIOF_BASE,  5u, 0u}, 40u, "PF5"},    // SEG3   big digit 3
    {{GPIOF_BASE,  6u, 0u}, 41u, "PF6"},    // SEG4   big digit 4
    {{GPIOF_BASE,  7u, 0u}, 42u, "PF7"},    // SEG5   small digit 1
    {{GPIOB_BASE, 11u, 0u}, 11u, "PB11"},   // SEG6   small digit 2
    {{GPIOB_BASE,  6u, 0u},  6u, "PB6"},    // SEG7   small digit 3
    {{GPIOB_BASE,  7u, 0u},  7u, "PB7"},    // SEG8   small digit 4
    {{GPIOB_BASE,  8u, 0u},  8u, "PB8"},    // SEG9   small digit 5
    {{GPIOB_BASE,  9u, 0u},  9u, "PB9"},    // SEG10  boxed character, T
    {{GPIOB_BASE, 10u, 0u}, 10u, "PB10"},   // SEG11  small character
    {{GPIOF_BASE,  3u, 0u}, 38u, "PF3"},    // SEG12  antenna, faces, switch
    {{GPIOF_BASE,  4u, 0u}, 39u, "PF4"},    // SEG13  antenna, quadrant
    {{GPIOD_BASE,  6u, 0u}, 22u, "PD6"},    // SEG14  quadrant
    {{GPIOA_BASE,  2u, 0u}, 30u, "PA2"},    // SEG15  antenna
    {{GPIOA_BASE,  1u, 0u}, 29u, "PA1"},    // SEG16  CREDIT
    {{GPIOA_BASE,  0u, 0u}, 28u, "PA0"},    // SEG17  kWArhz
    {{GPIOA_BASE,  3u, 0u}, 31u, "PA3"},    // SEG18  big digit 8
    {{GPIOA_BASE,  4u, 0u}, 32u, "PA4"},    // SEG19  big digit 7
    {{GPIOD_BASE,  0u, 0u}, 16u, "PD0"},    // SEG20  big digit 6
    {{GPIOD_BASE,  1u, 0u}, 17u, "PD1"},    // SEG21  big digit 5
};
static const uint8_t GLASS_SEGS = 21u;

// COM 1*-8* = COM0-COM7 on PD8-PD15; the bias rails on PB2-PB5 (second function).
static const uint8_t COM_FIRST_BIT = 8u;
static const uint8_t COMS = 8u;
static const LcdPin VLCD_PIN[4] = {
    {GPIOB_BASE, 2u, 1u}, {GPIOB_BASE, 3u, 1u},
    {GPIOB_BASE, 4u, 1u}, {GPIOB_BASE, 5u, 1u},
};

// --------------------------------------------------------------- glass map --
// A 7-segment digit: its glass SEG line and the common (0 = 1*) of each of
// a b c d e f g. NC = not mapped, never driven.
#define NC 0xFFu
struct Digit {
  uint8_t seg;       // glass SEG 1..21
  uint8_t com[7];    // a b c d e f g
};

// The big digits' order, confirmed on all eight:  a=1* b=3* c=6* d=7* e=5* f=2* g=4*
#define BIG_ORDER {0u, 2u, 5u, 6u, 4u, 1u, 3u}
static const Digit MAIN_ROW[8] = {           // left to right
    {1u, BIG_ORDER}, {2u, BIG_ORDER}, {3u, BIG_ORDER}, {4u, BIG_ORDER},
    {21u, BIG_ORDER}, {20u, BIG_ORDER}, {19u, BIG_ORDER}, {18u, BIG_ORDER},
};
// Not mapped yet -- assumed to follow the big digits.
static const Digit SMALL_ROW[5] = {
    {5u, BIG_ORDER}, {6u, BIG_ORDER}, {7u, BIG_ORDER}, {8u, BIG_ORDER}, {9u, BIG_ORDER},
};
// Mapped: f=1* d=2* c=3* g=4* b=5* e=6* a=8* (7* not answered).
static const Digit SMALL_CHAR = {11u, {7u, 4u, 2u, 1u, 5u, 0u, 3u}};
// Partly mapped: d=2* e=3* g=4* b=6*; a, c, f are on 5*/7*/8* in some order.
static const Digit BOX_CHAR = {10u, {NC, 5u, NC, 1u, 2u, NC, 3u}};

// Single icons: glass SEG and common (0 = 1*).
struct Icon {
  uint8_t seg;
  uint8_t com;
};
static const Icon TRIANGLE[4] = {{1u, 7u}, {2u, 7u}, {3u, 7u}, {4u, 7u}};   // under big digits 1-4
static const Icon ICON_T = {10u, 0u};
static const Icon ICON_SAD = {12u, 2u};
static const Icon ICON_CREDIT = {16u, 2u};
static const Icon ICON_CREDIT_BATTERY = {16u, 3u};
static const Icon UNIT_k = {17u, 0u};
static const Icon UNIT_W = {17u, 1u};
static const Icon UNIT_h = {17u, 6u};
static const Icon UNIT_z = {17u, 7u};
// Every mapped icon, including the parts of multi-part ones: the antenna (3),
// the quadrant (2), the smiling face (2), the switch (2), the letters the
// mapper saw twice (A, r), and the unexplained "W" on SEG16 2*.
static const Icon ALL_ICONS[] = {
    {1u, 7u}, {2u, 7u}, {3u, 7u}, {4u, 7u},             // triangles
    {10u, 0u},                                          // T
    {12u, 1u}, {13u, 0u}, {15u, 2u},                    // antenna parts
    {13u, 2u}, {14u, 2u},                               // quadrant parts
    {12u, 2u},                                          // sad face
    {12u, 3u}, {12u, 4u},                               // smiling face parts
    {12u, 5u}, {12u, 6u},                               // switch parts
    {16u, 1u}, {16u, 2u}, {16u, 3u},                    // "W", CREDIT, battery
    {17u, 0u}, {17u, 1u}, {17u, 2u}, {17u, 3u},         // k W r A
    {17u, 4u}, {17u, 5u}, {17u, 6u}, {17u, 7u},         // A r h z
};

// -------------------------------------------------------------- patterns --
// Bit 0 = a (top), then b c d e f g.
static const uint8_t DIGITS[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};
static const uint8_t BLANK = 0x00;
static const uint8_t ALL7 = 0x7F;
static const uint8_t L_A = 0x77, L_b = 0x7C, L_C = 0x39, L_d = 0x5E, L_E = 0x79,
                     L_I = 0x06, L_L = 0x38, L_n = 0x54, L_o = 0x5C, L_r = 0x50,
                     L_S = 0x6D, L_t = 0x78, L_u = 0x1C, L_MINUS = 0x40;

// ---------------------------------------------------------------- contrast --
// Steps in order of drive on a lit segment: 0-15 are 1/4 bias with VRSEL
// 15..0, 16-17 are 1/3 bias with VRSEL 1..0 (the two 1/3-bias settings that
// beat 1/4 at full VRSEL). Lower VRSEL = more of VDD.
static const uint8_t CONTRAST_STEPS = 18u;
#ifndef CONTRAST_LEVEL
#define CONTRAST_LEVEL 17u       // darkest
#endif

// ----------------------------------------------------------------- keypad --
static const uint8_t KEY_ROW[4] = {PIN_PD2, PIN_PD3, PIN_PD4, PIN_PD5};   // R1-R4
static const uint8_t KEY_COL[3] = {PIN_PA5, PIN_PC10, PIN_PC9};           // C1-C3
static const char KEYMAP[4][3] = {
    {'1', '2', '3'}, {'4', '5', '6'}, {'7', '8', '9'}, {'*', '0', '#'},
};
#define KEY_SCAN_MS   10UL     // scan period
#define KEY_STABLE    3u       // scans a change must hold for (30 ms debounce)
#define LONG_STAR_MS  800UL    // hold * = delete last digit
#define LONG_HASH_MS  1500UL   // hold # = contrast

// ---------------------------------------------------------------- timing --
#ifndef STEP_MS
#define STEP_MS 500UL            // meter counter step
#endif
#define STARTUP_HOLD_MS 3000UL
#ifndef WALK_MS
#define WALK_MS 1500UL           // per SEG x COM in walk mode
#endif
#define OP_WORD_MS 900UL         // how long the operator's name shows

// ----------------------------------------------------------------- state --
enum Mode : uint8_t { HOLD, METER, CALC, CONTRAST, WALK, MAP, STILL };
// Calculator operators. Declared up here, with the other types, because the
// Arduino build puts its generated function prototypes above the first
// function -- and calcApply() takes an Op.
enum Op : uint8_t { NONE, ADD, SUB, MUL, DIV };
static Mode mode = HOLD;
static Mode mode_before_contrast = CALC;

static uint8_t glass[21];        // COM bits per glass SEG line, bit n = COMn
static uint32_t value = 0;       // meter counter
static uint8_t contrast_level = CONTRAST_LEVEL;
static uint8_t position = 0;     // 0..167 for walk / map: glass SEG pos/8+1, COM pos%8
static unsigned long hold_start = 0, last_step = 0, last_walk = 0;
static unsigned long shown_seconds = 0xFFFFFFFFUL;
static String entry;

// ------------------------------------------------------------ hardware --
static void claimPin(const LcdPin& p) {
  const uint16_t mask = (uint16_t)(1u << p.bit);
  *ht5023_reg16(p.port, GPIO_IOCFG_OFFSET) |= mask;   // peripheral, not GPIO
  if (p.af) {
    *ht5023_reg16(p.port, GPIO_AFCFG_OFFSET) |= mask;
  } else {
    *ht5023_reg16(p.port, GPIO_AFCFG_OFFSET) &= (uint16_t)~mask;
  }
}

static void feedWatchdog() {
  volatile uint16_t* clr = ht5023_reg16(WDT_BASE, WDT_CLR_OFFSET);
  *clr = (uint16_t)((WDT_FEED_KEY << 8) | (*clr & 0x00FFu));   // keep SET
}

// The watchdog counter runs off the LRC, so a moving count means the LCD has
// its clock. A still one proves nothing on its own: JTAGWDTEN (PA6) low holds
// the watchdog off whatever the clock does (a jumper on J1 pins 1-2).
static bool lowFreqClockRunning() {
  const uint16_t first = *ht5023_reg16(WDT_BASE, WDT_CNT_OFFSET);
  const unsigned long start = millis();
  while (millis() - start < 400UL) {
    if (*ht5023_reg16(WDT_BASE, WDT_CNT_OFFSET) != first) return true;
  }
  return false;
}

static void lcdSlot(uint8_t seg, uint8_t coms) {
  *ht5023_reg32(LCD_BASE, LCD_BUF_OFFSET(seg)) = coms;   // word write, always
}

static void flush() {
  for (uint8_t i = 0; i < GLASS_SEGS; i++) lcdSlot(GLASS_SEG[i].seg, glass[i]);
}

static void setContrast(uint8_t level) {
  if (level >= CONTRAST_STEPS) level = CONTRAST_STEPS - 1u;
  contrast_level = level;
  const bool third = level >= 16u;
  const uint8_t vrsel = third ? (uint8_t)(17u - level) : (uint8_t)(15u - level);
  *ht5023_reg16(LCD_BASE, LCD_CLK_OFFSET) =
      (uint16_t)((third ? LCD_BIAS_13 : 0u) | LCD_DUTY_BITS | LCD_LCLK_BITS);
  *ht5023_reg16(LCD_BASE, LCD_CON_OFFSET) =
      (uint16_t)(LCD_CON_FCSET2 | LCD_CON_FCSET1 | vrsel);
}

static void printContrast() {
  Serial.print("contrast ");
  Serial.print(contrast_level);
  Serial.print('/');
  Serial.print(CONTRAST_STEPS - 1u);
  Serial.println(contrast_level >= 16u ? " (1/3 bias)" : " (1/4 bias)");
}

// ---------------------------------------------------------- line check --
// Segments missing on EVERY digit mean a common line that never reaches the
// glass; two neighbouring commons failing together is the signature of a
// solder bridge between them (found and confirmed on PD11/PD12, U1 pins
// 85/84). This takes all 29 LCD lines back as GPIO inputs with the 88k
// pull-up, then pulls each low in turn and reads the rest: a line that
// follows another low is shorted to it, one that reads low undriven is
// shorted to ground. About 0.2 s, then the pins go back to the LCD.
#define LINE_SETTLE_MS 5UL
static const uint8_t LINES = 21u + 8u;   // glass SEG1-21, then COM 1*-8*

static LcdPin linePin(uint8_t i) {
  if (i < GLASS_SEGS) return GLASS_SEG[i].pin;
  return LcdPin{GPIOD_BASE, (uint8_t)(COM_FIRST_BIT + (i - GLASS_SEGS)), 0u};
}

static void printLine(uint8_t i) {
  if (i < GLASS_SEGS) {
    Serial.print("glass SEG");
    Serial.print(i + 1u);
    Serial.print(" (");
    Serial.print(GLASS_SEG[i].pad);
  } else {
    Serial.print("COM ");
    Serial.print(i - GLASS_SEGS + 1u);
    Serial.print("* (PD");
    Serial.print(COM_FIRST_BIT + (i - GLASS_SEGS));
  }
  Serial.print(')');
}

static void lineInputPullup(const LcdPin& p) {
  const uint16_t mask = (uint16_t)(1u << p.bit);
  *ht5023_reg16(p.port, GPIO_IOCFG_OFFSET) &= (uint16_t)~mask;
  *ht5023_reg16(p.port, GPIO_PTDIR_OFFSET) &= (uint16_t)~mask;
  *ht5023_reg16(p.port, GPIO_PTUP_OFFSET) &= (uint16_t)~mask;
}

static void lineDriveLow(const LcdPin& p) {
  const uint16_t mask = (uint16_t)(1u << p.bit);
  *ht5023_reg16(p.port, GPIO_PTCLR_OFFSET) = mask;
  *ht5023_reg16(p.port, GPIO_PTOD_OFFSET) |= mask;
  *ht5023_reg16(p.port, GPIO_PTDIR_OFFSET) |= mask;
}

static bool lineHigh(const LcdPin& p) {
  return (*ht5023_reg16(p.port, GPIO_PTDAT_OFFSET) & (1u << p.bit)) != 0u;
}

static uint8_t checkLines() {
  uint8_t faults = 0;
  uint32_t grounded = 0;

  for (uint8_t i = 0; i < LINES; i++) lineInputPullup(linePin(i));
  delay(LINE_SETTLE_MS);
  for (uint8_t i = 0; i < LINES; i++) {
    if (!lineHigh(linePin(i))) {
      grounded |= 1UL << i;
      faults++;
      Serial.print("  ");
      printLine(i);
      Serial.println(" reads LOW with only its pull-up: shorted to GND");
    }
  }

  for (uint8_t a = 0; a < LINES; a++) {
    if ((grounded >> a) & 1u) continue;
    const LcdPin pa = linePin(a);
    lineDriveLow(pa);
    delay(LINE_SETTLE_MS);
    for (uint8_t b = (uint8_t)(a + 1u); b < LINES; b++) {
      if (((grounded >> b) & 1u) == 0u && !lineHigh(linePin(b))) {
        faults++;
        Serial.print("  ");
        printLine(a);
        Serial.print(" is SHORTED to ");
        printLine(b);
        Serial.println();
      }
    }
    if (lineHigh(pa)) {
      faults++;
      Serial.print("  ");
      printLine(a);
      Serial.println(" will not go low: shorted to 3.3 V?");
    }
    *ht5023_reg16(pa.port, GPIO_PTDIR_OFFSET) &= (uint16_t)~(1u << pa.bit);
    feedWatchdog();
  }

  for (uint8_t i = 0; i < LINES; i++) claimPin(linePin(i));   // back to the LCD
  return faults;
}

static void reportLines() {
  Serial.println("LCD line check (shorts between the 29 LCD lines, or to GND):");
  if (checkLines() == 0u) {
    Serial.println("  no shorts. Segments missing on every digit then mean an OPEN:");
    Serial.println("  reflow that common at U1 (PD8-PD13 = pins 88-83, PD14/15 = 52/53)");
    Serial.println("  and at U11 (1*-8* = pins 22-29).");
  }
}

// ------------------------------------------------------------- drawing --
static void clearGlass() {
  for (uint8_t i = 0; i < GLASS_SEGS; i++) glass[i] = 0u;
}

static void setIcon(const Icon& icon, bool on) {
  const uint8_t bit = (uint8_t)(1u << icon.com);
  if (on) {
    glass[icon.seg - 1u] |= bit;
  } else {
    glass[icon.seg - 1u] &= (uint8_t)~bit;
  }
}

// Draws a pattern on a digit's mapped segments only; anything else on the
// same SEG line (a triangle, an icon) is left as it is.
static void setDigit(const Digit& d, uint8_t pattern) {
  uint8_t coms = glass[d.seg - 1u];
  for (uint8_t i = 0; i < 7; i++) {
    if (d.com[i] == NC) continue;
    const uint8_t bit = (uint8_t)(1u << d.com[i]);
    coms = ((pattern >> i) & 1u) ? (uint8_t)(coms | bit) : (uint8_t)(coms & ~bit);
  }
  glass[d.seg - 1u] = coms;
}

// Right-aligned, leading zeros blank, the last digit always shown.
static void showNumber(const Digit* row, uint8_t count, uint32_t v) {
  for (int8_t i = (int8_t)(count - 1u); i >= 0; i--) {
    const bool leading = (v == 0u) && (i != (int8_t)(count - 1u));
    setDigit(row[i], leading ? BLANK : DIGITS[v % 10u]);
    v /= 10u;
  }
}

// Right-aligned with a minus sign just left of the first digit. False if the
// number does not fit.
static bool showSigned(const Digit* row, uint8_t count, int32_t v) {
  const bool negative = v < 0;
  uint32_t m = negative ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
  uint8_t len = 1u;
  for (uint32_t t = m; t >= 10u; t /= 10u) len++;
  if (len + (negative ? 1u : 0u) > count) return false;
  showNumber(row, count, m);
  if (negative) setDigit(row[count - len - 1u], L_MINUS);
  return true;
}

// Left-aligned letters, the rest of the row blank.
static void showWord(const Digit* row, uint8_t count, const uint8_t* letters, uint8_t n) {
  for (uint8_t i = 0; i < count; i++) setDigit(row[i], i < n ? letters[i] : BLANK);
}

static void fill(uint8_t coms) {
  for (uint8_t i = 0; i < GLASS_SEGS; i++) glass[i] = coms;
  flush();
}

// ------------------------------------------------------------- screens --
static void drawStartup() {
  clearGlass();
  showNumber(MAIN_ROW, 8u, 12345678UL);
  showNumber(SMALL_ROW, 5u, 12345UL);
  setDigit(SMALL_CHAR, ALL7);
  setDigit(BOX_CHAR, ALL7);
  for (const Icon& icon : ALL_ICONS) setIcon(icon, true);
  flush();
}

static void drawMeter(unsigned long now) {
  clearGlass();
  showNumber(MAIN_ROW, 8u, value);
  shown_seconds = now / 1000UL;
  showNumber(SMALL_ROW, 5u, shown_seconds % 100000UL);
  setIcon(UNIT_k, true);
  setIcon(UNIT_W, true);
  setIcon(UNIT_h, true);
  flush();
}

static void drawContrast() {
  static const uint8_t CON[3] = {L_C, L_o, L_n};
  clearGlass();
  showWord(MAIN_ROW, 8u, CON, 3u);
  showNumber(&MAIN_ROW[5], 3u, contrast_level);   // the number on the last 3 digits
  for (const Icon& t : TRIANGLE) setIcon(t, true);
  setDigit(SMALL_CHAR, ALL7);
  flush();
}

// ------------------------------------------------------------ calculator --
static const int32_t CALC_MAX = 99999999L, CALC_MIN = -9999999L;   // what 8 digits can show

struct Calc {
  int32_t acc;       // first operand, or the result
  int32_t entry;     // number being typed
  bool typing;
  Op op;
  bool result;       // acc is a fresh result from '#'
  bool error;
  unsigned long word_until;   // show the operator's name until then
};
static Calc calc;

static void calcReset() {
  calc = Calc{0, 0, false, NONE, false, false, 0UL};
}

static bool calcApply(int32_t a, Op op, int32_t b, int32_t* out) {
  int64_t r;
  switch (op) {
    case ADD: r = (int64_t)a + b; break;
    case SUB: r = (int64_t)a - b; break;
    case MUL: r = (int64_t)a * b; break;
    case DIV:
      if (b == 0) return false;
      r = (int64_t)a / b;
      break;
    default: r = b; break;
  }
  if (r > CALC_MAX || r < CALC_MIN) return false;
  *out = (int32_t)r;
  return true;
}

// Folds the typed number into acc with the pending operator.
static void calcFold() {
  if (!calc.typing) return;
  int32_t r = calc.entry;
  if (calc.op != NONE && !calcApply(calc.acc, calc.op, calc.entry, &r)) {
    calc.error = true;
    return;
  }
  calc.acc = r;
  calc.typing = false;
}

static void drawCalc(unsigned long now) {
  static const uint8_t ERR[3] = {L_E, L_r, L_r};
  static const uint8_t WORD_ADD[3] = {L_A, L_d, L_d};
  static const uint8_t WORD_SUB[3] = {L_S, L_u, L_b};
  static const uint8_t WORD_MUL[5] = {L_n, L_n, L_u, L_L, L_t};
  static const uint8_t WORD_DIV[3] = {L_d, L_I, L_u};
  static const uint8_t OP_CODE[5] = {BLANK, L_A, L_MINUS, L_n, L_d};

  clearGlass();
  if (calc.error) {
    showWord(MAIN_ROW, 8u, ERR, 3u);
  } else if (calc.op != NONE && (long)(calc.word_until - now) > 0) {
    switch (calc.op) {
      case ADD: showWord(MAIN_ROW, 8u, WORD_ADD, 3u); break;
      case SUB: showWord(MAIN_ROW, 8u, WORD_SUB, 3u); break;
      case MUL: showWord(MAIN_ROW, 8u, WORD_MUL, 5u); break;
      default:  showWord(MAIN_ROW, 8u, WORD_DIV, 3u); break;
    }
  } else if (!showSigned(MAIN_ROW, 8u, calc.typing ? calc.entry : calc.acc)) {
    showWord(MAIN_ROW, 8u, ERR, 3u);
  }
  setDigit(SMALL_CHAR, calc.error ? BLANK : OP_CODE[calc.op]);
  flush();
}

static void calcDigit(uint8_t d) {
  if (calc.error) calcReset();
  if (calc.result && calc.op == NONE) calcReset();   // a new sum after '='
  if (!calc.typing) {
    calc.entry = 0;
    calc.typing = true;
  }
  calc.result = false;
  const int32_t next = calc.entry * 10 + (int32_t)d;
  if (next <= CALC_MAX) calc.entry = next;          // 8 digits, leading zeros free
}

static void calcOperator(unsigned long now) {
  if (calc.error) {
    calcReset();
    return;
  }
  if (calc.typing) {
    calcFold();
    calc.op = ADD;                                  // a fresh operator
  } else {
    calc.op = calc.op == NONE ? ADD : (Op)(calc.op % 4u + 1u);   // + - x / +
  }
  calc.result = false;
  calc.word_until = now + OP_WORD_MS;
}

static void calcEquals() {
  if (calc.error) {
    calcReset();
  } else if (calc.op != NONE || calc.typing) {
    calcFold();
    calc.op = NONE;
    calc.result = true;
  } else {
    calcReset();                                    // second '#': clear
  }
}

static void calcBackspace() {
  if (calc.typing) calc.entry /= 10;
}

// ----------------------------------------------------------------- keypad --
static char key_raw = 0, key_down = 0;
static uint8_t key_same = 0;
static bool key_long_done = false;
static unsigned long key_since = 0, last_scan = 0;

// Rows idle as inputs; one at a time becomes an output low. Their output
// latches stay low, so switching direction is all it takes.
static char scanKeypad() {
  for (uint8_t r = 0; r < 4u; r++) {
    pinMode(KEY_ROW[r], OUTPUT);
    delayMicroseconds(30);
    char k = 0;
    for (uint8_t c = 0; c < 3u && !k; c++) {
      if (digitalRead(KEY_COL[c]) == LOW) k = KEYMAP[r][c];
    }
    pinMode(KEY_ROW[r], INPUT);
    if (k) return k;
  }
  return 0;
}

static void enterContrast() {
  mode_before_contrast = mode == CONTRAST ? mode_before_contrast : mode;
  mode = CONTRAST;
  printContrast();
}

static void onKeyDown(char k, unsigned long now) {
  if (mode == CONTRAST) {
    if (k == '2' && contrast_level + 1u < CONTRAST_STEPS) setContrast((uint8_t)(contrast_level + 1u));
    if (k == '8' && contrast_level > 0u) setContrast((uint8_t)(contrast_level - 1u));
    if (k == '2' || k == '8') printContrast();
    return;
  }
  if (mode != CALC) {                               // first key: calculator
    mode = CALC;
    calcReset();
    Serial.println("calculator");
  }
  if (k >= '0' && k <= '9') calcDigit((uint8_t)(k - '0'));
  (void)now;
}

static void onKeyLong(char k) {
  if (mode == CALC && k == '*') calcBackspace();
  if (k == '#' && mode != CONTRAST) enterContrast();
}

static void onKeyShort(char k, unsigned long now) {   // * and #, on release
  if (mode == CONTRAST) {
    mode = mode_before_contrast;
    return;
  }
  if (mode != CALC) return;
  if (k == '*') calcOperator(now);
  if (k == '#') calcEquals();
}

// Debounced: a change must hold for KEY_STABLE scans. Digits act when pressed;
// * and # when released, unless they were held long enough for their long
// action.
static bool pollKeypad(unsigned long now) {
  if (now - last_scan < KEY_SCAN_MS) return false;
  last_scan = now;
  const char raw = scanKeypad();
  key_same = raw == key_raw ? (uint8_t)(key_same < 255u ? key_same + 1u : 255u) : 0u;
  key_raw = raw;
  bool changed = false;
  if (key_same + 1u >= KEY_STABLE && raw != key_down) {
    if (key_down && !key_long_done && (key_down == '*' || key_down == '#')) {
      onKeyShort(key_down, now);
      changed = true;
    }
    key_down = raw;
    key_since = now;
    key_long_done = false;
    if (raw) {
      onKeyDown(raw, now);
      changed = true;
    }
  }
  if (key_down && !key_long_done) {
    const unsigned long hold = key_down == '#' ? LONG_HASH_MS : LONG_STAR_MS;
    if ((key_down == '*' || key_down == '#') && now - key_since >= hold) {
      key_long_done = true;
      onKeyLong(key_down);
      changed = true;
    }
  }
  return changed;
}

// ---------------------------------------------------------- walk / map --
static const uint8_t POSITIONS = 21u * 8u;   // every glass SEG x COM

// Lights exactly one glass SEG x COM and says which, in a form both a person
// and tools/lcd_mapper read:  "map 37: glass SEG5 (PF7) COM 6*".
static void showPosition() {
  const uint8_t seg = (uint8_t)(position / COMS);   // 0-based glass SEG
  const uint8_t com = (uint8_t)(position % COMS);
  clearGlass();
  glass[seg] = (uint8_t)(1u << com);
  flush();
  Serial.print("map ");
  Serial.print(position);
  Serial.print(": glass SEG");
  Serial.print(seg + 1u);
  Serial.print(" (");
  Serial.print(GLASS_SEG[seg].pad);
  Serial.print(") COM ");
  Serial.print(com + 1u);
  Serial.println('*');
}

// ----------------------------------------------------------------- setup --
void setup() {
  Serial.begin(115200);
  entry.reserve(10);

  *ht5023_reg16(CTRLBYFLASH_BASE, CTRLBYFLASH_OFFSET) |= (uint16_t)LRC_CTRL_BIT;

  // Power up the LCD block; read-modify-write, the boot code set other bits.
  *ht5023_reg16(CMU_BASE, CMU_WPREG_OFFSET) = CMU_WP_KEY;
  *ht5023_reg32(CMU_BASE, CMU_CLKCTRL0_OFFSET) |= LCD_EN_BIT;

  // The buffer powers up with junk: clear every slot, used or not.
  for (uint8_t s = 0; s <= LCD_SEG_MAX; s++) lcdSlot(s, 0u);

  for (uint8_t i = 0; i < GLASS_SEGS; i++) claimPin(GLASS_SEG[i].pin);
  for (uint8_t c = 0; c < COMS; c++) {
    claimPin(LcdPin{GPIOD_BASE, (uint8_t)(COM_FIRST_BIT + c), 0u});
  }
  for (uint8_t i = 0; i < 4; i++) claimPin(VLCD_PIN[i]);

  setContrast(contrast_level);
  *ht5023_reg16(LCD_BASE, LCD_OUT_OFFSET) = 0x0001u;   // OUTEN

  // Keypad: columns are inputs (10k pull-ups on the PCB, the internal one as
  // well); rows idle as inputs with their output latch low.
  for (uint8_t c = 0; c < 3u; c++) pinMode(KEY_COL[c], INPUT_PULLUP);
  for (uint8_t r = 0; r < 4u; r++) {
    pinMode(KEY_ROW[r], INPUT);
    digitalWrite(KEY_ROW[r], LOW);
  }

  Serial.println();
  Serial.println("HT5023 meter_lcd -- U11 on the LCD controller, 1/8 duty; keypad calculator");
  printContrast();
  if (lowFreqClockRunning()) {
    Serial.println("32 kHz clock running -- the LCD has a clock");
  } else {
    Serial.println("watchdog counter not moving: either the 32 kHz clock is off "
                   "(glass stays blank) or the watchdog is held off by a jumper "
                   "on J1 1-2 (fine, if the glass shows anything)");
  }
  feedWatchdog();
  reportLines();
  Serial.println("keypad: 0-9 | * operator | # equals/clear | hold * delete | hold # contrast");
  Serial.println("serial: <number> Enter | r meter | b blank | t all on | w walk | "
                 "<i>m / n / p one segment | k line check | +/- contrast");

  drawStartup();
  hold_start = last_step = millis();
}

// ------------------------------------------------------------------ loop --
void loop() {
  feedWatchdog();
  const unsigned long now = millis();

  if (mode == HOLD && now - hold_start >= STARTUP_HOLD_MS) {
    mode = METER;
    value = 0;
    last_step = now;
    drawMeter(now);
  }
  if (mode == METER) {
    if (now - last_step >= STEP_MS) {
      last_step = now;
      value = (value + 1u) % 100000000UL;
      drawMeter(now);
    } else if (now / 1000UL != shown_seconds) {
      drawMeter(now);
    }
  }
  if (mode == WALK && now - last_walk >= WALK_MS) {
    last_walk = now;
    position = (uint8_t)((position + 1u) % POSITIONS);
    showPosition();
  }

  const bool key_event = pollKeypad(now);
  if (mode == CALC) {
    // Redraw on a key, and once more when the operator's name times out.
    static bool word_was_up = false;
    const bool word_up = calc.op != NONE && (long)(calc.word_until - now) > 0;
    if (key_event || word_up != word_was_up) drawCalc(now);
    word_was_up = word_up;
  } else if (mode == CONTRAST && key_event) {
    drawContrast();
  } else if (key_event && mode == METER) {
    drawMeter(now);                                 // back from contrast
  }

  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c >= '0' && c <= '9') {
      if (entry.length() < 8) entry += c;
    } else if (c == '\r' || c == '\n') {
      if (entry.length() > 0) {
        mode = STILL;
        value = (uint32_t)entry.toInt();
        clearGlass();
        showNumber(MAIN_ROW, 8u, value);
        flush();
        entry = "";
      }
    } else if (c == 'r' || c == 'R') {
      mode = METER;
      entry = "";
      drawMeter(now);
      Serial.println("meter");
    } else if (c == 'b' || c == 'B') {
      mode = STILL;
      fill(0x00u);
      Serial.println("blank");
    } else if (c == 't' || c == 'T') {
      mode = STILL;
      fill(0xFFu);
      Serial.println("every segment on");
    } else if (c == 'w' || c == 'W') {
      const long from = entry.length() > 0 ? entry.toInt() : 1;
      entry = "";
      mode = WALK;
      position = (uint8_t)(((from >= 1 && from <= GLASS_SEGS) ? from - 1 : 0) * COMS);
      last_walk = now;
      showPosition();
    } else if (c == 'm' || c == 'M') {
      const long at = entry.length() > 0 ? entry.toInt() : 0;
      entry = "";
      mode = MAP;
      position = (uint8_t)((at >= 0 && at < POSITIONS) ? at : 0);
      showPosition();
    } else if (c == 'n' || c == 'N' || c == 'p' || c == 'P') {
      const bool forward = (c == 'n' || c == 'N');
      entry = "";
      mode = MAP;
      position = (uint8_t)((position + (forward ? 1u : POSITIONS - 1u)) % POSITIONS);
      showPosition();
    } else if (c == 'k' || c == 'K') {
      reportLines();
    } else if (c == '+' || c == '-') {
      if (c == '+' && contrast_level + 1u < CONTRAST_STEPS) setContrast((uint8_t)(contrast_level + 1u));
      if (c == '-' && contrast_level > 0u) setContrast((uint8_t)(contrast_level - 1u));
      printContrast();
    }
  }
}
