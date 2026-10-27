/*
 * Bootloader for the STM32F429ZI: flash sector 0 (16 KB). RAM is the
 * top 8 KB only, so the application's .noinit fault record survives.
 */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 16K
  RAM   (xrw) : ORIGIN = 0x2002E000, LENGTH = 8K
}

INCLUDE bootloader_sections.ld
