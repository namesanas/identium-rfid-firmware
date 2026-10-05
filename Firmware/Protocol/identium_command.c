#include "identium_command.h"

#include "identium_protocol.h"
#include "../Application/Silion/sillion_application.h"
#include "../Services/Time/time_service.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint8_t identiumRebootPending = 0U;
static uint32_t identiumRebootDeadlineMs = 0U;

static uint8_t Identium_Command_ExtractCommandType(
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

    key = strstr(json, "\"command_type\"");

    if(key == NULL)
    {
        return 0U;
    }

    cursor = key + strlen("\"command_type\"");

    while(
        (*cursor == ' ') ||
        (*cursor == '\t') ||
        (*cursor == '\r') ||
        (*cursor == '\n')
    )
    {
        cursor++;
    }

    if(*cursor != ':')
    {
        return 0U;
    }

    cursor++;

    while(
        (*cursor == ' ') ||
        (*cursor == '\t') ||
        (*cursor == '\r') ||
        (*cursor == '\n')
    )
    {
        cursor++;
    }

    if(*cursor != '"')
    {
        return 0U;
    }

    cursor++;

    while((*cursor != '\0') && (*cursor != '"'))
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

static uint8_t Identium_Command_ExtractUnsigned64(
    const char *json,
    uint64_t *value
)
{
    const char *key;
    const char *cursor;
    uint64_t parsed = 0U;
    uint8_t digitCount = 0U;

    if((json == NULL) || (value == NULL))
    {
        return 0U;
    }

    key = strstr(json, "\"command_data\"");

    if(key == NULL)
    {
        return 0U;
    }

    cursor = key + strlen("\"command_data\"");

    while(
        (*cursor == ' ') ||
        (*cursor == '\t') ||
        (*cursor == '\r') ||
        (*cursor == '\n')
    )
    {
        cursor++;
    }

    if(*cursor != ':')
    {
        return 0U;
    }

    cursor++;

    while(
        (*cursor == ' ') ||
        (*cursor == '\t') ||
        (*cursor == '\r') ||
        (*cursor == '\n')
    )
    {
        cursor++;
    }

    while((*cursor >= '0') && (*cursor <= '9'))
    {
        uint8_t digit = (uint8_t)(*cursor - '0');

        if(parsed > ((UINT64_MAX - digit) / 10U))
        {
            return 0U;
        }

        parsed = (parsed * 10U) + digit;
        digitCount++;
        cursor++;
    }

    if(digitCount == 0U)
    {
        return 0U;
    }

    *value = parsed;

    return 1U;
}

static uint8_t Identium_Command_SendReply(
    Identium_CommandReplyCallback_t replyCallback,
    const char *commandType,
    uint32_t errCode,
    const char *errString
)
{
    if(replyCallback == NULL)
    {
        return 0U;
    }

    return replyCallback(
        commandType,
        errCode,
        errString
    );
}

void Identium_Command_Init(void)
{
    identiumRebootPending = 0U;
    identiumRebootDeadlineMs = 0U;
}

void Identium_Command_ProcessJson(
    const char *json,
    Identium_CommandReplyCallback_t replyCallback
)
{
    char commandType[64];

    if(
        Identium_Command_ExtractCommandType(
            json,
            commandType,
            sizeof(commandType)
        ) == 0U
    )
    {
        (void)Identium_Command_SendReply(
            replyCallback,
            "",
            IDENTIUM_COMMAND_ERR_INVALID_COMMAND,
            "invalid command_type"
        );

        return;
    }

    if(strcmp(commandType, "pause_inventory") == 0)
    {
        if(SILION_Application_IsInventoryRunning() == 0U)
        {
            (void)Identium_Command_SendReply(
                replyCallback,
                commandType,
                IDENTIUM_COMMAND_ERR_OK,
                "ok"
            );

            return;
        }

        if(SILION_Application_StopInventory() != 0U)
        {
            (void)Identium_Command_SendReply(
                replyCallback,
                commandType,
                IDENTIUM_COMMAND_ERR_OK,
                "ok"
            );
        }
        else
        {
            (void)Identium_Command_SendReply(
                replyCallback,
                commandType,
                IDENTIUM_COMMAND_ERR_EXECUTION,
                "inventory stop failed"
            );
        }

        return;
    }

    if(strcmp(commandType, "resume_inventory") == 0)
    {
        if(SILION_Application_IsInventoryRunning() != 0U)
        {
            /* Protocol: desired state is already reached, treat as success. */
            (void)Identium_Command_SendReply(
                replyCallback,
                commandType,
                IDENTIUM_COMMAND_ERR_OK,
                "reader is inventorying now"
            );

            return;
        }

        if(SILION_Application_StartInventory() != 0U)
        {
            (void)Identium_Command_SendReply(
                replyCallback,
                commandType,
                IDENTIUM_COMMAND_ERR_OK,
                "ok"
            );
        }
        else
        {
            (void)Identium_Command_SendReply(
                replyCallback,
                commandType,
                IDENTIUM_COMMAND_ERR_EXECUTION,
                "inventory start failed"
            );
        }

        return;
    }

    if(strcmp(commandType, "sync_time") == 0)
    {
        uint64_t epochMs;

        if(
            (Identium_Command_ExtractUnsigned64(json, &epochMs) == 0U) ||
            (epochMs < 1000000000000ULL)
        )
        {
            (void)Identium_Command_SendReply(
                replyCallback,
                commandType,
                IDENTIUM_COMMAND_ERR_EXECUTION,
                "invalid epoch milliseconds"
            );

            return;
        }

        Time_Service_SetEpochMs(epochMs);

        (void)Identium_Command_SendReply(
            replyCallback,
            commandType,
            IDENTIUM_COMMAND_ERR_OK,
            "ok"
        );

        return;
    }

    if(strcmp(commandType, "reboot") == 0)
    {
        (void)Identium_Command_SendReply(
            replyCallback,
            commandType,
            IDENTIUM_COMMAND_ERR_OK,
            "ok"
        );

        identiumRebootPending = 1U;
        identiumRebootDeadlineMs =
            Time_GetTickMs() + 100U;

        return;
    }

    /* GPO/GPI and configuration commands are migrated in later stages. */
    if(
        (strcmp(commandType, "set_gpo") == 0) ||
        (strcmp(commandType, "get_gpi") == 0) ||
        (strcmp(commandType, "set_static_conf") == 0) ||
        (strcmp(commandType, "set_temp_static_conf") == 0) ||
        (strcmp(commandType, "set_runtime_conf") == 0) ||
        (strcmp(commandType, "update_fw_by_http") == 0)
    )
    {
        (void)Identium_Command_SendReply(
            replyCallback,
            commandType,
            IDENTIUM_COMMAND_ERR_NOT_SUPPORTED,
            "not supported operation"
        );

        return;
    }

    (void)Identium_Command_SendReply(
        replyCallback,
        commandType,
        IDENTIUM_COMMAND_ERR_INVALID_COMMAND,
        "invalid command_type"
    );
}

void Identium_Command_Task(
    uint32_t nowMs
)
{
    if(
        (identiumRebootPending != 0U) &&
        ((int32_t)(nowMs - identiumRebootDeadlineMs) >= 0)
    )
    {
        identiumRebootPending = 0U;
        NVIC_SystemReset();
    }
}
