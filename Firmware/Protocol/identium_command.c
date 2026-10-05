#include "identium_command.h"

#include "identium_protocol.h"
#include "../Application/Silion/sillion_application.h"
#include "../Services/Time/time_service.h"
#include "../Services/GPIO/gpio_service.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint8_t identiumRebootPending = 0U;
static uint32_t identiumRebootDeadlineMs = 0U;

static void Identium_Command_SkipWhitespace(
    const char **cursor
)
{
    while(
        (**cursor == ' ') ||
        (**cursor == '\t') ||
        (**cursor == '\r') ||
        (**cursor == '\n')
    )
    {
        (*cursor)++;
    }
}

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
    Identium_Command_SkipWhitespace(&cursor);

    if(*cursor != ':')
    {
        return 0U;
    }

    cursor++;
    Identium_Command_SkipWhitespace(&cursor);

    if(*cursor != '\"')
    {
        return 0U;
    }

    cursor++;

    while((*cursor != '\0') && (*cursor != '\"'))
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

    if(*cursor != '\"')
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
    Identium_Command_SkipWhitespace(&cursor);

    if(*cursor != ':')
    {
        return 0U;
    }

    cursor++;
    Identium_Command_SkipWhitespace(&cursor);

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

static const char *Identium_Command_FindObjectKey(
    const char *objectStart,
    const char *objectEnd,
    const char *keyName
)
{
    const char *cursor = objectStart;
    size_t keyLength;

    if(
        (objectStart == NULL) ||
        (objectEnd == NULL) ||
        (keyName == NULL) ||
        (objectStart >= objectEnd)
    )
    {
        return NULL;
    }

    keyLength = strlen(keyName);

    while(cursor < objectEnd)
    {
        const char *match = strstr(cursor, keyName);

        if(
            (match == NULL) ||
            (match >= objectEnd)
        )
        {
            return NULL;
        }

        if(
            ((match + keyLength) < objectEnd) &&
            (match[keyLength] == '"')
        )
        {
            /*
             * keyName already begins with a quote.  Verify that the quote
             * starts an object member rather than appearing inside a JSON
             * string value.  Whitespace before a member name is allowed.
             */
            const char *boundary = match;

            while(boundary > objectStart)
            {
                if(
                    (boundary[-1] == ' ') ||
                    (boundary[-1] == '\t') ||
                    (boundary[-1] == '\r') ||
                    (boundary[-1] == '\n')
                )
                {
                    boundary--;
                    continue;
                }

                if(
                    (boundary[-1] == '{') ||
                    (boundary[-1] == ',')
                )
                {
                    return match;
                }

                break;
            }

            if(boundary == objectStart)
            {
                return match;
            }
        }

        cursor = match + 1;
    }

    return NULL;
}

static uint8_t Identium_Command_ParseObjectUnsigned(
    const char *objectStart,
    const char *objectEnd,
    const char *key,
    uint32_t *value,
    uint8_t *missing
)
{
    const char *keyStart;
    const char *cursor;
    uint32_t parsed = 0U;
    uint8_t digits = 0U;

    if(
        (objectStart == NULL) ||
        (objectEnd == NULL) ||
        (key == NULL) ||
        (value == NULL) ||
        (missing == NULL)
    )
    {
        return 0U;
    }

    *missing = 0U;
    keyStart = Identium_Command_FindObjectKey(
        objectStart,
        objectEnd,
        key
    );

    if(keyStart == NULL)
    {
        *missing = 1U;
        return 1U;
    }

    cursor = keyStart + strlen(key);

    if((cursor >= objectEnd) || (*cursor != '"'))
    {
        return 0U;
    }

    cursor++;
    Identium_Command_SkipWhitespace(&cursor);

    if(
        (cursor >= objectEnd) ||
        (*cursor != ':')
    )
    {
        return 0U;
    }

    cursor++;
    Identium_Command_SkipWhitespace(&cursor);

    while(
        (cursor < objectEnd) &&
        (*cursor >= '0') &&
        (*cursor <= '9')
    )
    {
        uint32_t digit = (uint32_t)(*cursor - '0');

        if(parsed > ((UINT32_MAX - digit) / 10U))
        {
            return 0U;
        }

        parsed = (parsed * 10U) + digit;
        digits = 1U;
        cursor++;
    }

    if(digits == 0U)
    {
        return 0U;
    }

    *value = parsed;
    return 1U;
}

static uint8_t Identium_Command_ParseSetGpo(
    const char *json,
    uint8_t outputNumbers[],
    uint8_t states[],
    uint32_t durationsSeconds[],
    uint8_t *count,
    uint32_t *errorCode,
    const char **errorString
)
{
    const char *key;
    const char *cursor;
    uint8_t parsedCount = 0U;
    uint8_t outputNumber;
    uint8_t state;
    uint32_t durationSeconds;
    uint32_t value;
    uint8_t missing;

    if(
        (json == NULL) ||
        (outputNumbers == NULL) ||
        (states == NULL) ||
        (durationsSeconds == NULL) ||
        (count == NULL) ||
        (errorCode == NULL) ||
        (errorString == NULL)
    )
    {
        return 0U;
    }

    key = strstr(json, "\"command_data\"");
    if(key == NULL)
    {
        *errorCode = 20004U;
        *errorString = "set_gpo field missing";
        return 0U;
    }

    cursor = key + strlen("\"command_data\"");
    Identium_Command_SkipWhitespace(&cursor);

    if(*cursor != ':')
    {
        *errorCode = 20004U;
        *errorString = "set_gpo field missing";
        return 0U;
    }

    cursor++;
    Identium_Command_SkipWhitespace(&cursor);

    if(*cursor != '[')
    {
        *errorCode = 20003U;
        *errorString = "invalid set_gpo item";
        return 0U;
    }

    cursor++;
    Identium_Command_SkipWhitespace(&cursor);

    while(*cursor != '\0')
    {
        const char *objectStart;
        const char *objectEnd;

        if(*cursor == ']')
        {
            break;
        }

        if(*cursor != '{')
        {
            *errorCode = 20003U;
            *errorString = "invalid set_gpo item";
            return 0U;
        }

        objectStart = cursor + 1;
        objectEnd = strchr(objectStart, '}');

        if(objectEnd == NULL)
        {
            *errorCode = 20003U;
            *errorString = "invalid set_gpo item";
            return 0U;
        }

        if(parsedCount >= 4U)
        {
            *errorCode = 20003U;
            *errorString = "invalid set_gpo item";
            return 0U;
        }

        if(
            Identium_Command_ParseObjectUnsigned(
                objectStart,
                objectEnd,
                "\"gpo",
                &value,
                &missing
            ) == 0U
        )
        {
            *errorCode = 20003U;
            *errorString = "invalid set_gpo item";
            return 0U;
        }

        if(missing != 0U)
        {
            *errorCode = 20004U;
            *errorString = "set_gpo field missing";
            return 0U;
        }

        outputNumber = (uint8_t)value;

        if(
            Identium_Command_ParseObjectUnsigned(
                objectStart,
                objectEnd,
                "\"state",
                &value,
                &missing
            ) == 0U
        )
        {
            *errorCode = 20003U;
            *errorString = "invalid set_gpo item";
            return 0U;
        }

        if(missing != 0U)
        {
            *errorCode = 20004U;
            *errorString = "set_gpo field missing";
            return 0U;
        }

        state = (uint8_t)value;

        if(
            Identium_Command_ParseObjectUnsigned(
                objectStart,
                objectEnd,
                "\"duration",
                &value,
                &missing
            ) == 0U
        )
        {
            *errorCode = 20003U;
            *errorString = "invalid set_gpo item";
            return 0U;
        }

        if(missing != 0U)
        {
            *errorCode = 20004U;
            *errorString = "set_gpo field missing";
            return 0U;
        }

        durationSeconds = value;

        outputNumbers[parsedCount] = outputNumber;
        states[parsedCount] = state;
        durationsSeconds[parsedCount] = durationSeconds;
        parsedCount++;

        cursor = objectEnd + 1;
        Identium_Command_SkipWhitespace(&cursor);

        if(*cursor == ',')
        {
            cursor++;
            Identium_Command_SkipWhitespace(&cursor);
            continue;
        }

        if(*cursor == ']')
        {
            break;
        }

        *errorCode = 20003U;
        *errorString = "invalid set_gpo item";
        return 0U;
    }

    if(parsedCount == 0U)
    {
        *errorCode = 20003U;
        *errorString = "invalid set_gpo item";
        return 0U;
    }

    if(*cursor != ']')
    {
        *errorCode = 20003U;
        *errorString = "invalid set_gpo item";
        return 0U;
    }

    *count = parsedCount;
    *errorCode = 0U;
    *errorString = "ok";
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

    if(strcmp(commandType, "set_gpo") == 0)
    {
        uint8_t outputNumbers[4];
        uint8_t states[4];
        uint32_t durationsSeconds[4];
        uint8_t count;
        uint32_t errorCode;
        const char *errorString;

        if(
            Identium_Command_ParseSetGpo(
                json,
                outputNumbers,
                states,
                durationsSeconds,
                &count,
                &errorCode,
                &errorString
            ) == 0U
        )
        {
            (void)Identium_Command_SendReply(
                replyCallback,
                commandType,
                errorCode,
                errorString
            );
            return;
        }

        if(
            GPIO_Service_SetOutputs(
                outputNumbers,
                states,
                durationsSeconds,
                count,
                &errorCode,
                &errorString
            ) != GPIO_SERVICE_OK
        )
        {
            (void)Identium_Command_SendReply(
                replyCallback,
                commandType,
                errorCode,
                errorString
            );
            return;
        }

        (void)Identium_Command_SendReply(
            replyCallback,
            commandType,
            0U,
            "ok"
        );
        return;
    }

    if(strcmp(commandType, "get_gpi") == 0)
    {
        uint8_t states[INDUSTRIAL_IO_COUNT];

        GPIO_Service_GetAllInputs(states);
        Identium_Protocol_ReportGpiChange(states);

        (void)Identium_Command_SendReply(
            replyCallback,
            commandType,
            0U,
            "ok"
        );
        return;
    }

    if(
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
