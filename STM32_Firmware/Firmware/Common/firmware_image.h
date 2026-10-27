/*
 * firmware_image.h
 *
 * Signed firmware image format and the flash layout shared by the
 * bootloader (Bootloader/) and the application's update service
 * (Services/update/firmware_update.c). Tools/fw_sign.py writes the
 * same layout; keep the three in step.
 *
 * Flash layout:
 *
 *   F429 (2 MB internal flash)
 *     sector 0          0x08000000  16 KB   bootloader
 *     sectors 1-11      0x08004000  1008 KB application (linker limit 624 KB)
 *     sectors 12-20     0x08100000  640 KB  update staging
 *     sector 21         0x081A0000  128 KB  reader settings
 *     sectors 22-23     0x081C0000  256 KB  configuration A/B
 *
 *   F411 (512 KB internal flash + 4 MB GD25Q32 SPI flash)
 *     sector 0          0x08000000  16 KB   bootloader
 *     sectors 1-4       0x08004000  112 KB  application
 *     sectors 5-7       0x08020000  384 KB  reader settings, configuration
 *     SPI 0x000000      2 MB                tag backup log
 *     SPI 0x200000      2 MB                update staging
 *
 * Staging holds a 256-byte header followed by the application binary.
 * The application writes it and resets; the bootloader checks the
 * signature, copies the binary to the application area and records the
 * outcome in the header's state word.
 */

#ifndef RFID_CONTROLLER_FIRMWARE_IMAGE_H
#define RFID_CONTROLLER_FIRMWARE_IMAGE_H

#include <stdint.h>

#define FW_IMAGE_MAGIC              0x57464C54UL    /* "TLFW" */
#define FW_IMAGE_HEADER_VERSION     1U
#define FW_IMAGE_HEADER_SIZE        256U

/* The signature covers the first 128 header bytes, which include the
 * SHA-256 of the binary. */
#define FW_IMAGE_SIGNED_SIZE        128U

/* State word: the image file carries PENDING (erased flash); the
 * bootloader programs one of the others. Each is reachable from
 * all-ones by clearing bits only, so it needs no erase. */
#define FW_IMAGE_STATE_PENDING          0xFFFFFFFFUL
#define FW_IMAGE_STATE_INSTALLED        0x00000000UL
#define FW_IMAGE_STATE_BAD_HEADER       0x0000BAD1UL
#define FW_IMAGE_STATE_BAD_HASH         0x0000BAD2UL
#define FW_IMAGE_STATE_BAD_SIGNATURE    0x0000BAD3UL

typedef struct
{
    uint32_t magic;             /* 0x00 FW_IMAGE_MAGIC */
    uint16_t headerVersion;     /* 0x04 FW_IMAGE_HEADER_VERSION */
    uint16_t headerSize;        /* 0x06 FW_IMAGE_HEADER_SIZE */
    uint32_t boardId;           /* 0x08 MCU device ID: FW_BOARD_ID */
    uint32_t loadAddress;       /* 0x0C FW_APP_BASE */
    uint32_t imageSize;         /* 0x10 binary length in bytes */
    uint32_t reserved0[3];      /* 0x14 zero */
    char     version[32];       /* 0x20 NUL-terminated, e.g. "v0.11.0" */
    uint8_t  imageSha256[32];   /* 0x40 SHA-256 of the binary */
    uint8_t  reserved1[32];     /* 0x60 zero */
    uint8_t  signature[64];     /* 0x80 ECDSA P-256 r||s over SHA-256 of 0x00-0x7F */
    uint32_t state;             /* 0xC0 FW_IMAGE_STATE_* */
    uint8_t  reserved2[60];     /* 0xC4 0xFF */
} FwImage_Header_t;

#if defined(__GNUC__)
typedef char FwImage_HeaderSizeCheck[
    (sizeof(FwImage_Header_t) == FW_IMAGE_HEADER_SIZE) ? 1 : -1
];
#endif

#define FW_IMAGE_STATE_OFFSET       0xC0U

/* The bootloader keeps this block at a fixed address so the
 * application can tell that it is present. */
#define FW_BOOT_INFO_ADDRESS        0x08000100UL
#define FW_BOOT_INFO_MAGIC          0x4C424C54UL    /* "TLBL" */
#define FW_BOOT_INFO_VERSION        1U

typedef struct
{
    uint32_t magic;
    uint32_t version;
} FwBoot_Info_t;

#define FW_BOOT_BASE                0x08000000UL
#define FW_APP_BASE                 0x08004000UL

#if defined(STM32F411xE)
#define FW_BOARD_ID                 0x431UL
#define FW_APP_MAX_SIZE             (112UL * 1024UL)
/* Staging on the GD25Q32 SPI flash; the tag log keeps below it. */
#define FW_STAGING_SPI_FLASH        1U
#define FW_STAGING_ADDRESS          0x00200000UL
#define FW_STAGING_SIZE             0x00200000UL
#else
#define FW_BOARD_ID                 0x419UL
#define FW_APP_MAX_SIZE             (624UL * 1024UL)
#define FW_STAGING_SPI_FLASH        0U
#define FW_STAGING_ADDRESS          0x08100000UL    /* sector 12 */
#define FW_STAGING_SIZE             (640UL * 1024UL)
#endif

#include <stddef.h>

/* Reads staging bytes at an offset from the start of the header.
 * Returns 0 on a read error. */
typedef uint8_t (*FwImage_Read_t)(uint32_t offset, uint8_t *data, uint32_t length);

/* Magic, format, board, load address and size. Not the signature. */
uint8_t FwImage_HeaderIsValid(const FwImage_Header_t *header);

/* 1 when the SHA-256 of the staged binary matches the header. */
uint8_t FwImage_PayloadMatches(
    FwImage_Read_t read,
    const FwImage_Header_t *header
);

#endif /* RFID_CONTROLLER_FIRMWARE_IMAGE_H */
