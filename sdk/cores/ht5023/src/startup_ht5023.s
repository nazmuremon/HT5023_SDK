/* startup_ht5023.s — minimal Cortex-M0 startup */
.syntax unified
.cpu cortex-m0
.thumb

.section .isr_vector, "a", %progbits
.global g_pfnVectors
.global __Vectors
.global __Vectors_End
.global __Vectors_Size
.type   g_pfnVectors, %object
g_pfnVectors:
__Vectors:
.word  _estack                 /* Initial SP */
.word  Reset_Handler           /* Reset */
.word  NMI_Handler
.word  HardFault_Handler
.word  0,0,0,0,0,0,0           /* Reserved */
.word  SVC_Handler
.word  0,0                     /* Reserved */
.word  PendSV_Handler
.word  SysTick_Handler
/* Peripheral interrupts, IRQ 0..31, numbered as in HT502X User Manual 8.1
   ("Interrupt vector introduction"). Every entry is a weak alias for
   Default_Handler below, so an application only defines the ones it uses and
   nothing changes for code that never enables an interrupt. 23, 24 and 31 are
   not assigned in the manual. */
.word  PMU_IRQHandler          /*  0 */
.word  AES_IRQHandler          /*  1  AES / GHASH / RAND / ECC */
.word  EXTI0_IRQHandler        /*  2 */
.word  EXTI1_IRQHandler        /*  3 */
.word  EXTI2_IRQHandler        /*  4 */
.word  EXTI3_IRQHandler        /*  5 */
.word  EXTI4_IRQHandler        /*  6 */
.word  EXTI5_IRQHandler        /*  7 */
.word  EXTI6_IRQHandler        /*  8 */
.word  UART0_IRQHandler        /*  9 */
.word  UART1_IRQHandler        /* 10 */
.word  UART2_IRQHandler        /* 11 */
.word  UART3_IRQHandler        /* 12 */
.word  UART4_IRQHandler        /* 13 */
.word  UART5_IRQHandler        /* 14 */
.word  TMR0_IRQHandler         /* 15 */
.word  TMR1_IRQHandler         /* 16 */
.word  TMR2_IRQHandler         /* 17 */
.word  TMR3_IRQHandler         /* 18 */
.word  TBS_IRQHandler          /* 19 */
.word  RTC_IRQHandler          /* 20 */
.word  I2C_IRQHandler          /* 21 */
.word  SPI_IRQHandler          /* 22 */
.word  0                       /* 23  not assigned */
.word  0                       /* 24  not assigned */
.word  EMU_IRQHandler          /* 25 */
.word  DMA_IRQHandler          /* 26 */
.word  KEY_IRQHandler          /* 27 */
.word  EXTI7_IRQHandler        /* 28 */
.word  EXTI8_IRQHandler        /* 29 */
.word  EXTI9_IRQHandler        /* 30 */
.word  0                       /* 31  not assigned */
__Vectors_End:
.equ __Vectors_Size, __Vectors_End - __Vectors
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

/* Weak aliases rather than `Label: b Default_Handler`: a plain label is not a
   Thumb function, so the vector held an EVEN address and taking the exception
   faulted into lockup instead of reaching Default_Handler. .thumb_set carries
   the Thumb bit across. */
.weak NMI_Handler
.thumb_set NMI_Handler, Default_Handler
.weak HardFault_Handler
.thumb_set HardFault_Handler, Default_Handler
.weak SVC_Handler
.thumb_set SVC_Handler, Default_Handler
.weak PendSV_Handler
.thumb_set PendSV_Handler, Default_Handler
.weak SysTick_Handler
.thumb_set SysTick_Handler, Default_Handler

.weak PMU_IRQHandler
.thumb_set PMU_IRQHandler, Default_Handler
.weak AES_IRQHandler
.thumb_set AES_IRQHandler, Default_Handler
.weak EXTI0_IRQHandler
.thumb_set EXTI0_IRQHandler, Default_Handler
.weak EXTI1_IRQHandler
.thumb_set EXTI1_IRQHandler, Default_Handler
.weak EXTI2_IRQHandler
.thumb_set EXTI2_IRQHandler, Default_Handler
.weak EXTI3_IRQHandler
.thumb_set EXTI3_IRQHandler, Default_Handler
.weak EXTI4_IRQHandler
.thumb_set EXTI4_IRQHandler, Default_Handler
.weak EXTI5_IRQHandler
.thumb_set EXTI5_IRQHandler, Default_Handler
.weak EXTI6_IRQHandler
.thumb_set EXTI6_IRQHandler, Default_Handler
.weak UART0_IRQHandler
.thumb_set UART0_IRQHandler, Default_Handler
.weak UART1_IRQHandler
.thumb_set UART1_IRQHandler, Default_Handler
.weak UART2_IRQHandler
.thumb_set UART2_IRQHandler, Default_Handler
.weak UART3_IRQHandler
.thumb_set UART3_IRQHandler, Default_Handler
.weak UART4_IRQHandler
.thumb_set UART4_IRQHandler, Default_Handler
.weak UART5_IRQHandler
.thumb_set UART5_IRQHandler, Default_Handler
.weak TMR0_IRQHandler
.thumb_set TMR0_IRQHandler, Default_Handler
.weak TMR1_IRQHandler
.thumb_set TMR1_IRQHandler, Default_Handler
.weak TMR2_IRQHandler
.thumb_set TMR2_IRQHandler, Default_Handler
.weak TMR3_IRQHandler
.thumb_set TMR3_IRQHandler, Default_Handler
.weak TBS_IRQHandler
.thumb_set TBS_IRQHandler, Default_Handler
.weak RTC_IRQHandler
.thumb_set RTC_IRQHandler, Default_Handler
.weak I2C_IRQHandler
.thumb_set I2C_IRQHandler, Default_Handler
.weak SPI_IRQHandler
.thumb_set SPI_IRQHandler, Default_Handler
.weak EMU_IRQHandler
.thumb_set EMU_IRQHandler, Default_Handler
.weak DMA_IRQHandler
.thumb_set DMA_IRQHandler, Default_Handler
.weak KEY_IRQHandler
.thumb_set KEY_IRQHandler, Default_Handler
.weak EXTI7_IRQHandler
.thumb_set EXTI7_IRQHandler, Default_Handler
.weak EXTI8_IRQHandler
.thumb_set EXTI8_IRQHandler, Default_Handler
.weak EXTI9_IRQHandler
.thumb_set EXTI9_IRQHandler, Default_Handler
