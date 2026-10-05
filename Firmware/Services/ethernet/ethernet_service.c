#include "ethernet_service.h"

#include "../../Devices/w5500/w5500.h"
#include "../../Config/product_config.h"
#include "../Time/time_service.h"

#include <stddef.h>

/*
 * ------------------------------------------------------------
 * IDENTIUM ACTIVE-MODE TCP TRANSPORT
 * ------------------------------------------------------------
 *
 * The company protocol defines the reader as a TCP client:
 * the reader dials the configured server and keeps the socket
 * open.  Commands then travel back over the same connection.
 */

static Ethernet_RxByteCallback_t g_rxCallback = NULL;

#define ETHERNET_RX_BUFFER_SIZE     512U
#define ETHERNET_RECONNECT_MIN_MS   1000U
#define ETHERNET_RECONNECT_MAX_MS   30000U

static uint8_t ethernetRxBuffer[ETHERNET_RX_BUFFER_SIZE];
static uint8_t ethernetClientConnected = 0U;
static uint32_t ethernetNextConnectAttemptMs = 0U;
static uint32_t ethernetReconnectDelayMs = ETHERNET_RECONNECT_MIN_MS;

static const uint8_t ethernetServerIp[4] =
{
    PRODUCT_PROTOCOL_TCP_SERVER_IP_0,
    PRODUCT_PROTOCOL_TCP_SERVER_IP_1,
    PRODUCT_PROTOCOL_TCP_SERVER_IP_2,
    PRODUCT_PROTOCOL_TCP_SERVER_IP_3
};

uint8_t Ethernet_Service_Init(
    Ethernet_RxByteCallback_t rxCallback
)
{
    g_rxCallback = rxCallback;

    ethernetClientConnected = 0U;
    ethernetReconnectDelayMs = ETHERNET_RECONNECT_MIN_MS;
    ethernetNextConnectAttemptMs = 0U;

    return 1U;
}

void Ethernet_Service_Task(void)
{
    uint8_t socketStatus;
    uint16_t received;
    uint16_t i;
    uint32_t now;

    socketStatus = W5500_Socket0_GetStatus();
    now = Time_GetTickMs();

    if(socketStatus == W5500_Sn_SR_ESTABLISHED)
    {
        if(ethernetClientConnected == 0U)
        {
            ethernetClientConnected = 1U;
            ethernetReconnectDelayMs = ETHERNET_RECONNECT_MIN_MS;
        }

        received = W5500_Socket0_Receive(
            ethernetRxBuffer,
            sizeof(ethernetRxBuffer)
        );

        if(
            (received > 0U) &&
            (g_rxCallback != NULL)
        )
        {
            for(i = 0U; i < received; i++)
            {
                g_rxCallback(
                    ethernetRxBuffer[i]
                );
            }
        }

        return;
    }

    if(ethernetClientConnected != 0U)
    {
        ethernetClientConnected = 0U;
        ethernetNextConnectAttemptMs =
            now + ethernetReconnectDelayMs;

        if(ethernetReconnectDelayMs < ETHERNET_RECONNECT_MAX_MS)
        {
            ethernetReconnectDelayMs *= 2U;

            if(ethernetReconnectDelayMs > ETHERNET_RECONNECT_MAX_MS)
            {
                ethernetReconnectDelayMs = ETHERNET_RECONNECT_MAX_MS;
            }
        }
    }

    if(socketStatus == W5500_Sn_SR_CLOSE_WAIT)
    {
        (void)W5500_Socket0_Close();
        ethernetNextConnectAttemptMs =
            now + ethernetReconnectDelayMs;
        return;
    }

    if(socketStatus != W5500_Sn_SR_CLOSED)
    {
        return;
    }

    if((int32_t)(now - ethernetNextConnectAttemptMs) < 0)
    {
        return;
    }

    if(
        W5500_StartTCPClient(
            ethernetServerIp,
            PRODUCT_PROTOCOL_TCP_SERVER_PORT,
            PRODUCT_PROTOCOL_TCP_LOCAL_PORT
        ) != 0U
    )
    {
        ethernetNextConnectAttemptMs =
            now + ethernetReconnectDelayMs;
        return;
    }

    ethernetNextConnectAttemptMs =
        now + ethernetReconnectDelayMs;

    if(ethernetReconnectDelayMs < ETHERNET_RECONNECT_MAX_MS)
    {
        ethernetReconnectDelayMs *= 2U;

        if(ethernetReconnectDelayMs > ETHERNET_RECONNECT_MAX_MS)
        {
            ethernetReconnectDelayMs = ETHERNET_RECONNECT_MAX_MS;
        }
    }
}

uint16_t Ethernet_Service_Send(
    const uint8_t *buffer,
    uint16_t length
)
{
    if(
        (buffer == NULL) ||
        (length == 0U)
    )
    {
        return 0U;
    }

    if(ethernetClientConnected == 0U)
    {
        return 0U;
    }

    return W5500_Socket0_Send(
        buffer,
        length
    );
}

uint8_t Ethernet_Service_IsConnected(void)
{
    return ethernetClientConnected;
}
