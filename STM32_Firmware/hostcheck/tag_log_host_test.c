/*
 * Host-side test of the tag backup log (Services/storage/tag_log.c).
 *
 * The GD25Q32E is replaced by a RAM model with NOR rules: programming
 * only clears bits and must not cross a page, erases set a sector to
 * 0xFF, and the chip stays busy for a few status polls after each
 * program or erase. Power cuts are modelled by tearing a program or an
 * erase halfway and mounting the log again on the same flash image.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Firmware/Services/storage/tag_log.h"
#include "../Firmware/Devices/spi_flash/gd25q.h"
#include "../Firmware/Services/Time/time_service.h"

GPIO_TypeDef hostcheckGpioA;

void NVIC_SystemReset(void)
{
}

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if(!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while(0)

/* ============================================================
 * TIME STUB
 * ============================================================ */

static uint64_t g_epochMs = 0U;

uint32_t Time_GetTickMs(void) { return (uint32_t)g_epochMs; }
uint64_t Time_GetEpochMs(void) { return g_epochMs; }
void Time_DelayMs(uint32_t delayMs) { (void)delayMs; }
void Time_Service_Init(void) {}
void Time_Service_SysTick_Handler(void) {}
void Time_Service_SetEpochMs(uint64_t epochMs) { g_epochMs = epochMs; }

/* ============================================================
 * FLASH MODEL
 * ============================================================ */

static uint8_t g_flash[GD25Q_SIZE_BYTES];
static uint32_t g_busyPolls = 0U;
static uint8_t g_present = 1U;
static int g_tearNextProgramAt = -1;    /* bytes kept from the next program */
static uint8_t g_tearNextErase = 0U;
static uint32_t g_programs = 0U;
static uint32_t g_sectorErases = 0U;

uint8_t GD25Q_Init(SPI_Handle_t *pSPI, GPIO_RegDef_t *pCSPort, uint8_t csPin)
{
    (void)pSPI; (void)pCSPort; (void)csPin;
    g_busyPolls = 0U;
    return g_present;
}

uint8_t GD25Q_IsPresent(void) { return g_present; }
uint32_t GD25Q_ReadJedecId(void) { return 0xC84016UL; }

uint8_t GD25Q_IsBusy(void)
{
    if(g_busyPolls > 0U)
    {
        g_busyPolls--;
        return 1U;
    }
    return 0U;
}

static uint8_t g_forceBusy;
uint8_t GD25Q_WaitReady(uint32_t timeoutMs)
{
    (void)timeoutMs;
    if(g_forceBusy) return 0U;
    g_busyPolls = 0U;
    return 1U;
}

uint8_t GD25Q_Read(uint32_t address, uint8_t *pData, uint32_t length)
{
    CHECK(g_busyPolls == 0U);   /* a busy chip ignores reads */
    if((address >= GD25Q_SIZE_BYTES) || (length > GD25Q_SIZE_BYTES - address))
        return 0U;
    memcpy(pData, &g_flash[address], length);
    return 1U;
}

uint8_t GD25Q_StartPageProgram(uint32_t address, const uint8_t *pData, uint32_t length)
{
    uint32_t i;
    uint32_t keep = length;

    if(g_busyPolls > 0U)
        return 0U;

    CHECK(((address % GD25Q_PAGE_SIZE) + length) <= GD25Q_PAGE_SIZE);

    if(g_tearNextProgramAt >= 0)
    {
        keep = (uint32_t)g_tearNextProgramAt;
        g_tearNextProgramAt = -1;
    }

    for(i = 0U; i < length; i++)
    {
        CHECK(g_flash[address + i] == 0xFFU);   /* never program twice */
        if(i < keep)
            g_flash[address + i] &= pData[i];
    }

    g_programs++;
    g_busyPolls = 1U;
    return 1U;
}

uint8_t GD25Q_StartSectorErase(uint32_t address)
{
    uint32_t base = address - (address % GD25Q_SECTOR_SIZE);
    uint32_t length = GD25Q_SECTOR_SIZE;

    if(g_busyPolls > 0U)
        return 0U;

    if(g_tearNextErase != 0U)
    {
        g_tearNextErase = 0U;
        length = GD25Q_SECTOR_SIZE / 3U;    /* cut short */
    }

    memset(&g_flash[base], 0xFF, length);
    g_sectorErases++;
    g_busyPolls = 5U;
    return 1U;
}

uint8_t GD25Q_StartBlockErase(uint32_t address)
{
    uint32_t base = address - (address % GD25Q_BLOCK_SIZE);

    if(g_busyPolls > 0U)
        return 0U;
    memset(&g_flash[base], 0xFF, GD25Q_BLOCK_SIZE);
    g_busyPolls = 10U;
    return 1U;
}

/* The log must never erase the whole chip: the upper half holds the
 * firmware update staging area. */
uint8_t GD25Q_StartChipErase(void)
{
    CHECK(0 && "chip erase");
    return 0U;
}

/* ============================================================
 * HELPERS
 * ============================================================ */

static SPI_Handle_t g_spi;

/* EPC and other fields derived from a tag number, so any stored
 * record can be checked without keeping a copy. */
static void MakeTag(uint32_t n, SILION_Tag_t *tag)
{
    uint8_t i;

    memset(tag, 0, sizeof(*tag));
    tag->epcLengthBytes = 12U;
    for(i = 0U; i < 12U; i++)
        tag->epc[i] = (uint8_t)(n >> ((i % 4U) * 8U)) ^ i;
    tag->antenna = (uint8_t)(1U + (n % 4U));
    tag->rssi = (int8_t)(-(int)(n % 90U));
    tag->readCount = (uint8_t)n;
}

static uint8_t RecordMatches(const TagLog_Record_t *rec, uint32_t n)
{
    SILION_Tag_t tag;

    MakeTag(n, &tag);
    return (rec->epcLength == 12U) &&
           (memcmp(rec->epc, tag.epc, 12U) == 0) &&
           (rec->antenna == tag.antenna) &&
           (rec->rssi == tag.rssi) &&
           (rec->readCount == tag.readCount);
}

static void Drain(void)
{
    TagLog_Status_t status;
    int guard = 0;

    do
    {
        TagLog_Task();
        TagLog_GetStatus(&status);
    } while(((status.pending != 0U) || (status.state == TAG_LOG_STATE_CLEARING)) &&
            (++guard < 100000));

    /* Let the last program finish. */
    for(guard = 0; guard < 100; guard++)
        TagLog_Task();
}

/* Appends tags; tag number n is stored with seq (seqBase + n - first). */
static void AppendTags(uint32_t first, uint32_t count)
{
    SILION_Tag_t tag;
    uint32_t i;

    for(i = 0U; i < count; i++)
    {
        MakeTag(first + i, &tag);
        TagLog_Append(&tag);
        TagLog_Task();
        g_epochMs++;
    }
    Drain();
}

static void Status(TagLog_Status_t *s)
{
    TagLog_GetStatus(s);
}

/* Checks that seqs [from, to) hold tags starting at tag number tagFirst. */
static uint32_t VerifyRange(uint32_t from, uint32_t to, uint32_t tagFirst)
{
    TagLog_Record_t rec;
    uint32_t seq;
    uint32_t bad = 0U;

    for(seq = from; seq != to; seq++)
    {
        if((TagLog_Read(seq, &rec) == 0U) ||
           (rec.seq != seq) ||
           (RecordMatches(&rec, tagFirst + (seq - from)) == 0U))
        {
            bad++;
        }
    }
    return bad;
}

/* ============================================================
 * TESTS
 * ============================================================ */

static void TestEmptyAndBasic(void)
{
    TagLog_Status_t s;
    TagLog_Record_t rec;

    memset(g_flash, 0xFF, sizeof(g_flash));
    g_epochMs = 1760000000000ULL;   /* host-set Unix clock */

    CHECK(TagLog_Init(&g_spi) == 1U);
    Status(&s);
    CHECK(s.state == TAG_LOG_STATE_READY);
    CHECK(s.oldestSeq == 0U);
    CHECK(s.nextSeq == 0U);
    CHECK(s.capacity == 65280U);

    AppendTags(0U, 10U);
    Status(&s);
    CHECK(s.nextSeq == 10U);
    CHECK(s.oldestSeq == 0U);
    CHECK(VerifyRange(0U, 10U, 0U) == 0U);

    CHECK(TagLog_Read(3U, &rec) == 1U);
    CHECK((rec.flags & TAG_LOG_FLAG_EPOCH_TIME) != 0U);
    CHECK(rec.timeMs == 1760000000003ULL);
    CHECK(TagLog_Read(10U, &rec) == 0U);    /* not written yet */

    /* Reboot: the log comes back as it was. */
    CHECK(TagLog_Init(&g_spi) == 1U);
    Status(&s);
    CHECK(s.nextSeq == 10U);
    CHECK(s.oldestSeq == 0U);
    CHECK(VerifyRange(0U, 10U, 0U) == 0U);

    /* Long EPC is cut to 16 bytes and flagged. */
    {
        SILION_Tag_t tag;
        memset(&tag, 0, sizeof(tag));
        tag.epcLengthBytes = 30U;
        memset(tag.epc, 0xAB, 30U);
        TagLog_Append(&tag);
        Drain();
        CHECK(TagLog_Read(10U, &rec) == 1U);
        CHECK(rec.epcLength == 30U);
        CHECK((rec.flags & TAG_LOG_FLAG_EPC_TRUNCATED) != 0U);
        CHECK(rec.epc[15] == 0xABU);
    }
}

static void TestWrapAround(void)
{
    TagLog_Status_t s;
    uint32_t total = 300000U;
    uint32_t count;

    memset(g_flash, 0xFF, sizeof(g_flash));
    CHECK(TagLog_Init(&g_spi) == 1U);

    AppendTags(0U, total);
    Status(&s);
    CHECK(s.nextSeq == total);
    CHECK(s.dropped == 0U);

    count = s.nextSeq - s.oldestSeq;
    printf("  after %lu tags: oldest=%lu next=%lu kept=%lu erases=%lu\n",
           (unsigned long)total, (unsigned long)s.oldestSeq,
           (unsigned long)s.nextSeq, (unsigned long)count,
           (unsigned long)g_sectorErases);
    CHECK(count >= 65280U);
    CHECK(count <= 65408U);
    CHECK(VerifyRange(s.oldestSeq, s.nextSeq, s.oldestSeq) == 0U);

    /* The upper 2 MB is the firmware update staging area. */
    {
        uint32_t i;
        uint8_t untouched = 1U;

        for(i = GD25Q_SIZE_BYTES / 2U; i < GD25Q_SIZE_BYTES; i++)
        {
            if(g_flash[i] != 0xFFU)
                untouched = 0U;
        }

        CHECK(untouched == 1U);
    }

    /* One before the oldest is gone. */
    {
        TagLog_Record_t rec;
        CHECK(TagLog_Read(s.oldestSeq - 1U, &rec) == 0U);
    }

    /* Reboot after wrapping finds the same window. */
    {
        TagLog_Status_t again;
        CHECK(TagLog_Init(&g_spi) == 1U);
        Status(&again);
        CHECK(again.nextSeq == s.nextSeq);
        CHECK(again.oldestSeq == s.oldestSeq);
        CHECK(VerifyRange(again.oldestSeq, again.nextSeq, again.oldestSeq) == 0U);

        /* Keeps logging after the reboot. */
        AppendTags(total, 1000U);
        Status(&again);
        CHECK(again.nextSeq == total + 1000U);
        CHECK(VerifyRange(again.nextSeq - 1000U, again.nextSeq, total) == 0U);
    }
}

static void TestTornProgram(void)
{
    TagLog_Status_t s;
    TagLog_Record_t rec;
    SILION_Tag_t tag;
    uint32_t before;

    memset(g_flash, 0xFF, sizeof(g_flash));
    CHECK(TagLog_Init(&g_spi) == 1U);
    AppendTags(0U, 37U);
    Status(&s);
    before = s.nextSeq;

    /* Power fails 20 bytes into the next record. */
    g_tearNextProgramAt = 20;
    MakeTag(37U, &tag);
    TagLog_Append(&tag);
    Drain();

    CHECK(TagLog_Init(&g_spi) == 1U);
    Status(&s);
    CHECK(s.nextSeq == before + 1U);        /* torn slot skipped */
    CHECK(TagLog_Read(before, &rec) == 0U);
    CHECK(VerifyRange(0U, before, 0U) == 0U);

    AppendTags(1000U, 50U);
    Status(&s);
    CHECK(VerifyRange(before + 1U, s.nextSeq, 1000U) == 0U);
}

static void TestTornFirstRecordOfSector(void)
{
    TagLog_Status_t s;
    SILION_Tag_t tag;

    memset(g_flash, 0xFF, sizeof(g_flash));
    CHECK(TagLog_Init(&g_spi) == 1U);
    AppendTags(0U, 128U);                   /* sector 0 full */

    g_tearNextProgramAt = 5;                /* first record of sector 1 */
    MakeTag(128U, &tag);
    TagLog_Append(&tag);
    Drain();

    CHECK(TagLog_Init(&g_spi) == 1U);
    Status(&s);
    CHECK(VerifyRange(0U, 128U, 0U) == 0U);

    AppendTags(500U, 300U);
    Status(&s);
    CHECK(VerifyRange(s.nextSeq - 300U, s.nextSeq, 500U) == 0U);
    CHECK(VerifyRange(0U, 128U, 0U) == 0U);
}

static void TestTornErase(void)
{
    TagLog_Status_t s;
    TagLog_Status_t again;

    memset(g_flash, 0xFF, sizeof(g_flash));
    CHECK(TagLog_Init(&g_spi) == 1U);
    AppendTags(0U, 140000U);                /* wrapped once */

    /* Power fails during the erase-ahead of the next sector. */
    Status(&s);
    g_tearNextErase = 1U;
    AppendTags(140000U, 130U);              /* crosses into a new sector */
    Status(&s);

    CHECK(TagLog_Init(&g_spi) == 1U);
    Status(&again);
    CHECK(again.nextSeq == s.nextSeq);
    CHECK(VerifyRange(again.oldestSeq, again.nextSeq, again.oldestSeq) == 0U);

    AppendTags(140130U, 2000U);
    Status(&again);
    CHECK(VerifyRange(again.oldestSeq, again.nextSeq, again.oldestSeq) == 0U);
}

static void TestGarbageChip(void)
{
    TagLog_Status_t s;
    uint32_t i;

    srand(7);
    for(i = 0U; i < sizeof(g_flash); i++)
        g_flash[i] = (uint8_t)rand();

    CHECK(TagLog_Init(&g_spi) == 1U);
    Status(&s);
    CHECK(s.nextSeq == 0U);
    CHECK(s.oldestSeq == 0U);

    AppendTags(0U, 5000U);
    Status(&s);
    CHECK(VerifyRange(0U, 5000U, 0U) == 0U);
}

static void TestClear(void)
{
    TagLog_Status_t s;
    TagLog_Record_t rec;

    memset(g_flash, 0xFF, sizeof(g_flash));
    CHECK(TagLog_Init(&g_spi) == 1U);
    AppendTags(0U, 500U);

    /* Something staged in the upper half, which a clear must keep. */
    g_flash[GD25Q_SIZE_BYTES / 2U] = 0x54U;
    g_flash[GD25Q_SIZE_BYTES - 1U] = 0x54U;

    CHECK(TagLog_Clear() == 1U);
    TagLog_Task();
    Status(&s);
    CHECK(s.state == TAG_LOG_STATE_CLEARING);
    CHECK(TagLog_Read(10U, &rec) == 0U);

    Drain();
    Status(&s);
    CHECK(s.state == TAG_LOG_STATE_READY);
    CHECK(s.oldestSeq == 500U);
    CHECK(s.nextSeq == 500U);
    CHECK(TagLog_Read(10U, &rec) == 0U);
    CHECK(g_flash[GD25Q_SIZE_BYTES / 2U] == 0x54U);
    CHECK(g_flash[GD25Q_SIZE_BYTES - 1U] == 0x54U);
    CHECK(g_flash[GD25Q_SIZE_BYTES / 2U - 1U] == 0xFFU);
    g_flash[GD25Q_SIZE_BYTES / 2U] = 0xFFU;
    g_flash[GD25Q_SIZE_BYTES - 1U] = 0xFFU;

    AppendTags(9000U, 20U);
    CHECK(VerifyRange(500U, 520U, 9000U) == 0U);
}

static void TestQueueOverflowAndAbsent(void)
{
    TagLog_Status_t s;
    SILION_Tag_t tag;
    uint32_t i;

    memset(g_flash, 0xFF, sizeof(g_flash));
    CHECK(TagLog_Init(&g_spi) == 1U);

    /* No Task calls: the RAM queue fills and the rest are counted. */
    MakeTag(1U, &tag);
    for(i = 0U; i < 100U; i++)
        TagLog_Append(&tag);
    Status(&s);
    CHECK(s.pending == 64U);
    CHECK(s.dropped == 36U);
    Drain();
    Status(&s);
    CHECK(s.nextSeq == 64U);

    g_present = 0U;
    CHECK(TagLog_Init(&g_spi) == 0U);
    Status(&s);
    CHECK(s.state == TAG_LOG_STATE_ABSENT);
    TagLog_Append(&tag);
    TagLog_Task();
    CHECK(TagLog_Clear() == 0U);
    g_present = 1U;
}

static void TestBusyRead(void)
{
    SILION_Tag_t tag;
    TagLog_Record_t record;
    memset(g_flash, 0xFF, sizeof(g_flash));
    CHECK(TagLog_Init(&g_spi) == 1U);
    MakeTag(42U, &tag);
    TagLog_Append(&tag);
    TagLog_Task();
    g_forceBusy = 1U;
    CHECK(TagLog_ReadResult(0U, &record) == TAG_LOG_READ_BUSY);
    CHECK(TagLog_Read(0U, &record) == 0U);
    g_forceBusy = 0U;
    CHECK(TagLog_ReadResult(0U, &record) == TAG_LOG_READ_OK);
    CHECK(record.seq == 0U);
    CHECK(TagLog_ReadResult(99U, &record) == TAG_LOG_READ_GONE);
    Drain();
}

int main(void)
{
    printf("tag log host test\n");
    TestBusyRead();
    TestEmptyAndBasic();
    TestWrapAround();
    TestTornProgram();
    TestTornFirstRecordOfSector();
    TestTornErase();
    TestGarbageChip();
    TestClear();
    TestQueueOverflowAndAbsent();

    if(g_failures != 0)
    {
        printf("%d check(s) failed\n", g_failures);
        return 1;
    }

    printf("all tag log checks passed\n");
    return 0;
}
