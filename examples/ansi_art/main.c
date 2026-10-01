// examples/ansi_art/main.c — plasma colour-wave animation (UART2, purely visual)
//
// PC11 = TX2. Terminal: 9600 8N1, ANSI colours, dark background.
// Frame: 20×12 coloured blocks (~840 B → ~1 fps at 9600 baud).
// Three modes cycle automatically every 64 frames:
//   0: plasma  — three interfering sine waves
//   1: rings   — concentric diamond pulses from centre
//   2: checker — diagonal XOR colour morph

#include <stdint.h>
#include "ht5023/board/blinky_gpio.h"
#include "ht5023/uart.h"
#include "ht5023/core/system.h"

extern void led_gpio_init(void);
extern void led_toggle(void);

#ifndef ANSI_ART_SYSCLK_HZ
#define ANSI_ART_SYSCLK_HZ HT5023_FSYS_HZ
#endif
#ifndef ANSI_ART_BAUD
#define ANSI_ART_BAUD 9600u
#endif

static void uputs(const char *s) { ht5023_uart_write_str(HT5023_UART_ID_2, s); }
static void uputc(char c)        { ht5023_uart_write_byte(HT5023_UART_ID_2, (uint8_t)c); }

/* ── Sine LUT ─────────────────────────────────────────────────────────────
 * 64 samples, one full period. Values 10..118, centre 64, amplitude 54.  */
static const uint8_t SIN64[64] = {
     64, 69, 75, 80, 85, 90, 94, 98,102,106,109,112,114,116,117,118,
    118,118,117,116,114,112,109,106,102, 98, 94, 90, 85, 80, 75, 69,
     64, 59, 53, 48, 43, 39, 34, 30, 26, 22, 19, 16, 14, 12, 11, 11,
     10, 10, 11, 12, 14, 16, 19, 22, 26, 30, 34, 39, 43, 48, 53, 59,
};

/* Hue-wheel background codes (ANSI): red→yellow→green→cyan→blue→magenta */
static const uint8_t BGCOL[6] = { 41, 43, 42, 46, 44, 45 };

#define COLS 20u   /* logical pixel columns (2 terminal chars wide each)  */
#define ROWS 12u   /* logical pixel rows                                   */

/* ── Colour mapping helpers ──────────────────────────────────────────────
 * Map an integer into 0..5 with a triangle bounce (no hard wrap seams).  */
static uint8_t bounce6(uint8_t s) {
    s %= 10u;
    return s > 5u ? (uint8_t)(10u - s) : s;
}

/* ── Animation modes ─────────────────────────────────────────────────────*/

/* Three interfering sine waves — the classic plasma look */
static uint8_t mode_plasma(uint8_t x, uint8_t y, uint8_t t) {
    uint8_t v1 = SIN64[((uint8_t)(x * 3u) + t        ) & 63u];
    uint8_t v2 = SIN64[((uint8_t)(y * 5u) + t        ) & 63u];
    uint8_t v3 = SIN64[((uint8_t)((x + y) * 2u) + (uint8_t)(t * 2u)) & 63u];
    return bounce6((uint8_t)(((uint16_t)v1 + v2 + v3 - 30u) >> 4u));
}

/* Concentric diamond rings expanding outward */
static uint8_t mode_rings(uint8_t x, uint8_t y, uint8_t t) {
    uint8_t dx = x > (COLS / 2u) ? (uint8_t)(x - COLS / 2u) : (uint8_t)(COLS / 2u - x);
    uint8_t dy = y > (ROWS / 2u) ? (uint8_t)(y - ROWS / 2u) : (uint8_t)(ROWS / 2u - y);
    return bounce6((uint8_t)((uint8_t)(dx + dy) * 2u + t));
}

/* Diagonal XOR colour morph */
static uint8_t mode_check(uint8_t x, uint8_t y, uint8_t t) {
    return bounce6((uint8_t)((uint8_t)(x ^ y) + t));
}

/* ── Rendering ───────────────────────────────────────────────────────────*/

static void emit_bg(uint8_t code) {
    uputc('\x1b'); uputc('[');
    uputc((char)('0' + code / 10u));
    uputc((char)('0' + code % 10u));
    uputc('m');
}

static void draw_frame(uint8_t t) {
    /* select mode: 0-63=plasma, 64-127=rings, 128-191=checker, repeat */
    uint8_t mode = (uint8_t)((t >> 6u) % 3u);

    uputs("\x1b[H");  /* cursor home, overwrite in place */

    for (uint8_t y = 0; y < ROWS; ++y) {
        uint8_t last = 0xFFu;
        for (uint8_t x = 0; x < COLS; ++x) {
            uint8_t ci;
            if      (mode == 0u) ci = mode_plasma(x, y, t);
            else if (mode == 1u) ci = mode_rings (x, y, t);
            else                 ci = mode_check (x, y, t);

            if (ci != last) { emit_bg(BGCOL[ci]); last = ci; }
            uputc(' '); uputc(' ');  /* 2-char wide pixel for square aspect */
        }
        uputs("\x1b[0m\r\n");
    }
    uputs("\x1b[0m");
}

/* ── Entry point ─────────────────────────────────────────────────────────*/

int main(void) {
    const ht5023_uart_config_t uart_cfg = {
        .sysclk_hz = ANSI_ART_SYSCLK_HZ,
        .baud      = ANSI_ART_BAUD,
        .enable_tx = true,
        .enable_rx = false,
    };

    led_gpio_init();
    ht5023_uart2_pins_init_pc11_pc12();
    (void)ht5023_uart_init(HT5023_UART_ID_2, &uart_cfg);

    uputs("\x1b[2J\x1b[H\x1b[?25l");  /* clear screen, home, hide cursor */

    for (uint8_t t = 0u;; ++t) {
        draw_frame(t);
        if ((t & 7u) == 0u) led_toggle();
    }
}
