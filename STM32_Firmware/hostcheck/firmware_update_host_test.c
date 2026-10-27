/*
 * Host test of the application's update receiver
 * (Services/update/firmware_update.c), F411 backend, on a RAM model of
 * the GD25Q32 with NOR rules (program only clears bits, never twice,
 * never across a page). The staged result is then handed to the
 * bootloader's image check (Bootloader/boot_image.c).
 *
 * Usage: firmware_update_host_test <f411 image> <f429 image>
 */

#include "../Firmware/Services/update/firmware_update.h"
#include "../Firmware/Devices/spi_flash/gd25q.h"
#include "../Bootloader/boot_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t g_bootPublicKey[64];

FwBoot_Info_t hostcheckBootInfo;

static int g_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if(!(cond)) {                                                   \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);            \
            g_failures++;                                               \
        }                                                               \
    } while(0)

/* ============================================================
 * FLASH MODEL
 * ============================================================ */

static uint8_t g_flash[GD25Q_SIZE_BYTES];
static uint8_t g_present = 1U;
static uint32_t g_tagLogWrites;

uint8_t GD25Q_IsPresent(void) { return g_present; }

uint8_t GD25Q_WaitReady(uint32_t timeoutMs)
{
    (void)timeoutMs;
    return 1U;
}

uint8_t GD25Q_Read(uint32_t address, uint8_t *pData, uint32_t length)
{
    if((address >= GD25Q_SIZE_BYTES) || (length > GD25Q_SIZE_BYTES - address))
        return 0U;
    memcpy(pData, &g_flash[address], length);
    return 1U;
}

uint8_t GD25Q_StartPageProgram(uint32_t address, const uint8_t *pData, uint32_t length)
{
    uint32_t i;

    CHECK(((address % GD25Q_PAGE_SIZE) + length) <= GD25Q_PAGE_SIZE);

    if(address < FW_STAGING_ADDRESS)
        g_tagLogWrites++;

    for(i = 0U; i < length; i++)
    {
        CHECK(g_flash[address + i] == 0xFFU);
        g_flash[address + i] &= pData[i];
    }
    return 1U;
}

uint8_t GD25Q_StartSectorErase(uint32_t address)
{
    address -= address % GD25Q_SECTOR_SIZE;

    if(address < FW_STAGING_ADDRESS)
        g_tagLogWrites++;

    memset(&g_flash[address], 0xFF, GD25Q_SECTOR_SIZE);
    return 1U;
}

/* ============================================================
 * HELPERS
 * ============================================================ */

static uint8_t ReadStaging(uint32_t offset, uint8_t *data, uint32_t length)
{
    return GD25Q_Read(FW_STAGING_ADDRESS + offset, data, length);
}

static BootImage_Result_t BootloaderVerdict(void)
{
    FwImage_Header_t header;

    return BootImage_Check(ReadStaging, g_bootPublicKey, &header);
}

static uint8_t *LoadFile(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data;

    if(f == NULL)
        return NULL;

    data = malloc(FW_STAGING_SIZE);
    *size = (uint32_t)fread(data, 1, FW_STAGING_SIZE, f);
    fclose(f);
    return data;
}

/* Sends the whole image in chunks; returns the first failure. */
static FirmwareUpdate_Result_t Send(const uint8_t *image, uint32_t size, uint32_t chunk)
{
    FirmwareUpdate_Result_t result;
    uint32_t offset;

    result = FirmwareUpdate_Begin(size);
    if(result != FW_UPDATE_OK)
        return result;

    for(offset = 0U; offset < size; offset += chunk)
    {
        uint32_t n = ((size - offset) < chunk) ? (size - offset) : chunk;

        result = FirmwareUpdate_Write(offset, &image[offset], n);
        if(result != FW_UPDATE_OK)
            return result;
    }

    return FW_UPDATE_OK;
}

static int IsErased(uint32_t address, uint32_t length)
{
    uint32_t i;

    for(i = 0U; i < length; i++)
    {
        if(g_flash[address + i] != 0xFFU)
            return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    uint8_t *image;
    uint8_t *other;
    uint32_t size;
    uint32_t otherSize;
    uint32_t offset;
    char version[32];

    printf("firmware update host test\n");

    if(argc != 3)
        return 1;

    image = LoadFile(argv[1], &size);
    other = LoadFile(argv[2], &otherSize);

    if((image == NULL) || (other == NULL))
    {
        printf("  cannot open the test images\n");
        return 1;
    }

    memset(g_flash, 0xFF, sizeof(g_flash));

    /* No bootloader in sector 0: refuse. */
    CHECK(FirmwareUpdate_Begin(size) == FW_UPDATE_ERR_NO_BOOTLOADER);
    hostcheckBootInfo.magic = FW_BOOT_INFO_MAGIC;
    hostcheckBootInfo.version = FW_BOOT_INFO_VERSION;

    /* No SPI flash: refuse. */
    g_present = 0U;
    CHECK(FirmwareUpdate_Begin(size) == FW_UPDATE_ERR_NO_STAGING);
    g_present = 1U;

    /* Out of order. */
    CHECK(FirmwareUpdate_Write(0U, image, 128U) == FW_UPDATE_ERR_SEQUENCE);
    CHECK(FirmwareUpdate_Finish() == FW_UPDATE_ERR_SEQUENCE);
    CHECK(FirmwareUpdate_Apply() == FW_UPDATE_ERR_SEQUENCE);

    /* Sizes. */
    CHECK(FirmwareUpdate_Begin(FW_IMAGE_HEADER_SIZE) == FW_UPDATE_ERR_SIZE);
    CHECK(FirmwareUpdate_Begin(FW_IMAGE_HEADER_SIZE + FW_APP_MAX_SIZE + 1U) == FW_UPDATE_ERR_SIZE);
    CHECK(FirmwareUpdate_Begin(FW_IMAGE_HEADER_SIZE + FW_APP_MAX_SIZE) == FW_UPDATE_OK);
    FirmwareUpdate_Abort();

    /* --- A full upload with a repeated chunk and a skipped one. --- */
    CHECK(FirmwareUpdate_Begin(size) == FW_UPDATE_OK);

    for(offset = 0U; offset < size; offset += 128U)
    {
        uint32_t n = ((size - offset) < 128U) ? (size - offset) : 128U;

        if(offset == 128U * 300U)
        {
            /* Ahead of the reader: rejected, position unchanged. */
            CHECK(FirmwareUpdate_Write(offset + 128U, &image[offset + 128U], 128U) == FW_UPDATE_ERR_OFFSET);
            CHECK(FirmwareUpdate_GetReceived() == offset);
        }

        CHECK(FirmwareUpdate_Write(offset, &image[offset], n) == FW_UPDATE_OK);

        if(offset == 128U * 500U)
        {
            /* The host lost the reply and sends it again. */
            CHECK(FirmwareUpdate_Write(offset, &image[offset], n) == FW_UPDATE_OK);
            CHECK(FirmwareUpdate_GetReceived() == offset + n);
        }

        /* An interrupted upload leaves nothing for the bootloader. */
        if(offset == 128U * 400U)
            CHECK(BootloaderVerdict() == BOOT_IMAGE_NONE);
    }

    CHECK(FirmwareUpdate_Write(size, image, 1U) == FW_UPDATE_ERR_SIZE);
    CHECK(FirmwareUpdate_Finish() == FW_UPDATE_OK);
    CHECK(strcmp(FirmwareUpdate_GetVersion(), "v9.8.7-test") == 0);
    CHECK(FirmwareUpdate_GetState() == FW_UPDATE_READY);

    /* Header not written yet. */
    CHECK(IsErased(FW_STAGING_ADDRESS, FW_IMAGE_HEADER_SIZE));
    CHECK(BootloaderVerdict() == BOOT_IMAGE_NONE);

    CHECK(FirmwareUpdate_Apply() == FW_UPDATE_OK);
    CHECK(memcmp(&g_flash[FW_STAGING_ADDRESS], image, size) == 0);
    CHECK(strcmp(FirmwareUpdate_GetLastOutcome(version, sizeof(version)), "PENDING") == 0);
    CHECK(strcmp(version, "v9.8.7-test") == 0);
    CHECK(BootloaderVerdict() == BOOT_IMAGE_VALID);

    /* The bootloader marks it installed. */
    memset(&g_flash[FW_STAGING_ADDRESS + FW_IMAGE_STATE_OFFSET], 0x00, 4U);
    CHECK(strcmp(FirmwareUpdate_GetLastOutcome(version, sizeof(version)), "INSTALLED") == 0);
    CHECK(BootloaderVerdict() == BOOT_IMAGE_NONE);

    /* --- Second upload over the first, odd chunk size. --- */
    CHECK(Send(image, size, 77U) == FW_UPDATE_OK);
    CHECK(FirmwareUpdate_Finish() == FW_UPDATE_OK);
    CHECK(FirmwareUpdate_Apply() == FW_UPDATE_OK);
    CHECK(BootloaderVerdict() == BOOT_IMAGE_VALID);

    /* --- Corrupted on the way: SHA-256 mismatch at FW_END. --- */
    image[size / 2U] ^= 0x01U;
    CHECK(Send(image, size, 128U) == FW_UPDATE_OK);
    CHECK(FirmwareUpdate_Finish() == FW_UPDATE_ERR_HASH);
    CHECK(FirmwareUpdate_GetState() == FW_UPDATE_IDLE);
    CHECK(FirmwareUpdate_Apply() == FW_UPDATE_ERR_SEQUENCE);
    CHECK(BootloaderVerdict() == BOOT_IMAGE_NONE);
    image[size / 2U] ^= 0x01U;

    /* --- An image for the other board. --- */
    if(otherSize <= (FW_IMAGE_HEADER_SIZE + FW_APP_MAX_SIZE))
    {
        CHECK(Send(other, otherSize, 128U) == FW_UPDATE_OK);
        CHECK(FirmwareUpdate_Finish() == FW_UPDATE_ERR_HEADER);
    }
    else
    {
        CHECK(FirmwareUpdate_Begin(otherSize) == FW_UPDATE_ERR_SIZE);
    }

    /* --- A short upload: FW_END before all bytes. --- */
    CHECK(FirmwareUpdate_Begin(size) == FW_UPDATE_OK);
    CHECK(FirmwareUpdate_Write(0U, image, 128U) == FW_UPDATE_OK);
    CHECK(FirmwareUpdate_Finish() == FW_UPDATE_ERR_SEQUENCE);
    CHECK(FirmwareUpdate_Write(128U, &image[128], size) == FW_UPDATE_ERR_SIZE);
    FirmwareUpdate_Abort();

    /* The tag log half was never touched. */
    CHECK(g_tagLogWrites == 0U);
    CHECK(IsErased(0U, FW_STAGING_ADDRESS));

    free(image);
    free(other);

    if(g_failures != 0)
    {
        printf("%d firmware update checks FAILED\n", g_failures);
        return 1;
    }

    printf("all firmware update checks passed\n");
    return 0;
}
