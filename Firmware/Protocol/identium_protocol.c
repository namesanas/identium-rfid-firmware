#include "identium_protocol.h"

#include "../Config/product_config.h"
#include "../Application/Silion/sillion_application.h"
#include "../Services/ethernet/ethernet_service.h"
#include "../Services/Time/time_service.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>


static char identiumRxLine[IDENTIUM_PROTOCOL_RX_BUFFER_SIZE];
static uint16_t identiumRxIndex = 0U;
static uint8_t identiumRxDiscardLine = 0U;

static uint32_t identiumHeartbeatCounter = 0U;
static uint32_t identiumLastHeartbeatMs = 0U;
static uint8_t identiumWasConnected = 0U;
static uint8_t identiumRebootPending = 0U;
static uint32_t identiumRebootDeadlineMs = 0U;

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

static uint8_t Identium_Protocol_ExtractCommandType(
    const char *json,
    char *command,
    uint16_t commandSize
)
{
    const char *key;
    const char *cursor;
    uint16_t index = 0U;

    if(
        (json == NULL) ||
        (command == NULL) ||
        (commandSize < 2U)
    )
    {
        return 0U;
    }

    key = strstr(
        json,
        "\"command_type\""
    );

    if(key == NULL)
    {
        return 0U;
    }

    cursor = key + strlen("\"command_type\"");

    while((*cursor == ' ') || (*cursor == '\t') || (*cursor == '\r') || (*cursor == '\n'))
    {
        cursor++;
    }

    if(*cursor != ':')
    {
        return 0U;
    }

    cursor++;

    while((*cursor == ' ') || (*cursor == '\t') || (*cursor == '\r') || (*cursor == '\n'))
    {
        cursor++;
    }

    if(*cursor != '"')
    {
        return 0U;
    }

    cursor++;

    while(*cursor != '\0' && *cursor != '"')
    {
        if(*cursor == '\\')
        {
            return 0U;
        }

        if(index >= (commandSize - 1U))
        {
            return 0U;
        }

        command[index++] = *cursor++;
    }

    if(*cursor != '"')
    {
        return 0U;
    }

    command[index] = '\0';

    return (index > 0U) ? 1U : 0U;
}

static uint8_t Identium_Protocol_SendCommandReply(
    const char *commandType,
    uint8_t errCode,
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

static void Identium_Protocol_ProcessCommand(
    const char *json
)
{
    char commandType[64];

    if(
        Identium_Protocol_ExtractCommandType(
            json,
            commandType,
            sizeof(commandType)
        ) == 0U
    )
    {
        (void)Identium_Protocol_SendCommandReply(
            "",
            IDENTIUM_PROTOCOL_ERR_INVALID_COMMAND,
            "invalid command_type"
        );

        return;
    }

    if(strcmp(commandType, "pause_inventory") == 0)
    {
        if(SILION_Application_IsInventoryRunning() == 0U)
        {
            (void)Identium_Protocol_SendCommandReply(
                commandType,
                IDENTIUM_PROTOCOL_ERR_OK,
                "ok"
            );

            return;
        }

        if(SILION_Application_StopInventory() != 0U)
        {
            (void)Identium_Protocol_SendCommandReply(
                commandType,
                IDENTIUM_PROTOCOL_ERR_OK,
                "ok"
            );
        }
        else
        {
            (void)Identium_Protocol_SendCommandReply(
                commandType,
                IDENTIUM_PROTOCOL_ERR_EXECUTION,
                "inventory stop failed"
            );
        }

        return;
    }

    if(strcmp(commandType, "resume_inventory") == 0)
    {
        if(SILION_Application_IsInventoryRunning() != 0U)
        {
            /* Protocol specification explicitly treats this as success. */
            (void)Identium_Protocol_SendCommandReply(
                commandType,
                IDENTIUM_PROTOCOL_ERR_OK,
                "reader is inventorying now"
            );

            return;
        }

        if(SILION_Application_StartInventory() != 0U)
        {
            (void)Identium_Protocol_SendCommandReply(
                commandType,
                IDENTIUM_PROTOCOL_ERR_OK,
                "ok"
            );
        }
        else
        {
            (void)Identium_Protocol_SendCommandReply(
                commandType,
                IDENTIUM_PROTOCOL_ERR_EXECUTION,
                "inventory start failed"
            );
        }

        return;
    }

    if(strcmp(commandType, "reboot") == 0)
    {
        (void)Identium_Protocol_SendCommandReply(
            commandType,
            IDENTIUM_PROTOCOL_ERR_OK,
            "ok"
        );

        identiumRebootPending = 1U;
        identiumRebootDeadlineMs =
            Time_GetTickMs() + 100U;

        return;
    }

    /* GPIO and full configuration commands are migrated in later stages. */
    if(
        (strcmp(commandType, "set_gpo") == 0) ||
        (strcmp(commandType, "get_gpi") == 0) ||
        (strcmp(commandType, "sync_time") == 0) ||
        (strcmp(commandType, "set_static_conf") == 0) ||
        (strcmp(commandType, "set_temp_static_conf") == 0) ||
        (strcmp(commandType, "set_runtime_conf") == 0) ||
        (strcmp(commandType, "update_fw_by_http") == 0)
    )
    {
        (void)Identium_Protocol_SendCommandReply(
            commandType,
            IDENTIUM_PROTOCOL_ERR_NOT_SUPPORTED,
            "not supported operation"
        );

        return;
    }

    (void)Identium_Protocol_SendCommandReply(
        commandType,
        IDENTIUM_PROTOCOL_ERR_INVALID_COMMAND,
        "invalid command_type"
    );
}

void Identium_Protocol_Init(void)
{
    identiumRxIndex = 0U;
    identiumRxLine[0] = '\0';
    identiumRxDiscardLine = 0U;

    identiumHeartbeatCounter = 0U;
    identiumLastHeartbeatMs = 0U;
    identiumWasConnected = 0U;
    identiumRebootPending = 0U;
    identiumRebootDeadlineMs = 0U;
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
            Identium_Protocol_ProcessCommand(identiumRxLine);
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

    if(
        (identiumRebootPending != 0U) &&
        ((int32_t)(now - identiumRebootDeadlineMs) >= 0)
    )
    {
        NVIC_SystemReset();
    }
}
