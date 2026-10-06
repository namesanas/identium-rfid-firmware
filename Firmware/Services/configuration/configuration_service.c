#include "configuration_service.h"

#include "configuration_storage.h"
#include "../../Config/product_config.h"
#include "../../Application/Silion/sillion_application.h"
#include "../../Services/GPIO/gpio_service.h"
#include "../../Protocols/silion/silion_protocol.h"
#include "../../Protocol/identium_protocol.h"

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern SILION_ReaderConfig_t readerConfig;

static Configuration_StorageData_t g_persistentConfiguration;
static Configuration_StorageData_t g_liveConfiguration;
static uint8_t g_initialized = 0U;

/* ----------------------------- JSON parser ----------------------------- */

typedef struct
{
    const char *p;
    const char *end;
} Configuration_JsonCursor_t;

static void Configuration_JsonSkipWhitespace(
    Configuration_JsonCursor_t *cursor
)
{
    while(
        (cursor->p < cursor->end) &&
        (
            (*cursor->p == ' ') ||
            (*cursor->p == '\t') ||
            (*cursor->p == '\r') ||
            (*cursor->p == '\n')
        )
    )
    {
        cursor->p++;
    }
}

static uint8_t Configuration_JsonConsume(
    Configuration_JsonCursor_t *cursor,
    char expected
)
{
    Configuration_JsonSkipWhitespace(cursor);

    if(
        (cursor->p >= cursor->end) ||
        (*cursor->p != expected)
    )
    {
        return 0U;
    }

    cursor->p++;
    return 1U;
}

static uint8_t Configuration_JsonParseString(
    Configuration_JsonCursor_t *cursor,
    char *value,
    uint16_t valueSize
)
{
    uint16_t length = 0U;

    if(value == NULL || valueSize == 0U)
    {
        return 0U;
    }

    if(Configuration_JsonConsume(cursor, '"') == 0U)
    {
        return 0U;
    }

    while(cursor->p < cursor->end)
    {
        char c = *cursor->p++;

        if(c == '"')
        {
            value[length] = '\0';
            return 1U;
        }

        if(c == '\\')
        {
            /* Configuration keys/values supported in this step are ASCII. */
            if(cursor->p >= cursor->end)
            {
                return 0U;
            }

            c = *cursor->p++;
            if(
                (c != '"') &&
                (c != '\\') &&
                (c != '/')
            )
            {
                return 0U;
            }
        }
        else if((unsigned char)c < 0x20U)
        {
            return 0U;
        }

        if(length >= (uint16_t)(valueSize - 1U))
        {
            return 0U;
        }

        value[length++] = c;
    }

    return 0U;
}

static uint8_t Configuration_JsonParseUnsigned(
    Configuration_JsonCursor_t *cursor,
    uint32_t *value
)
{
    uint32_t parsed = 0U;
    uint8_t digits = 0U;

    if(value == NULL)
    {
        return 0U;
    }

    Configuration_JsonSkipWhitespace(cursor);

    while(cursor->p < cursor->end)
    {
        uint32_t digit;

        if(
            (*cursor->p < '0') ||
            (*cursor->p > '9')
        )
        {
            break;
        }

        digit = (uint32_t)(*cursor->p - '0');
        if(parsed > ((UINT32_MAX - digit) / 10U))
        {
            return 0U;
        }

        parsed = (parsed * 10U) + digit;
        digits = 1U;
        cursor->p++;
    }

    if(digits == 0U)
    {
        return 0U;
    }

    *value = parsed;
    return 1U;
}

static uint8_t Configuration_JsonSkipString(
    Configuration_JsonCursor_t *cursor
)
{
    if(Configuration_JsonConsume(cursor, '"') == 0U)
    {
        return 0U;
    }

    while(cursor->p < cursor->end)
    {
        char c = *cursor->p++;

        if(c == '"')
        {
            return 1U;
        }

        if(c == '\\')
        {
            if(cursor->p >= cursor->end)
            {
                return 0U;
            }

            c = *cursor->p++;
            if(
                (c != '"') &&
                (c != '\\') &&
                (c != '/')
            )
            {
                return 0U;
            }
        }
        else if((unsigned char)c < 0x20U)
        {
            return 0U;
        }
    }

    return 0U;
}

static uint8_t Configuration_JsonSkipValue(
    Configuration_JsonCursor_t *cursor,
    uint8_t depth
)
{
    Configuration_JsonSkipWhitespace(cursor);

    if(cursor->p >= cursor->end || depth > 12U)
    {
        return 0U;
    }

    if(*cursor->p == '"')
    {
        return Configuration_JsonSkipString(cursor);
    }

    if(
        (*cursor->p == '-') ||
        ((*cursor->p >= '0') && (*cursor->p <= '9'))
    )
    {
        uint32_t ignored;

        if(*cursor->p == '-')
        {
            cursor->p++;
            return Configuration_JsonParseUnsigned(cursor, &ignored);
        }

        return Configuration_JsonParseUnsigned(cursor, &ignored);
    }

    if(*cursor->p == '{')
    {
        cursor->p++;
        Configuration_JsonSkipWhitespace(cursor);

        if(
            (cursor->p < cursor->end) &&
            (*cursor->p == '}')
        )
        {
            cursor->p++;
            return 1U;
        }

        for(;;)
        {
            char key[64];

            if(
                Configuration_JsonParseString(
                    cursor,
                    key,
                    sizeof(key)
                ) == 0U
            )
            {
                return 0U;
            }

            if(Configuration_JsonConsume(cursor, ':') == 0U)
            {
                return 0U;
            }

            if(Configuration_JsonSkipValue(cursor, (uint8_t)(depth + 1U)) == 0U)
            {
                return 0U;
            }

            Configuration_JsonSkipWhitespace(cursor);
            if(
                (cursor->p < cursor->end) &&
                (*cursor->p == ',')
            )
            {
                cursor->p++;
                continue;
            }

            if(
                (cursor->p < cursor->end) &&
                (*cursor->p == '}')
            )
            {
                cursor->p++;
                return 1U;
            }

            return 0U;
        }
    }

    if(*cursor->p == '[')
    {
        cursor->p++;
        Configuration_JsonSkipWhitespace(cursor);

        if(
            (cursor->p < cursor->end) &&
            (*cursor->p == ']')
        )
        {
            cursor->p++;
            return 1U;
        }

        for(;;)
        {
            if(Configuration_JsonSkipValue(cursor, (uint8_t)(depth + 1U)) == 0U)
            {
                return 0U;
            }

            Configuration_JsonSkipWhitespace(cursor);
            if(
                (cursor->p < cursor->end) &&
                (*cursor->p == ',')
            )
            {
                cursor->p++;
                continue;
            }

            if(
                (cursor->p < cursor->end) &&
                (*cursor->p == ']')
            )
            {
                cursor->p++;
                return 1U;
            }

            return 0U;
        }
    }

    if(
        ((cursor->end - cursor->p) >= 4) &&
        (memcmp(cursor->p, "true", 4U) == 0)
    )
    {
        cursor->p += 4;
        return 1U;
    }

    if(
        ((cursor->end - cursor->p) >= 5) &&
        (memcmp(cursor->p, "false", 5U) == 0)
    )
    {
        cursor->p += 5;
        return 1U;
    }

    if(
        ((cursor->end - cursor->p) >= 4) &&
        (memcmp(cursor->p, "null", 4U) == 0)
    )
    {
        cursor->p += 4;
        return 1U;
    }

    return 0U;
}

static uint8_t Configuration_JsonKeyEquals(
    const char *key,
    const char *expected
)
{
    return (
        (key != NULL) &&
        (expected != NULL) &&
        (strcmp(key, expected) == 0)
    ) ? 1U : 0U;
}

/* --------------------------- configuration state ---------------------- */

static void Configuration_Service_SetResult(
    uint32_t *errorCode,
    const char **errorString,
    uint32_t code,
    const char *string
)
{
    if(errorCode != NULL)
    {
        *errorCode = code;
    }

    if(errorString != NULL)
    {
        *errorString = string;
    }
}

void Configuration_Service_Init(void)
{
    Configuration_Storage_GetDefaults(&g_persistentConfiguration);
    g_liveConfiguration = g_persistentConfiguration;
    g_initialized = 1U;
}

uint8_t Configuration_Service_LoadPersistent(void)
{
    Configuration_StorageData_t loaded;

    if(g_initialized == 0U)
    {
        return 0U;
    }

    if(Configuration_Storage_Load(&loaded) == 0U)
    {
        return 0U;
    }

    if(
        (loaded.heartbeatSeconds < 5U) ||
        (loaded.heartbeatSeconds > 7200U) ||
        (loaded.startInventoryAtBoot > 1U) ||
        (loaded.session > 3U)
    )
    {
        return 0U;
    }

    g_persistentConfiguration = loaded;
    g_liveConfiguration = loaded;
    readerConfig.session = loaded.session;

    return 1U;
}

uint32_t Configuration_Service_GetHeartbeatSeconds(void)
{
    return g_liveConfiguration.heartbeatSeconds;
}

uint8_t Configuration_Service_GetStartInventoryAtBoot(void)
{
    return g_liveConfiguration.startInventoryAtBoot;
}

uint8_t Configuration_Service_GetWorkState(void)
{
    (void)g_initialized;
    return 3U;
}

/* ------------------------------ JSON output ---------------------------- */

static int Configuration_Service_Append(
    char **cursor,
    size_t *remaining,
    const char *format,
    ...
)
{
    va_list args;
    int written;

    if(
        (cursor == NULL) ||
        (*cursor == NULL) ||
        (remaining == NULL) ||
        (format == NULL) ||
        (*remaining == 0U)
    )
    {
        return 0;
    }

    va_start(args, format);
    written = vsnprintf(*cursor, *remaining, format, args);
    va_end(args);

    if(
        (written < 0) ||
        ((size_t)written >= *remaining)
    )
    {
        return 0;
    }

    *cursor += written;
    *remaining -= (size_t)written;
    return 1;
}

static uint8_t Configuration_Service_AppendLiteral(
    char **cursor,
    size_t *remaining,
    const char *text
)
{
    size_t length;

    if(
        (cursor == NULL) ||
        (*cursor == NULL) ||
        (remaining == NULL) ||
        (text == NULL)
    )
    {
        return 0U;
    }

    length = strlen(text);
    if(length >= *remaining)
    {
        return 0U;
    }

    memcpy(*cursor, text, length);
    *cursor += length;
    *remaining -= length;
    **cursor = '\0';

    return 1U;
}

static uint8_t Configuration_Service_AppendUnsigned(
    char **cursor,
    size_t *remaining,
    uint32_t value
)
{
    char digits[11];
    uint8_t index = 0U;
    uint8_t outIndex;

    if(
        (cursor == NULL) ||
        (*cursor == NULL) ||
        (remaining == NULL)
    )
    {
        return 0U;
    }

    do
    {
        digits[index++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while(value != 0U);

    if((size_t)index >= *remaining)
    {
        return 0U;
    }

    for(outIndex = 0U; outIndex < index; outIndex++)
    {
        (*cursor)[outIndex] = digits[(uint8_t)(index - 1U - outIndex)];
    }

    *cursor += index;
    *remaining -= index;
    **cursor = '\0';

    return 1U;
}

static uint8_t Configuration_Service_AppendEscapedName(
    char **cursor,
    size_t *remaining,
    const char *source
)
{
    uint16_t index = 0U;

    if(
        (cursor == NULL) ||
        (*cursor == NULL) ||
        (remaining == NULL) ||
        (source == NULL)
    )
    {
        return 0U;
    }

    while(source[index] != '\0')
    {
        char c = source[index++];

        if((c == '"') || (c == '\\'))
        {
            if(*remaining <= 2U)
            {
                return 0U;
            }

            *(*cursor)++ = '\\';
            *(*cursor)++ = c;
            *remaining -= 2U;
        }
        else if((unsigned char)c < 0x20U)
        {
            return 0U;
        }
        else
        {
            if(*remaining <= 1U)
            {
                return 0U;
            }

            *(*cursor)++ = c;
            *remaining -= 1U;
        }
    }

    **cursor = '\0';
    return 1U;
}

uint16_t Configuration_Service_BuildActiveJson(
    char *buffer,
    uint16_t bufferSize
)
{
    char *cursor = buffer;
    size_t remaining = bufferSize;

    if((buffer == NULL) || (bufferSize == 0U))
    {
        return 0U;
    }

    *buffer = '\0';

    if(!Configuration_Service_AppendLiteral(
           &cursor,
           &remaining,
           "{\"glob_params\":{\"hb_cylce\":"))
    {
        return 0U;
    }

    if(!Configuration_Service_AppendUnsigned(
           &cursor,
           &remaining,
           g_liveConfiguration.heartbeatSeconds))
    {
        return 0U;
    }

    if(!Configuration_Service_AppendLiteral(
           &cursor,
           &remaining,
           ",\"s_buf_size\":1580,\"thwdt_net_timeout\":0,\"thwdt_inv_timeout\":0,\"start_inv_atbeg\":"))
    {
        return 0U;
    }

    if(!Configuration_Service_AppendUnsigned(
           &cursor,
           &remaining,
           g_liveConfiguration.startInventoryAtBoot))
    {
        return 0U;
    }

    if(!Configuration_Service_AppendLiteral(
           &cursor,
           &remaining,
           ",\"remote_cmd_reply\":1,\"name\":\""))
    {
        return 0U;
    }

    if(!Configuration_Service_AppendEscapedName(
           &cursor,
           &remaining,
           PRODUCT_PROTOCOL_READER_NAME))
    {
        return 0U;
    }

    if(!Configuration_Service_AppendLiteral(
           &cursor,
           &remaining,
           "\"},\"upload\":{\"client_ack\":0,\"crc_enable\":0,\"recv_timeout\":10,\"clr_r_buf_time\":8,\"hw_inf\":1,\"tcp\":{\"ser_ip\":\""))
    {
        return 0U;
    }

    if(!Configuration_Service_AppendUnsigned(&cursor, &remaining, PRODUCT_PROTOCOL_TCP_SERVER_IP_0) ||
       !Configuration_Service_AppendLiteral(&cursor, &remaining, ".") ||
       !Configuration_Service_AppendUnsigned(&cursor, &remaining, PRODUCT_PROTOCOL_TCP_SERVER_IP_1) ||
       !Configuration_Service_AppendLiteral(&cursor, &remaining, ".") ||
       !Configuration_Service_AppendUnsigned(&cursor, &remaining, PRODUCT_PROTOCOL_TCP_SERVER_IP_2) ||
       !Configuration_Service_AppendLiteral(&cursor, &remaining, ".") ||
       !Configuration_Service_AppendUnsigned(&cursor, &remaining, PRODUCT_PROTOCOL_TCP_SERVER_IP_3) ||
       !Configuration_Service_AppendLiteral(&cursor, &remaining, "\",\"ser_port\":\""))
    {
        return 0U;
    }

    if(!Configuration_Service_AppendUnsigned(
           &cursor,
           &remaining,
           PRODUCT_PROTOCOL_TCP_SERVER_PORT))
    {
        return 0U;
    }

    if(!Configuration_Service_AppendLiteral(
           &cursor,
           &remaining,
           "\"},\"data_aggr\":{\"mode\":0,\"timeval\":1000}},\"gpo_act\":[],\"events\":[1,2,3],\"tag_json_format\":[1,2,5,8],\"gpi_trigger\":{\"mode\":0,\"timeval\":0,\"cond_order\":0,\"cond_1\":[],\"cond_2\":[]},\"cus_param\":\"\"}"))
    {
        return 0U;
    }

    return (uint16_t)strlen(buffer);
}

uint16_t Configuration_Service_BuildStaticJson(
    char *buffer,
    uint16_t bufferSize
)
{
    char *cursor = buffer;
    size_t remaining = bufferSize;

    if((buffer == NULL) || (bufferSize == 0U))
    {
        return 0U;
    }

    *buffer = '\0';

    if(!Configuration_Service_Append(
           &cursor,
           &remaining,
           "{\"rf\":{\"region\":%u,\"hop_mode\":0,\"hop_table\":[],"
           "\"tx_powers\":[{\"id\":%u,\"rp\":%u,\"wp\":%u}],"
           "\"ant_max_dwell_time\":0},",
           (unsigned int)readerConfig.region,
           (unsigned int)readerConfig.txAntenna,
           (unsigned int)readerConfig.readPower,
           (unsigned int)readerConfig.writePower))
    {
        return 0U;
    }

    if(!Configuration_Service_Append(
           &cursor,
           &remaining,
           "\"protocol\":{\"gen2\":{\"session\":%u,\"target\":2,\"q\":-1,\"profile\":-1}},",
           (unsigned int)g_liveConfiguration.session))
    {
        return 0U;
    }

    if(!Configuration_Service_Append(
           &cursor,
           &remaining,
           "\"tagops_param\":{" 
           "\"inventory\":{\"cycle\":150,\"interval\":10,\"inv_mode\":0,\"ants\":[%u]},"
           "\"tagfilter\":{\"bank\":\"1\",\"start\":\"32\",\"mask\":\"\",\"match\":\"1\"},"
           "\"bankdata\":{\"bank\":\"0\",\"start\":\"0\",\"blkcnt\":\"0\"},"
           "\"accessop\":{\"ant\":%u,\"timeout\":0,\"aespwd\":\"00000000\"}},",
           (unsigned int)readerConfig.txAntenna,
           (unsigned int)readerConfig.txAntenna))
    {
        return 0U;
    }

    if(!Configuration_Service_AppendLiteral(
           &cursor,
           &remaining,
           "\"tag_data\":{\"unique_by_antenna\":0,\"unique_by_bank_data\":0,\"record_highest_rssi\":0},"
           "\"gpos\":[{\"id\":1,\"state\":0},{\"id\":2,\"state\":0},"
           "{\"id\":3,\"state\":0},{\"id\":4,\"state\":0}],"
           "\"uart1\":{\"baud\":115200,\"data_bits\":8,\"stop_bits\":1,\"parity\":0,\"flow_ctrl\":0,\"address\":1},"
           "\"uart2\":{\"baud\":115200,\"data_bits\":8,\"stop_bits\":1,\"parity\":0,\"flow_ctrl\":0,\"address\":2},"
           "\"app_init\":{\"usb_type\":1,\"max_tb_rec_len\":128,\"evt_que_len\":32},"
           "\"ethernet\":{\"ip\":\"192.168.150.50\",\"nm\":\"255.255.255.0\","
           "\"gw\":\"192.168.150.1\",\"dns\":\"0.0.0.0\",\"lport\":8080}}"
    ))
    {
        return 0U;
    }

    return (uint16_t)strlen(buffer);
}

/* ---------------------------- command parsing ------------------------- */

static uint8_t Configuration_JsonParseObjectEnd(
    Configuration_JsonCursor_t *cursor
)
{
    return Configuration_JsonConsume(cursor, '}');
}

static uint8_t Configuration_Service_ParseRuntimeGlobParams(
    Configuration_JsonCursor_t *cursor,
    Configuration_StorageData_t *candidate,
    uint8_t *changed
)
{
    char key[64];
    uint8_t foundAny = 0U;
    uint8_t seenHeartbeat = 0U;
    uint8_t seenStartAtBoot = 0U;

    if(Configuration_JsonConsume(cursor, '{') == 0U)
    {
        return 0U;
    }

    Configuration_JsonSkipWhitespace(cursor);
    if(
        (cursor->p < cursor->end) &&
        (*cursor->p == '}')
    )
    {
        cursor->p++;
        return 1U;
    }

    for(;;)
    {
        uint32_t value;

        if(Configuration_JsonParseString(cursor, key, sizeof(key)) == 0U)
        {
            return 0U;
        }

        if(Configuration_JsonConsume(cursor, ':') == 0U)
        {
            return 0U;
        }

        if(
            Configuration_JsonKeyEquals(key, "hb_cylce") != 0U
        )
        {
            if(seenHeartbeat != 0U)
            {
                return 0U;
            }

            if(Configuration_JsonParseUnsigned(cursor, &value) == 0U)
            {
                return 0U;
            }

            if((value < 5U) || (value > 7200U))
            {
                return 0U;
            }

            candidate->heartbeatSeconds = value;
            seenHeartbeat = 1U;
            foundAny = 1U;
        }
        else if(
            Configuration_JsonKeyEquals(key, "start_inv_atbeg") != 0U
        )
        {
            if(seenStartAtBoot != 0U)
            {
                return 0U;
            }

            if(Configuration_JsonParseUnsigned(cursor, &value) == 0U)
            {
                return 0U;
            }

            if(value > 1U)
            {
                return 0U;
            }

            candidate->startInventoryAtBoot = (uint8_t)value;
            seenStartAtBoot = 1U;
            foundAny = 1U;
        }
        else
        {
            return 0U;
        }

        Configuration_JsonSkipWhitespace(cursor);
        if(
            (cursor->p < cursor->end) &&
            (*cursor->p == ',')
        )
        {
            cursor->p++;
            continue;
        }

        if(Configuration_JsonParseObjectEnd(cursor) != 0U)
        {
            *changed = foundAny;
            return 1U;
        }

        return 0U;
    }
}

static uint8_t Configuration_Service_ParseRuntimeData(
    Configuration_JsonCursor_t *cursor,
    Configuration_StorageData_t *candidate,
    uint8_t *changed
)
{
    char key[64];
    uint8_t foundAny = 0U;

    if(Configuration_JsonConsume(cursor, '{') == 0U)
    {
        return 0U;
    }

    Configuration_JsonSkipWhitespace(cursor);
    if(
        (cursor->p < cursor->end) &&
        (*cursor->p == '}')
    )
    {
        cursor->p++;
        *changed = 0U;
        return 1U;
    }

    for(;;)
    {
        if(Configuration_JsonParseString(cursor, key, sizeof(key)) == 0U)
        {
            return 0U;
        }

        if(Configuration_JsonConsume(cursor, ':') == 0U)
        {
            return 0U;
        }

        if(Configuration_JsonKeyEquals(key, "glob_params") == 0U)
        {
            return 0U;
        }

        if(foundAny != 0U)
        {
            return 0U;
        }

        if(Configuration_Service_ParseRuntimeGlobParams(cursor, candidate, &foundAny) == 0U)
        {
            return 0U;
        }

        Configuration_JsonSkipWhitespace(cursor);
        if(
            (cursor->p < cursor->end) &&
            (*cursor->p == ',')
        )
        {
            cursor->p++;
            continue;
        }

        if(Configuration_JsonParseObjectEnd(cursor) == 0U)
        {
            return 0U;
        }

        *changed = foundAny;
        return 1U;
    }
}

static uint8_t Configuration_Service_ParseRuntimeConfig(
    const char *json,
    Configuration_StorageData_t *candidate,
    uint8_t *changed
)
{
    Configuration_JsonCursor_t cursor;
    char key[64];
    uint8_t foundData = 0U;

    if(
        (json == NULL) ||
        (candidate == NULL) ||
        (changed == NULL)
    )
    {
        return 0U;
    }

    cursor.p = json;
    cursor.end = json + strlen(json);

    if(Configuration_JsonConsume(&cursor, '{') == 0U)
    {
        return 0U;
    }

    Configuration_JsonSkipWhitespace(&cursor);
    if(
        (cursor.p < cursor.end) &&
        (*cursor.p == '}')
    )
    {
        return 0U;
    }

    for(;;)
    {
        if(Configuration_JsonParseString(&cursor, key, sizeof(key)) == 0U)
        {
            return 0U;
        }

        if(Configuration_JsonConsume(&cursor, ':') == 0U)
        {
            return 0U;
        }

        if(Configuration_JsonKeyEquals(key, "command_type") != 0U)
        {
            if(Configuration_JsonSkipValue(&cursor, 0U) == 0U)
            {
                return 0U;
            }
        }
        else if(Configuration_JsonKeyEquals(key, "command_data") != 0U)
        {
            if(foundData != 0U)
            {
                return 0U;
            }

            if(Configuration_Service_ParseRuntimeData(&cursor, candidate, changed) == 0U)
            {
                return 0U;
            }
            foundData = 1U;
        }
        else
        {
            return 0U;
        }

        Configuration_JsonSkipWhitespace(&cursor);
        if(
            (cursor.p < cursor.end) &&
            (*cursor.p == ',')
        )
        {
            cursor.p++;
            continue;
        }

        if(Configuration_JsonParseObjectEnd(&cursor) == 0U)
        {
            return 0U;
        }

        Configuration_JsonSkipWhitespace(&cursor);
        return (
            (foundData != 0U) &&
            (cursor.p == cursor.end)
        ) ? 1U : 0U;
    }
}

static uint8_t Configuration_Service_ParseStaticGen2(
    Configuration_JsonCursor_t *cursor,
    Configuration_StorageData_t *candidate,
    uint8_t *changed
)
{
    char key[64];
    uint8_t foundAny = 0U;

    if(Configuration_JsonConsume(cursor, '{') == 0U)
    {
        return 0U;
    }

    Configuration_JsonSkipWhitespace(cursor);
    if(
        (cursor->p < cursor->end) &&
        (*cursor->p == '}')
    )
    {
        cursor->p++;
        *changed = 0U;
        return 1U;
    }

    for(;;)
    {
        uint32_t value;

        if(Configuration_JsonParseString(cursor, key, sizeof(key)) == 0U)
        {
            return 0U;
        }

        if(Configuration_JsonConsume(cursor, ':') == 0U)
        {
            return 0U;
        }

        if(Configuration_JsonKeyEquals(key, "session") == 0U)
        {
            return 0U;
        }

        if(foundAny != 0U)
        {
            return 0U;
        }

        if(Configuration_JsonParseUnsigned(cursor, &value) == 0U || value > 3U)
        {
            return 0U;
        }

        candidate->session = (uint8_t)value;
        foundAny = 1U;

        Configuration_JsonSkipWhitespace(cursor);
        if(
            (cursor->p < cursor->end) &&
            (*cursor->p == ',')
        )
        {
            cursor->p++;
            continue;
        }

        if(Configuration_JsonParseObjectEnd(cursor) == 0U)
        {
            return 0U;
        }

        *changed = foundAny;
        return 1U;
    }
}

static uint8_t Configuration_Service_ParseStaticProtocol(
    Configuration_JsonCursor_t *cursor,
    Configuration_StorageData_t *candidate,
    uint8_t *changed
)
{
    char key[64];

    if(Configuration_JsonConsume(cursor, '{') == 0U)
    {
        return 0U;
    }

    Configuration_JsonSkipWhitespace(cursor);
    if(
        (cursor->p < cursor->end) &&
        (*cursor->p == '}')
    )
    {
        cursor->p++;
        *changed = 0U;
        return 1U;
    }

    for(;;)
    {
        if(Configuration_JsonParseString(cursor, key, sizeof(key)) == 0U)
        {
            return 0U;
        }

        if(Configuration_JsonConsume(cursor, ':') == 0U)
        {
            return 0U;
        }

        if(Configuration_JsonKeyEquals(key, "gen2") == 0U)
        {
            return 0U;
        }

        if(Configuration_Service_ParseStaticGen2(cursor, candidate, changed) == 0U)
        {
            return 0U;
        }

        Configuration_JsonSkipWhitespace(cursor);
        if(
            (cursor->p < cursor->end) &&
            (*cursor->p == ',')
        )
        {
            cursor->p++;
            continue;
        }

        return Configuration_JsonParseObjectEnd(cursor);
    }
}

static uint8_t Configuration_Service_ParseStaticData(
    Configuration_JsonCursor_t *cursor,
    Configuration_StorageData_t *candidate,
    uint8_t *changed
)
{
    char key[64];

    if(Configuration_JsonConsume(cursor, '{') == 0U)
    {
        return 0U;
    }

    Configuration_JsonSkipWhitespace(cursor);
    if(
        (cursor->p < cursor->end) &&
        (*cursor->p == '}')
    )
    {
        cursor->p++;
        *changed = 0U;
        return 1U;
    }

    for(;;)
    {
        if(Configuration_JsonParseString(cursor, key, sizeof(key)) == 0U)
        {
            return 0U;
        }

        if(Configuration_JsonConsume(cursor, ':') == 0U)
        {
            return 0U;
        }

        if(Configuration_JsonKeyEquals(key, "protocol") == 0U)
        {
            return 0U;
        }

        if(Configuration_Service_ParseStaticProtocol(cursor, candidate, changed) == 0U)
        {
            return 0U;
        }

        Configuration_JsonSkipWhitespace(cursor);
        if(
            (cursor->p < cursor->end) &&
            (*cursor->p == ',')
        )
        {
            cursor->p++;
            continue;
        }

        return Configuration_JsonParseObjectEnd(cursor);
    }
}

static uint8_t Configuration_Service_ParseStaticConfig(
    const char *json,
    Configuration_StorageData_t *candidate,
    uint8_t *changed,
    uint8_t *rebootRequested,
    uint8_t *resetRequested
)
{
    Configuration_JsonCursor_t cursor;
    char key[64];
    uint8_t foundData = 0U;

    if(
        (json == NULL) ||
        (candidate == NULL) ||
        (changed == NULL) ||
        (rebootRequested == NULL) ||
        (resetRequested == NULL)
    )
    {
        return 0U;
    }

    *rebootRequested = 0U;
    *resetRequested = 0U;

    cursor.p = json;
    cursor.end = json + strlen(json);

    if(Configuration_JsonConsume(&cursor, '{') == 0U)
    {
        return 0U;
    }

    Configuration_JsonSkipWhitespace(&cursor);
    if(
        (cursor.p < cursor.end) &&
        (*cursor.p == '}')
    )
    {
        return 0U;
    }

    for(;;)
    {
        if(Configuration_JsonParseString(&cursor, key, sizeof(key)) == 0U)
        {
            return 0U;
        }

        if(Configuration_JsonConsume(&cursor, ':') == 0U)
        {
            return 0U;
        }

        if(Configuration_JsonKeyEquals(key, "command_type") != 0U)
        {
            if(Configuration_JsonSkipValue(&cursor, 0U) == 0U)
            {
                return 0U;
            }
        }
        else if(Configuration_JsonKeyEquals(key, "command_data") != 0U)
        {
            if(foundData != 0U)
            {
                return 0U;
            }

            if(Configuration_Service_ParseStaticData(&cursor, candidate, changed) == 0U)
            {
                return 0U;
            }
            foundData = 1U;
        }
        else if(
            Configuration_JsonKeyEquals(key, "reboot") != 0U ||
            Configuration_JsonKeyEquals(key, "reset") != 0U
        )
        {
            uint32_t value;

            if(Configuration_JsonParseUnsigned(&cursor, &value) == 0U || value > 1U)
            {
                return 0U;
            }

            if(Configuration_JsonKeyEquals(key, "reboot") != 0U)
            {
                *rebootRequested = (uint8_t)value;
            }
            else
            {
                *resetRequested = (uint8_t)value;
            }
        }
        else
        {
            return 0U;
        }

        Configuration_JsonSkipWhitespace(&cursor);
        if(
            (cursor.p < cursor.end) &&
            (*cursor.p == ',')
        )
        {
            cursor.p++;
            continue;
        }

        if(Configuration_JsonParseObjectEnd(&cursor) == 0U)
        {
            return 0U;
        }

        Configuration_JsonSkipWhitespace(&cursor);
        return (cursor.p == cursor.end) ? 1U : 0U;
    }
}

/* ---------------------------- public writes --------------------------- */

uint8_t Configuration_Service_ApplyRuntimeConfig(
    const char *json,
    uint32_t *errorCode,
    const char **errorString
)
{
    Configuration_StorageData_t candidate = g_persistentConfiguration;
    uint8_t changed = 0U;

    Configuration_Service_SetResult(
        errorCode,
        errorString,
        CONFIGURATION_SERVICE_ERR_INVALID,
        "invalid configuration"
    );

    if(Configuration_Service_ParseRuntimeConfig(
           json,
           &candidate,
           &changed
       ) == 0U)
    {
        return 0U;
    }

    if(changed == 0U)
    {
        return 0U;
    }

    if(Configuration_Storage_Save(&candidate) == 0U)
    {
        Configuration_Service_SetResult(
            errorCode,
            errorString,
            CONFIGURATION_SERVICE_ERR_EXECUTION,
            "configuration save failed"
        );
        return 0U;
    }

    g_persistentConfiguration = candidate;
    g_liveConfiguration = candidate;

    Configuration_Service_SetResult(
        errorCode,
        errorString,
        CONFIGURATION_SERVICE_ERR_OK,
        "ok"
    );
    return 1U;
}

uint8_t Configuration_Service_ApplyStaticConfig(
    const char *json,
    uint32_t *errorCode,
    const char **errorString,
    uint8_t *rebootRequested
)
{
    Configuration_StorageData_t candidate = g_persistentConfiguration;
    uint8_t changed = 0U;
    uint8_t resetRequested = 0U;
    uint8_t oldSession = g_liveConfiguration.session;

    if(rebootRequested != NULL)
    {
        *rebootRequested = 0U;
    }

    Configuration_Service_SetResult(
        errorCode,
        errorString,
        CONFIGURATION_SERVICE_ERR_INVALID,
        "invalid configuration"
    );

    if(Configuration_Service_ParseStaticConfig(
           json,
           &candidate,
           &changed,
           rebootRequested,
           &resetRequested
       ) == 0U)
    {
        return 0U;
    }

    if(resetRequested != 0U)
    {
        if(changed != 0U)
        {
            Configuration_Service_SetResult(
                errorCode,
                errorString,
                CONFIGURATION_SERVICE_ERR_INVALID,
                "reset cannot be combined with configuration fields"
            );
            return 0U;
        }

        Configuration_Storage_GetDefaults(&candidate);
        changed = 1U;
    }

    if(changed == 0U)
    {
        Configuration_Service_SetResult(
            errorCode,
            errorString,
            CONFIGURATION_SERVICE_ERR_INVALID,
            "no supported configuration fields"
        );
        return 0U;
    }

    if(candidate.session != oldSession)
    {
        if(SILION_Application_SetSession(candidate.session) == 0U)
        {
            Configuration_Service_SetResult(
                errorCode,
                errorString,
                CONFIGURATION_SERVICE_ERR_EXECUTION,
                "session apply failed"
            );
            return 0U;
        }
    }

    if(Configuration_Storage_Save(&candidate) == 0U)
    {
        /* Put the live RF setting back to the previous value if the save fails. */
        if(candidate.session != oldSession)
        {
            (void)SILION_Application_SetSession(oldSession);
        }

        Configuration_Service_SetResult(
            errorCode,
            errorString,
            CONFIGURATION_SERVICE_ERR_EXECUTION,
            "configuration save failed"
        );
        return 0U;
    }

    g_persistentConfiguration = candidate;
    g_liveConfiguration = candidate;

    Configuration_Service_SetResult(
        errorCode,
        errorString,
        CONFIGURATION_SERVICE_ERR_OK,
        "ok"
    );
    return 1U;
}

uint8_t Configuration_Service_ApplyTempStaticConfig(
    const char *json,
    uint32_t *errorCode,
    const char **errorString
)
{
    Configuration_StorageData_t candidate = g_liveConfiguration;
    uint8_t changed = 0U;
    uint8_t rebootRequested = 0U;
    uint8_t resetRequested = 0U;
    uint8_t oldSession = g_liveConfiguration.session;
    Configuration_Service_SetResult(
        errorCode,
        errorString,
        CONFIGURATION_SERVICE_ERR_INVALID,
        "invalid configuration"
    );

    if(SILION_Application_IsInventoryRunning() != 0U)
    {
        Configuration_Service_SetResult(
            errorCode,
            errorString,
            CONFIGURATION_SERVICE_ERR_INVALID,
            "invalid operation, please pause the inventory first"
        );
        return 0U;
    }

    if(Configuration_Service_ParseStaticConfig(
           json,
           &candidate,
           &changed,
           &rebootRequested,
           &resetRequested
       ) == 0U)
    {
        return 0U;
    }

    if((rebootRequested != 0U) || (resetRequested != 0U))
    {
        Configuration_Service_SetResult(
            errorCode,
            errorString,
            CONFIGURATION_SERVICE_ERR_UNSUPPORTED,
            "not supported operation"
        );
        return 0U;
    }

    if(changed == 0U)
    {
        Configuration_Service_SetResult(
            errorCode,
            errorString,
            CONFIGURATION_SERVICE_ERR_INVALID,
            "no supported configuration fields"
        );
        return 0U;
    }

    if(candidate.session != oldSession)
    {
        if(SILION_Application_SetSession(candidate.session) == 0U)
        {
            Configuration_Service_SetResult(
                errorCode,
                errorString,
                CONFIGURATION_SERVICE_ERR_EXECUTION,
                "session apply failed"
            );
            return 0U;
        }
    }

    /* Deliberately not saved: this is a live temporary configuration. */
    g_liveConfiguration = candidate;

    Configuration_Service_SetResult(
        errorCode,
        errorString,
        CONFIGURATION_SERVICE_ERR_OK,
        "ok"
    );
    return 1U;
}
