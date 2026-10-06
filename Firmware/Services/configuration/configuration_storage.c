#include "configuration_storage.h"

#include "stm32f4xx_hal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * STM32F429ZI 2-Mbyte flash ends with sectors 22 and 23.
 * The linker reserves both sectors for configuration storage.
 * The application executes from the other flash sectors while these
 * Bank-2 sectors are erased/programmed.
 */
#define CONFIG_STORAGE_SECTOR_A       FLASH_SECTOR_22
#define CONFIG_STORAGE_SECTOR_B       FLASH_SECTOR_23
#define CONFIG_STORAGE_ADDRESS_A      0x081C0000UL
#define CONFIG_STORAGE_ADDRESS_B      0x081E0000UL
#define CONFIG_STORAGE_MAGIC          0x43464731UL /* "CFG1" */
#define CONFIG_STORAGE_VERSION        1U

#define CONFIG_STORAGE_RECORD_WORDS   6U
#define CONFIG_STORAGE_RECORD_BYTES   (CONFIG_STORAGE_RECORD_WORDS * sizeof(uint32_t))

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t dataSize;
    uint32_t sequence;
    Configuration_StorageData_t data;
    uint32_t crc32;
} Configuration_StorageRecord_t;

#if defined(__GNUC__)
typedef char Configuration_StorageRecordSizeCheck[
    (sizeof(Configuration_StorageRecord_t) == CONFIG_STORAGE_RECORD_BYTES) ? 1 : -1
];
#endif

static uint32_t Configuration_Storage_Crc32(
    const uint8_t *data,
    uint32_t length
)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;

    for(i = 0U; i < length; i++)
    {
        uint32_t bit;
        crc ^= data[i];

        for(bit = 0U; bit < 8U; bit++)
        {
            if((crc & 1UL) != 0UL)
            {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

static uint8_t Configuration_Storage_RecordIsValid(
    uint32_t address,
    Configuration_StorageRecord_t *record
)
{
    const Configuration_StorageRecord_t *stored;
    uint32_t crc;

    if(record == NULL)
    {
        return 0U;
    }

    stored = (const Configuration_StorageRecord_t *)(uintptr_t)address;
    memcpy(record, stored, sizeof(*record));

    if(record->magic != CONFIG_STORAGE_MAGIC)
    {
        return 0U;
    }

    if(record->version != CONFIG_STORAGE_VERSION)
    {
        return 0U;
    }

    if(record->dataSize != sizeof(Configuration_StorageData_t))
    {
        return 0U;
    }

    crc = Configuration_Storage_Crc32(
        (const uint8_t *)&record->data,
        sizeof(record->data)
    );

    return (crc == record->crc32) ? 1U : 0U;
}

static uint8_t Configuration_Storage_IsSequenceNewer(
    uint32_t candidate,
    uint32_t current
)
{
    return ((int32_t)(candidate - current) > 0) ? 1U : 0U;
}

static uint8_t Configuration_Storage_EraseSector(
    uint32_t sector
)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t sectorError = 0xFFFFFFFFUL;
    HAL_StatusTypeDef status;

    memset(&erase, 0, sizeof(erase));
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    erase.Sector = sector;
    erase.NbSectors = 1U;

    status = HAL_FLASHEx_Erase(&erase, &sectorError);

    return (
        (status == HAL_OK) &&
        (sectorError == 0xFFFFFFFFUL)
    ) ? 1U : 0U;
}

static uint8_t Configuration_Storage_ProgramRecord(
    uint32_t address,
    const Configuration_StorageRecord_t *record
)
{
    const uint32_t *words;
    uint32_t index;

    if(record == NULL)
    {
        return 0U;
    }

    words = (const uint32_t *)record;

    for(index = 0U; index < CONFIG_STORAGE_RECORD_WORDS; index++)
    {
        if(
            HAL_FLASH_Program(
                FLASH_TYPEPROGRAM_WORD,
                address + (index * sizeof(uint32_t)),
                words[index]
            ) != HAL_OK
        )
        {
            return 0U;
        }
    }

    return 1U;
}

void Configuration_Storage_GetDefaults(
    Configuration_StorageData_t *data
)
{
    if(data == NULL)
    {
        return;
    }

    memset(data, 0, sizeof(*data));
    data->heartbeatSeconds = 10U;
    data->startInventoryAtBoot = 0U;
    data->session = 0U;
}

uint8_t Configuration_Storage_Load(
    Configuration_StorageData_t *data
)
{
    Configuration_StorageRecord_t recordA;
    Configuration_StorageRecord_t recordB;
    uint8_t validA;
    uint8_t validB;

    if(data == NULL)
    {
        return 0U;
    }

    validA = Configuration_Storage_RecordIsValid(
        CONFIG_STORAGE_ADDRESS_A,
        &recordA
    );

    validB = Configuration_Storage_RecordIsValid(
        CONFIG_STORAGE_ADDRESS_B,
        &recordB
    );

    if((validA == 0U) && (validB == 0U))
    {
        return 0U;
    }

    if(
        (validA != 0U) &&
        (
            (validB == 0U) ||
            (Configuration_Storage_IsSequenceNewer(
                recordA.sequence,
                recordB.sequence
            ) != 0U)
        )
    )
    {
        *data = recordA.data;
    }
    else
    {
        *data = recordB.data;
    }

    return 1U;
}

uint8_t Configuration_Storage_Save(
    const Configuration_StorageData_t *data
)
{
    Configuration_StorageRecord_t recordA;
    Configuration_StorageRecord_t recordB;
    Configuration_StorageRecord_t newRecord;
    uint8_t validA;
    uint8_t validB;
    uint32_t targetAddress;
    uint32_t targetSector;
    uint32_t nextSequence = 1U;
    uint8_t flashUnlocked = 0U;
    uint8_t result = 0U;

    if(data == NULL)
    {
        return 0U;
    }

    if(
        (data->heartbeatSeconds < 5U) ||
        (data->heartbeatSeconds > 7200U) ||
        (data->startInventoryAtBoot > 1U) ||
        (data->session > 3U)
    )
    {
        return 0U;
    }

    validA = Configuration_Storage_RecordIsValid(
        CONFIG_STORAGE_ADDRESS_A,
        &recordA
    );

    validB = Configuration_Storage_RecordIsValid(
        CONFIG_STORAGE_ADDRESS_B,
        &recordB
    );

    if((validA == 0U) && (validB == 0U))
    {
        targetAddress = CONFIG_STORAGE_ADDRESS_A;
        targetSector = CONFIG_STORAGE_SECTOR_A;
        nextSequence = 1U;
    }
    else if(validA != 0U)
    {
        targetAddress = CONFIG_STORAGE_ADDRESS_B;
        targetSector = CONFIG_STORAGE_SECTOR_B;
        nextSequence = recordA.sequence + 1U;
    }
    else
    {
        targetAddress = CONFIG_STORAGE_ADDRESS_A;
        targetSector = CONFIG_STORAGE_SECTOR_A;
        nextSequence = recordB.sequence + 1U;
    }

    memset(&newRecord, 0, sizeof(newRecord));
    newRecord.magic = CONFIG_STORAGE_MAGIC;
    newRecord.version = CONFIG_STORAGE_VERSION;
    newRecord.dataSize = sizeof(Configuration_StorageData_t);
    newRecord.sequence = nextSequence;
    newRecord.data = *data;
    newRecord.crc32 = Configuration_Storage_Crc32(
        (const uint8_t *)&newRecord.data,
        sizeof(newRecord.data)
    );

    if(HAL_FLASH_Unlock() != HAL_OK)
    {
        return 0U;
    }
    flashUnlocked = 1U;

    if(Configuration_Storage_EraseSector(targetSector) == 0U)
    {
        goto cleanup;
    }

    if(Configuration_Storage_ProgramRecord(targetAddress, &newRecord) == 0U)
    {
        goto cleanup;
    }

    if(Configuration_Storage_RecordIsValid(targetAddress, &newRecord) == 0U)
    {
        goto cleanup;
    }

    result = 1U;

cleanup:
    if(flashUnlocked != 0U)
    {
        (void)HAL_FLASH_Lock();
    }

    return result;
}
