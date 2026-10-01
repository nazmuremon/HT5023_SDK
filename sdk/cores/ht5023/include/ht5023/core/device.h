#pragma once

#include <stdint.h>

#define HT5023_DEVICE 1
#define HT5023_FLASH_SIZE_BYTES      (256u * 1024u)
#define HT5023_INFO_FLASH_SIZE_BYTES (1u   * 1024u)
#define HT5023_SRAM_SIZE_BYTES       (32u  * 1024u)
#define HT5023_CORE_CORTEX_M0 1

/* CMSIS-compatible core configuration (Cortex-M0). */
#ifndef __CM0_REV
#define __CM0_REV               0x0000u
#endif
#ifndef __MPU_PRESENT
#define __MPU_PRESENT           0u
#endif
#ifndef __NVIC_PRIO_BITS
#define __NVIC_PRIO_BITS        2u
#endif
#ifndef __Vendor_SysTickConfig
#define __Vendor_SysTickConfig  0u
#endif
#ifndef __VTOR_PRESENT
#define __VTOR_PRESENT          0u
#endif

/* CMSIS-style compiler qualifiers/macros (minimal subset used by drivers/apps). */
#ifndef __I
#define __I  volatile const
#endif
#ifndef __O
#define __O  volatile
#endif
#ifndef __IO
#define __IO volatile
#endif
#ifndef __IM
#define __IM volatile const
#endif
#ifndef __OM
#define __OM volatile
#endif
#ifndef __IOM
#define __IOM volatile
#endif

#ifndef __ASM
#define __ASM __asm
#endif
#ifndef __INLINE
#define __INLINE inline
#endif
#ifndef __STATIC_INLINE
#define __STATIC_INLINE static inline
#endif
#ifndef __STATIC_FORCEINLINE
#define __STATIC_FORCEINLINE __attribute__((always_inline)) static inline
#endif
#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif
#ifndef __PACKED
#define __PACKED __attribute__((packed))
#endif
#ifndef __ALIGNED
#define __ALIGNED(x) __attribute__((aligned(x)))
#endif
#ifndef __NO_RETURN
#define __NO_RETURN __attribute__((noreturn))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* CMSIS-compatible exception numbers. Device IRQs are not enumerated yet. */
typedef enum IRQn {
  NonMaskableInt_IRQn = -14,
  HardFault_IRQn      = -13,
  SVCall_IRQn         = -5,
  PendSV_IRQn         = -2,
  SysTick_IRQn        = -1,
} IRQn_Type;

/* Memory-mapped register accessors.
 * These are the only correct way to read/write peripheral registers on HT5023.
 * Using uintptr_t for the intermediate cast avoids pointer-from-integer warnings
 * on targets where sizeof(pointer) != sizeof(uint32_t). */
__STATIC_INLINE volatile uint16_t* ht5023_reg16(uint32_t base, uint32_t off) {
  return (volatile uint16_t*)(uintptr_t)(base + off);
}
__STATIC_INLINE volatile uint32_t* ht5023_reg32(uint32_t base, uint32_t off) {
  return (volatile uint32_t*)(uintptr_t)(base + off);
}

#ifdef __cplusplus
}
#endif
