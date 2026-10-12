#include "configuration_port.h"

#include "configuration_service.h"
#include "module_bridge.h"
#include "reader_settings.h"
#include "vendor_config.h"
#include "discovery_service.h"
#include "../host/host_transport.h"
#include "../../Config/product_config.h"
#include "../../Devices/w5500/w5500.h"
#include "../../Services/Time/time_service.h"

#include <stddef.h>
#include <string.h>

/* Large enough for a set-active / set-static JSON request. */
#define CONFIGURATION_PORT_RX_BUFFER_SIZE (6U + 2048U)
#define CONFIGURATION_PORT_RESPONSE_SIZE  (CONFIGURATION_SERVICE_MAX_JSON + 16U)

#define CONFIGURATION_PORT_CMD_WORK_STATE  0x22U
#define CONFIGURATION_PORT_CMD_ACTIVE_JSON 0x24U
#define CONFIGURATION_PORT_CMD_STATIC_JSON 0x1EU

static uint8_t g_rxBuffer[CONFIGURATION_PORT_RX_BUFFER_SIZE];
static uint8_t g_responseBuffer[CONFIGURATION_PORT_RESPONSE_SIZE];
static uint8_t g_contentBuffer[CONFIGURATION_SERVICE_MAX_JSON + 16U];
static uint16_t g_rxLength = 0U;
static uint8_t g_started = 0U;
static uint8_t g_waitingForClientClose = 0U;

/* Where EE replies go: TCP socket 1, or the serial link (USB CDC or
 * RS232/RS485) the request arrived on (COM route). */
#define CONFIGURATION_PORT_REPLY_TCP  0xFFU
static uint8_t g_replyLink = CONFIGURATION_PORT_REPLY_TCP;

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

    if(g_replyLink != CONFIGURATION_PORT_REPLY_TCP)
    {
        return (
            HOST_Transport_LinkSend(g_replyLink, response, totalLength) == totalLength
        ) ? 1U : 0U;
    }

    return (
        W5500_Socket1_Send(
            response,
            totalLength
        ) == totalLength
    ) ? 1U : 0U;
}

/* Handles one complete EE request (header + content) and sends the
 * reply on the current response transport. */
static void Configuration_Port_Dispatch(
    const uint8_t *frame,
    uint16_t dataLength
)
{
    uint8_t commandId;
    uint8_t *content = g_contentBuffer;
    uint16_t contentLength;

    commandId = frame[4];

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
                /* No NUL padding: ReaderSense passes the content
                 * straight to JSON.parse(), which rejects NULs. */
                (void)Configuration_Port_SendResponse(
                    commandId,
                    content,
                    contentLength,
                    0U
                );
            }
        }
    }
    else if(
        (commandId == VENDOR_CONFIG_CMD_SET_ACTIVE) ||
        (commandId == VENDOR_CONFIG_CMD_SET_STATIC) ||
        (commandId == VENDOR_CONFIG_CMD_CLEAR) ||
        (commandId == VENDOR_CONFIG_CMD_SWITCH)
    )
    {
        uint32_t vendorStatus;
        uint8_t replyByte = 0U;

        vendorStatus = VendorConfig_Handle(
            commandId,
            (const char *)&frame[6],
            dataLength,
            &replyByte
        );

        (void)Configuration_Port_SendResponse(
            commandId,
            &replyByte,
            1U,
            vendorStatus
        );
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

    /* Deferred work: settings changes, reboot, config writes, discovery. */
    ReaderSettings_Task();
    VendorConfig_Task();
    Discovery_Service_Task();
    Module_Bridge_Poll();

    status = W5500_Socket1_GetStatus();
    {
        if(status == W5500_Sn_SR_LISTEN)
        {
            g_started = 1U;
            return;
        }

        if(status == W5500_Sn_SR_ESTABLISHED)
        {
            /* Reader Manager module session: persistent connection. */
            if(Module_Bridge_IsActive() != 0U)
            {
                Module_Bridge_Task();
                return;
            }

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

            if(g_rxLength == 0U)
            {
                return;
            }

            /* The first byte selects the protocol for the whole
             * connection: EE = Identium configuration request (one per
             * connection), anything else = Reader Manager module
             * session (FF module frames, ASCII mainboard commands). */
            if(g_rxBuffer[0] != 0xEEU)
            {
                if(Module_Bridge_Begin(g_rxBuffer, g_rxLength) != 0U)
                {
                    Configuration_Port_ResetConnectionState();
                    return;
                }

                (void)W5500_Socket1_Close();
                Configuration_Port_ResetConnectionState();
                return;
            }

            if(g_rxLength < 6U)
            {
                return;
            }

            if(g_rxBuffer[1] != 0x00U)
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

            Configuration_Port_Dispatch(g_rxBuffer, dataLength);

            g_waitingForClientClose = 1U;
            g_rxLength = 0U;
            return;
        }

        if(status == W5500_Sn_SR_CLOSE_WAIT)
        {
            /* Peer has closed its side after receiving the response. Now it is
             * safe to close our side and immediately reopen the listening
             * socket for the next one-request connection. */
            if(Module_Bridge_IsActive() != 0U)
            {
                Module_Bridge_End();
            }
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
            if(Module_Bridge_IsActive() != 0U)
            {
                Module_Bridge_End();
            }
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

void Configuration_Port_HandleLinkRequest(
    uint8_t link,
    const uint8_t *frame,
    uint16_t length
)
{
    uint16_t dataLength;

    if((frame == NULL) || (length < 6U) || (frame[0] != 0xEEU) || (frame[1] != 0x00U))
    {
        return;
    }

    dataLength = (uint16_t)(((uint16_t)frame[2] << 8U) | frame[3]);
    if((uint16_t)(6U + dataLength) > length)
    {
        return;
    }

    if((link != HOST_TRANSPORT_USB) && (link != HOST_TRANSPORT_SERIAL))
    {
        return;
    }

    g_replyLink = link;
    Configuration_Port_Dispatch(frame, dataLength);
    g_replyLink = CONFIGURATION_PORT_REPLY_TCP;
}
