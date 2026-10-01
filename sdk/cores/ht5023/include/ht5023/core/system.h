#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- clocks --
 * The HT5023 runs two clocks off the internal HRC (HT502X User Manual 3.2):
 *
 *   Fsys  clocks the peripherals -- every UART, SPI, I2C and timer divider.
 *   Fcpu  clocks the CPU, GPIO and DMA, and therefore SysTick and busy loops.
 *         Fcpu = Fsys >> SYSCLK_DIV[2:0], and SYSCLK_DIV resets to 001, so by
 *         default Fcpu is HALF of Fsys.
 *
 * The manual gives the HRC as 9.5 MHz. On the development board it measures
 * 9.196 MHz: SysTick's reload was read back over SWD and its count timed
 * against wall-clock time, giving Fcpu = 4.597 MHz in two separate firmwares
 * (and matching the Arduino core's earlier independent 4.598 MHz). At 115200
 * baud that is -0.22 % error, against -2.65 % when 9.5 MHz is assumed.
 *
 * The HRC is not trimmed per part. After measuring a different board, override
 * with -DHT5023_FSYS_HZ=<Hz> -- through EXTRA_CFLAGS for the Makefile. */
#if !defined(HT5023_FSYS_HZ) && defined(HT5023_SYSTEM_CORE_CLOCK_HZ)
#define HT5023_FSYS_HZ HT5023_SYSTEM_CORE_CLOCK_HZ   /* the older override name */
#endif
#ifndef HT5023_FSYS_HZ
#define HT5023_FSYS_HZ 9196000u
#endif

/* Fcpu at the reset-default divider, for compile-time constants. If anything
 * may have changed SYSCLK_DIV, use ht5023_fcpu_hz() instead. */
#define HT5023_FCPU_HZ (HT5023_FSYS_HZ >> 1)

/* Fcpu from the live SYSCLKDIV register. */
uint32_t ht5023_fcpu_hz(void);

/* CMSIS: the CPU clock (Fcpu), kept current by SystemCoreClockUpdate(). */
extern uint32_t SystemCoreClock;
void SystemInit(void);
void SystemCoreClockUpdate(void);

/* Board hook, called at the end of SystemInit() -- before main(), with .data
 * and .bss already set up but SysTick not yet started. The core's version does
 * nothing; a board-support package overrides it to put pins that misbehave
 * out of reset into a safe state. */
void ht5023_board_init(void);

#ifdef __cplusplus
}
#endif
