// bootloader/main.c — UART firmware loader for the HT5023
//
// Lets you program the application over a USB-UART adapter (FTDI, CP210x,
// CH340) instead of the J-Link. The chip has no factory ISP, so this bootloader
// itself has to be flashed once over SWD; after that, application updates go
// over UART2.
//
// Wiring: PC11 = TX2 -> adapter RX, PC12 = RX2 <- adapter TX, GND common,
// 115200 8N1.
//
// At reset it prints a banner and listens BOOT_WINDOW_MS for the magic string.
// Send it and the bootloader stays up; stay quiet and it starts the application
// at 0x2000 (if one is present). Flash the bootloader and the application at
// their own addresses; they never overlap.
//
// Protocol — line commands, replies end with \r\n:
//
//   HT5023BOOT           enter/stay in the bootloader
//                        -> "HT5023 bootloader v1 app=0x00002000 ..."
//   I                    identify        -> same banner
//   E <hexaddr> <hexlen> erase that range (page granular)   -> "OK"
//   W <hexaddr> <hexlen> write; the payload bytes follow raw -> "OK <hexsum>"
//   J                    jump to the application            -> "GO"
//
// A write is acknowledged with the 16-bit sum of the bytes as programmed, read
// back out of flash, so the host can verify every chunk.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ht5023/uart.h"
#include "ht5023/core/system.h"

// ===========================================================================
// CONFIG
// ===========================================================================
#ifndef BOOT_BAUD
#define BOOT_BAUD 115200u
#endif

#ifndef BOOT_SYSCLK_HZ
#define BOOT_SYSCLK_HZ HT5023_FSYS_HZ
#endif

// How long to wait for the magic string after reset before starting the app.
// Generous on purpose: entry is a race against however fast the host can get
// its magic out after you press reset, and a second of boot delay costs little.
#ifndef BOOT_WINDOW_MS
#define BOOT_WINDOW_MS 1500u
#endif

// Where the application lives. Must match APP_BASE in startup_boot.s and the
// application's linker script.
#ifndef BOOT_APP_BASE
#define BOOT_APP_BASE 0x00002000u
#endif

#ifndef BOOT_FLASH_END
#define BOOT_FLASH_END 0x00040000u
#endif

// Erase granularity. The datasheet documents page erase without stating the
// code-flash page size, so this stays conservative: erasing the same physical
// page twice is harmless because the whole range is erased before anything is
// programmed.
#ifndef BOOT_PAGE
#define BOOT_PAGE 256u
#endif

#ifndef BOOT_CHUNK_MAX
#define BOOT_CHUNK_MAX 256u
#endif

// Busy-loop iterations per microsecond at BOOT_SYSCLK_HZ.
#ifndef BOOT_LOOPS_PER_US
#define BOOT_LOOPS_PER_US 2u
#endif

#define BOOT_MAGIC "HT5023BOOT"

// ===========================================================================
// Flash controller (CMU block)
// ===========================================================================
#define CMU_BASE 0x4000F000u
#define REG16(a) (*(volatile uint16_t*)(a))
#define REG32(a) (*(volatile uint32_t*)(a))

#define WPREG REG16(CMU_BASE + 0x00u)
#define FLASHCON REG16(CMU_BASE + 0x34u)
#define FLASHLOCK REG16(CMU_BASE + 0x38u)

#define WP_KEY 0xA55Au
#define FLASH_UNLOCK 0x7A68u
#define FLASHCON_PROGRAM 0x01u
#define FLASHCON_PAGE_ERASE 0x02u
#define FLASHCON_IDLE 0x00u

// Sector-lock keys: these gate programming, and require FLASHLOCK to hold
// something other than 0x7A68 (datasheet 2.3.2).
static const uint16_t FS_LOCK_OFFSET[] = {0x60u, 0x64u, 0x68u, 0x6Cu, 0x70u, 0x74u,
                                          0x78u, 0x7Cu, 0x80u, 0x84u, 0x88u};
static const uint16_t FS_LOCK_KEY[] = {0x7161u, 0x7262u, 0x7363u, 0x7464u, 0x7565u,
                                       0x7666u, 0x7767u, 0x7868u, 0x7969u, 0x7A6Au,
                                       0x7B6Bu};

// The datasheet gives maximum times rather than a documented BUSY bit position,
// so waits are fixed and generous: 20 us to program a word, 2 ms to erase a page.
static void delay_us(uint32_t us) {
  for (volatile uint32_t i = 0; i < us * BOOT_LOOPS_PER_US; ++i) {
    __asm__ volatile("nop");
  }
}

static void delay_ms(uint32_t ms) {
  while (ms--) delay_us(1000u);
}

static void flash_unlock_sectors(void) {
  WPREG = WP_KEY;
  FLASHLOCK = 0x0000u;  // must not be 0x7A68 for the sector keys to apply
  for (uint32_t i = 0; i < sizeof(FS_LOCK_OFFSET) / sizeof(FS_LOCK_OFFSET[0]); ++i) {
    REG16(CMU_BASE + FS_LOCK_OFFSET[i]) = FS_LOCK_KEY[i];
  }
}

// The watchdog runs off the low-frequency RC. When the LRC is left enabled --
// which it must be for the LCD peripheral to have a clock -- the watchdog is
// live from reset, and the bootloader's own waiting is long enough to trip it.
// A board that resets in a loop and never reaches the application looks exactly
// like a dead bootloader, so every waiting loop in here feeds it.
#define BOOT_WDT_CLR  0x40010004u
#define BOOT_WDT_KEY  0xAAu

static void boot_wdt_feed(void) {
  *(volatile uint8_t*)BOOT_WDT_CLR = BOOT_WDT_KEY;
}

static void flash_erase_page(uint32_t addr) {
  boot_wdt_feed();
  WPREG = WP_KEY;
  FLASHLOCK = FLASH_UNLOCK;  // full erase rights over the array
  FLASHCON = FLASHCON_PAGE_ERASE;
  REG32(addr) = 0xFFFFFFFFu;  // any write triggers the erase
  delay_ms(3u);               // datasheet: 2 ms max
  FLASHCON = FLASHCON_IDLE;
  FLASHLOCK = 0x0000u;
}

static void flash_write_word(uint32_t addr, uint32_t word) {
  flash_unlock_sectors();
  FLASHCON = FLASHCON_PROGRAM;
  REG32(addr) = word;
  delay_us(40u);  // datasheet: 20 us max
  FLASHCON = FLASHCON_IDLE;
}

// ===========================================================================
// UART helpers
// ===========================================================================
#define UART_ID HT5023_UART_ID_2

static void put(const char* s) { ht5023_uart_write_str(UART_ID, s); }

static void put_line(const char* s) {
  put(s);
  put("\r\n");
}

static void put_hex32(uint32_t v) {
  static const char hex[] = "0123456789ABCDEF";
  char out[11];
  out[0] = '0';
  out[1] = 'x';
  for (int i = 0; i < 8; ++i) out[2 + i] = hex[(v >> (28 - 4 * i)) & 0xFu];
  out[10] = '\0';
  put(out);
}

// Polls for one byte with a spin budget rather than a delay loop.
//
// The receiver holds a single byte and a payload arrives at one byte every
// ~87 us at 115200, so the poll must never pause: an earlier version spun for
// ~1 ms and then slept 900 us, which silently dropped whatever landed during
// the sleep. The count is calibrated so BOOT_SPINS_PER_MS iterations take about
// a millisecond at BOOT_SYSCLK_HZ.
#ifndef BOOT_SPINS_PER_MS
#define BOOT_SPINS_PER_MS 400u
#endif

static bool get_byte(uint8_t* out, uint32_t ms) {
  uint32_t spins = ms * BOOT_SPINS_PER_MS;
  while (spins--) {
    if (ht5023_uart_try_read_byte(UART_ID, out)) return true;
    if ((spins & 0x3FFu) == 0u) boot_wdt_feed();
  }
  return false;
}

// Reads a line (up to CR/LF). Returns false on timeout.
static bool get_line(char* buf, uint32_t cap, uint32_t ms) {
  uint32_t n = 0;
  for (;;) {
    uint8_t c;
    if (!get_byte(&c, ms)) return false;
    if (c == '\r' || c == '\n') {
      if (n == 0u) continue;  // tolerate CRLF and blank lines
      buf[n] = '\0';
      return true;
    }
    if (n + 1u < cap) buf[n++] = (char)c;
  }
}

static uint32_t parse_hex(const char** p) {
  uint32_t v = 0;
  const char* s = *p;
  while (*s == ' ') ++s;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
  while (1) {
    const char c = *s;
    uint32_t d;
    if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
    else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
    else break;
    v = (v << 4) | d;
    ++s;
  }
  *p = s;
  return v;
}

static bool starts_with(const char* s, const char* prefix) {
  while (*prefix) {
    if (*s++ != *prefix++) return false;
  }
  return true;
}

// ===========================================================================
// Application handover
// ===========================================================================
volatile uint32_t g_app_running;  // read by Trampoline in startup_boot.s

extern void boot_jump_to_app(uint32_t sp, uint32_t pc);

// An application is present when its vector table looks sane: a stack pointer
// inside RAM and a reset handler inside the application region.
static bool app_present(void) {
  const uint32_t sp = REG32(BOOT_APP_BASE);
  const uint32_t pc = REG32(BOOT_APP_BASE + 4u);
  if (sp < 0x20000000u || sp > 0x20008000u) return false;
  if (pc < BOOT_APP_BASE || pc >= BOOT_FLASH_END) return false;
  return (pc & 1u) != 0u;  // Thumb bit
}

static void start_app(void) {
  const uint32_t sp = REG32(BOOT_APP_BASE);
  const uint32_t pc = REG32(BOOT_APP_BASE + 4u);
  g_app_running = 1u;  // from now on exceptions go to the application's table
  // Masked only across the switch itself; boot_jump_to_app unmasks again before
  // it branches, because the application expects post-reset state.
  __asm__ volatile("cpsid i");
  boot_jump_to_app(sp, pc);
}

// ===========================================================================
static void banner(void) {
  put("HT5023 bootloader v1 app=");
  put_hex32(BOOT_APP_BASE);
  put(" end=");
  put_hex32(BOOT_FLASH_END);
  put(" page=");
  put_hex32(BOOT_PAGE);
  put_line("");
}

static void cmd_erase(const char* args) {
  const char* p = args;
  uint32_t addr = parse_hex(&p);
  uint32_t len = parse_hex(&p);

  if (addr < BOOT_APP_BASE || addr + len > BOOT_FLASH_END || len == 0u) {
    put_line("ERR range");
    return;
  }
  const uint32_t first = addr & ~(BOOT_PAGE - 1u);
  for (uint32_t a = first; a < addr + len; a += BOOT_PAGE) {
    flash_erase_page(a);
  }
  put_line("OK");
}

static void cmd_write(const char* args) {
  static uint8_t buf[BOOT_CHUNK_MAX];
  const char* p = args;
  uint32_t addr = parse_hex(&p);
  uint32_t len = parse_hex(&p);

  if (addr < BOOT_APP_BASE || addr + len > BOOT_FLASH_END) {
    put_line("ERR range");
    return;
  }
  if (len == 0u || len > BOOT_CHUNK_MAX || (addr & 3u) != 0u) {
    put_line("ERR length");
    return;
  }

  for (uint32_t i = 0; i < len; ++i) {
    if (!get_byte(&buf[i], 500u)) {
      put_line("ERR timeout");
      return;
    }
  }
  while ((len & 3u) != 0u) buf[len++] = 0xFFu;  // pad to a word

  for (uint32_t i = 0; i < len; i += 4u) {
    const uint32_t w = (uint32_t)buf[i] | ((uint32_t)buf[i + 1] << 8) |
                       ((uint32_t)buf[i + 2] << 16) | ((uint32_t)buf[i + 3] << 24);
    if (w != 0xFFFFFFFFu) flash_write_word(addr + i, w);
  }

  // Sum what actually landed in flash, so the host verifies the device rather
  // than its own copy of the data.
  uint16_t sum = 0;
  for (uint32_t i = 0; i < len; ++i) {
    sum = (uint16_t)(sum + ((const volatile uint8_t*)(uintptr_t)addr)[i]);
  }
  put("OK ");
  put_hex32(sum);
  put_line("");
}

int main(void) {
  const ht5023_uart_config_t cfg = {
    .sysclk_hz = BOOT_SYSCLK_HZ,
    .baud = BOOT_BAUD,
    .enable_tx = true,
    .enable_rx = true,
  };
  ht5023_uart2_pins_init_pc11_pc12();
  (void)ht5023_uart_init(UART_ID, &cfg);

  banner();
  put_line("send " BOOT_MAGIC " to stay in the bootloader");

  // Entry window: any line containing the magic keeps us here.
  char line[80];
  bool stay = false;
  for (uint32_t tries = 0; tries < 4u && !stay; ++tries) {
    if (!get_line(line, sizeof(line), BOOT_WINDOW_MS / 4u)) break;
    if (starts_with(line, BOOT_MAGIC)) stay = true;
  }

  if (!stay) {
    if (app_present()) {
      put_line("starting application");
      start_app();
    }
    put_line("no application present -- staying in the bootloader");
  }

  banner();
  for (;;) {
    if (!get_line(line, sizeof(line), 60000u)) continue;

    if (starts_with(line, BOOT_MAGIC) || line[0] == 'I') {
      banner();
    } else if (line[0] == 'E') {
      cmd_erase(line + 1);
    } else if (line[0] == 'W') {
      cmd_write(line + 1);
    } else if (line[0] == 'J') {
      if (!app_present()) {
        put_line("ERR no application");
        continue;
      }
      put_line("GO");
      delay_ms(20u);  // let the reply drain before the UART goes away
      // Reset rather than jump straight in. After a programming session the
      // flash controller, UART and CMU write protection are all in whatever
      // state the last command left them, and starting the application on top
      // of that proved unreliable -- it faulted and locked the core up. A reset
      // gives it exactly the same clean machine it gets after power-on; the
      // bootloader then hands over through its normal timeout path.
      REG32(0xE000ED0Cu) = 0x05FA0004u;  // AIRCR.SYSRESETREQ
      for (;;) {
      }
    } else {
      put_line("ERR command");
    }
  }
}
