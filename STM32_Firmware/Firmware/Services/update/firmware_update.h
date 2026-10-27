/*
 * firmware_update.h
 *
 * Receives a signed firmware image (Tools/fw_sign.py) from the host
 * into the staging area; the bootloader installs it at the next reset
 * (docs/design/0020-bootloader-and-signed-update.md).
 *
 *   Begin(size)    erase as needed, expect `size` bytes
 *   Write(...)     sequential chunks; payload pages go to staging
 *   Finish()       checks the header and the SHA-256 of the staged
 *                  binary (the signature is the bootloader's job)
 *   Apply()        writes the header, which arms the bootloader; the
 *                  caller then resets
 *
 * The header is written last, so an interrupted upload leaves nothing
 * for the bootloader to act on.
 */

#ifndef RFID_CONTROLLER_FIRMWARE_UPDATE_H
#define RFID_CONTROLLER_FIRMWARE_UPDATE_H

#include <stdint.h>

typedef enum
{
    FW_UPDATE_IDLE = 0,
    FW_UPDATE_RECEIVING,
    FW_UPDATE_READY             /* Finish() passed, waiting for Apply() */
} FirmwareUpdate_State_t;

typedef enum
{
    FW_UPDATE_OK = 0,
    FW_UPDATE_ERR_NO_BOOTLOADER,
    FW_UPDATE_ERR_NO_STAGING,   /* F411 without its SPI flash */
    FW_UPDATE_ERR_SIZE,
    FW_UPDATE_ERR_SEQUENCE,     /* command out of order */
    FW_UPDATE_ERR_OFFSET,
    FW_UPDATE_ERR_FLASH,
    FW_UPDATE_ERR_HEADER,       /* wrong format, board or size */
    FW_UPDATE_ERR_HASH
} FirmwareUpdate_Result_t;

FirmwareUpdate_Result_t FirmwareUpdate_Begin(uint32_t totalSize);

/* Offsets count from the start of the image file. A chunk that was
 * already received (a host retry) is accepted and ignored. */
FirmwareUpdate_Result_t FirmwareUpdate_Write(
    uint32_t offset,
    const uint8_t *data,
    uint32_t length
);

FirmwareUpdate_Result_t FirmwareUpdate_Finish(void);
FirmwareUpdate_Result_t FirmwareUpdate_Apply(void);
void FirmwareUpdate_Abort(void);

FirmwareUpdate_State_t FirmwareUpdate_GetState(void);
uint32_t FirmwareUpdate_GetReceived(void);

/* Version text of the image being received (valid once the header is in). */
const char *FirmwareUpdate_GetVersion(void);

uint8_t FirmwareUpdate_BootloaderPresent(void);

/* What the bootloader did with the last staged image: NONE, PENDING,
 * INSTALLED, BAD_HEADER, BAD_HASH or BAD_SIGNATURE, and its version. */
const char *FirmwareUpdate_GetLastOutcome(char *version, uint32_t versionSize);

const char *FirmwareUpdate_ResultName(FirmwareUpdate_Result_t result);

#endif /* RFID_CONTROLLER_FIRMWARE_UPDATE_H */
