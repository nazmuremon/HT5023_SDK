/* boot.ld — bootloader occupies the first 8 KB; the application starts at 0x2000
 *
 * The flash option bytes live at 0x0FC0-0x0FC3 and a non-0xFF value at 0x0FC1
 * turns on read protection, which stops the part being read or debugged until a
 * mass erase. So the vector table stays in its own region below that word and
 * the bootloader body starts after it, exactly as the application linker script
 * does.
 */
MEMORY
{
  VECTORS (rx) : ORIGIN = 0x00000000, LENGTH = 0x0FC0
  FLASH   (rx) : ORIGIN = 0x00001000, LENGTH = 0x1000   /* up to 0x1FFF */
  RAM     (rwx): ORIGIN = 0x20000000, LENGTH = 32K
}

ENTRY(Reset_Handler)

SECTIONS
{
  .isr_vector : {
    KEEP(*(.isr_vector))
  } > VECTORS

  .text : {
    *(.text*) *(.rodata*) *(.glue_7) *(.glue_7t)
    . = ALIGN(4);
  } > FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH

  .data : AT (ADDR(.text) + SIZEOF(.text) + SIZEOF(.ARM.extab) + SIZEOF(.ARM.exidx)) {
    __data_load__ = LOADADDR(.data);
    __data_start__ = .;
    *(.data*)
    . = ALIGN(4);
    __data_end__ = .;
  } > RAM

  .bss : {
    __bss_start__ = .;
    *(.bss*) *(COMMON)
    . = ALIGN(4);
    __bss_end__ = .;
  } > RAM

  _estack = ORIGIN(RAM) + LENGTH(RAM);

  /* Fail the build rather than silently overrunning into the application. */
  ASSERT(SIZEOF(.text) + SIZEOF(.ARM.extab) + SIZEOF(.ARM.exidx) + SIZEOF(.data)
         <= LENGTH(FLASH),
         "bootloader does not fit below the application base at 0x2000")
}
