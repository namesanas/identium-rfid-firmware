#include "identium_protocol.h"
#include "identium_command.h"

#include "../Config/product_config.h"
#include "../Application/Silion/sillion_application.h"
#include "../Services/ethernet/ethernet_service.h"
#include "../Services/Time/time_service.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>


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

static char identiumRxLine[IDENTIUM_PROTOCOL_RX_BUFFER_SIZE];
static uint16_t identiumRxIndex = 0U;
static uint8_t identiumRxDiscardLine = 0U;

static uint32_t identiumHeartbeatCounter = 0U;
static uint32_t identiumLastHeartbeatMs = 0U;
static uint8_t identiumWasConnected = 0U;

static uint8_t Identium_Protocol_AppendByte(
    uint8_t byte
)
{
    if(identiumRxIndex >= (IDENTIUM_PROTOCOL_RX_BUFFER_SIZE - 1U))
    {
        identiumRxIndex = 0U;
        identiumRxLine[0] = '\0';
        identiumRxDiscardLine = 1U;
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

    sent = Ethernet_Service_Send(
        (const uint8_t *)json,
        length
    );

    return (sent == length) ? 1U : 0U;
}

static uint8_t Identium_Protocol_SendCommandReply(
    const char *commandType,
    uint32_t errCode,
    const char *errString
)
{
    char buffer[384];
    int written;

    if(
        (commandType == NULL) ||
        (errString == NULL)
    )
    {
        return 0U;
    }

    written = snprintf(
        buffer,
        sizeof(buffer),
        "{\"reader_name\":\"%s\",\"event_type\":\"command_reply\",\"event_data\":{"
        "\"command_type\":\"%s\",\"err_code\":%u,\"err_string\":\"%s\"}}\n",
        PRODUCT_PROTOCOL_READER_NAME,
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

void Identium_Protocol_Init(void)
{
    identiumRxIndex = 0U;
    identiumRxLine[0] = '\0';
    identiumRxDiscardLine = 0U;

    identiumHeartbeatCounter = 0U;
    identiumLastHeartbeatMs = 0U;
    identiumWasConnected = 0U;
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
        return;
    }

    if(identiumRxDiscardLine != 0U)
    {
        return;
    }

    (void)Identium_Protocol_AppendByte(
        receivedByte
    );
}

uint8_t Identium_Protocol_SendHeartbeat(void)
{
    char buffer[192];
    int written;

    identiumHeartbeatCounter++;

    written = snprintf(
        buffer,
        sizeof(buffer),
        "{\"reader_name\":\"%s\",\"event_type\":\"heart_beat\",\"event_data\":%lu}\n",
        PRODUCT_PROTOCOL_READER_NAME,
        (unsigned long)identiumHeartbeatCounter
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

uint8_t Identium_Protocol_ReportTag(
    const SILION_Tag_t *tag
)
{
    char buffer[320];
    uint16_t pos = 0U;
    uint16_t i;
    int written;

    if(tag == NULL)
    {
        return 0U;
    }

    written = snprintf(
        &buffer[pos],
        sizeof(buffer) - pos,
        "{\"reader_name\":\"%s\",\"event_type\":\"tag_read\",\"event_data\":[{\"ep\":\"",
        PRODUCT_PROTOCOL_READER_NAME
    );

    if(
        (written < 0) ||
        ((size_t)written >= (sizeof(buffer) - pos))
    )
    {
        return 0U;
    }

    pos = (uint16_t)(pos + written);

    for(i = 0U; i < tag->epcLengthBytes; i++)
    {
        written = snprintf(
            &buffer[pos],
            sizeof(buffer) - pos,
            "%02X",
            tag->epc[i]
        );

        if(
            (written < 0) ||
            ((size_t)written >= (sizeof(buffer) - pos))
        )
        {
            return 0U;
        }

        pos = (uint16_t)(pos + written);
    }

    written = snprintf(
        &buffer[pos],
        sizeof(buffer) - pos,
        "\",\"at\":%u,\"rc\":%u,\"ri\":%d,\"fq\":%lu}]}\n",
        (unsigned int)tag->antenna,
        (unsigned int)tag->readCount,
        (int)tag->rssi,
        (unsigned long)tag->frequencyKHz
    );

    if(
        (written < 0) ||
        ((size_t)written >= (sizeof(buffer) - pos))
    )
    {
        return 0U;
    }

    pos = (uint16_t)(pos + written);

    return Identium_Protocol_SendJsonLine(
        buffer
    );
}

void Identium_Protocol_ReportGpiChange(
    const uint8_t states[4]
)
{
    char buffer[320];
    char timestamp[21];
    int written;

    if(states == NULL)
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
        PRODUCT_PROTOCOL_READER_NAME,
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
    return Ethernet_Service_IsConnected();
}

void Identium_Protocol_Task(void)
{
    uint32_t now;
    uint32_t heartbeatPeriodMs;
    uint8_t connected;

    connected = Identium_Protocol_IsConnected();
    now = Time_GetTickMs();
    heartbeatPeriodMs =
        IDENTIUM_PROTOCOL_HEARTBEAT_SECONDS * 1000U;

    if(connected == 0U)
    {
        identiumWasConnected = 0U;
        return;
    }

    if(identiumWasConnected == 0U)
    {
        identiumWasConnected = 1U;

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

    Identium_Command_Task(now);
}
