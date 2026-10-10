#include "ethernet_service.h"

#include "../../Devices/w5500/w5500.h"
#include "../../Config/product_config.h"
#include "../Time/time_service.h"
#include "../configuration/reader_settings.h"
#include "http_upload.h"
#include "../upload/active_upload_transport.h"

#include <stddef.h>
#include <string.h>

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
static uint16_t ethernetHttpSourcePort = 0U;

/* upload.http selected in the settings (Part D). Otherwise JSON lines
 * over a plain TCP connection to upload.tcp (Part C.11). */
static uint8_t Ethernet_Service_IsHttp(void)
{
    return (ReaderSettings_GetUploadMode() == READER_SETTINGS_UPLOAD_HTTP) ? 1U : 0U;
}

static void Ethernet_Service_ConnectionLost(void)
{
    ethernetClientConnected = 0U;

    if(Ethernet_Service_IsHttp() != 0U)
    {
        HttpUpload_OnDisconnected();
    }
}


uint8_t Ethernet_Service_Init(
    Ethernet_RxByteCallback_t rxCallback
)
{
    g_rxCallback = rxCallback;
    HttpUpload_Init(rxCallback);

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
    uint8_t targetIp[4];
    uint16_t targetPort;
    uint16_t sourcePort;

    socketStatus = W5500_Socket0_GetStatus();
    now = Time_GetTickMs();

    /* Socket 0 is reserved for active uploads. Host TCP commands use socket 3
     * and remain available regardless of the selected upload interface. */
    if(ActiveUpload_Transport_GetSelected() != ACTIVE_UPLOAD_INTERFACE_ETH)
    {
        if(socketStatus != W5500_Sn_SR_CLOSED)
            (void)W5500_Socket0_Close();
        if(ethernetClientConnected != 0U)
            Ethernet_Service_ConnectionLost();
        ethernetReconnectDelayMs = ETHERNET_RECONNECT_MIN_MS;
        ethernetNextConnectAttemptMs = now + ETHERNET_RECONNECT_MIN_MS;
        return;
    }

    /* Passive work state (set by the vendor tools): no upload connection. */
    if(ReaderSettings_GetWorkState() != READER_SETTINGS_WORK_ACTIVE)
    {
        if(socketStatus != W5500_Sn_SR_CLOSED)
        {
            (void)W5500_Socket0_Close();
        }
        if(ethernetClientConnected != 0U)
        {
            Ethernet_Service_ConnectionLost();
        }
        ethernetReconnectDelayMs = ETHERNET_RECONNECT_MIN_MS;
        ethernetNextConnectAttemptMs = now;
        return;
    }

    if(Ethernet_Service_IsHttp() != 0U)
    {
        /* No usable URL (e.g. a hostname): nothing to connect to. */
        if(HttpUpload_GetTarget(targetIp, &targetPort) == 0U)
        {
            if(socketStatus != W5500_Sn_SR_CLOSED)
            {
                (void)W5500_Socket0_Close();
            }
            if(ethernetClientConnected != 0U)
            {
                Ethernet_Service_ConnectionLost();
            }
            return;
        }
    }
    else
    {
        memcpy(targetIp, ReaderSettings_Get()->serverIp, 4U);
        targetPort = ReaderSettings_Get()->serverPort;
    }

    if(socketStatus == W5500_Sn_SR_ESTABLISHED)
    {
        if(ethernetClientConnected == 0U)
        {
            ethernetClientConnected = 1U;
            ethernetReconnectDelayMs = ETHERNET_RECONNECT_MIN_MS;
        }

        if(Ethernet_Service_IsHttp() != 0U)
        {
            HttpUpload_Task();
            return;
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
        Ethernet_Service_ConnectionLost();
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

    /* HTTP servers close idle keep-alive connections; a new source port
     * per connection avoids reusing a port the server still holds in
     * TIME_WAIT. */
    sourcePort = PRODUCT_PROTOCOL_TCP_LOCAL_PORT;
    if(Ethernet_Service_IsHttp() != 0U)
    {
        ethernetHttpSourcePort = (uint16_t)((ethernetHttpSourcePort + 1U) % 1000U);
        sourcePort = (uint16_t)(50000U + ethernetHttpSourcePort);
    }

    if(
        W5500_StartTCPClient(
            targetIp,
            targetPort,
            sourcePort
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

    if(
        (ethernetClientConnected == 0U) ||
        (Ethernet_Service_IsHttp() != 0U)
    )
    {
        return 0U;
    }

    return W5500_Socket0_Send(
        buffer,
        length
    );
}

uint16_t Ethernet_Service_SendComplete(
    const uint8_t *buffer,
    uint16_t length
)
{
    uint16_t txFreeSize;
    uint16_t sent;

    if(
        (buffer == NULL) ||
        (length == 0U) ||
        (ethernetClientConnected == 0U)
    )
    {
        return 0U;
    }

    /* HTTP: one POST per message, queued while a request is in flight. */
    if(Ethernet_Service_IsHttp() != 0U)
    {
        return HttpUpload_Submit(buffer, length);
    }

    /* W5500_Socket0_Send() historically accepted a shorter write when TX
     * space was insufficient.  For application-level JSON that would create
     * a truncated message.  Check the complete length first and retry later. */
    txFreeSize = W5500_Socket0_GetTxFreeSize();

    if(txFreeSize < length)
    {
        return 0U;
    }

    sent = W5500_Socket0_Send(
        buffer,
        length
    );

    return (sent == length) ? sent : 0U;
}

uint8_t Ethernet_Service_IsConnected(void)
{
    return ethernetClientConnected;
}

void Ethernet_Service_Restart(void)
{
    /* Upload target changed: drop the connection and redial now. */
    (void)W5500_Socket0_Close();
    ethernetClientConnected = 0U;
    (void)HttpUpload_Configure();
    ethernetReconnectDelayMs = ETHERNET_RECONNECT_MIN_MS;
    ethernetNextConnectAttemptMs = Time_GetTickMs();
}
