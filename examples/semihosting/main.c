// examples/semihosting/main.c — ARM semihosting "hello" over the J-Link debug
// channel (no UART wiring needed). View the output in the debugger's semihosting
// console (J-Link GDB Server / RTT-less semihosting), e.g.:
//
//   JLinkGDBServerCL -device Cortex-M0 -if SWD -speed 1000
//   arm-none-eabi-gdb build/app.elf -ex "target remote :2331" \
//       -ex "monitor semihosting enable"
//
// Semihosting works ONLY while a debugger is attached: a `bkpt 0xAB` with no
// debugger halts into a HardFault. So this example guards every semihosting call
// behind a "debugger attached" check (DHCSR.C_DEBUGEN) and an optional override
// the debugger can set. With no debugger it just blinks the LEDs — safe to flash
// standalone.

#include <stdint.h>

extern void led_gpio_init(void);
extern void led_toggle(void);

// --- ARM semihosting primitive (Cortex-M halt-mode uses BKPT 0xAB) -----------
#define SYS_WRITE0 0x04u  // r1 -> pointer to NUL-terminated string

static int semihost(int op, void *arg) {
  register int r0 __asm__("r0") = op;
  register void *r1 __asm__("r1") = arg;
  __asm__ volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory", "cc");
  return r0;
}

static void sh_write0(const char *s) { (void)semihost(SYS_WRITE0, (void *)s); }

// --- "is a debugger attached?" (ARMv6-M Debug Halting Control/Status Reg) -----
#define DHCSR (*(volatile uint32_t *)0xE000EDF0u)
#define C_DEBUGEN (1u << 0)

// Lets an attached debugger force the semihosting path on (belt-and-suspenders
// for automated tests): `set var g_force_semihost = 1` after reaching main().
volatile int g_force_semihost = 0;

static int debugger_present(void) {
  return ((DHCSR & C_DEBUGEN) != 0u) || g_force_semihost;
}

// --- tiny unsigned -> decimal (avoids pulling in newlib printf) ---------------
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

static void delay_cycles(uint32_t cycles) {
  for (volatile uint32_t i = 0; i < cycles; ++i) {
    __asm__ volatile("nop");
  }
}

int main(void) {
  led_gpio_init();

  if (debugger_present()) {
    sh_write0("HT5023 semihosting: hello from the target!\r\n");

    for (uint32_t tick = 0; tick < 10u; ++tick) {
      char line[48];
      char *p = line;
      const char *pre = "HT5023 semihosting tick=";
      while (*pre) *p++ = *pre++;
      p = u32_to_str(tick, p);
      *p++ = '\r';
      *p++ = '\n';
      *p = '\0';
      sh_write0(line);

      led_toggle();
      delay_cycles(50000);
    }

    sh_write0("HT5023 semihosting: done.\r\n");

    // Normal breakpoint (NOT 0xAB) -> returns control to GDB so a batch/test
    // run can detect completion and exit cleanly.
    __asm__ volatile("bkpt #0");
  }

  // Standalone heartbeat (and what runs after the debugger detaches).
  while (1) {
    led_toggle();
    delay_cycles(250000);
  }
}
