#include "configuration_service.h"

#include "../../Config/product_config.h"
#include "../../Application/Silion/sillion_application.h"
#include "../../Services/GPIO/gpio_service.h"
#include "../../Protocols/silion/silion_protocol.h"
#include "../../Protocol/identium_protocol.h"

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern SILION_ReaderConfig_t readerConfig;

static uint8_t g_initialized = 0U;
static char g_escapedReaderName[96];

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

static uint8_t Configuration_Service_JsonStringSafe(
    const char *source,
    char *destination,
    uint16_t destinationSize
)
{
    uint16_t inIndex = 0U;
    uint16_t outIndex = 0U;

    if(
        (source == NULL) ||
        (destination == NULL) ||
        (destinationSize < 2U)
    )
    {
        return 0U;
    }

    while(source[inIndex] != '\0')
    {
        char c = source[inIndex++];

        if(
            (c == '"') ||
            (c == '\\')
        )
        {
            if((outIndex + 2U) >= destinationSize)
            {
                return 0U;
            }

            destination[outIndex++] = '\\';
            destination[outIndex++] = c;
        }
        else if((unsigned char)c < 0x20U)
        {
            return 0U;
        }
        else
        {
            if((outIndex + 1U) >= destinationSize)
            {
                return 0U;
            }

            destination[outIndex++] = c;
        }
    }

    destination[outIndex] = '\0';
    return 1U;
}

void Configuration_Service_Init(void)
{
    g_initialized = 1U;
}

uint8_t Configuration_Service_GetWorkState(void)
{
    (void)g_initialized;
    return 3U;
}

uint16_t Configuration_Service_BuildActiveJson(
    char *buffer,
    uint16_t bufferSize
)
{
    char *cursor = buffer;
    size_t remaining = bufferSize;

    if(
        (buffer == NULL) ||
        (bufferSize == 0U)
    )
    {
        return 0U;
    }

    if(Configuration_Service_JsonStringSafe(
           PRODUCT_PROTOCOL_READER_NAME,
           g_escapedReaderName,
           sizeof(g_escapedReaderName)) == 0U)
    {
        return 0U;
    }

    if(!Configuration_Service_Append(
        &cursor, &remaining,
        "{\"glob_params\":{\"hb_cylce\":%u,\"s_buf_size\":1580,"
        "\"thwdt_net_timeout\":0,\"thwdt_inv_timeout\":0,"
        "\"start_inv_atbeg\":0,\"remote_cmd_reply\":1,\"name\":\"%s\"},",
        (unsigned int)IDENTIUM_PROTOCOL_HEARTBEAT_SECONDS,
        g_escapedReaderName))
    {
        return 0U;
    }

    if(!Configuration_Service_Append(
        &cursor, &remaining,
        "\"upload\":{\"client_ack\":0,\"crc_enable\":0,"
        "\"recv_timeout\":10,\"clr_r_buf_time\":8,\"hw_inf\":1,"
        "\"tcp\":{\"ser_ip\":\"%u.%u.%u.%u\",\"ser_port\":\"%u\"},"
        "\"data_aggr\":{\"mode\":0,\"timeval\":1000}},",
        PRODUCT_PROTOCOL_TCP_SERVER_IP_0,
        PRODUCT_PROTOCOL_TCP_SERVER_IP_1,
        PRODUCT_PROTOCOL_TCP_SERVER_IP_2,
        PRODUCT_PROTOCOL_TCP_SERVER_IP_3,
        PRODUCT_PROTOCOL_TCP_SERVER_PORT))
    {
        return 0U;
    }

    if(!Configuration_Service_Append(
        &cursor, &remaining,
        "\"gpo_act\":[],\"events\":[1,2,3],"
        "\"tag_json_format\":[1,2,5,8],"
        "\"gpi_trigger\":{\"mode\":0,\"timeval\":0,\"cond_order\":0,"
        "\"cond_1\":[],\"cond_2\":[]},\"cus_param\":\"\"}"
    ))
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

    if(
        (buffer == NULL) ||
        (bufferSize == 0U)
    )
    {
        return 0U;
    }

    if(!Configuration_Service_Append(
        &cursor, &remaining,
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
        &cursor, &remaining,
        "\"protocol\":{\"gen2\":{\"session\":%u,\"target\":2,\"q\":-1,\"profile\":-1}},",
        (unsigned int)readerConfig.session))
    {
        return 0U;
    }

    if(!Configuration_Service_Append(
        &cursor, &remaining,
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

    if(!Configuration_Service_Append(
        &cursor, &remaining,
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
