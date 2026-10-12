#include "configuration_storage.h"

#include "stm32f4xx_hal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(STM32F411xE)
/*
 * STM32F411CE 512-Kbyte flash ends with 128 KB sectors 5, 6 and 7:
 * 5 holds the reader settings, 6 and 7 the configuration. The linker
 * script limits the application to sectors 0-4.
 */
#define CONFIG_STORAGE_SECTOR_A       FLASH_SECTOR_6
#define CONFIG_STORAGE_SECTOR_B       FLASH_SECTOR_7
#define CONFIG_STORAGE_ADDRESS_A      0x08040000UL
#define CONFIG_STORAGE_ADDRESS_B      0x08060000UL
#else
/*
 * STM32F429ZI 2-Mbyte flash ends with sectors 22 and 23.
 * The linker reserves both sectors for configuration storage.
 */
#define CONFIG_STORAGE_SECTOR_A       FLASH_SECTOR_22
#define CONFIG_STORAGE_SECTOR_B       FLASH_SECTOR_23
#define CONFIG_STORAGE_ADDRESS_A      0x081C0000UL
#define CONFIG_STORAGE_ADDRESS_B      0x081E0000UL
#endif
#define CONFIG_STORAGE_MAGIC          0x43464731UL /* "CFG1" */
#define CONFIG_STORAGE_VERSION_V1     1U
#define CONFIG_STORAGE_VERSION_V2     2U

/* Exact V1 payload retained for backwards-compatible migration. */
typedef struct
{
    uint32_t heartbeatSeconds;
    uint8_t startInventoryAtBoot;
    uint8_t session;
    uint8_t reserved[2U];
} Configuration_StorageDataV1_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t dataSize;
    uint32_t sequence;
    Configuration_StorageDataV1_t data;
    uint32_t crc32;
} Configuration_StorageRecordV1_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t dataSize;
    uint32_t sequence;
    Configuration_StorageData_t data;
    uint32_t crc32;
} Configuration_StorageRecordV2_t;

#if defined(__GNUC__)
typedef char Configuration_StorageV1SizeCheck[
    (sizeof(Configuration_StorageRecordV1_t) % sizeof(uint32_t) == 0U) ? 1 : -1
];
typedef char Configuration_StorageV2SizeCheck[
    (sizeof(Configuration_StorageRecordV2_t) % sizeof(uint32_t) == 0U) ? 1 : -1
];
#endif

static uint32_t Configuration_Storage_Crc32(
    const uint8_t *data,
    uint32_t length
)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;

    if(data == NULL)
    {
        return 0U;
    }

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

static uint8_t Configuration_Storage_IsSequenceNewer(
    uint32_t candidate,
    uint32_t current
)
{
    return ((int32_t)(candidate - current) > 0) ? 1U : 0U;
}

static uint8_t Configuration_Storage_ValidateData(
    const Configuration_StorageData_t *data
)
{
    uint8_t i;
    uint8_t j;

    if(data == NULL)
    {
        return 0U;
    }

    if(
        (data->heartbeatSeconds < 5U) ||
        (data->heartbeatSeconds > 7200U) ||
        (data->sBufSize < 1500U) ||
        (data->sBufSize > 8192U) ||
        (data->startInventoryAtBoot > 1U) ||
        (data->remoteCmdReply > 1U) ||
        /* Non-zero application acknowledgement is a binary-TCP feature;
         * Upload is JSON-over-TCP, so keep it disabled. */
        (data->clientAck != 0U) ||
        (data->crcEnable != 0U) ||
        (data->dataAggregationMode > 1U) ||
        (data->hwInf != 1U) ||
        (data->session > 3U) ||
        (data->eventCount == 0U) ||
        (data->eventCount > CONFIGURATION_MAX_EVENTS)
    )
    {
        return 0U;
    }

    if(
        (data->dataAggregationTimeMs < 50U) ||
        (data->dataAggregationTimeMs > 86400000UL)
    )
    {
        return 0U;
    }

    for(i = 0U; i < data->eventCount; i++)
    {
        /* The firmware can emit 1, 2, 3 and 6.  Events 4 and 5 require
         * reader-side semantics not yet established by the supplied
         * implementation evidence. */
        if(
            (data->events[i] < 1U) ||
            (data->events[i] > 6U) ||
            (data->events[i] == 4U) ||
            (data->events[i] == 5U)
        )
        {
            return 0U;
        }

        for(j = (uint8_t)(i + 1U); j < data->eventCount; j++)
        {
            if(data->events[i] == data->events[j])
            {
                return 0U;
            }
        }
    }

    /* cusParam is intentionally a fixed-size C string. */
    if(data->cusParam[CONFIGURATION_MAX_CUS_PARAM_LENGTH] != '\0')
    {
        return 0U;
    }

    return 1U;
}

static uint8_t Configuration_Storage_ValidateRecordV1(
    uint32_t address,
    Configuration_StorageRecordV1_t *record
)
{
    const Configuration_StorageRecordV1_t *stored;
    uint32_t crc;

    if(record == NULL)
    {
        return 0U;
    }

    stored = (const Configuration_StorageRecordV1_t *)(uintptr_t)address;
    memcpy(record, stored, sizeof(*record));

    if(
        (record->magic != CONFIG_STORAGE_MAGIC) ||
        (record->version != CONFIG_STORAGE_VERSION_V1) ||
        (record->dataSize != sizeof(Configuration_StorageDataV1_t))
    )
    {
        return 0U;
    }

    crc = Configuration_Storage_Crc32(
        (const uint8_t *)&record->data,
        sizeof(record->data)
    );

    return (crc == record->crc32) ? 1U : 0U;
}

static uint8_t Configuration_Storage_ValidateRecordV2(
    uint32_t address,
    Configuration_StorageRecordV2_t *record
)
{
    const Configuration_StorageRecordV2_t *stored;
    uint32_t crc;

    if(record == NULL)
    {
        return 0U;
    }

    stored = (const Configuration_StorageRecordV2_t *)(uintptr_t)address;
    memcpy(record, stored, sizeof(*record));

    if(
        (record->magic != CONFIG_STORAGE_MAGIC) ||
        (record->version != CONFIG_STORAGE_VERSION_V2) ||
        (record->dataSize != sizeof(Configuration_StorageData_t))
    )
    {
        return 0U;
    }

    crc = Configuration_Storage_Crc32(
        (const uint8_t *)&record->data,
        sizeof(record->data)
    );

    if(crc != record->crc32)
    {
        return 0U;
    }

    return Configuration_Storage_ValidateData(&record->data);
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

static uint8_t Configuration_Storage_ProgramWords(
    uint32_t address,
    const uint32_t *words,
    uint32_t wordCount
)
{
    uint32_t index;

    if(words == NULL)
    {
        return 0U;
    }

    for(index = 0U; index < wordCount; index++)
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

static void Configuration_Storage_ApplyV1Migration(
    const Configuration_StorageDataV1_t *oldData,
    Configuration_StorageData_t *data
)
{
    Configuration_Storage_GetDefaults(data);

    if(oldData == NULL)
    {
        return;
    }

    data->heartbeatSeconds = oldData->heartbeatSeconds;
    data->startInventoryAtBoot = oldData->startInventoryAtBoot;
    data->session = oldData->session;
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
    data->sBufSize = 1580U;
    data->recvTimeoutSeconds = 10U;
    data->clearBufferTimeSeconds = 8U;
    data->dataAggregationTimeMs = 1000U;
    data->networkWatchdogMinutes = 0U;
    data->inventoryWatchdogMinutes = 0U;

    data->startInventoryAtBoot = 0U;
    data->remoteCmdReply = 1U;
    data->clientAck = 0U;
    data->crcEnable = 0U;
    data->dataAggregationMode = 0U;
    data->hwInf = 1U;
    data->session = 0U;

    data->eventCount = 3U;
    data->events[0] = 1U;
    data->events[1] = 2U;
    data->events[2] = 3U;

    data->cusParam[0] = '\0';
}

uint8_t Configuration_Storage_Load(
    Configuration_StorageData_t *data
)
{
    Configuration_StorageRecordV2_t recordA2;
    Configuration_StorageRecordV2_t recordB2;
    Configuration_StorageRecordV1_t recordA1;
    Configuration_StorageRecordV1_t recordB1;
    uint8_t validA2;
    uint8_t validB2;
    uint8_t validA1;
    uint8_t validB1;

    if(data == NULL)
    {
        return 0U;
    }

    validA2 = Configuration_Storage_ValidateRecordV2(
        CONFIG_STORAGE_ADDRESS_A,
        &recordA2
    );
    validB2 = Configuration_Storage_ValidateRecordV2(
        CONFIG_STORAGE_ADDRESS_B,
        &recordB2
    );

    if((validA2 != 0U) || (validB2 != 0U))
    {
        if(
            (validA2 != 0U) &&
            (
                (validB2 == 0U) ||
                (Configuration_Storage_IsSequenceNewer(
                    recordA2.sequence,
                    recordB2.sequence
                ) != 0U)
            )
        )
        {
            *data = recordA2.data;
        }
        else
        {
            *data = recordB2.data;
        }

        return 1U;
    }

    /* Backwards compatibility with the V1 configuration record. */
    validA1 = Configuration_Storage_ValidateRecordV1(
        CONFIG_STORAGE_ADDRESS_A,
        &recordA1
    );
    validB1 = Configuration_Storage_ValidateRecordV1(
        CONFIG_STORAGE_ADDRESS_B,
        &recordB1
    );

    if((validA1 == 0U) && (validB1 == 0U))
    {
        return 0U;
    }

    if(
        (validA1 != 0U) &&
        (
            (validB1 == 0U) ||
            (Configuration_Storage_IsSequenceNewer(
                recordA1.sequence,
                recordB1.sequence
            ) != 0U)
        )
    )
    {
        Configuration_Storage_ApplyV1Migration(&recordA1.data, data);
    }
    else
    {
        Configuration_Storage_ApplyV1Migration(&recordB1.data, data);
    }

    return Configuration_Storage_ValidateData(data);
}

uint8_t Configuration_Storage_Save(
    const Configuration_StorageData_t *data
)
{
    Configuration_StorageRecordV2_t recordA;
    Configuration_StorageRecordV2_t recordB;
    Configuration_StorageRecordV1_t recordA1;
    Configuration_StorageRecordV1_t recordB1;
    Configuration_StorageRecordV2_t newRecord;
    uint8_t validA2;
    uint8_t validB2;
    uint8_t validA1;
    uint8_t validB1;
    uint32_t targetAddress;
    uint32_t targetSector;
    uint32_t nextSequence = 1U;
    uint8_t flashUnlocked = 0U;
    uint8_t result = 0U;

    if(Configuration_Storage_ValidateData(data) == 0U)
    {
        return 0U;
    }

    validA2 = Configuration_Storage_ValidateRecordV2(
        CONFIG_STORAGE_ADDRESS_A,
        &recordA
    );
    validB2 = Configuration_Storage_ValidateRecordV2(
        CONFIG_STORAGE_ADDRESS_B,
        &recordB
    );

    if((validA2 == 0U) && (validB2 == 0U))
    {
        /* Migrate from an existing V1 installation onto the opposite slot
         * of the newest valid V1 record so the original copy survives a
         * failed migration write. */
        validA1 = Configuration_Storage_ValidateRecordV1(
            CONFIG_STORAGE_ADDRESS_A,
            &recordA1
        );
        validB1 = Configuration_Storage_ValidateRecordV1(
            CONFIG_STORAGE_ADDRESS_B,
            &recordB1
        );

        if((validA1 == 0U) && (validB1 == 0U))
        {
            targetAddress = CONFIG_STORAGE_ADDRESS_A;
            targetSector = CONFIG_STORAGE_SECTOR_A;
            nextSequence = 1U;
        }
        else if(
            (validA1 != 0U) &&
            (
                (validB1 == 0U) ||
                (Configuration_Storage_IsSequenceNewer(
                    recordA1.sequence,
                    recordB1.sequence
                ) != 0U)
            )
        )
        {
            targetAddress = CONFIG_STORAGE_ADDRESS_B;
            targetSector = CONFIG_STORAGE_SECTOR_B;
            nextSequence = recordA1.sequence + 1U;
        }
        else
        {
            targetAddress = CONFIG_STORAGE_ADDRESS_A;
            targetSector = CONFIG_STORAGE_SECTOR_A;
            nextSequence = recordB1.sequence + 1U;
        }
    }
    else if(validA2 != 0U)
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
    newRecord.version = CONFIG_STORAGE_VERSION_V2;
    newRecord.dataSize = (uint16_t)sizeof(Configuration_StorageData_t);
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

    if(
        Configuration_Storage_ProgramWords(
            targetAddress,
            (const uint32_t *)&newRecord,
            sizeof(newRecord) / sizeof(uint32_t)
        ) == 0U
    )
    {
        goto cleanup;
    }

    {
        Configuration_StorageRecordV2_t verify;

        if(
            Configuration_Storage_ValidateRecordV2(
                targetAddress,
                &verify
            ) == 0U
        )
        {
            goto cleanup;
        }
    }

    result = 1U;

cleanup:
    if(flashUnlocked != 0U)
    {
        (void)HAL_FLASH_Lock();
    }

    return result;
}
