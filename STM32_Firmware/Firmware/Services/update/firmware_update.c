#include "firmware_update.h"

#include "../../Common/firmware_image.h"

#if FW_STAGING_SPI_FLASH
#include "../../Devices/spi_flash/gd25q.h"
#else
#include "stm32f4xx_hal.h"
#endif

#include <stddef.h>
#include <string.h>

#define FW_UPDATE_PAGE_SIZE         256U

/* SPI flash program and erase times are well below these. */
#define FW_UPDATE_SPI_IDLE_MS       1000U

/* The host test (hostcheck/firmware_update_host_test.c) points this at RAM. */
#ifndef FW_UPDATE_BOOT_INFO
#define FW_UPDATE_BOOT_INFO         ((const FwBoot_Info_t *)FW_BOOT_INFO_ADDRESS)
#endif

typedef struct
{
    FirmwareUpdate_State_t state;
    uint32_t totalSize;
    uint32_t received;
    uint32_t erasedEnd;         /* staging bytes [0, erasedEnd) are erased */
    union
    {
        FwImage_Header_t header;
        uint8_t bytes[FW_IMAGE_HEADER_SIZE];
    } head;
    uint32_t page[FW_UPDATE_PAGE_SIZE / 4U];
} FirmwareUpdate_Context_t;

static FirmwareUpdate_Context_t g_update;

/* ============================================================
 * STAGING BACKENDS
 * ============================================================ */

#if FW_STAGING_SPI_FLASH

/* F411: upper 2 MB of the GD25Q32, 4 KB erase sectors. */

static uint8_t FirmwareUpdate_StagingAvailable(void)
{
    return GD25Q_IsPresent();
}

static uint8_t FirmwareUpdate_EraseFrom(uint32_t offset)
{
    uint32_t sectorStart = offset - (offset % GD25Q_SECTOR_SIZE);

    /* The tag log may still have a program or erase running. */
    if(
        (GD25Q_WaitReady(FW_UPDATE_SPI_IDLE_MS) == 0U) ||
        (GD25Q_StartSectorErase(FW_STAGING_ADDRESS + sectorStart) == 0U) ||
        (GD25Q_WaitReady(FW_UPDATE_SPI_IDLE_MS) == 0U)
    )
    {
        return 0U;
    }

    g_update.erasedEnd = sectorStart + GD25Q_SECTOR_SIZE;

    return 1U;
}

static uint8_t FirmwareUpdate_ProgramPage(uint32_t offset, const uint32_t *words)
{
    if(
        (GD25Q_WaitReady(FW_UPDATE_SPI_IDLE_MS) == 0U) ||
        (GD25Q_StartPageProgram(
            FW_STAGING_ADDRESS + offset,
            (const uint8_t *)words,
            FW_UPDATE_PAGE_SIZE
        ) == 0U) ||
        (GD25Q_WaitReady(FW_UPDATE_SPI_IDLE_MS) == 0U)
    )
    {
        return 0U;
    }

    return 1U;
}

static uint8_t FirmwareUpdate_StagingRead(uint32_t offset, uint8_t *data, uint32_t length)
{
    if(
        ((offset + length) > FW_STAGING_SIZE) ||
        (GD25Q_IsPresent() == 0U) ||
        (GD25Q_WaitReady(FW_UPDATE_SPI_IDLE_MS) == 0U)
    )
    {
        return 0U;
    }

    return GD25Q_Read(FW_STAGING_ADDRESS + offset, data, length);
}

#else

/* F429: internal flash sectors 12-20 (bank 2, so the CPU keeps
 * running from bank 1 while they erase). */

static uint8_t FirmwareUpdate_StagingAvailable(void)
{
    return 1U;
}

/* Sector 12 starts the staging area: 4 x 16 KB, 64 KB, 4 x 128 KB. */
static uint32_t FirmwareUpdate_SectorOf(uint32_t offset, uint32_t *sectorEnd)
{
    if(offset < 0x10000UL)
    {
        *sectorEnd = (offset / 0x4000UL + 1U) * 0x4000UL;
        return FLASH_SECTOR_12 + (offset / 0x4000UL);
    }

    if(offset < 0x20000UL)
    {
        *sectorEnd = 0x20000UL;
        return FLASH_SECTOR_16;
    }

    *sectorEnd = (offset / 0x20000UL + 1U) * 0x20000UL;
    return FLASH_SECTOR_16 + (offset / 0x20000UL);
}

static uint8_t FirmwareUpdate_EraseFrom(uint32_t offset)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sectorError = 0U;
    uint32_t sectorEnd;
    HAL_StatusTypeDef status;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = FirmwareUpdate_SectorOf(offset, &sectorEnd);
    erase.NbSectors = 1U;

    if(HAL_FLASH_Unlock() != HAL_OK)
        return 0U;

    status = HAL_FLASHEx_Erase(&erase, &sectorError);
    (void)HAL_FLASH_Lock();

    if(status != HAL_OK)
        return 0U;

    g_update.erasedEnd = sectorEnd;

    return 1U;
}

static uint8_t FirmwareUpdate_ProgramPage(uint32_t offset, const uint32_t *words)
{
    uint32_t i;
    uint8_t ok = 1U;

    if(HAL_FLASH_Unlock() != HAL_OK)
        return 0U;

    for(i = 0U; i < (FW_UPDATE_PAGE_SIZE / 4U); i++)
    {
        if(
            HAL_FLASH_Program(
                FLASH_TYPEPROGRAM_WORD,
                FW_STAGING_ADDRESS + offset + (i * 4U),
                words[i]
            ) != HAL_OK
        )
        {
            ok = 0U;
            break;
        }
    }

    (void)HAL_FLASH_Lock();

    return ok;
}

static uint8_t FirmwareUpdate_StagingRead(uint32_t offset, uint8_t *data, uint32_t length)
{
    if((offset + length) > FW_STAGING_SIZE)
        return 0U;

    memcpy(data, (const void *)(FW_STAGING_ADDRESS + offset), length);

    return 1U;
}

#endif

/* Erases ahead of the page, then programs it. */
static uint8_t FirmwareUpdate_StorePage(uint32_t offset, const uint32_t *words)
{
    while(g_update.erasedEnd < (offset + FW_UPDATE_PAGE_SIZE))
    {
        if(FirmwareUpdate_EraseFrom(g_update.erasedEnd) == 0U)
            return 0U;
    }

    return FirmwareUpdate_ProgramPage(offset, words);
}

/* Freshly programmed staging must not be read from stale cache lines. */
static void FirmwareUpdate_FlushDataCache(void)
{
#if !FW_STAGING_SPI_FLASH
    if((FLASH->ACR & FLASH_ACR_DCEN) != 0U)
    {
        __HAL_FLASH_DATA_CACHE_DISABLE();
        __HAL_FLASH_DATA_CACHE_RESET();
        __HAL_FLASH_DATA_CACHE_ENABLE();
    }
#endif
}

/* ============================================================
 * PUBLIC
 * ============================================================ */

uint8_t FirmwareUpdate_BootloaderPresent(void)
{
    return (FW_UPDATE_BOOT_INFO->magic == FW_BOOT_INFO_MAGIC) ? 1U : 0U;
}

FirmwareUpdate_Result_t FirmwareUpdate_Begin(uint32_t totalSize)
{
    FirmwareUpdate_Abort();

    if(FirmwareUpdate_BootloaderPresent() == 0U)
        return FW_UPDATE_ERR_NO_BOOTLOADER;

    if(FirmwareUpdate_StagingAvailable() == 0U)
        return FW_UPDATE_ERR_NO_STAGING;

    if(
        (totalSize <= FW_IMAGE_HEADER_SIZE) ||
        ((totalSize - FW_IMAGE_HEADER_SIZE) > FW_APP_MAX_SIZE)
    )
    {
        return FW_UPDATE_ERR_SIZE;
    }

    g_update.totalSize = totalSize;
    g_update.state = FW_UPDATE_RECEIVING;

    return FW_UPDATE_OK;
}

FirmwareUpdate_Result_t FirmwareUpdate_Write(
    uint32_t offset,
    const uint8_t *data,
    uint32_t length
)
{
    uint32_t i;

    if(g_update.state != FW_UPDATE_RECEIVING)
        return FW_UPDATE_ERR_SEQUENCE;

    /* Repeated chunk: its reply was lost and the host sent it again. */
    if((offset < g_update.received) && ((offset + length) <= g_update.received))
        return FW_UPDATE_OK;

    if(offset != g_update.received)
        return FW_UPDATE_ERR_OFFSET;

    if((length == 0U) || (length > (g_update.totalSize - offset)))
        return FW_UPDATE_ERR_SIZE;

    for(i = 0U; i < length; i++)
    {
        uint32_t position = offset + i;

        if(position < FW_IMAGE_HEADER_SIZE)
        {
            g_update.head.bytes[position] = data[i];
            continue;
        }

        ((uint8_t *)g_update.page)[position % FW_UPDATE_PAGE_SIZE] = data[i];

        if((position % FW_UPDATE_PAGE_SIZE) == (FW_UPDATE_PAGE_SIZE - 1U))
        {
            if(
                FirmwareUpdate_StorePage(
                    position - (FW_UPDATE_PAGE_SIZE - 1U),
                    g_update.page
                ) == 0U
            )
            {
                FirmwareUpdate_Abort();
                return FW_UPDATE_ERR_FLASH;
            }

            memset(g_update.page, 0xFF, sizeof(g_update.page));
        }
    }

    g_update.received += length;

    return FW_UPDATE_OK;
}

FirmwareUpdate_Result_t FirmwareUpdate_Finish(void)
{
    const FwImage_Header_t *header = &g_update.head.header;
    uint32_t tail;

    if(
        (g_update.state != FW_UPDATE_RECEIVING) ||
        (g_update.received != g_update.totalSize)
    )
    {
        return FW_UPDATE_ERR_SEQUENCE;
    }

    tail = g_update.received % FW_UPDATE_PAGE_SIZE;

    if(tail != 0U)
    {
        if(FirmwareUpdate_StorePage(g_update.received - tail, g_update.page) == 0U)
        {
            FirmwareUpdate_Abort();
            return FW_UPDATE_ERR_FLASH;
        }
    }

    if(
        (FwImage_HeaderIsValid(header) == 0U) ||
        (header->state != FW_IMAGE_STATE_PENDING) ||
        (g_update.totalSize != (FW_IMAGE_HEADER_SIZE + header->imageSize))
    )
    {
        FirmwareUpdate_Abort();
        return FW_UPDATE_ERR_HEADER;
    }

    FirmwareUpdate_FlushDataCache();

    if(FwImage_PayloadMatches(FirmwareUpdate_StagingRead, header) == 0U)
    {
        FirmwareUpdate_Abort();
        return FW_UPDATE_ERR_HASH;
    }

    g_update.state = FW_UPDATE_READY;

    return FW_UPDATE_OK;
}

FirmwareUpdate_Result_t FirmwareUpdate_Apply(void)
{
    FwImage_Header_t check;

    if(g_update.state != FW_UPDATE_READY)
        return FW_UPDATE_ERR_SEQUENCE;

    if(FirmwareUpdate_StorePage(0U, (const uint32_t *)g_update.head.bytes) == 0U)
        return FW_UPDATE_ERR_FLASH;

    FirmwareUpdate_FlushDataCache();

    if(
        (FirmwareUpdate_StagingRead(0U, (uint8_t *)&check, sizeof(check)) == 0U) ||
        (memcmp(&check, &g_update.head.header, sizeof(check)) != 0)
    )
    {
        return FW_UPDATE_ERR_FLASH;
    }

    return FW_UPDATE_OK;
}

void FirmwareUpdate_Abort(void)
{
    memset(&g_update, 0, sizeof(g_update));
    memset(g_update.page, 0xFF, sizeof(g_update.page));
    g_update.state = FW_UPDATE_IDLE;
}

FirmwareUpdate_State_t FirmwareUpdate_GetState(void)
{
    return g_update.state;
}

uint32_t FirmwareUpdate_GetReceived(void)
{
    return g_update.received;
}

const char *FirmwareUpdate_GetVersion(void)
{
    if(g_update.received < FW_IMAGE_HEADER_SIZE)
        return "";

    /* NUL-terminated within the field once Finish() has checked it;
     * before that, fall back to empty for an unterminated field. */
    if(memchr(g_update.head.header.version, '\0', sizeof(g_update.head.header.version)) == NULL)
        return "";

    return g_update.head.header.version;
}

const char *FirmwareUpdate_GetLastOutcome(char *version, uint32_t versionSize)
{
    FwImage_Header_t header;

    if(versionSize > 0U)
        version[0] = '\0';

    if(
        (FirmwareUpdate_StagingAvailable() == 0U) ||
        (FirmwareUpdate_StagingRead(0U, (uint8_t *)&header, sizeof(header)) == 0U) ||
        (header.magic != FW_IMAGE_MAGIC)
    )
    {
        return "NONE";
    }

    if(versionSize > 0U)
    {
        header.version[sizeof(header.version) - 1U] = '\0';
        strncpy(version, header.version, versionSize - 1U);
        version[versionSize - 1U] = '\0';
    }

    switch(header.state)
    {
        case FW_IMAGE_STATE_PENDING:        return "PENDING";
        case FW_IMAGE_STATE_INSTALLED:      return "INSTALLED";
        case FW_IMAGE_STATE_BAD_HEADER:     return "BAD_HEADER";
        case FW_IMAGE_STATE_BAD_HASH:       return "BAD_HASH";
        case FW_IMAGE_STATE_BAD_SIGNATURE:  return "BAD_SIGNATURE";
        default:                            return "UNKNOWN";
    }
}

const char *FirmwareUpdate_ResultName(FirmwareUpdate_Result_t result)
{
    switch(result)
    {
        case FW_UPDATE_OK:                  return "OK";
        case FW_UPDATE_ERR_NO_BOOTLOADER:   return "NO_BOOTLOADER";
        case FW_UPDATE_ERR_NO_STAGING:      return "NO_STAGING";
        case FW_UPDATE_ERR_SIZE:            return "SIZE";
        case FW_UPDATE_ERR_SEQUENCE:        return "SEQUENCE";
        case FW_UPDATE_ERR_OFFSET:          return "OFFSET";
        case FW_UPDATE_ERR_FLASH:           return "FLASH";
        case FW_UPDATE_ERR_HEADER:          return "HEADER";
        default:                            return "HASH";
    }
}
