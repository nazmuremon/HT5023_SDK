// examples/semihost_rng/main.c — ARM semihosting random-number demo.
//
// Generates pseudo-random numbers with an xorshift32 PRNG (seeded from the
// free-running SysTick counter) and prints them over the J-Link debug channel
// via semihosting (bkpt 0xAB / SYS_WRITE0). No UART wiring needed.
//
// View with the GUI flasher's "Semihosting Monitor", or:
//   JLinkGDBServerCL -device Cortex-M0 -if SWD -speed 1000 -port 2331
//   arm-none-eabi-gdb build/app.elf -ex "target remote :2331" \
//       -ex "monitor reset" -ex "monitor semihosting enable" \
//       -ex "monitor semihosting IOClient 3" -ex "continue"
//
// Like the `semihosting` example, every semihosting call is guarded by a
// debugger-attached check, so flashing this standalone is safe (it just blinks).

#include <stdint.h>

extern void led_gpio_init(void);
extern void led_toggle(void);

// --- ARM semihosting primitive (Cortex-M halt-mode uses BKPT 0xAB) -----------
#define SYS_WRITE0 0x04u

static int semihost(int op, void *arg) {
  register int r0 __asm__("r0") = op;
  register void *r1 __asm__("r1") = arg;
  __asm__ volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory", "cc");
  return r0;
}

static void sh_write0(const char *s) { (void)semihost(SYS_WRITE0, (void *)s); }

// --- "is a debugger attached?" guard -----------------------------------------
#define DHCSR (*(volatile uint32_t *)0xE000EDF0u)
#define C_DEBUGEN (1u << 0)
volatile int g_force_semihost = 0;

static int debugger_present(void) {
  return ((DHCSR & C_DEBUGEN) != 0u) || g_force_semihost;
}

// --- SysTick registers (used only to seed the PRNG) --------------------------
#define SYST_CSR (*(volatile uint32_t *)0xE000E010u)
#define SYST_RVR (*(volatile uint32_t *)0xE000E014u)
#define SYST_CVR (*(volatile uint32_t *)0xE000E018u)

// --- xorshift32 PRNG ---------------------------------------------------------
static uint32_t rng_state = 0x12345678u;

static uint32_t rng_next(void) {
  uint32_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_state = x;
  return x;
}

// --- helpers -----------------------------------------------------------------
static char *u32_to_str(uint32_t v, char *out) {
  char tmp[10];
  int n = 0;
  if (v == 0u) {
    *out++ = '0';
  } else {
    while (v) {
      tmp[n++] = (char)('0' + (v % 10u));
      v /= 10u;
    }
    while (n) *out++ = tmp[--n];
  }
  *out = '\0';
  return out;
}

static char *append(char *p, const char *s) {
  while (*s) *p++ = *s++;
  return p;
}

static void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; ++i) {
    __asm__ volatile("nop");
  }
}

int main(void) {
  led_gpio_init();

  // Free-run SysTick so its current value gives us a seed.
  SYST_RVR = 0x00FFFFFFu;
  SYST_CVR = 0u;
  SYST_CSR = 0x5u;  // ENABLE | CLKSOURCE=core

  if (debugger_present()) {
    rng_state = SYST_CVR ^ 0xA5A5F00Du;
    if (rng_state == 0u) rng_state = 0xDEADBEEFu;

    sh_write0("HT5023 semihosting RNG: continuous (xorshift32) -- press Stop to end\r\n");

    for (uint32_t i = 0;; ++i) {  // non-stop
      uint32_t r = rng_next();
      char line[64];
      char *p = line;
      p = append(p, "rand[");
      p = u32_to_str(i, p);
      p = append(p, "] = ");
      p = u32_to_str(r, p);
      p = append(p, "  (1..100: ");
      p = u32_to_str((r % 100u) + 1u, p);
      p = append(p, ")\r\n");
      *p = '\0';
      sh_write0(line);

      led_toggle();
      delay_cycles(300000);
    }
  }

  // Standalone / post-detach heartbeat.
  while (1) {
    led_toggle();
    delay_cycles(250000);
  }
}
