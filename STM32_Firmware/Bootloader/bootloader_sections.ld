/*
 * Bootloader sections, shared by bootloader_f429.ld and bootloader_f411.ld.
 * The vector table sits at the start of sector 0; the boot info block
 * (FW_BOOT_INFO_ADDRESS) at a fixed offset after it.
 */

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector :
  {
    KEEP(*(.isr_vector))
  } >FLASH

  .boot_info ORIGIN(FLASH) + 0x100 :
  {
    KEEP(*(.boot_info))
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.exidx :
  {
    *(.ARM.exidx*)
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >RAM

  /* At least 4 KB of the 8 KB stays free for the stack. */
  ASSERT(_ebss + 0x1000 <= _estack, "bootloader RAM: no room for the stack")
}
