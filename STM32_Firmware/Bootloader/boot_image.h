/*
 * boot_image.h
 *
 * Decides what to do with the staged image: nothing, install it, or
 * reject it with a reason. No hardware access, so the host test
 * (hostcheck/boot_image_host_test.c) runs the same code.
 */

#ifndef RFID_CONTROLLER_BOOT_IMAGE_H
#define RFID_CONTROLLER_BOOT_IMAGE_H

#include <stdint.h>
#include "../Firmware/Common/firmware_image.h"

typedef enum
{
    BOOT_IMAGE_NONE = 0,        /* no header, or already handled */
    BOOT_IMAGE_VALID,           /* signed by our key: install */
    BOOT_IMAGE_BAD_HEADER,
    BOOT_IMAGE_BAD_HASH,
    BOOT_IMAGE_BAD_SIGNATURE
} BootImage_Result_t;

/* Reads the header into *header and checks the staged image against
 * the 64-byte P-256 public key (X || Y, big-endian). */
BootImage_Result_t BootImage_Check(
    FwImage_Read_t read,
    const uint8_t publicKey[64],
    FwImage_Header_t *header
);

/* FW_IMAGE_STATE_* to record for a rejected image. */
uint32_t BootImage_RejectState(BootImage_Result_t result);

#endif /* RFID_CONTROLLER_BOOT_IMAGE_H */
