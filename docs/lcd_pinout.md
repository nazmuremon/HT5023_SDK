# Two-digit TN LCD — pin maps

Wiring history for the two-digit seven-segment TN panel driven by the sketches
in `sketches/`. Every LCD function below is quoted from the pad tables in
`HT5023.pdf`, which is the authority when this file and a sketch disagree.

The panel shares **one** set of seven segment lines between both digits and
selects a digit with a backplane (common) pin. That is what makes it a
multiplexed panel rather than two independent static displays.

---

## Current pin map

Segments:

| Segment | Position | Pin | LCD function (datasheet) |
|---------|----------|-----|--------------------------|
| a | top | **PC8** | `PC.8/TOUT1/INT7` — **no SEG** |
| b | top right | **PC13** | `PC.13/SCL` — **no SEG** |
| c | bottom right | **PF3** | `PF.3/SEG38` |
| d | bottom | **PB11** | `PB.11/SEG11` |
| e | bottom left | **PF4** | `PF.4/SEG39` |
| f | top left | **PA0** | `PA.0/SEG28` — **via a 10 kΩ series resistor** |
| g | middle | **PE3** | `PE.3/CLKOUT/SEG53` |

Commons (unchanged from the previous map):

| | Pin | LCD function |
|---|-----|--------------|
| digit 1 | **PD8** | `COM0/PD.8` |
| digit 2 | **PD14** | `COM6/PD.14` |

### PC8 and PC13 have no LCD segment output

Port C carries **no** `SEG` function anywhere on this part — grepping the pad
tables for `PC.<n>/…SEG<n>` returns nothing. `PC8` is `TOUT1/INT7` and `PC13`
is `SCL`, and that is all they are.

Consequence: **`LcdHw.ino` cannot drive segments a and b with this wiring.**
That sketch hands the pins to the chip's LCD peripheral, and the peripheral has
no path to those two pads. The GPIO sketches are unaffected — they toggle the
pins as ordinary outputs, which every pin can do.

To use the hardware LCD controller, move a and b onto pads that have a `SEG`
function. Anything in the table further down will do.

### The 10 kΩ resistor on f (PA0)

Segment f reaches the panel through a 10 kΩ series resistor. The panel is
capacitive and draws essentially no DC, so the full swing still develops across
the pixel; the resistor only slows the edge. Against a segment capacitance of
about 1 nF that is a ~10 µs time constant, against a 2500 µs sub-frame — so it
settles well inside one sub-frame and costs no measurable contrast.

---

## Original pin map (superseded — kept for reference)

This is what `LcdTwoDigit`, `LcdStatic` and `LcdHw` used before the change
above. Every one of these pads has a real `SEG` function, which is why the
hardware-controller sketch worked with it.

| Segment | Position | Pin | LCD function |
|---------|----------|-----|--------------|
| a | top | PD0 | `PD.0/SEG16` |
| b | top right | PF3 | `PF.3/SEG38` |
| c | bottom right | PF7 | `PF.7/SEG42` |
| d | bottom | PA12 | `PA.12/SEG35/ADCIN0` |
| e | bottom left | PF2 | `PF.2/SFOUT/SEG48` |
| f | top left | PA11 | `PA.11/INT6/SEG50` |
| g | middle | PF4 | `PF.4/SEG39` |

Commons: digit 1 = PD8 (`COM0`), digit 2 = PD14 (`COM6`).

### Older map, used only by `LcdSevenSeg`

An earlier revision, kept because that sketch still carries it. It differs in
three places and is **not** compatible with either map above:

| Segment | Pin | vs. the original map |
|---------|-----|----------------------|
| b | PF0 | was PF3 |
| g | PF1 | was PF4 |
| commons | PA13, PE8 | were PD8, PD14 |

---

## Pads that can carry an LCD segment

For picking replacement pins. Taken from the `HT5023.pdf` pad tables; `AF`
is the alternate-function index the code writes into `AFCFG` (`0` = the pin's
first multiplexed function, `1` = its second).

| Pin | SEG | AF | Also |
|-----|-----|----|------|
| PB11 | SEG11 | 0 | — |
| PD0 | SEG16 | 0 | — |
| PD14 | SEG26 | 1 | `COM6` is its first function |
| PA0 | SEG28 | 0 | — |
| PA12 | SEG35 | 0 | ADCIN0 |
| PF3 | SEG38 | 0 | — |
| PF4 | SEG39 | 0 | — |
| PF7 | SEG42 | 0 | — |
| PF2 | SEG48 | 1 | SFOUT is first |
| PA11 | SEG50 | 1 | INT6 is first |
| PE3 | SEG53 | 1 | CLKOUT is first |

This lists only the pads the sketches have used or now use — it is not the
complete set the part offers. Check `HT5023.pdf` before choosing anything else.

### Commons

All eight backplanes live on port D, consecutively:

| COM | Pin | | COM | Pin |
|-----|-----|---|-----|-----|
| COM0 | PD8 | | COM4 | PD12 |
| COM1 | PD9 | | COM5 | PD13 |
| COM2 | PD10 | | COM6 | PD14 |
| COM3 | PD11 | | COM7 | PD15 |

**Duty follows the highest COM used.** The peripheral offers 1/4 (COM0–3), 1/6
(COM0–5) and 1/8 (COM0–7) only. Digit 2 on **PD14 = COM6** therefore forces 1/8
duty — eight time slots for the two the panel actually needs — and costs
contrast: about 1.45:1 against 1.73:1 at 1/4 duty. Moving that wire to **PD9
(COM1)** and setting `DIGIT2_COM` to `1` in `LcdHw.ino` recovers it.

---

## Driving the panel when the pins cannot change

`PC8` and `PC13` rule out the hardware LCD controller, so the panel has to be
bit-banged from GPIO. That runs into a second problem: a multiplexed panel
normally needs **three** drive levels (0, V/2, V), and this chip cannot make
the middle one. The datasheet is explicit — there is an **88 kΩ internal
pull-up and no pull-down** (`PTUP`, offset `0x0C`: `0` = enable pull-up, `1` =
high-impedance), so a pin can be driven to either rail or released, never held
at half rail.

`LcdTwoDigit` therefore carries two schemes, switchable at runtime with `m`.

### `BIAS_RMS` — the default, no external components

Eight sub-frames per frame. The third level is replaced by **time**: the idle
backplane spends half of every polarity half at 0 V and half at VDD.

| Sub-frame | Active | COM (active) | COM (idle) | Segment line |
|-----------|--------|--------------|------------|--------------|
| 0 | A | 0 | 0 | VDD where A is on |
| 1 | A | 0 | VDD | ” |
| 2 | B | 0 | 0 | VDD where B is on |
| 3 | B | 0 | VDD | ” |
| 4 | A | VDD | VDD | inverted |
| 5 | A | VDD | 0 | inverted |
| 6 | B | VDD | VDD | inverted |
| 7 | B | VDD | 0 | inverted |

Because the idle backplane splits its time evenly between the rails, the energy
it puts into the other digit's pixels is the same whatever the segments are
doing. Over a frame:

```
on   Vrms = VDD * sqrt(6)/(2*sqrt(2)) = 0.866 * VDD   (2.86 V at 3.3 V)
off  Vrms = VDD * sqrt(2)/(2*sqrt(2)) = 0.500 * VDD   (1.65 V at 3.3 V)
```

Contrast is exactly **√3 = 1.73**, and — the point of the whole exercise — the
result contains no term for the other digit's pattern, which is what makes two
*different* digits legible. Sub-frames 4–7 mirror 0–3, so the DC across every
pixel is exactly zero.

Checked exhaustively over all 2⁷ × 2⁷ digit pairs, both digits, all seven
segments: every on-pixel lands on 0.866, every off-pixel on 0.500, every DC
offset on 0.000.

### `BIAS_TRISTATE` — the classic scheme, needs four resistors

Four sub-frames, idle backplane tri-stated and expected to sit at V/2. Better
contrast (**√5 = 2.24**, on 0.79 × VDD, off 0.35 × VDD) but a tri-stated pin
does not sit at half rail by itself — it capacitively divides against the seven
segment lines, so its voltage moves with whatever is on screen. That is why,
without help, two different digits look wrong while "both digits, same number"
looks fine.

To use it, fit **100 kΩ to VDD and 100 kΩ to GND on each common** (PD8 and
PD14) — four resistors.

### Choosing

| Scheme | Contrast | On-pixel | External parts | Two different digits? |
|--------|----------|----------|----------------|-----------------------|
| `BIAS_RMS` (default) | 1.73 | 2.86 V | **none** | yes |
| `BIAS_TRISTATE` | 2.24 | 2.60 V | 4 resistors | yes |
| `LcdStatic` | — | 3.30 V | none | no, both show the same |
| `LcdHw` | 1.45 | 1.50 V | none | **impossible — PC8/PC13** |

`BIAS_RMS` has a lower ratio than `BIAS_TRISTATE` but drives the lit pixel
*harder* (2.86 V against 2.60 V), and 1.73 is the same contrast the chip's own
LCD peripheral manages at 1/4 duty. It should be the more readable of the two
multiplexed options on this glass.

---

## Which sketch drives what

| Sketch | Drive | Digits | Needs SEG pads? |
|--------|-------|--------|-----------------|
| `LcdTwoDigit` | GPIO, 1/2 duty 1/2 bias, tri-state mid level | both, 00–99 | no |
| `LcdHw` | the chip's LCD peripheral | both, 00–99 | **yes** |
| `LcdSevenSeg` | GPIO | one digit, 0–9 | no |
| `LcdStatic` | GPIO, static square wave — brightest | both, same value | no |

RMS voltage across a lit pixel, measured on this panel at 3.3 V:

| Drive | On-pixel RMS | Different digits? |
|-------|--------------|-------------------|
| static, 50% square wave | 3.30 V | no |
| GPIO 1/2 duty, 1/2 bias | 2.60 V | yes |
| hardware controller, 1/4 duty | 1.90 V | yes |
| hardware controller, 1/8 duty | 1.50 V | yes |

The chip's LCD peripheral cannot do better than 1/4 duty, so it is the dimmest
way to drive this panel — but it is the only one that needs no external
resistors and no CPU time.

---

## Energy-meter PCB — 29-pin custom LCD (U11)

A different panel from everything above: the `ht5023_energy_meter_v1` board's
own glass, **8 commons (1/8 duty, 1/4 bias)** and 21 segment lines, driven by
`sketches/meter_lcd` through the chip's LCD controller. Pads from the KiCad
netlist, functions from the `HT5023.pdf` pad table — every one is the pad's
**first** function (`AFCFG` 0) except the VLCD rails.

| Glass SEG | Pad | Controller SEG | | Glass SEG | Pad | Controller SEG |
|---|---|---|---|---|---|---|
| 1 | PB0 | SEG0 | | 12 | PF3 | SEG38 |
| 2 | PB1 | SEG1 | | 13 | PF4 | SEG39 |
| 3 | PF5 | SEG40 | | 14 | PD6 | SEG22 |
| 4 | PF6 | SEG41 | | 15 | PA2 | SEG30 |
| 5 | PF7 | SEG42 | | 16 | PA1 | SEG29 |
| 6 | PB11 | SEG11 | | 17 | PA0 | SEG28 |
| 7 | PB6 | SEG6 | | 18 | PA3 | SEG31 |
| 8 | PB7 | SEG7 | | 19 | PA4 | SEG32 |
| 9 | PB8 | SEG8 | | 20 | PD0 | SEG16 |
| 10 | PB9 | SEG9 | | 21 | PD1 | SEG17 |
| 11 | PB10 | SEG10 | | | | |

Commons `1*`–`8*` = COM0–COM7 on PD8–PD15. VLCD0–3 = PB2–PB5 (second
function), unconnected on the PCB but still handed to the LCD block.

Glass map, confirmed on the glass with `tools/lcd_mapper` (answers in
`sketches/meter_lcd/segment_map.json`). Each 7-segment digit is one glass SEG
line:

| What | Glass SEG | Commons |
|---|---|---|
| Big digits, left to right | 1 2 3 4 21 20 19 18 | a=`1*` f=`2*` b=`3*` g=`4*` e=`5*` c=`6*` d=`7*` |
| ▽ under big digits 1–4 | 1–4 | `8*` (big digits 5–8 have nothing on `8*`) |
| Small character | 11 | f=`1*` d=`2*` c=`3*` g=`4*` b=`5*` e=`6*` a=`8*` — its own order |
| Boxed character, T | 10 | T=`1*` d=`2*` e=`3*` g=`4*` b=`6*`; a, c, f not found yet |
| Units | 17 | k=`1*` W=`2*` r=`3*` A=`4*` A=`5*` r=`6*` h=`7*` z=`8*` |
| CREDIT, its battery | 16 | `3*`, `4*` (`2*` lit something read as "W") |
| Antenna (3 parts) | 12, 13, 15 | `2*`, `1*`, `3*` |
| Quadrant (2 parts) | 13, 14 | `3*`, `3*` |
| Sad face; smiling face; switch | 12 | `3*`; `4*`+`5*`; `6*`+`7*` |
| Small top row | 5–9 | **not mapped** — `meter_lcd` assumes the big digits' order |

Not found yet: the L1–L3 rows and arrows, and the rest of SEG13–15.

Contrast: the glass needs more than the datasheet's 1/8-duty choice of 1/4
bias gives at 3.3 V (very faint). `meter_lcd` runs 1/3 bias, VRSEL 0 and
FCSET2 by default — `LCDCLK` 0x0094, `LCDCON` 0x0180 — with an 18-step ladder
on the keypad (hold `#`).

The two-digit GPIO schemes above cannot drive this glass: stretched to 8
commons they put 1.5–2.2 V RMS on dark segments, against the ~1.40 V a lit
segment gets from the controller at 1/4 bias.
