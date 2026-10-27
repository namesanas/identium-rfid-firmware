/*
 * boot_flash.h
 *
 * Internal flash erase and program, register level (no HAL). The
 * bootloader runs on the 16 MHz HSI with zero wait states and the
 * flash caches off, so reads after a write never see stale data.
 */

#ifndef RFID_CONTROLLER_BOOT_FLASH_H
#define RFID_CONTROLLER_BOOT_FLASH_H

#include <stdint.h>

void BootFlash_Unlock(void);
void BootFlash_Lock(void);

/* Erases every sector that overlaps [address, address + length). */
uint8_t BootFlash_EraseRange(uint32_t address, uint32_t length);

/* Programs 32-bit words; address must be word aligned. */
uint8_t BootFlash_ProgramWords(uint32_t address, const uint32_t *words, uint32_t count);

#endif /* RFID_CONTROLLER_BOOT_FLASH_H */
