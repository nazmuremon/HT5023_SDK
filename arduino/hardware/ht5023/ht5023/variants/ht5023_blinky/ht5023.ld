/* ht5023.ld -- standalone sketch, programmed with the J-Link.
 *
 * 0x0FC0-0x0FC3 holds the flash option bytes and a non-0xFF value at 0x0FC1
 * enables read protection, which stops the part being read or debugged until a
 * mass erase. The vector table therefore lives in its own region below that
 * word and the code starts after it.
 */
MEMORY
{
  VECTORS (rx) : ORIGIN = 0x00000000, LENGTH = 0x0FC0
  FLASH   (rx) : ORIGIN = 0x00001000, LENGTH = 256K - 0x1000
  RAM     (rwx): ORIGIN = 0x20000000, LENGTH = 32K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector :
  {
    KEEP(*(.isr_vector))
  } > VECTORS

  .text :
  {
    *(.text*)
    *(.rodata*)
    *(.glue_7)
    *(.glue_7t)
    KEEP(*(.init))
    KEEP(*(.fini))
  } > FLASH

  .ARM.extab :
  {
    *(.ARM.extab*)
  } > FLASH

  .ARM.exidx :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } > FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } > FLASH

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } > FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } > FLASH

  __data_load__ = LOADADDR(.data);
  .data : AT (ADDR(.fini_array) + SIZEOF(.fini_array))
  {
    __data_start__ = .;
    *(.data*)
    __data_end__ = .;
  } > RAM

  __ramfunc_load__ = LOADADDR(.ramfunc);
  .ramfunc :
  {
    __ramfunc_start__ = .;
    *(.ramfunc*)
    __ramfunc_end__ = .;
  } > RAM AT > FLASH

  .bss (NOLOAD) :
  {
    __bss_start__ = .;
    *(.bss*)
    *(COMMON)
    __bss_end__ = .;
  } > RAM

  . = ALIGN(8);
  end = .;
  _end = .;
}
