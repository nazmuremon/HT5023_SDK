/* startup_ht5023.s — minimal Cortex-M0 startup */
.syntax unified
.cpu cortex-m0
.thumb

.section .isr_vector, "a", %progbits
.global g_pfnVectors
.type   g_pfnVectors, %object
g_pfnVectors:
.word  _estack                 /* Initial SP */
.word  Reset_Handler           /* Reset */
.word  NMI_Handler
.word  HardFault_Handler
.word  0,0,0,0,0,0,0           /* Reserved */
.word  SVC_Handler
.word  0,0                     /* Reserved */
.word  PendSV_Handler
.word  SysTick_Handler
.size   g_pfnVectors, .-g_pfnVectors

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

  /* Copy .ramfunc */
  ldr r0, =__ramfunc_load__
  ldr r1, =__ramfunc_start__
  ldr r2, =__ramfunc_end__
3:
  cmp r1, r2
  bhs 4f
  ldr r3, [r0]
  adds r0, #4
  str r3, [r1]
  adds r1, #4
  b 3b
4:

  /* Zero .bss */
  ldr r0, =__bss_start__
  ldr r1, =__bss_end__
  movs r2, #0
5:
  cmp r0, r1
  bhs 6f
  str r2, [r0]
  adds r0, #4
  b 5b
6:

  bl  SystemInit
  bl  main

.thumb_func
Default_Handler:
  b .

.weak NMI_Handler, HardFault_Handler, SVC_Handler, PendSV_Handler, SysTick_Handler
NMI_Handler:        b Default_Handler
HardFault_Handler:  b Default_Handler
SVC_Handler:        b Default_Handler
PendSV_Handler:     b Default_Handler
SysTick_Handler:    b Default_Handler
