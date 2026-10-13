/*
 * gd25q.h
 *
 * GigaDevice GD25Q32E 32 Mbit (4 MB) SPI NOR flash.
 *
 * The chip shares its SPI bus with the W5500 and has its own chip
 * select. Every bus access runs from the main loop (the W5500 is
 * polled), so the two drivers never interleave and need no lock.
 *
 * Program and erase are started here and finish inside the chip; the
 * caller polls GD25Q_IsBusy() instead of blocking the main loop.
 */

#ifndef RFID_CONTROLLER_DEVICE_GD25Q_H
#define RFID_CONTROLLER_DEVICE_GD25Q_H

#include <stdint.h>
#include "../../Drivers/spi/spi.h"
#include "../../Drivers/gpio/gpio.h"

#define GD25Q_JEDEC_MANUFACTURER    0xC8U   /* GigaDevice */
#define GD25Q_JEDEC_MEMORY_TYPE     0x40U
#define GD25Q_JEDEC_CAPACITY_32MBIT 0x16U

#define GD25Q_SIZE_BYTES            (4UL * 1024UL * 1024UL)
#define GD25Q_SECTOR_SIZE           4096UL
#define GD25Q_PAGE_SIZE             256UL
#define GD25Q_SECTOR_COUNT          (GD25Q_SIZE_BYTES / GD25Q_SECTOR_SIZE)

/* Releases deep power-down, checks the JEDEC ID and clears any block
 * protection. The chip select pin must already be an output driven
 * high. Returns 1 when a GD25Q32 answered. */
uint8_t GD25Q_Init(
    SPI_Handle_t *pSPI,
    GPIO_RegDef_t *pCSPort,
    uint8_t csPin
);

uint8_t GD25Q_IsPresent(void);

/* The 3-byte JEDEC ID as 0x00MMTTCC. */
uint32_t GD25Q_ReadJedecId(void);

/* 1 while a program or erase is still running inside the chip. */
uint8_t GD25Q_IsBusy(void);

/* Waits for the chip to go idle. Returns 0 on timeout. */
uint8_t GD25Q_WaitReady(uint32_t timeoutMs);

/* Reads any length from any address. The chip must be idle. */
uint8_t GD25Q_Read(uint32_t address, uint8_t *pData, uint32_t length);

/* Starts a page program. The data must not cross a 256-byte page
 * boundary and the target bytes must be erased. Returns 0 if the chip
 * is busy or the arguments are invalid. */
uint8_t GD25Q_StartPageProgram(
    uint32_t address,
    const uint8_t *pData,
    uint32_t length
);

/* Starts erasing the 4 KB sector that holds the address. */
uint8_t GD25Q_StartSectorErase(uint32_t address);

/* Starts erasing the whole chip (several seconds). */
uint8_t GD25Q_StartChipErase(void);

#endif /* RFID_CONTROLLER_DEVICE_GD25Q_H */
