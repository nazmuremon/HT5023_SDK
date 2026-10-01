/* ht5023_app.ld -- sketch that runs under the UART bootloader.
 *
 * The bootloader owns the first 8 KB and forwards exceptions to the vector
 * table here at 0x2000 (the Cortex-M0 has no VTOR to relocate it). Upload with
 * tools/ht5023_uart_flash.py or the Flasher's 'USB (bootloader)' option.
 */
MEMORY
{
  VECTORS (rx) : ORIGIN = 0x00002000, LENGTH = 0x100
  FLASH   (rx) : ORIGIN = 0x00002100, LENGTH = 256K - 0x2100
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
