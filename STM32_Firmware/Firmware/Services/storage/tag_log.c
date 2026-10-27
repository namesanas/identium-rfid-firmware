#include "tag_log.h"

#include "../../Config/firmware_config.h"
#if (TARGET_BOARD == BOARD_F429_DISCOVERY)
#include "../../BSP/f429_discovery/board_config.h"
#else
#include "../../BSP/f411_custom/board_config.h"
#endif

#include "../../Devices/spi_flash/gd25q.h"
#include "../Time/time_service.h"

#include <stddef.h>
#include <string.h>

/* ============================================================
 * LAYOUT
 *
 * 32-byte record, little endian:
 *
 *   0  seq          u32
 *   4  timeMs       48-bit (ms, Unix time or uptime)
 *  10  antenna      u8
 *  11  rssi         s8
 *  12  readCount    u8
 *  13  epcLength    bits 0-5, flags bits 6-7
 *  14  epc          16 bytes, zero padded
 *  30  crc16        CCITT over bytes 0-29
 *
 * Record seq lives in slot (seq % TAG_LOG_SLOT_COUNT). The slot count
 * is a power of two that divides 2^32, so the mapping survives the
 * sequence counter wrapping.
 *
 * The sector after the one being written is always kept erased, so
 * the log holds at least (sectors - 2) full sectors once it is full.
 * ============================================================ */

/* The lower half of the flash: the upper half is the firmware update
 * staging area (Common/firmware_image.h, FW_STAGING_ADDRESS). */
#define TAG_LOG_FLASH_BYTES         (GD25Q_SIZE_BYTES / 2UL)

#define TAG_LOG_RECORD_SIZE         32U
#define TAG_LOG_SLOT_COUNT          (TAG_LOG_FLASH_BYTES / TAG_LOG_RECORD_SIZE)
#define TAG_LOG_SLOTS_PER_SECTOR    (GD25Q_SECTOR_SIZE / TAG_LOG_RECORD_SIZE)
#define TAG_LOG_SLOTS_PER_PAGE      (GD25Q_PAGE_SIZE / TAG_LOG_RECORD_SIZE)
#define TAG_LOG_SECTOR_COUNT        (TAG_LOG_FLASH_BYTES / GD25Q_SECTOR_SIZE)
#define TAG_LOG_BLOCK_COUNT         (TAG_LOG_FLASH_BYTES / GD25Q_BLOCK_SIZE)
#define TAG_LOG_CAPACITY            \
    ((TAG_LOG_SECTOR_COUNT - 2UL) * TAG_LOG_SLOTS_PER_SECTOR)

/* RAM queue: covers a worst-case 400 ms sector erase at a few hundred
 * reads per second. */
#define TAG_LOG_QUEUE_SIZE          64U

#define TAG_LOG_ERASE_TIMEOUT_MS    1000U
#define TAG_LOG_READ_WAIT_MS        50U

/* Times below 2001-09-09 are uptime, not a host-set clock. */
#define TAG_LOG_EPOCH_MIN_MS        1000000000000ULL

#define TAG_LOG_OFF_SEQ             0U
#define TAG_LOG_OFF_TIME            4U
#define TAG_LOG_OFF_ANTENNA         10U
#define TAG_LOG_OFF_RSSI            11U
#define TAG_LOG_OFF_COUNT           12U
#define TAG_LOG_OFF_EPC_LEN         13U
#define TAG_LOG_OFF_EPC             14U
#define TAG_LOG_OFF_CRC             30U

#define TAG_LOG_EPC_LEN_MASK        0x3FU

typedef struct
{
    TagLog_State_t state;

    uint32_t nextSeq;       /* next sequence to land in flash */
    uint32_t oldestSeq;
    uint32_t headSector;    /* sector of nextSeq; the one after is erased */

    uint8_t flashBusy;      /* a program or erase was started */
    uint8_t clearRequested;
    uint32_t clearBlock;    /* next 64 KB block to erase while clearing */

    uint8_t queue[TAG_LOG_QUEUE_SIZE][TAG_LOG_RECORD_SIZE];
    uint16_t queueHead;
    uint16_t queueCount;

    uint32_t dropped;

} TagLog_Context_t;

static TagLog_Context_t g_tagLog;

/* ============================================================
 * HELPERS
 * ============================================================ */

/* Sequence comparison that survives the counter wrapping. */
static int32_t TagLog_SeqDiff(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b);
}

static uint32_t TagLog_SlotOf(uint32_t seq)
{
    return seq & (TAG_LOG_SLOT_COUNT - 1UL);
}

static uint32_t TagLog_SectorOf(uint32_t seq)
{
    return TagLog_SlotOf(seq) / TAG_LOG_SLOTS_PER_SECTOR;
}

static uint32_t TagLog_SlotAddress(uint32_t slot)
{
    return slot * TAG_LOG_RECORD_SIZE;
}

static uint16_t TagLog_Crc16(const uint8_t *pData, uint32_t length)
{
    uint16_t crc = 0xFFFFU;
    uint32_t i;
    uint8_t bit;

    for(i = 0U; i < length; i++)
    {
        crc ^= (uint16_t)((uint16_t)pData[i] << 8U);

        for(bit = 0U; bit < 8U; bit++)
        {
            if((crc & 0x8000U) != 0U)
            {
                crc = (uint16_t)((crc << 1U) ^ 0x1021U);
            }
            else
            {
                crc = (uint16_t)(crc << 1U);
            }
        }
    }

    return crc;
}

static void TagLog_Encode(
    uint8_t *pRaw,
    uint32_t seq,
    const SILION_Tag_t *tag
)
{
    uint64_t timeMs = Time_GetEpochMs();
    uint16_t epcLength = tag->epcLengthBytes;
    uint8_t lengthByte;
    uint16_t crc;
    uint8_t i;

    memset(pRaw, 0, TAG_LOG_RECORD_SIZE);

    for(i = 0U; i < 4U; i++)
    {
        pRaw[TAG_LOG_OFF_SEQ + i] = (uint8_t)(seq >> (8U * i));
    }

    for(i = 0U; i < 6U; i++)
    {
        pRaw[TAG_LOG_OFF_TIME + i] = (uint8_t)(timeMs >> (8U * i));
    }

    pRaw[TAG_LOG_OFF_ANTENNA] = tag->antenna;
    pRaw[TAG_LOG_OFF_RSSI] = (uint8_t)tag->rssi;
    pRaw[TAG_LOG_OFF_COUNT] = tag->readCount;

    if(epcLength > sizeof(tag->epc))
    {
        epcLength = sizeof(tag->epc);
    }

    lengthByte = (uint8_t)((epcLength > TAG_LOG_EPC_LEN_MASK) ?
        TAG_LOG_EPC_LEN_MASK : epcLength);

    if(epcLength > TAG_LOG_EPC_MAX_BYTES)
    {
        lengthByte |= TAG_LOG_FLAG_EPC_TRUNCATED;
        epcLength = TAG_LOG_EPC_MAX_BYTES;
    }

    if(timeMs >= TAG_LOG_EPOCH_MIN_MS)
    {
        lengthByte |= TAG_LOG_FLAG_EPOCH_TIME;
    }

    pRaw[TAG_LOG_OFF_EPC_LEN] = lengthByte;
    memcpy(&pRaw[TAG_LOG_OFF_EPC], tag->epc, epcLength);

    crc = TagLog_Crc16(pRaw, TAG_LOG_OFF_CRC);
    pRaw[TAG_LOG_OFF_CRC] = (uint8_t)crc;
    pRaw[TAG_LOG_OFF_CRC + 1U] = (uint8_t)(crc >> 8U);
}

/* Decodes a raw slot. Returns 0 for erased, torn or misplaced data. */
static uint8_t TagLog_Decode(
    const uint8_t *pRaw,
    uint32_t slot,
    TagLog_Record_t *pRecord
)
{
    uint16_t storedCrc;
    uint8_t i;
    uint8_t copyLength;

    storedCrc = (uint16_t)pRaw[TAG_LOG_OFF_CRC] |
                (uint16_t)((uint16_t)pRaw[TAG_LOG_OFF_CRC + 1U] << 8U);

    if(TagLog_Crc16(pRaw, TAG_LOG_OFF_CRC) != storedCrc)
    {
        return 0U;
    }

    pRecord->seq = 0U;

    for(i = 0U; i < 4U; i++)
    {
        pRecord->seq |= (uint32_t)pRaw[TAG_LOG_OFF_SEQ + i] << (8U * i);
    }

    if(TagLog_SlotOf(pRecord->seq) != slot)
    {
        return 0U;
    }

    pRecord->timeMs = 0U;

    for(i = 0U; i < 6U; i++)
    {
        pRecord->timeMs |= (uint64_t)pRaw[TAG_LOG_OFF_TIME + i] << (8U * i);
    }

    pRecord->antenna = pRaw[TAG_LOG_OFF_ANTENNA];
    pRecord->rssi = (int8_t)pRaw[TAG_LOG_OFF_RSSI];
    pRecord->readCount = pRaw[TAG_LOG_OFF_COUNT];
    pRecord->epcLength = pRaw[TAG_LOG_OFF_EPC_LEN] & TAG_LOG_EPC_LEN_MASK;
    pRecord->flags = pRaw[TAG_LOG_OFF_EPC_LEN] & (uint8_t)~TAG_LOG_EPC_LEN_MASK;

    copyLength = (pRecord->epcLength > TAG_LOG_EPC_MAX_BYTES) ?
        TAG_LOG_EPC_MAX_BYTES : pRecord->epcLength;

    memset(pRecord->epc, 0, sizeof(pRecord->epc));
    memcpy(pRecord->epc, &pRaw[TAG_LOG_OFF_EPC], copyLength);

    return 1U;
}

/* ============================================================
 * MOUNT
 * ============================================================ */

#if (BOARD_TAG_LOG_ENABLED != 0U)

static uint8_t TagLog_IsErased(const uint8_t *pData, uint32_t length)
{
    uint32_t i;

    for(i = 0U; i < length; i++)
    {
        if(pData[i] != 0xFFU)
        {
            return 0U;
        }
    }

    return 1U;
}

/* First intact record of a sector. Writes run front to back, so an
 * erased slot before any intact one means the sector holds nothing. */
static uint8_t TagLog_SectorFirstSeq(uint32_t sector, uint32_t *pSeq)
{
    uint8_t raw[TAG_LOG_RECORD_SIZE];
    TagLog_Record_t record;
    uint32_t slot;
    uint32_t i;

    for(i = 0U; i < TAG_LOG_SLOTS_PER_SECTOR; i++)
    {
        slot = (sector * TAG_LOG_SLOTS_PER_SECTOR) + i;

        if(GD25Q_Read(TagLog_SlotAddress(slot), raw, sizeof(raw)) == 0U)
        {
            return 0U;
        }

        if(TagLog_IsErased(raw, sizeof(raw)) != 0U)
        {
            return 0U;
        }

        if(TagLog_Decode(raw, slot, &record) != 0U)
        {
            *pSeq = record.seq;
            return 1U;
        }
    }

    return 0U;
}

static uint8_t TagLog_SlotIsErased(uint32_t slot)
{
    uint8_t raw[TAG_LOG_RECORD_SIZE];

    if(GD25Q_Read(TagLog_SlotAddress(slot), raw, sizeof(raw)) == 0U)
    {
        return 0U;
    }

    return TagLog_IsErased(raw, sizeof(raw));
}

/* Blocking: only used while mounting. */
static uint8_t TagLog_EnsureSectorErased(uint32_t sector)
{
    uint8_t chunk[GD25Q_PAGE_SIZE];
    uint32_t address = sector * GD25Q_SECTOR_SIZE;
    uint32_t offset;

    for(offset = 0U; offset < GD25Q_SECTOR_SIZE; offset += sizeof(chunk))
    {
        if(GD25Q_Read(address + offset, chunk, sizeof(chunk)) == 0U)
        {
            return 0U;
        }

        if(TagLog_IsErased(chunk, sizeof(chunk)) == 0U)
        {
            if(GD25Q_StartSectorErase(address) == 0U)
            {
                return 0U;
            }

            return GD25Q_WaitReady(TAG_LOG_ERASE_TIMEOUT_MS);
        }
    }

    return 1U;
}

static uint8_t TagLog_Mount(void)
{
    uint8_t raw[TAG_LOG_RECORD_SIZE];
    TagLog_Record_t record;
    uint32_t sector;
    uint32_t newestSector = 0U;
    uint32_t newestSeq = 0U;
    uint32_t seq;
    uint32_t slot;
    uint32_t i;
    uint8_t found = 0U;

    /* The sector whose first record is newest holds the write head. */
    for(sector = 0U; sector < TAG_LOG_SECTOR_COUNT; sector++)
    {
        if(TagLog_SectorFirstSeq(sector, &seq) != 0U)
        {
            if((found == 0U) || (TagLog_SeqDiff(seq, newestSeq) > 0))
            {
                newestSeq = seq;
                newestSector = sector;
                found = 1U;
            }
        }
    }

    if(found == 0U)
    {
        g_tagLog.nextSeq = 0U;
    }
    else
    {
        for(i = 0U; i < TAG_LOG_SLOTS_PER_SECTOR; i++)
        {
            slot = (newestSector * TAG_LOG_SLOTS_PER_SECTOR) + i;

            if(
                (GD25Q_Read(TagLog_SlotAddress(slot), raw, sizeof(raw)) != 0U) &&
                (TagLog_Decode(raw, slot, &record) != 0U) &&
                (TagLog_SeqDiff(record.seq, newestSeq) > 0)
            )
            {
                newestSeq = record.seq;
            }
        }

        g_tagLog.nextSeq = newestSeq + 1U;
    }

    /* A record torn by a power cut cannot be programmed again; skip
     * past it to the next erased slot of the sector. */
    while(
        ((g_tagLog.nextSeq % TAG_LOG_SLOTS_PER_SECTOR) != 0U) &&
        (TagLog_SlotIsErased(TagLog_SlotOf(g_tagLog.nextSeq)) == 0U)
    )
    {
        g_tagLog.nextSeq++;
    }

    g_tagLog.headSector = TagLog_SectorOf(g_tagLog.nextSeq);

    if((g_tagLog.nextSeq % TAG_LOG_SLOTS_PER_SECTOR) == 0U)
    {
        if(TagLog_EnsureSectorErased(g_tagLog.headSector) == 0U)
        {
            return 0U;
        }
    }

    if(
        TagLog_EnsureSectorErased(
            (g_tagLog.headSector + 1U) % TAG_LOG_SECTOR_COUNT
        ) == 0U
    )
    {
        return 0U;
    }

    /* Oldest record: first sector after the write head that has one. */
    g_tagLog.oldestSeq = g_tagLog.nextSeq;

    for(i = 1U; i <= TAG_LOG_SECTOR_COUNT; i++)
    {
        sector = (g_tagLog.headSector + i) % TAG_LOG_SECTOR_COUNT;

        if(
            (TagLog_SectorFirstSeq(sector, &seq) != 0U) &&
            (TagLog_SeqDiff(seq, g_tagLog.nextSeq) < 0)
        )
        {
            g_tagLog.oldestSeq = seq;
            break;
        }
    }

    return 1U;
}

#endif /* BOARD_TAG_LOG_ENABLED */

/* ============================================================
 * PUBLIC API
 * ============================================================ */

uint8_t TagLog_Init(SPI_Handle_t *pSPI)
{
    memset(&g_tagLog, 0, sizeof(g_tagLog));
    g_tagLog.state = TAG_LOG_STATE_ABSENT;

#if (BOARD_TAG_LOG_ENABLED != 0U)
    if(GD25Q_Init(pSPI, BOARD_FLASH_CS_PORT, BOARD_FLASH_CS_PIN) == 0U)
    {
        return 0U;
    }

    if(TagLog_Mount() == 0U)
    {
        return 0U;
    }

    g_tagLog.state = TAG_LOG_STATE_READY;
    return 1U;
#else
    (void)pSPI;
    return 0U;
#endif
}

void TagLog_Append(const SILION_Tag_t *tag)
{
    uint16_t tail;

    if((g_tagLog.state == TAG_LOG_STATE_ABSENT) || (tag == NULL))
    {
        return;
    }

    if(g_tagLog.queueCount >= TAG_LOG_QUEUE_SIZE)
    {
        g_tagLog.dropped++;
        return;
    }

    tail = (uint16_t)(
        (g_tagLog.queueHead + g_tagLog.queueCount) % TAG_LOG_QUEUE_SIZE
    );

    TagLog_Encode(
        g_tagLog.queue[tail],
        g_tagLog.nextSeq + g_tagLog.queueCount,
        tag
    );

    g_tagLog.queueCount++;
}

void TagLog_Task(void)
{
    uint8_t page[GD25Q_PAGE_SIZE];
    uint32_t sector;
    uint32_t candidate;
    uint32_t count;
    uint32_t i;

    if(g_tagLog.state == TAG_LOG_STATE_ABSENT)
    {
        return;
    }

    if(g_tagLog.flashBusy != 0U)
    {
        if(GD25Q_IsBusy() != 0U)
        {
            return;
        }

        g_tagLog.flashBusy = 0U;
    }

    if(g_tagLog.clearRequested != 0U)
    {
        g_tagLog.clearRequested = 0U;
        g_tagLog.clearBlock = 0U;
        g_tagLog.state = TAG_LOG_STATE_CLEARING;
    }

    if(g_tagLog.state == TAG_LOG_STATE_CLEARING)
    {
        /* Erase only the log area, one 64 KB block per call: a chip
         * erase would also wipe the firmware-update staging area. */
        if(g_tagLog.clearBlock < TAG_LOG_BLOCK_COUNT)
        {
            if(GD25Q_StartBlockErase(g_tagLog.clearBlock * GD25Q_BLOCK_SIZE) != 0U)
            {
                g_tagLog.clearBlock++;
                g_tagLog.flashBusy = 1U;
            }

            return;
        }

        /* Log area erased: the log restarts at the current seq. */
        g_tagLog.state = TAG_LOG_STATE_READY;
        g_tagLog.oldestSeq = g_tagLog.nextSeq;
        g_tagLog.headSector = TagLog_SectorOf(g_tagLog.nextSeq);
    }

    if(g_tagLog.queueCount == 0U)
    {
        return;
    }

    /* Entering a new sector: erase the one after it first, so the
     * write head always has an erased sector in front of it. */
    sector = TagLog_SectorOf(g_tagLog.nextSeq);

    if(sector != g_tagLog.headSector)
    {
        if(
            GD25Q_StartSectorErase(
                ((sector + 1U) % TAG_LOG_SECTOR_COUNT) * GD25Q_SECTOR_SIZE
            ) != 0U
        )
        {
            g_tagLog.flashBusy = 1U;
            g_tagLog.headSector = sector;

            /* Everything older than (sectors - 2) sectors back is gone. */
            candidate = (g_tagLog.nextSeq -
                         (g_tagLog.nextSeq % TAG_LOG_SLOTS_PER_SECTOR)) -
                        TAG_LOG_CAPACITY;

            if(TagLog_SeqDiff(candidate, g_tagLog.oldestSeq) > 0)
            {
                g_tagLog.oldestSeq = candidate;
            }
        }

        return;
    }

    /* Program as many queued records as fit in the current page. */
    count = TAG_LOG_SLOTS_PER_PAGE -
            (TagLog_SlotOf(g_tagLog.nextSeq) % TAG_LOG_SLOTS_PER_PAGE);

    if(count > g_tagLog.queueCount)
    {
        count = g_tagLog.queueCount;
    }

    for(i = 0U; i < count; i++)
    {
        memcpy(
            &page[i * TAG_LOG_RECORD_SIZE],
            g_tagLog.queue[(g_tagLog.queueHead + i) % TAG_LOG_QUEUE_SIZE],
            TAG_LOG_RECORD_SIZE
        );
    }

    if(
        GD25Q_StartPageProgram(
            TagLog_SlotAddress(TagLog_SlotOf(g_tagLog.nextSeq)),
            page,
            count * TAG_LOG_RECORD_SIZE
        ) != 0U
    )
    {
        g_tagLog.flashBusy = 1U;
        g_tagLog.queueHead = (uint16_t)(
            (g_tagLog.queueHead + count) % TAG_LOG_QUEUE_SIZE
        );
        g_tagLog.queueCount = (uint16_t)(g_tagLog.queueCount - count);
        g_tagLog.nextSeq += count;
    }
}

void TagLog_GetStatus(TagLog_Status_t *pStatus)
{
    if(pStatus == NULL)
    {
        return;
    }

    pStatus->state = g_tagLog.state;
    pStatus->oldestSeq = g_tagLog.oldestSeq;
    pStatus->nextSeq = g_tagLog.nextSeq;
    pStatus->capacity = (g_tagLog.state == TAG_LOG_STATE_ABSENT) ?
        0U : TAG_LOG_CAPACITY;
    pStatus->pending = g_tagLog.queueCount;
    pStatus->dropped = g_tagLog.dropped;
}

uint8_t TagLog_Read(uint32_t seq, TagLog_Record_t *pRecord)
{
    uint8_t raw[TAG_LOG_RECORD_SIZE];
    uint32_t slot;

    if(
        (g_tagLog.state != TAG_LOG_STATE_READY) ||
        (pRecord == NULL) ||
        (TagLog_SeqDiff(seq, g_tagLog.oldestSeq) < 0) ||
        (TagLog_SeqDiff(seq, g_tagLog.nextSeq) >= 0)
    )
    {
        return 0U;
    }

    /* A page program is ~0.5 ms; a sector erase can be longer. */
    if(
        (g_tagLog.flashBusy != 0U) &&
        (GD25Q_WaitReady(TAG_LOG_READ_WAIT_MS) == 0U)
    )
    {
        return 0U;
    }

    slot = TagLog_SlotOf(seq);

    if(GD25Q_Read(TagLog_SlotAddress(slot), raw, sizeof(raw)) == 0U)
    {
        return 0U;
    }

    if(TagLog_Decode(raw, slot, pRecord) == 0U)
    {
        return 0U;
    }

    return (pRecord->seq == seq) ? 1U : 0U;
}

uint8_t TagLog_Clear(void)
{
    if(g_tagLog.state == TAG_LOG_STATE_ABSENT)
    {
        return 0U;
    }

    g_tagLog.clearRequested = 1U;
    return 1U;
}
