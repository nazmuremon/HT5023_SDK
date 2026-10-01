/* ht5023.ld — 256K Flash, 32K RAM */
MEMORY
{
  /* 0x0FC0-0x0FC3 holds the flash option bytes: a non-0xFF value at
     0x0FC1 enables read protection, which bricks debugging until a mass
     erase. Keep the image out of that word by starting after it. */
  FLASH (rx)  : ORIGIN = 0x00001000, LENGTH = 256K - 0x1000
  VECTORS (rx): ORIGIN = 0x00000000, LENGTH = 0x0FC0
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 32K
}

ENTRY(Reset_Handler)

SECTIONS
{
  .isr_vector : {
    KEEP(*(.isr_vector))
  } > VECTORS

  .text : {
    *(.text*) *(.rodata*) *(.glue_7) *(.glue_7t)
    *(.eh_frame*)
  } > FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH

  .ramfunc ALIGN(4) : AT (ADDR(.text) + SIZEOF(.text)) {
    __ramfunc_start__ = .;
    *(.ramfunc*)
    __ramfunc_end__ = .;
  } > RAM
  __ramfunc_load__ = LOADADDR(.ramfunc);

  .data : AT (ADDR(.ramfunc) + SIZEOF(.ramfunc)) {
    __data_load__ = LOADADDR(.data);
    __data_start__ = .;
    *(.data*)
    __data_end__ = .;
  } > RAM

  .bss (NOLOAD) : {
    __bss_start__ = .;
    *(.bss*) *(COMMON)
    __bss_end__ = .;
  } > RAM

  . = ALIGN(8);
  _estack = ORIGIN(RAM) + LENGTH(RAM);
}
