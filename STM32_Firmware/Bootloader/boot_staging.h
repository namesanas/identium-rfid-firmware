/*
 * boot_staging.h
 *
 * Access to the update staging area: internal flash sectors 12-20 on
 * the F429, the upper half of the GD25Q32 SPI flash on the F411.
 */

#ifndef RFID_CONTROLLER_BOOT_STAGING_H
#define RFID_CONTROLLER_BOOT_STAGING_H

#include <stdint.h>

/* Returns 0 when there is no staging area (F411 without SPI flash). */
uint8_t BootStaging_Init(void);

/* Offsets count from the start of the image header. */
uint8_t BootStaging_Read(uint32_t offset, uint8_t *data, uint32_t length);

uint8_t BootStaging_SetState(uint32_t state);

/* Returns the pins and peripherals to their reset state. */
void BootStaging_Deinit(void);

#endif /* RFID_CONTROLLER_BOOT_STAGING_H */
