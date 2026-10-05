#include "configuration_port.h"

#include "configuration_service.h"
#include "../../Config/product_config.h"
#include "../../Devices/w5500/w5500.h"
#include "../../Services/Time/time_service.h"

#include <stddef.h>
#include <string.h>

#define CONFIGURATION_PORT_RX_BUFFER_SIZE 64U
#define CONFIGURATION_PORT_RESPONSE_SIZE  (CONFIGURATION_SERVICE_MAX_JSON + 16U)
#define CONFIGURATION_PORT_TCP_LISTEN_PORT 8080U

#define CONFIGURATION_PORT_CMD_WORK_STATE  0x22U
#define CONFIGURATION_PORT_CMD_ACTIVE_JSON 0x24U
#define CONFIGURATION_PORT_CMD_STATIC_JSON 0x1EU

static uint8_t g_rxBuffer[CONFIGURATION_PORT_RX_BUFFER_SIZE];
static uint8_t g_responseBuffer[CONFIGURATION_PORT_RESPONSE_SIZE];
static uint8_t g_contentBuffer[CONFIGURATION_SERVICE_MAX_JSON + 16U];
static uint16_t g_rxLength = 0U;
static uint8_t g_started = 0U;
static uint8_t g_waitingForClientClose = 0U;

static uint16_t Configuration_Port_PadJson(
    uint8_t *buffer,
    uint16_t length,
    uint16_t capacity
)
{
    uint16_t paddedLength = (uint16_t)((length + 3U) & (uint16_t)~3U);

    if(paddedLength > capacity)
    {
        return 0U;
    }

    while(length < paddedLength)
    {
        buffer[length++] = 0U;
    }

    return paddedLength;
}

static uint8_t Configuration_Port_SendResponse(
    uint8_t commandId,
    const uint8_t *content,
    uint16_t contentLength,
    uint32_t status
)
{
    uint8_t *response = g_responseBuffer;
    uint16_t totalLength;
    uint16_t i;

    if(
        (content == NULL) ||
        (contentLength == 0U)
    )
    {
        return 0U;
    }

    if((uint16_t)(10U + contentLength) > CONFIGURATION_PORT_RESPONSE_SIZE)
    {
        return 0U;
    }

    response[0] = 0xFFU;
    response[1] = 0U;
    response[2] = (uint8_t)(contentLength >> 8U);
    response[3] = (uint8_t)(contentLength & 0xFFU);
    response[4] = commandId;
    response[5] = 0U;
    response[6] = (uint8_t)(status >> 24U);
    response[7] = (uint8_t)(status >> 16U);
    response[8] = (uint8_t)(status >> 8U);
    response[9] = (uint8_t)status;

    for(i = 0U; i < contentLength; i++)
    {
        response[10U + i] = content[i];
    }

    totalLength = (uint16_t)(10U + contentLength);

    return (
        W5500_Socket1_Send(
            response,
            totalLength
        ) == totalLength
    ) ? 1U : 0U;
}

static void Configuration_Port_ResetConnectionState(void)
{
    g_rxLength = 0U;
    memset(g_rxBuffer, 0, sizeof(g_rxBuffer));
    g_waitingForClientClose = 0U;
}

void Configuration_Port_Init(void)
{
    Configuration_Port_ResetConnectionState();
    g_started = 0U;
}

void Configuration_Port_Task(void)
{
    uint8_t status;
    uint16_t received;
    uint16_t dataLength;
    uint16_t totalLength;
    uint8_t commandId;
    uint8_t *content = g_contentBuffer;
    uint16_t contentLength;

    status = W5500_Socket1_GetStatus();
    {
        if(status == W5500_Sn_SR_LISTEN)
        {
            g_started = 1U;
            return;
        }

        if(status == W5500_Sn_SR_ESTABLISHED)
        {
            /* The protocol is one request per connection. After sending the
             * response, let the client close the connection cleanly instead
             * of forcing a fixed-time local close. This avoids a race with
             * the next configuration-port connection. */
            if(g_waitingForClientClose != 0U)
            {
                return;
            }

            received = W5500_Socket1_Receive(
                &g_rxBuffer[g_rxLength],
                (uint16_t)(sizeof(g_rxBuffer) - g_rxLength)
            );

            if(received > 0U)
            {
                g_rxLength = (uint16_t)(g_rxLength + received);
            }

            if(g_rxLength < 6U)
            {
                return;
            }

            if(
                (g_rxBuffer[0] != 0xEEU) ||
                (g_rxBuffer[1] != 0x00U)
            )
            {
                (void)W5500_Socket1_Close();
                Configuration_Port_ResetConnectionState();
                return;
            }

            dataLength = (uint16_t)(
                ((uint16_t)g_rxBuffer[2] << 8U) |
                g_rxBuffer[3]
            );

            totalLength = (uint16_t)(6U + dataLength);

            if(dataLength > (sizeof(g_rxBuffer) - 6U))
            {
                (void)W5500_Socket1_Close();
                Configuration_Port_ResetConnectionState();
                return;
            }

            if(g_rxLength < totalLength)
            {
                return;
            }

            commandId = g_rxBuffer[4];

            if(commandId == CONFIGURATION_PORT_CMD_WORK_STATE)
            {
                if(dataLength != 1U)
                {
                    uint8_t invalidContent = 0U;
                    (void)Configuration_Port_SendResponse(
                        commandId,
                        &invalidContent,
                        1U,
                        1U
                    );
                }
                else
                {
                    content[0] = Configuration_Service_GetWorkState();
                    (void)Configuration_Port_SendResponse(
                        commandId,
                        content,
                        1U,
                        0U
                    );
                }
            }
            else if(
                (commandId == CONFIGURATION_PORT_CMD_ACTIVE_JSON) ||
                (commandId == CONFIGURATION_PORT_CMD_STATIC_JSON)
            )
            {
                if(dataLength != 2U)
                {
                    contentLength = 1U;
                    content[0] = 0U;
                    (void)Configuration_Port_SendResponse(
                        commandId,
                        content,
                        contentLength,
                        1U
                    );
                }
                else
                {
                    if(commandId == CONFIGURATION_PORT_CMD_ACTIVE_JSON)
                    {
                        contentLength = Configuration_Service_BuildActiveJson(
                            (char *)content,
                            sizeof(g_contentBuffer)
                        );
                    }
                    else
                    {
                        contentLength = Configuration_Service_BuildStaticJson(
                            (char *)content,
                            sizeof(g_contentBuffer)
                        );
                    }

                    if(contentLength == 0U)
                    {
                        content[0] = 0U;
                        contentLength = 1U;
                        (void)Configuration_Port_SendResponse(
                            commandId,
                            content,
                            contentLength,
                            2U
                        );
                    }
                    else
                    {
                        contentLength = Configuration_Port_PadJson(
                            content,
                            contentLength,
                            sizeof(g_contentBuffer)
                        );

                        if(contentLength == 0U)
                        {
                            content[0] = 0U;
                            contentLength = 1U;
                        }

                        (void)Configuration_Port_SendResponse(
                            commandId,
                            content,
                            contentLength,
                            0U
                        );
                    }
                }
            }
            else
            {
                content[0] = 0U;
                (void)Configuration_Port_SendResponse(
                    commandId,
                    content,
                    1U,
                    1U
                );
            }

            g_waitingForClientClose = 1U;
            g_rxLength = 0U;
            return;
        }

        if(status == W5500_Sn_SR_CLOSE_WAIT)
        {
            /* Peer has closed its side after receiving the response. Now it is
             * safe to close our side and immediately reopen the listening
             * socket for the next one-request connection. */
            (void)W5500_Socket1_Close();
            Configuration_Port_ResetConnectionState();

            if(
                W5500_StartTCPServer1(
                    CONFIGURATION_PORT_TCP_LISTEN_PORT
                ) != 0U
            )
            {
                g_started = 1U;
            }
        }
        else if(status == W5500_Sn_SR_CLOSED)
        {
            Configuration_Port_ResetConnectionState();

            if(
                W5500_StartTCPServer1(
                    CONFIGURATION_PORT_TCP_LISTEN_PORT
                ) != 0U
            )
            {
                g_started = 1U;
            }
        }
        else
        {
            g_started = 0U;
        }
    }
}
