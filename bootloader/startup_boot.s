/* startup_boot.s — bootloader startup and interrupt forwarding
 *
 * The Cortex-M0 in this part is ARMv6-M and has no VTOR, so the vector table is
 * permanently at 0x00000000 and belongs to whoever owns the reset vector. The
 * bootloader therefore keeps the real table and forwards exceptions to the
 * application's own table at APP_BASE once the application is running.
 */
.syntax unified
.cpu cortex-m0
.thumb

.equ APP_BASE, 0x00002000

.section .isr_vector, "a", %progbits
.global __Vectors
.global __Vectors_End
__Vectors:
.word  _estack                 /* Initial SP                       */
.word  Reset_Handler           /* Reset: always the bootloader     */
.word  Trampoline              /* NMI                              */
.word  Trampoline              /* HardFault                        */
.word  Trampoline, Trampoline, Trampoline, Trampoline
.word  Trampoline, Trampoline, Trampoline
.word  Trampoline              /* SVC                              */
.word  Trampoline, Trampoline
.word  Trampoline              /* PendSV                           */
.word  Trampoline              /* SysTick                          */
/* 32 peripheral interrupts */
.word  Trampoline, Trampoline, Trampoline, Trampoline
.word  Trampoline, Trampoline, Trampoline, Trampoline
.word  Trampoline, Trampoline, Trampoline, Trampoline
.word  Trampoline, Trampoline, Trampoline, Trampoline
.word  Trampoline, Trampoline, Trampoline, Trampoline
.word  Trampoline, Trampoline, Trampoline, Trampoline
.word  Trampoline, Trampoline, Trampoline, Trampoline
.word  Trampoline, Trampoline, Trampoline, Trampoline
__Vectors_End:

/* Forwards an exception to the application's vector table.
 *
 * Only once g_app_running is set: before that (while the bootloader itself is
 * working) there may be no valid application at all, so a fault stops here
 * instead of branching into unprogrammed flash.
 *
 * r0-r3 are already stacked by the hardware on exception entry, so clobbering
 * them is safe, and LR still holds EXC_RETURN for the application's handler.
 */
.section .text.Trampoline
.thumb_func
.global Trampoline
Trampoline:
  ldr  r0, =g_app_running
  ldr  r0, [r0]
  cmp  r0, #0
  beq  .Lhang
  mrs  r0, ipsr             /* exception number */
  lsls r0, r0, #2           /* -> table offset  */
  ldr  r1, =APP_BASE
  adds r0, r0, r1
  ldr  r0, [r0]
  cmp  r0, #0               /* empty slot: nothing to forward to */
  beq  .Lhang
  movs r1, #1               /* branching to an even address switches to ARM   */
  orrs r0, r1               /* state and locks the core up, so force Thumb    */
  bx   r0
.Lhang:
  b    .Lhang

.section .text.Reset_Handler
.global Reset_Handler
.thumb_func
Reset_Handler:
  /* Copy .data */
  ldr r0, =__data_load__
  ldr r1, =__data_start__
  ldr r2, =__data_end__
1:
  cmp r1, r2
  bhs 2f
  ldr r3, [r0]
  adds r0, #4
  str r3, [r1]
  adds r1, #4
  b 1b
2:
  /* Zero .bss */
  ldr r0, =__bss_start__
  ldr r1, =__bss_end__
  movs r2, #0
3:
  cmp r0, r1
  bhs 4f
  str r2, [r0]
  adds r0, #4
  b 3b
4:
  bl  SystemInit
  bl  main
5:
  b 5b

.section .text.boot_jump_to_app
.global boot_jump_to_app
.thumb_func
/* void boot_jump_to_app(uint32_t sp, uint32_t pc); */
boot_jump_to_app:
  msr  msp, r0
  /* The caller masks interrupts so nothing can fire while g_app_running and
   * the stack pointer are being swapped. Unmask again here: the application
   * starts expecting the state it would have after a reset, and one left
   * masked means no SysTick -- millis() frozen and delay() never returning. */
  cpsie i
  bx   r1
