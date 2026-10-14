#include "../Services/configuration/reader_settings.h"
#include "identium_protocol.h"
#include "identium_command.h"

#if defined(STM32F411xE)
#include "stm32f411xe.h"
#else
#include "stm32f429xx.h"
#endif

#include "../Config/product_config.h"
#include "../Application/Silion/sillion_application.h"
#include "../Services/ethernet/ethernet_service.h"
#include "../Services/upload/active_upload_transport.h"
#include "../Services/Time/time_service.h"
#include "../Services/configuration/configuration_service.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * ------------------------------------------------------------
 * JSON-over-TCP RX framing
 * ------------------------------------------------------------
 *
 * The active Identium connection is a byte stream.  A TCP receive
 * boundary is not a JSON message boundary, so the accumulator below
 * persists across calls and only dispatches on LF.
 */
static char identiumRxLine[IDENTIUM_PROTOCOL_RX_BUFFER_SIZE];
static uint16_t identiumRxIndex = 0U;
static uint8_t identiumRxDiscardLine = 0U;
static uint8_t identiumRxSeekingObject = 1U;

/*
 * ------------------------------------------------------------
 * Outbound tag buffering
 * ------------------------------------------------------------
 *
 * The protocol specification defines s_buf_size as the largest single
 * upload.  We therefore queue raw tag records, not pre-built JSON, and
 * serialize only the batch that is actually going to be transmitted.
 * This keeps the queue bounded and prevents stale network outages from
 * consuming unbounded RAM.
 *
 * Records are packed into a byte ring with only the fields the upload
 * uses and only as many EPC bytes as the tag has:
 *
 *   0  queuedAtMs    u32
 *   4  frequencyKHz  u32
 *   8  epcLength     u8
 *   9  antenna       u8
 *  10  readCount     u8
 *  11  rssi          s8
 *  12  epc           epcLength bytes
 *
 * A 96-bit EPC takes 24 bytes instead of a full SILION_Tag_t, so the
 * ring holds the 256-tag limit for EPCs up to 128 bits, and still 110
 * of the longest (62-byte) EPCs.
 */
#define IDENTIUM_TAG_QUEUE_SIZE        256U     /* tags */
#define IDENTIUM_TAG_QUEUE_BYTES       8192U
#define IDENTIUM_TAG_RECORD_HEADER     12U
#define IDENTIUM_TAG_BATCH_MAX_TAGS    64U
#define IDENTIUM_TAG_FLUSH_LIMIT       4U
#define IDENTIUM_TAG_BATCH_BUFFER_SIZE  8193U

static uint8_t identiumTagQueue[IDENTIUM_TAG_QUEUE_BYTES];
static uint16_t identiumTagQueueHead = 0U;  /* byte offsets */
static uint16_t identiumTagQueueTail = 0U;
static uint16_t identiumTagQueueUsed = 0U;
static uint16_t identiumTagQueueCount = 0U;
static uint32_t identiumTagAggregationStartMs = 0U;
static uint8_t identiumTagAggregationActive = 0U;
/* Tags lost before upload, since boot (GET_DIAG). */
static uint32_t identiumTagDropped = 0U;  /* queue full: oldest dropped */
static uint32_t identiumTagExpired = 0U;  /* older than clear_buf_time */

static char identiumTagBatchBuffer[IDENTIUM_TAG_BATCH_BUFFER_SIZE];
static char identiumTagObjectBuffer[256U];

static uint32_t identiumHeartbeatCounter = 0U;
static uint32_t identiumLastHeartbeatMs = 0U;
static uint8_t identiumWasConnected = 0U;
static uint8_t identiumSyncTimeRequested = 0U;

/* Recovery timers.  Both are disabled by the current product profile (0). */
static uint8_t identiumNetworkTimerActive = 0U;
static uint32_t identiumDisconnectedSinceMs = 0U;
static uint8_t identiumInventoryTimerActive = 0U;
static uint32_t identiumInventoryQuietSinceMs = 0U;

static uint8_t Identium_Protocol_FormatUint64(
    char *buffer,
    uint16_t bufferSize,
    uint64_t value
)
{
    char reversed[20];
    uint8_t digitCount = 0U;
    uint8_t i;

    if(
        (buffer == NULL) ||
        (bufferSize < 2U)
    )
    {
        return 0U;
    }

    do
    {
        reversed[digitCount++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while(
        (value != 0U) &&
        (digitCount < sizeof(reversed))
    );

    if(
        (value != 0U) ||
        ((uint16_t)digitCount + 1U > bufferSize)
    )
    {
        return 0U;
    }

    for(i = 0U; i < digitCount; i++)
    {
        buffer[i] = reversed[digitCount - 1U - i];
    }

    buffer[digitCount] = '\0';
    return 1U;
}

static uint8_t Identium_Protocol_AppendByte(
    uint8_t byte
)
{
    if(identiumRxIndex >= (IDENTIUM_PROTOCOL_RX_BUFFER_SIZE - 1U))
    {
        identiumRxIndex = 0U;
        identiumRxLine[0] = '\0';
        identiumRxDiscardLine = 1U;
        identiumRxSeekingObject = 1U;
        return 0U;
    }

    identiumRxLine[identiumRxIndex++] = (char)byte;
    identiumRxLine[identiumRxIndex] = '\0';

    return 1U;
}

static uint8_t Identium_Protocol_SendJsonLine(
    const char *json
)
{
    uint16_t length;
    uint16_t sent;

    if(json == NULL)
    {
        return 0U;
    }

    if(Identium_Protocol_IsConnected() == 0U)
    {
        return 0U;
    }

    length = (uint16_t)strlen(json);

    if(length == 0U)
    {
        return 0U;
    }

    sent = ActiveUpload_Transport_SendComplete(
        (const uint8_t *)json,
        length
    );

    return (sent == length) ? 1U : 0U;
}

static uint8_t Identium_Protocol_EventEnabled(
    uint8_t eventCode
)
{
    return Configuration_Service_IsEventEnabled(eventCode);
}

static uint8_t Identium_Protocol_SendSyncTimeRequest(void)
{
    static char buffer[192U];
    int written;

    if(
        Identium_Protocol_IsConnected() == 0U ||
        Identium_Protocol_EventEnabled(6U) == 0U
    )
    {
        return 1U;
    }

    written = snprintf(
        buffer,
        sizeof(buffer),
        "{\"reader_name\":\"%s\",\"event_type\":\"sync_time_req\"}\n",
        ReaderSettings_GetName()
    );

    if(
        (written < 0) ||
        ((size_t)written >= sizeof(buffer))
    )
    {
        return 0U;
    }

    return Identium_Protocol_SendJsonLine(buffer);
}

static uint8_t Identium_Protocol_SendReaderException(
    uint32_t errorCode,
    const char *errorString
)
{
    static char buffer[320U];
    int written;

    if(errorString == NULL)
    {
        return 0U;
    }

    written = snprintf(
        buffer,
        sizeof(buffer),
        "{\"reader_name\":\"%s\",\"event_type\":\"reader_exception\",\"event_data\":{\"err_code\":%lu,\"err_string\":\"%s\"}}\n",
        ReaderSettings_GetName(),
        (unsigned long)errorCode,
        errorString
    );

    if(
        (written < 0) ||
        ((size_t)written >= sizeof(buffer))
    )
    {
        return 0U;
    }

    return Identium_Protocol_SendJsonLine(buffer);
}

static uint8_t Identium_Protocol_SendCommandReply(
    const char *commandType,
    uint32_t errCode,
    const char *errString
)
{
    static char buffer[384U];
    int written;

    if(
        (commandType == NULL) ||
        (errString == NULL)
    )
    {
        return 0U;
    }

    if(Configuration_Service_GetRemoteCmdReply() == 0U)
    {
        return 1U;
    }

    written = snprintf(
        buffer,
        sizeof(buffer),
        "{\"reader_name\":\"%s\",\"event_type\":\"command_reply\",\"event_data\":{"
        "\"command_type\":\"%s\",\"err_code\":%u,\"err_string\":\"%s\"}}\n",
        ReaderSettings_GetName(),
        commandType,
        (unsigned int)errCode,
        errString
    );

    if(
        (written < 0) ||
        ((size_t)written >= sizeof(buffer))
    )
    {
        return 0U;
    }

    return Identium_Protocol_SendJsonLine(buffer);
}

static void Identium_Protocol_TagRingWrite(
    uint16_t offset,
    const uint8_t *data,
    uint16_t length
)
{
    uint16_t i;

    for(i = 0U; i < length; i++)
    {
        identiumTagQueue[(offset + i) % IDENTIUM_TAG_QUEUE_BYTES] = data[i];
    }
}

static void Identium_Protocol_TagRingRead(
    uint16_t offset,
    uint8_t *data,
    uint16_t length
)
{
    uint16_t i;

    for(i = 0U; i < length; i++)
    {
        data[i] = identiumTagQueue[(offset + i) % IDENTIUM_TAG_QUEUE_BYTES];
    }
}

/* Unpacks the record at a byte offset. Returns its size in the ring. */
static uint16_t Identium_Protocol_TagRecordRead(
    uint16_t offset,
    SILION_Tag_t *tag,
    uint32_t *queuedAtMs
)
{
    uint8_t header[IDENTIUM_TAG_RECORD_HEADER];

    Identium_Protocol_TagRingRead(offset, header, sizeof(header));

    if(queuedAtMs != NULL)
    {
        *queuedAtMs = (uint32_t)header[0] |
                      ((uint32_t)header[1] << 8U) |
                      ((uint32_t)header[2] << 16U) |
                      ((uint32_t)header[3] << 24U);
    }

    if(tag != NULL)
    {
        memset(tag, 0, sizeof(*tag));
        tag->frequencyKHz = (uint32_t)header[4] |
                            ((uint32_t)header[5] << 8U) |
                            ((uint32_t)header[6] << 16U) |
                            ((uint32_t)header[7] << 24U);
        tag->epcLengthBytes = header[8];
        tag->antenna = header[9];
        tag->readCount = header[10];
        tag->rssi = (int8_t)header[11];

        Identium_Protocol_TagRingRead(
            (uint16_t)((offset + IDENTIUM_TAG_RECORD_HEADER) %
                       IDENTIUM_TAG_QUEUE_BYTES),
            tag->epc,
            tag->epcLengthBytes
        );
    }

    return (uint16_t)(IDENTIUM_TAG_RECORD_HEADER + header[8]);
}

static void Identium_Protocol_TagQueuePop(
    uint16_t count
)
{
    uint16_t recordSize;

    while(
        (count > 0U) &&
        (identiumTagQueueCount > 0U)
    )
    {
        recordSize = Identium_Protocol_TagRecordRead(
            identiumTagQueueTail,
            NULL,
            NULL
        );

        identiumTagQueueTail = (uint16_t)(
            (identiumTagQueueTail + recordSize) % IDENTIUM_TAG_QUEUE_BYTES
        );
        identiumTagQueueUsed = (uint16_t)(identiumTagQueueUsed - recordSize);
        identiumTagQueueCount--;
        count--;
    }

    if(identiumTagQueueCount == 0U)
    {
        identiumTagAggregationStartMs = 0U;
        identiumTagAggregationActive = 0U;
    }
    else
    {
        (void)Identium_Protocol_TagRecordRead(
            identiumTagQueueTail,
            NULL,
            &identiumTagAggregationStartMs
        );
        identiumTagAggregationActive = 1U;
    }
}

static void Identium_Protocol_DiscardStaleTags(
    uint32_t nowMs
)
{
    while(identiumTagQueueCount > 0U)
    {
        uint32_t queuedAtMs;
        uint32_t ageMs;

        (void)Identium_Protocol_TagRecordRead(
            identiumTagQueueTail,
            NULL,
            &queuedAtMs
        );

        ageMs = nowMs - queuedAtMs;

        uint32_t clearSeconds = Configuration_Service_GetClearBufferTimeSeconds();

        if(
            (clearSeconds == 0U) ||
            (clearSeconds > 4294967UL) ||
            (ageMs < (clearSeconds * 1000U))
        )
        {
            break;
        }

        Identium_Protocol_TagQueuePop(1U);
        identiumTagExpired++;
    }
}

static uint8_t Identium_Protocol_QueueTag(
    const SILION_Tag_t *tag,
    uint32_t nowMs
)
{
    uint8_t header[IDENTIUM_TAG_RECORD_HEADER];
    uint16_t recordSize;

    if(tag == NULL)
    {
        return 0U;
    }

    if(
        (tag->epcLengthBytes == 0U) ||
        (tag->epcLengthBytes > sizeof(tag->epc))
    )
    {
        return 0U;
    }

    recordSize = (uint16_t)(IDENTIUM_TAG_RECORD_HEADER + tag->epcLengthBytes);

    /* Keep the newest information when the bounded queue is full. */
    while(
        (identiumTagQueueCount >= IDENTIUM_TAG_QUEUE_SIZE) ||
        ((uint32_t)identiumTagQueueUsed + recordSize > IDENTIUM_TAG_QUEUE_BYTES)
    )
    {
        Identium_Protocol_TagQueuePop(1U);
        identiumTagDropped++;
    }

    if(identiumTagQueueCount == 0U)
    {
        identiumTagAggregationStartMs = nowMs;
        identiumTagAggregationActive = 1U;
    }

    header[0] = (uint8_t)nowMs;
    header[1] = (uint8_t)(nowMs >> 8U);
    header[2] = (uint8_t)(nowMs >> 16U);
    header[3] = (uint8_t)(nowMs >> 24U);
    header[4] = (uint8_t)tag->frequencyKHz;
    header[5] = (uint8_t)(tag->frequencyKHz >> 8U);
    header[6] = (uint8_t)(tag->frequencyKHz >> 16U);
    header[7] = (uint8_t)(tag->frequencyKHz >> 24U);
    header[8] = (uint8_t)tag->epcLengthBytes;
    header[9] = tag->antenna;
    header[10] = tag->readCount;
    header[11] = (uint8_t)tag->rssi;

    Identium_Protocol_TagRingWrite(
        identiumTagQueueHead,
        header,
        sizeof(header)
    );
    Identium_Protocol_TagRingWrite(
        (uint16_t)((identiumTagQueueHead + IDENTIUM_TAG_RECORD_HEADER) %
                   IDENTIUM_TAG_QUEUE_BYTES),
        tag->epc,
        tag->epcLengthBytes
    );

    identiumTagQueueHead = (uint16_t)(
        (identiumTagQueueHead + recordSize) % IDENTIUM_TAG_QUEUE_BYTES
    );
    identiumTagQueueUsed = (uint16_t)(identiumTagQueueUsed + recordSize);
    identiumTagQueueCount++;
    return 1U;
}

static uint8_t Identium_Protocol_BuildTagObject(
    const SILION_Tag_t *tag,
    char *buffer,
    uint16_t bufferSize
)
{
    uint16_t pos = 0U;
    uint16_t i;
    int written;

    if(
        (tag == NULL) ||
        (buffer == NULL) ||
        (bufferSize == 0U)
    )
    {
        return 0U;
    }

    written = snprintf(
        &buffer[pos],
        bufferSize - pos,
        "{\"ep\":\""
    );

    if(
        (written < 0) ||
        ((size_t)written >= (size_t)(bufferSize - pos))
    )
    {
        return 0U;
    }

    pos = (uint16_t)(pos + written);

    for(i = 0U; i < tag->epcLengthBytes; i++)
    {
        written = snprintf(
            &buffer[pos],
            bufferSize - pos,
            "%02X",
            tag->epc[i]
        );

        if(
            (written < 0) ||
            ((size_t)written >= (size_t)(bufferSize - pos))
        )
        {
            return 0U;
        }

        pos = (uint16_t)(pos + written);
    }

    written = snprintf(
        &buffer[pos],
        bufferSize - pos,
        "\",\"at\":%u,\"rc\":%u,\"ri\":%d,\"fq\":%lu}",
        (unsigned int)tag->antenna,
        (unsigned int)tag->readCount,
        (int)tag->rssi,
        (unsigned long)tag->frequencyKHz
    );

    if(
        (written < 0) ||
        ((size_t)written >= (size_t)(bufferSize - pos))
    )
    {
        return 0U;
    }

    pos = (uint16_t)(pos + written);
    (void)pos;
    return 1U;
}

static uint8_t Identium_Protocol_BuildTagBatch(
    uint16_t maximumTags,
    uint32_t maximumBytes,
    uint16_t *tagCount,
    uint16_t *messageLength
)
{
    uint16_t pos = 0U;
    uint16_t count = 0U;
    uint16_t i;
    uint16_t recordOffset = identiumTagQueueTail;
    int written;
    const uint16_t reserveForClosing = 3U; /* ]}\n */

    if(
        (tagCount == NULL) ||
        (messageLength == NULL) ||
        (maximumTags == 0U)
    )
    {
        return 0U;
    }

    written = snprintf(
        identiumTagBatchBuffer,
        sizeof(identiumTagBatchBuffer),
        "{\"reader_name\":\"%s\",\"event_type\":\"tag_read\",\"event_data\":[",
        ReaderSettings_GetName()
    );

    if(
        (written < 0) ||
        ((size_t)written >= sizeof(identiumTagBatchBuffer))
    )
    {
        return 0U;
    }

    pos = (uint16_t)written;

    if(maximumTags > IDENTIUM_TAG_QUEUE_SIZE)
    {
        maximumTags = IDENTIUM_TAG_QUEUE_SIZE;
    }

    for(i = 0U; (i < maximumTags) && (i < identiumTagQueueCount); i++)
    {
        SILION_Tag_t tag;
        uint16_t objectLength;

        recordOffset = (uint16_t)(
            (recordOffset +
             Identium_Protocol_TagRecordRead(recordOffset, &tag, NULL)) %
            IDENTIUM_TAG_QUEUE_BYTES
        );

        if(
            Identium_Protocol_BuildTagObject(
                &tag,
                identiumTagObjectBuffer,
                sizeof(identiumTagObjectBuffer)
            ) == 0U
        )
        {
            return 0U;
        }

        objectLength = (uint16_t)strlen(identiumTagObjectBuffer);

        /* Leave space for the closing ]}\n.  A batch may stop before the
         * next tag; the tag itself remains queued for the next upload. */
        if(
            ((uint32_t)pos +
             (count > 0U ? 1U : 0U) +
             objectLength +
             reserveForClosing) >
            maximumBytes
        )
        {
            break;
        }

        if(count > 0U)
        {
            identiumTagBatchBuffer[pos++] = ',';
        }

        memcpy(
            &identiumTagBatchBuffer[pos],
            identiumTagObjectBuffer,
            objectLength
        );
        pos = (uint16_t)(pos + objectLength);
        count++;
    }

    if(count == 0U)
    {
        return 0U;
    }

    written = snprintf(
        &identiumTagBatchBuffer[pos],
        sizeof(identiumTagBatchBuffer) - pos,
        "]}\n"
    );

    if(
        (written < 0) ||
        ((size_t)written >= (sizeof(identiumTagBatchBuffer) - pos))
    )
    {
        return 0U;
    }

    pos = (uint16_t)(pos + written);

    *tagCount = count;
    *messageLength = pos;
    return 1U;
}

static void Identium_Protocol_FlushTags(
    uint32_t nowMs
)
{
    uint16_t flushBudget;

    if(
        (identiumTagQueueCount == 0U) ||
        (Identium_Protocol_IsConnected() == 0U)
    )
    {
        return;
    }

    /* HTTP sends one POST at a time and waits for the response, so a
     * few tags per message cap the upload at a few tags per round trip
     * and a busy reader overflows the queue. Wait until the previous
     * POST is done, then send as much of the queue as fits in one 8 KB
     * POST (about 130 tags with 96-bit EPCs). */
    if(ActiveUpload_Transport_IsHttp() != 0U)
    {
        uint16_t batchCount;
        uint16_t messageLength;

        if(
            (ActiveUpload_Transport_IsHttpBusy() != 0U) ||
            (
                (Configuration_Service_GetDataAggregationMode() != 0U) &&
                (
                    (identiumTagAggregationActive == 0U) ||
                    ((nowMs - identiumTagAggregationStartMs) <
                     Configuration_Service_GetDataAggregationTimeMs())
                )
            )
        )
        {
            return;
        }

        if(
            (Identium_Protocol_BuildTagBatch(
                IDENTIUM_TAG_QUEUE_SIZE,
                IDENTIUM_TAG_BATCH_BUFFER_SIZE - 1U,
                &batchCount,
                &messageLength
            ) != 0U) &&
            (ActiveUpload_Transport_SendComplete(
                (const uint8_t *)identiumTagBatchBuffer,
                messageLength
            ) == messageLength)
        )
        {
            Identium_Protocol_TagQueuePop(batchCount);
        }

        return;
    }

    if(Configuration_Service_GetDataAggregationMode() == 0U)
    {
        flushBudget = IDENTIUM_TAG_FLUSH_LIMIT;
    }
    else
    {
        if(
            (identiumTagAggregationActive == 0U) ||
            ((nowMs - identiumTagAggregationStartMs) <
             Configuration_Service_GetDataAggregationTimeMs())
        )
        {
            return;
        }

        flushBudget = IDENTIUM_TAG_BATCH_MAX_TAGS;
    }

    while(
        (flushBudget > 0U) &&
        (identiumTagQueueCount > 0U)
    )
    {
        uint16_t batchCount;
        uint16_t messageLength;

        if(
            Identium_Protocol_BuildTagBatch(
                flushBudget,
                Configuration_Service_GetSBufSize(),
                &batchCount,
                &messageLength
            ) == 0U
        )
        {
            return;
        }

        if(
            ActiveUpload_Transport_SendComplete(
                (const uint8_t *)identiumTagBatchBuffer,
                messageLength
            ) != messageLength
        )
        {
            /* Keep the complete batch intact and retry later. */
            return;
        }

        Identium_Protocol_TagQueuePop(batchCount);

        if(Configuration_Service_GetDataAggregationMode() != 0U)
        {
            /* One aggregation window per cycle.  Do not start the next
             * window from the age of the oldest queued tag, otherwise a
             * backlog would be transmitted continuously in a tight loop. */
            if(identiumTagQueueCount > 0U)
            {
                identiumTagAggregationStartMs = nowMs;
                identiumTagAggregationActive = 1U;
            }
            break;
        }

        flushBudget--;
    }
}

static void Identium_Protocol_UpdateNetworkRecovery(
    uint32_t nowMs,
    uint8_t connected
)
{
    uint32_t timeoutMs;

    if(connected != 0U)
    {
        identiumNetworkTimerActive = 0U;
        return;
    }

    if(Configuration_Service_GetNetworkWatchdogMinutes() == 0U)
    {
        return;
    }

    if(identiumNetworkTimerActive == 0U)
    {
        identiumNetworkTimerActive = 1U;
        identiumDisconnectedSinceMs = nowMs;
        return;
    }

    if(Configuration_Service_GetNetworkWatchdogMinutes() > (UINT32_MAX / 60000U))
    {
        return;
    }

    timeoutMs =
        Configuration_Service_GetNetworkWatchdogMinutes() * 60000U;

    if((nowMs - identiumDisconnectedSinceMs) >= timeoutMs)
    {
        NVIC_SystemReset();
    }
}

static void Identium_Protocol_UpdateInventoryRecovery(
    uint32_t nowMs
)
{
    uint8_t inventoryRunning;
    uint32_t timeoutMs;

    if(Configuration_Service_GetInventoryWatchdogMinutes() == 0U)
    {
        identiumInventoryTimerActive = 0U;
        return;
    }

    inventoryRunning =
        SILION_Application_IsInventoryRunning();

    if(inventoryRunning == 0U)
    {
        identiumInventoryTimerActive = 0U;
        return;
    }

    if(identiumInventoryTimerActive == 0U)
    {
        identiumInventoryTimerActive = 1U;
        identiumInventoryQuietSinceMs = nowMs;
        return;
    }

    if(Configuration_Service_GetInventoryWatchdogMinutes() > (UINT32_MAX / 60000U))
    {
        return;
    }

    timeoutMs =
        Configuration_Service_GetInventoryWatchdogMinutes() * 60000U;

    if((nowMs - identiumInventoryQuietSinceMs) >= timeoutMs)
    {
        NVIC_SystemReset();
    }
}

void Identium_Protocol_Init(void)
{
    identiumRxIndex = 0U;
    identiumRxLine[0] = '\0';
    identiumRxDiscardLine = 0U;
    identiumRxSeekingObject = 1U;

    identiumHeartbeatCounter = 0U;
    identiumLastHeartbeatMs = 0U;
    identiumWasConnected = 0U;
    identiumSyncTimeRequested = 0U;

    identiumTagQueueHead = 0U;
    identiumTagQueueTail = 0U;
    identiumTagQueueUsed = 0U;
    identiumTagQueueCount = 0U;
    identiumTagAggregationStartMs = 0U;
    identiumTagAggregationActive = 0U;

    identiumNetworkTimerActive = 0U;
    identiumDisconnectedSinceMs = 0U;
    identiumInventoryTimerActive = 0U;
    identiumInventoryQuietSinceMs = 0U;

    Identium_Command_Init();
}

void Identium_Protocol_PushTcpByte(
    uint8_t receivedByte
)
{
    if(receivedByte == '\r')
    {
        return;
    }

    if(receivedByte == '\n')
    {
        if(
            (identiumRxDiscardLine == 0U) &&
            (identiumRxIndex > 0U)
        )
        {
            identiumRxLine[identiumRxIndex] = '\0';
            Identium_Command_ProcessJson(
                identiumRxLine,
                Identium_Protocol_SendCommandReply
            );
        }

        identiumRxIndex = 0U;
        identiumRxLine[0] = '\0';
        identiumRxDiscardLine = 0U;
        identiumRxSeekingObject = 1U;
        return;
    }

    if(identiumRxDiscardLine != 0U)
    {
        return;
    }

    /* ReaderSense accepts whitespace before JSON and the protocol
     * conformance test deliberately includes garbage before a valid frame.
     * Keep seeking until the first object marker rather than poisoning the
     * whole line with the garbage prefix. */
    if(identiumRxSeekingObject != 0U)
    {
        if(
            (receivedByte == ' ') ||
            (receivedByte == '\t')
        )
        {
            return;
        }

        if(receivedByte != '{')
        {
            return;
        }

        identiumRxSeekingObject = 0U;
    }

    (void)Identium_Protocol_AppendByte(
        receivedByte
    );
}

uint8_t Identium_Protocol_SendHeartbeat(void)
{
    char buffer[192U];
    uint32_t nextCounter;
    int written;

    nextCounter = identiumHeartbeatCounter + 1U;

    written = snprintf(
        buffer,
        sizeof(buffer),
        "{\"reader_name\":\"%s\",\"event_type\":\"heart_beat\",\"event_data\":%lu}\n",
        ReaderSettings_GetName(),
        (unsigned long)nextCounter
    );

    if(
        (written < 0) ||
        ((size_t)written >= sizeof(buffer))
    )
    {
        return 0U;
    }

    if(Identium_Protocol_EventEnabled(2U) == 0U)
    {
        return 1U;
    }

    if(Identium_Protocol_SendJsonLine(buffer) == 0U)
    {
        return 0U;
    }

    identiumHeartbeatCounter = nextCounter;
    return 1U;
}

uint32_t Identium_Protocol_GetTagsDropped(void)
{
    return identiumTagDropped;
}

uint32_t Identium_Protocol_GetTagsExpired(void)
{
    return identiumTagExpired;
}

uint8_t Identium_Protocol_ReportTag(
    const SILION_Tag_t *tag
)
{
    uint32_t nowMs;

    if(tag == NULL)
    {
        return 0U;
    }

    nowMs = Time_GetTickMs();

    if(Identium_Protocol_EventEnabled(1U) == 0U)
    {
        return 1U;
    }

    if(Identium_Protocol_QueueTag(tag, nowMs) == 0U)
    {
        return 0U;
    }

    /* A real tag read resets the inventory inactivity timer. */
    identiumInventoryQuietSinceMs = nowMs;
    identiumInventoryTimerActive = 1U;

    return 1U;
}

void Identium_Protocol_ReportGpiChange(
    const uint8_t states[4]
)
{
    static char buffer[320U];
    char timestamp[21U];
    int written;

    if(states == NULL)
    {
        return;
    }

    if(Identium_Protocol_EventEnabled(3U) == 0U)
    {
        return;
    }

    if(
        Identium_Protocol_FormatUint64(
            timestamp,
            sizeof(timestamp),
            Time_GetEpochMs()
        ) == 0U
    )
    {
        return;
    }

    written = snprintf(
        buffer,
        sizeof(buffer),
        "{\"reader_name\":\"%s\",\"event_type\":\"gpi_changed\",\"event_data\":{"
        "\"gpi_states\":[{\"gpi\":1,\"state\":%u},{\"gpi\":2,\"state\":%u},"
        "{\"gpi\":3,\"state\":%u},{\"gpi\":4,\"state\":%u}],\"timestamp\":%s}}\n",
        ReaderSettings_GetName(),
        (unsigned int)states[0],
        (unsigned int)states[1],
        (unsigned int)states[2],
        (unsigned int)states[3],
        timestamp
    );

    if(
        (written < 0) ||
        ((size_t)written >= sizeof(buffer))
    )
    {
        return;
    }

    (void)Identium_Protocol_SendJsonLine(buffer);
}

uint8_t Identium_Protocol_IsConnected(void)
{
    return ActiveUpload_Transport_IsConnected();
}

uint8_t Identium_Protocol_ReportReaderException(
    uint32_t errorCode,
    const char *errorString
)
{
    return Identium_Protocol_SendReaderException(errorCode, errorString);
}

void Identium_Protocol_Task(void)
{
    uint32_t now;
    uint32_t heartbeatPeriodMs;
    uint8_t connected;

    now = Time_GetTickMs();
    connected = Identium_Protocol_IsConnected();

    heartbeatPeriodMs =
        Configuration_Service_GetHeartbeatSeconds() * 1000U;

    /* Delayed operations such as the mandated configuration reboot must
     * continue even if the TCP socket closes immediately after the reply. */
    Identium_Command_Task(now);

    Identium_Protocol_UpdateNetworkRecovery(
        now,
        connected
    );

    Identium_Protocol_UpdateInventoryRecovery(now);
    Identium_Protocol_DiscardStaleTags(now);

    if(connected == 0U)
    {
        identiumWasConnected = 0U;
        return;
    }

    if(identiumWasConnected == 0U)
    {
        identiumWasConnected = 1U;

        /* The protocol requests time synchronisation at boot.  A TCP
         * connection is the first opportunity to deliver that event.  Do it
         * once per boot rather than once per reconnect. */
        if(identiumSyncTimeRequested == 0U)
        {
            if(Identium_Protocol_SendSyncTimeRequest() != 0U)
            {
                identiumSyncTimeRequested = 1U;
            }
        }

        /* First heartbeat immediately after a connection is established. */
        identiumLastHeartbeatMs =
            now - heartbeatPeriodMs;
    }

    if((now - identiumLastHeartbeatMs) >= heartbeatPeriodMs)
    {
        if(Identium_Protocol_SendHeartbeat() != 0U)
        {
            identiumLastHeartbeatMs = now;
        }
    }

    /* Heartbeat has priority after connect; tag traffic is drained after
     * the reader has announced that the session is alive. */
    Identium_Protocol_FlushTags(now);
}
