/*
 * tag_log.h
 *
 * Tag read backup in the external SPI flash (F411 custom board).
 *
 * Every tag the reader reports is appended to a ring log of 32-byte
 * records in the lower 2 MB of the GD25Q32E (the upper 2 MB stages
 * firmware updates). Once full, the oldest 4 KB sector is erased to
 * make room, so the log always holds the latest ~65,000 reads.
 *
 * Each record has a sequence number that only grows; a record's flash
 * slot is its sequence number modulo the slot count. A host reads the
 * log by sequence number and remembers where it stopped.
 *
 * Appends go into a RAM queue and TagLog_Task() writes them to flash
 * without blocking, so a 45 ms sector erase never stalls the main loop.
 *
 * On boards without the flash (BOARD_TAG_LOG_ENABLED == 0) every call
 * is a harmless no-op and the state reads TAG_LOG_STATE_ABSENT.
 */

#ifndef RFID_CONTROLLER_TAG_LOG_H_
#define RFID_CONTROLLER_TAG_LOG_H_

#include <stdint.h>
#include "../../Drivers/spi/spi.h"
#include "../../Protocols/silion/silion_protocol.h"

#define TAG_LOG_EPC_MAX_BYTES       16U

/* Record flags. */
#define TAG_LOG_FLAG_EPC_TRUNCATED  0x40U   /* EPC longer than 16 bytes */
#define TAG_LOG_FLAG_EPOCH_TIME     0x80U   /* timeMs is Unix time, else uptime */

typedef enum
{
    TAG_LOG_STATE_ABSENT = 0,   /* no flash, or it did not answer */
    TAG_LOG_STATE_READY,
    TAG_LOG_STATE_CLEARING      /* erasing the log area */
} TagLog_State_t;

typedef struct
{
    uint32_t seq;
    uint64_t timeMs;
    uint8_t  antenna;
    int8_t   rssi;
    uint8_t  readCount;
    uint8_t  flags;
    uint8_t  epcLength;
    uint8_t  epc[TAG_LOG_EPC_MAX_BYTES];
} TagLog_Record_t;

typedef struct
{
    TagLog_State_t state;
    uint32_t oldestSeq;     /* first sequence still in flash */
    uint32_t nextSeq;       /* sequence the next stored record gets */
    uint32_t capacity;      /* records the log keeps once full */
    uint32_t pending;       /* records still waiting in RAM */
    uint32_t dropped;       /* lost because the RAM queue was full */
} TagLog_Status_t;

/* Mounts the log on the flash behind this (already running) SPI bus:
 * finds the newest record and prepares the next sector. Takes a few
 * hundred ms. Returns 1 when the flash is usable. */
uint8_t TagLog_Init(SPI_Handle_t *pSPI);

/* Queues one tag read. Never blocks. */
void TagLog_Append(const SILION_Tag_t *tag);

/* Writes queued records and runs erases. Call from the main loop. */
void TagLog_Task(void);

void TagLog_GetStatus(TagLog_Status_t *pStatus);

/* Reads the record with this sequence number. Returns 1 if it is in
 * flash and intact, 0 if it was overwritten, never written, or the
 * flash stayed busy. */
uint8_t TagLog_Read(uint32_t seq, TagLog_Record_t *pRecord);
typedef enum { TAG_LOG_READ_GONE = 0, TAG_LOG_READ_OK,
               TAG_LOG_READ_BUSY, TAG_LOG_READ_ERROR } TagLog_ReadResult_t;
TagLog_ReadResult_t TagLog_ReadResult(uint32_t seq, TagLog_Record_t *pRecord);

/* Erases the whole log in the background. Sequence numbers keep
 * counting from where they were. Returns 0 if the log is absent. */
uint8_t TagLog_Clear(void);

#endif /* RFID_CONTROLLER_TAG_LOG_H_ */
