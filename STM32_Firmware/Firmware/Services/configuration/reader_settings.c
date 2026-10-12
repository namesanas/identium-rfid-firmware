#include "reader_settings.h"

#include "../../Config/product_config.h"
/* w5500.h pulls in the register-level SPI/GPIO drivers, whose macro
 * names clash with the HAL needed here for flash access. */
extern uint8_t W5500_SetNetworkConfigValues(
    const uint8_t ip[4],
    const uint8_t subnet[4],
    const uint8_t gateway[4],
    const uint8_t mac[6]
);
extern uint8_t W5500_Socket1_Close(void);
#include "../ethernet/ethernet_service.h"
#include "../Time/time_service.h"

#include "stm32f4xx_hal.h"

#include <stddef.h>
#include <string.h>

/*
 * Flash sector 21 (128 KB at 0x081A0000) holds an append-only log of
 * fixed-size records. Load takes the last valid record; Save appends.
 * The sector is erased only when it is full, so a power failure during
 * a save leaves the previous record intact. The linker scripts end the
 * application flash region before this sector.
 */
#define READER_SETTINGS_SECTOR         FLASH_SECTOR_21
#define READER_SETTINGS_ADDRESS        0x081A0000UL
#define READER_SETTINGS_SECTOR_SIZE    0x00020000UL
#define READER_SETTINGS_MAGIC          0x52535431UL /* "RST1" */
#define READER_SETTINGS_VERSION        3U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t dataSize;
    uint32_t sequence;
    ReaderSettings_t data;
    uint32_t crc32;
} ReaderSettings_Record_t;

/* Version 1 layout (before the HTTP upload fields). Its fields are the
 * leading part of ReaderSettings_t, so a v1 record is read once and the
 * next save erases the sector and starts a v3 log. */
typedef struct
{
    uint8_t ip[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    uint8_t mac[6];
    uint16_t configPort;
    uint8_t serverIp[4];
    uint16_t serverPort;
    uint8_t workState;
    uint8_t rfValid;
    uint8_t region;
    uint8_t reserved;
    uint16_t readPower;
    uint16_t writePower;
    char name[READER_SETTINGS_NAME_MAX + 1U];
} ReaderSettings_V1_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t dataSize;
    uint32_t sequence;
    ReaderSettings_V1_t data;
    uint32_t crc32;
} ReaderSettings_RecordV1_t;

/* Version 2 is the pre-WiFi ReaderSettings_t layout (HTTP upload included). */
typedef struct
{
    uint8_t ip[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    uint8_t mac[6];
    uint16_t configPort;
    uint8_t serverIp[4];
    uint16_t serverPort;
    uint8_t workState;
    uint8_t rfValid;
    uint8_t region;
    uint8_t reserved;
    uint16_t readPower;
    uint16_t writePower;
    char name[READER_SETTINGS_NAME_MAX + 1U];
    uint8_t uploadMode;
    uint16_t httpTimeout;
    char httpUrl[READER_SETTINGS_URL_MAX + 1U];
} ReaderSettings_V2_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t dataSize;
    uint32_t sequence;
    ReaderSettings_V2_t data;
    uint32_t crc32;
} ReaderSettings_RecordV2_t;

#define READER_SETTINGS_V2_SLOT_COUNT \
    (READER_SETTINGS_SECTOR_SIZE / sizeof(ReaderSettings_RecordV2_t))

#define READER_SETTINGS_V1_SLOT_COUNT \
    (READER_SETTINGS_SECTOR_SIZE / sizeof(ReaderSettings_RecordV1_t))

#if defined(__GNUC__)
typedef char ReaderSettings_RecordSizeCheck[
    (sizeof(ReaderSettings_Record_t) % sizeof(uint32_t) == 0U) ? 1 : -1
];
#endif

#define READER_SETTINGS_SLOT_COUNT \
    (READER_SETTINGS_SECTOR_SIZE / sizeof(ReaderSettings_Record_t))

static ReaderSettings_t g_settings;
static uint32_t g_sequence = 0U;
static uint32_t g_nextSlot = 0U;

static uint8_t g_networkApplyPending = 0U;
static uint32_t g_networkApplyAtMs = 0U;
static uint8_t g_rebootPending = 0U;
static uint32_t g_rebootAtMs = 0U;

static uint32_t ReaderSettings_Crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint32_t bit;

    for(i = 0U; i < length; i++)
    {
        crc ^= data[i];

        for(bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1UL) != 0UL) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

static void ReaderSettings_GetDefaults(ReaderSettings_t *settings)
{
    const uint32_t *uid = (const uint32_t *)UID_BASE;
    uint32_t hash;

    memset(settings, 0, sizeof(*settings));

    settings->ip[0] = 192U;
    settings->ip[1] = 168U;
    settings->ip[2] = 150U;
    settings->ip[3] = 50U;

    settings->netmask[0] = 255U;
    settings->netmask[1] = 255U;
    settings->netmask[2] = 255U;
    settings->netmask[3] = 0U;

    settings->gateway[0] = 192U;
    settings->gateway[1] = 168U;
    settings->gateway[2] = 150U;
    settings->gateway[3] = 1U;

    /* Locally administered unicast MAC derived from the MCU unique ID.
     * (The previous fixed 11:22:33:44:55:66 had the multicast bit set.) */
    hash = ReaderSettings_Crc32((const uint8_t *)uid, 12U);
    settings->mac[0] = 0x02U;
    settings->mac[1] = 0x1DU;
    settings->mac[2] = (uint8_t)(hash >> 24U);
    settings->mac[3] = (uint8_t)(hash >> 16U);
    settings->mac[4] = (uint8_t)(hash >> 8U);
    settings->mac[5] = (uint8_t)hash;

    settings->configPort = 8080U;

    settings->serverIp[0] = PRODUCT_PROTOCOL_TCP_SERVER_IP_0;
    settings->serverIp[1] = PRODUCT_PROTOCOL_TCP_SERVER_IP_1;
    settings->serverIp[2] = PRODUCT_PROTOCOL_TCP_SERVER_IP_2;
    settings->serverIp[3] = PRODUCT_PROTOCOL_TCP_SERVER_IP_3;
    settings->serverPort = PRODUCT_PROTOCOL_TCP_SERVER_PORT;

    settings->workState = READER_SETTINGS_WORK_ACTIVE;

    settings->wifiEnabled = 0U;
    settings->wifiMode = READER_SETTINGS_WIFI_STA;
    settings->wifiDhcp = 1U;
    settings->wifiApChannel = 1U;
    settings->wifiStaticIp[0] = 192U;
    settings->wifiStaticIp[1] = 168U;
    settings->wifiStaticIp[2] = 150U;
    settings->wifiStaticIp[3] = 51U;
    settings->wifiNetmask[0] = 255U;
    settings->wifiNetmask[1] = 255U;
    settings->wifiNetmask[2] = 255U;
    settings->wifiNetmask[3] = 0U;
    settings->wifiGateway[0] = 192U;
    settings->wifiGateway[1] = 168U;
    settings->wifiGateway[2] = 150U;
    settings->wifiGateway[3] = 1U;
    settings->wifiDns[0] = 8U;
    settings->wifiDns[1] = 8U;
    settings->wifiDns[2] = 8U;
    settings->wifiDns[3] = 8U;
    (void)strncpy(settings->wifiApSsid, "RFID-Reader", READER_SETTINGS_WIFI_SSID_MAX);
    settings->uploadInterface = READER_SETTINGS_UPLOAD_IF_AUTO;

    (void)strncpy(settings->name, PRODUCT_PROTOCOL_READER_NAME, READER_SETTINGS_NAME_MAX);
}

static uint8_t ReaderSettings_IsSlotErased(const ReaderSettings_Record_t *slot)
{
    const uint32_t *words = (const uint32_t *)slot;
    uint32_t i;

    for(i = 0U; i < (sizeof(*slot) / sizeof(uint32_t)); i++)
    {
        if(words[i] != 0xFFFFFFFFUL)
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t ReaderSettings_IsSlotValid(const ReaderSettings_Record_t *slot)
{
    if(
        (slot->magic != READER_SETTINGS_MAGIC) ||
        (slot->version != READER_SETTINGS_VERSION) ||
        (slot->dataSize != sizeof(ReaderSettings_t))
    )
    {
        return 0U;
    }

    return (
        ReaderSettings_Crc32((const uint8_t *)&slot->data, sizeof(slot->data))
        == slot->crc32
    ) ? 1U : 0U;
}

static uint8_t ReaderSettings_IsSane(const ReaderSettings_t *settings)
{
    /* Reject an unusable address: 0.0.0.0, multicast/broadcast, or a
     * multicast MAC. */
    if(
        ((settings->ip[0] | settings->ip[1] | settings->ip[2] | settings->ip[3]) == 0U) ||
        (settings->ip[0] >= 224U) ||
        ((settings->mac[0] & 0x01U) != 0U) ||
        ((settings->workState != READER_SETTINGS_WORK_PASSIVE) &&
         (settings->workState != READER_SETTINGS_WORK_ACTIVE)) ||
        (settings->uploadMode > READER_SETTINGS_UPLOAD_HTTP) ||
        (settings->wifiEnabled > 1U) ||
        (settings->wifiMode > READER_SETTINGS_WIFI_AP) ||
        (settings->wifiDhcp > 1U) ||
        (settings->wifiApChannel == 0U) || (settings->wifiApChannel > 13U) ||
        (settings->uploadInterface > READER_SETTINGS_UPLOAD_IF_AUTO) ||
        (settings->wifiSsid[READER_SETTINGS_WIFI_SSID_MAX] != '\0') ||
        (settings->wifiPassword[READER_SETTINGS_WIFI_PASSWORD_MAX] != '\0') ||
        (settings->wifiApSsid[READER_SETTINGS_WIFI_SSID_MAX] != '\0') ||
        (settings->wifiApPassword[READER_SETTINGS_WIFI_PASSWORD_MAX] != '\0')
    )
    {
        return 0U;
    }

    return 1U;
}

static void ReaderSettings_LoadV1(void)
{
    const ReaderSettings_RecordV1_t *slots =
        (const ReaderSettings_RecordV1_t *)READER_SETTINGS_ADDRESS;
    ReaderSettings_t candidate;
    uint32_t index;

    for(index = 0U; index < READER_SETTINGS_V1_SLOT_COUNT; index++)
    {
        const ReaderSettings_RecordV1_t *slot = &slots[index];

        if(
            (slot->magic != READER_SETTINGS_MAGIC) ||
            (slot->version != 1U) ||
            (slot->dataSize != sizeof(ReaderSettings_V1_t)) ||
            (ReaderSettings_Crc32((const uint8_t *)&slot->data, sizeof(slot->data)) != slot->crc32)
        )
        {
            continue;
        }

        ReaderSettings_GetDefaults(&candidate);
        memcpy(&candidate, &slot->data, sizeof(slot->data));
        candidate.uploadMode = READER_SETTINGS_UPLOAD_TCP;
        candidate.httpTimeout = 0U;
        memset(candidate.httpUrl, 0, sizeof(candidate.httpUrl));

        if(ReaderSettings_IsSane(&candidate) != 0U)
        {
            g_settings = candidate;
            g_settings.name[READER_SETTINGS_NAME_MAX] = '\0';
            g_sequence = slot->sequence;
        }
    }

    /* Slot sizes differ between versions: the next save erases first. */
    g_nextSlot = READER_SETTINGS_SLOT_COUNT;
}

static void ReaderSettings_LoadV2(void)
{
    const ReaderSettings_RecordV2_t *slots =
        (const ReaderSettings_RecordV2_t *)READER_SETTINGS_ADDRESS;
    ReaderSettings_t candidate;
    uint32_t index;

    for(index = 0U; index < READER_SETTINGS_V2_SLOT_COUNT; index++)
    {
        const ReaderSettings_RecordV2_t *slot = &slots[index];

        if((slot->magic != READER_SETTINGS_MAGIC) ||
           (slot->version != 2U) ||
           (slot->dataSize != sizeof(ReaderSettings_V2_t)) ||
           (ReaderSettings_Crc32((const uint8_t *)&slot->data, sizeof(slot->data)) != slot->crc32))
        {
            continue;
        }

        ReaderSettings_GetDefaults(&candidate);
        memcpy(&candidate, &slot->data, sizeof(slot->data));
        candidate.name[READER_SETTINGS_NAME_MAX] = '\0';
        candidate.httpUrl[READER_SETTINGS_URL_MAX] = '\0';

        if(ReaderSettings_IsSane(&candidate) != 0U)
        {
            g_settings = candidate;
            g_sequence = slot->sequence;
        }
    }

    /* The record size changed; force the first v3 save to erase the sector. */
    g_nextSlot = READER_SETTINGS_SLOT_COUNT;
}

void ReaderSettings_Init(void)
{
    const ReaderSettings_Record_t *slots =
        (const ReaderSettings_Record_t *)READER_SETTINGS_ADDRESS;
    uint32_t index;
    uint8_t found = 0U;

    ReaderSettings_GetDefaults(&g_settings);
    g_sequence = 0U;
    g_nextSlot = 0U;

    if((slots[0].magic == READER_SETTINGS_MAGIC) &&
       (slots[0].version == 1U))
    {
        ReaderSettings_LoadV1();
        return;
    }
    if((slots[0].magic == READER_SETTINGS_MAGIC) &&
       (slots[0].version == 2U))
    {
        ReaderSettings_LoadV2();
        return;
    }

    for(index = 0U; index < READER_SETTINGS_SLOT_COUNT; index++)
    {
        if(ReaderSettings_IsSlotErased(&slots[index]) != 0U)
        {
            g_nextSlot = index;
            break;
        }

        g_nextSlot = index + 1U;

        if(
            (ReaderSettings_IsSlotValid(&slots[index]) != 0U) &&
            (ReaderSettings_IsSane(&slots[index].data) != 0U)
        )
        {
            g_settings = slots[index].data;
            g_settings.name[READER_SETTINGS_NAME_MAX] = '\0';
            g_sequence = slots[index].sequence;
            found = 1U;
        }
    }

    (void)found;
}

const ReaderSettings_t *ReaderSettings_Get(void)
{
    return &g_settings;
}

uint8_t ReaderSettings_Save(const ReaderSettings_t *settings)
{
    ReaderSettings_Record_t record;
    const ReaderSettings_Record_t *slots =
        (const ReaderSettings_Record_t *)READER_SETTINGS_ADDRESS;
    const uint32_t *words;
    uint32_t address;
    uint32_t i;
    uint8_t result = 0U;

    if((settings == NULL) || (ReaderSettings_IsSane(settings) == 0U))
    {
        return 0U;
    }

    memset(&record, 0, sizeof(record));
    record.magic = READER_SETTINGS_MAGIC;
    record.version = READER_SETTINGS_VERSION;
    record.dataSize = (uint16_t)sizeof(ReaderSettings_t);
    record.sequence = g_sequence + 1U;
    record.data = *settings;
    record.data.name[READER_SETTINGS_NAME_MAX] = '\0';
    record.crc32 = ReaderSettings_Crc32((const uint8_t *)&record.data, sizeof(record.data));

    if(HAL_FLASH_Unlock() != HAL_OK)
    {
        return 0U;
    }

    if(
        (g_nextSlot >= READER_SETTINGS_SLOT_COUNT) ||
        (ReaderSettings_IsSlotErased(&slots[g_nextSlot]) == 0U)
    )
    {
        FLASH_EraseInitTypeDef erase;
        uint32_t sectorError = 0xFFFFFFFFUL;

        memset(&erase, 0, sizeof(erase));
        erase.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        erase.Sector = READER_SETTINGS_SECTOR;
        erase.NbSectors = 1U;

        if(
            (HAL_FLASHEx_Erase(&erase, &sectorError) != HAL_OK) ||
            (sectorError != 0xFFFFFFFFUL)
        )
        {
            goto cleanup;
        }

        g_nextSlot = 0U;
    }

    address = READER_SETTINGS_ADDRESS + (g_nextSlot * sizeof(ReaderSettings_Record_t));
    words = (const uint32_t *)&record;

    for(i = 0U; i < (sizeof(record) / sizeof(uint32_t)); i++)
    {
        if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + (i * 4U), words[i]) != HAL_OK)
        {
            goto cleanup;
        }
    }

    if(ReaderSettings_IsSlotValid(&slots[g_nextSlot]) == 0U)
    {
        goto cleanup;
    }

    g_settings = record.data;
    g_sequence = record.sequence;
    g_nextSlot++;
    result = 1U;

cleanup:
    (void)HAL_FLASH_Lock();
    return result;
}

uint8_t ReaderSettings_ApplyNetwork(void)
{
    return W5500_SetNetworkConfigValues(
        g_settings.ip,
        g_settings.netmask,
        g_settings.gateway,
        g_settings.mac
    );
}

const char *ReaderSettings_GetName(void)
{
    return (g_settings.name[0] != '\0') ? g_settings.name : PRODUCT_PROTOCOL_READER_NAME;
}

uint8_t ReaderSettings_GetWorkState(void)
{
    return g_settings.workState;
}

uint8_t ReaderSettings_GetUploadMode(void)
{
    return g_settings.uploadMode;
}

uint8_t ReaderSettings_GetUploadInterface(void)
{
    return g_settings.uploadInterface;
}

void ReaderSettings_RequestNetworkApply(uint32_t delayMs)
{
    g_networkApplyPending = 1U;
    g_networkApplyAtMs = Time_GetTickMs() + delayMs;
}

void ReaderSettings_RequestReboot(uint32_t delayMs)
{
    g_rebootPending = 1U;
    g_rebootAtMs = Time_GetTickMs() + delayMs;
}

void ReaderSettings_Task(void)
{
    uint32_t now = Time_GetTickMs();

    if(
        (g_rebootPending != 0U) &&
        ((int32_t)(now - g_rebootAtMs) >= 0)
    )
    {
        NVIC_SystemReset();
    }

    if(
        (g_networkApplyPending != 0U) &&
        ((int32_t)(now - g_networkApplyAtMs) >= 0)
    )
    {
        g_networkApplyPending = 0U;

        /* Connections made on the old address cannot survive the change. */
        (void)ReaderSettings_ApplyNetwork();
        (void)W5500_Socket1_Close();
        Ethernet_Service_Restart();
    }
}
