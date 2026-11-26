#include "host_tcp_server.h"

#include "../../Config/product_config.h"
#include "../../Devices/w5500/w5500.h"
#include "../host/host_transport.h"
#include "../Time/time_service.h"

#include <stddef.h>

#define HOST_TCP_SERVER_TX_SIZE    2048U
#define HOST_TCP_SERVER_RX_CHUNK   128U
#define HOST_TCP_SERVER_RETRY_MS   1000U
#define HOST_TCP_SERVER_IDLE_MS    60000U

static uint8_t g_txQueue[HOST_TCP_SERVER_TX_SIZE];
static uint16_t g_txHead = 0U;
static uint16_t g_txTail = 0U;
static uint8_t g_connected = 0U;
static uint32_t g_lastListenMs = 0U;
static uint32_t g_lastActivityMs = 0U;

static void Host_Tcp_Server_Listen(void)
{
    g_lastListenMs = Time_GetTickMs();
    (void)W5500_Socket3_Close();
    (void)W5500_Socket3_Listen(PRODUCT_ETHERNET_TCP_PORT);
}

/* The client is gone: hand the legacy parser back so the USB and
 * serial ports are not locked out by a dropped connection. */
static void Host_Tcp_Server_Disconnected(void)
{
    if(g_connected != 0U)
    {
        g_connected = 0U;

        if(HOST_Transport_GetActive() == HOST_TRANSPORT_ETHERNET)
        {
            HOST_Transport_Release();
        }
    }

    g_txHead = 0U;
    g_txTail = 0U;
}

static void Host_Tcp_Server_Flush(void)
{
    uint16_t length;
    uint16_t sent;

    while(g_txTail != g_txHead)
    {
        /* Contiguous part of the ring. */
        length = (g_txHead > g_txTail)
            ? (uint16_t)(g_txHead - g_txTail)
            : (uint16_t)(HOST_TCP_SERVER_TX_SIZE - g_txTail);

        sent = W5500_Socket3_Send(&g_txQueue[g_txTail], length);
        if(sent == 0U)
        {
            return;   /* previous send in flight or socket buffer full */
        }

        g_lastActivityMs = Time_GetTickMs();
        g_txTail = (uint16_t)((g_txTail + sent) % HOST_TCP_SERVER_TX_SIZE);
    }
}

void Host_Tcp_Server_Init(void)
{
    g_connected = 0U;
    g_txHead = 0U;
    g_txTail = 0U;
    Host_Tcp_Server_Listen();
}

void Host_Tcp_Server_Task(void)
{
    uint8_t chunk[HOST_TCP_SERVER_RX_CHUNK];
    uint16_t received;
    uint16_t i;

    switch(W5500_Socket3_GetStatus())
    {
        case W5500_Sn_SR_LISTEN:
            break;

        case W5500_Sn_SR_ESTABLISHED:
            if(g_connected == 0U)
            {
                g_connected = 1U;
                g_lastActivityMs = Time_GetTickMs();
                g_txHead = 0U;
                g_txTail = 0U;
            }

            received = W5500_Socket3_Receive(chunk, sizeof(chunk));
            if(received != 0U) g_lastActivityMs = Time_GetTickMs();
            for(i = 0U; i < received; i++)
            {
                HOST_Transport_PushByte(HOST_TRANSPORT_ETHERNET, chunk[i]);
            }

            Host_Tcp_Server_Flush();
            if((Time_GetTickMs() - g_lastActivityMs) >= HOST_TCP_SERVER_IDLE_MS)
            {
                Host_Tcp_Server_Disconnected();
                Host_Tcp_Server_Listen();
            }
            break;

        case W5500_Sn_SR_CLOSE_WAIT:
            Host_Tcp_Server_Disconnected();
            Host_Tcp_Server_Listen();
            break;

        case W5500_Sn_SR_INIT:
        case W5500_Sn_SR_CLOSED:
            Host_Tcp_Server_Disconnected();

            /* A failed listen is retried slowly: each attempt waits on
             * the W5500 for a few milliseconds. */
            if((Time_GetTickMs() - g_lastListenMs) >= HOST_TCP_SERVER_RETRY_MS)
            {
                Host_Tcp_Server_Listen();
            }
            break;

        default:
            /* SYN received, FIN/closing states: transient. */
            break;
    }
}

uint16_t Host_Tcp_Server_Send(const uint8_t *data, uint16_t length)
{
    uint16_t accepted = 0U;
    uint16_t next;

    if((data == NULL) || (g_connected == 0U))
    {
        return 0U;
    }

    while(accepted < length)
    {
        next = (uint16_t)((g_txHead + 1U) % HOST_TCP_SERVER_TX_SIZE);
        if(next == g_txTail)
        {
            break;   /* queue full: drop the rest */
        }

        g_txQueue[g_txHead] = data[accepted];
        g_txHead = next;
        accepted++;
    }

    Host_Tcp_Server_Flush();

    return accepted;
}

uint8_t Host_Tcp_Server_IsConnected(void)
{
    return g_connected;
}
