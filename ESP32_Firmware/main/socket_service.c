#include "socket_service.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"

#include "wifi_manager.h"

#define SOCKET_SERVICE_RING_SIZE       2048U
#define SOCKET_SERVICE_IO_CHUNK        512U
#define SOCKET_SERVICE_RETRY_MS        1000U
#define SOCKET_SERVICE_EVENT_CONNECTED 0x04U
#define SOCKET_SERVICE_EVENT_CLOSED    0x08U

static const char *TAG = "socket_service";

typedef struct
{
    uint8_t data[SOCKET_SERVICE_RING_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t used;
} SocketService_Ring_t;

typedef struct
{
    int listenFd;
    int dataFd;
    uint8_t wantsOpen;
    uint8_t type;
    uint8_t connecting;
    uint16_t port;
    uint8_t ip[4];
    uint64_t retryAtMs;
    SocketService_Ring_t rx;
    SocketService_Ring_t tx;
} SocketService_Socket_t;

static SocketService_Socket_t g_sockets[ESP32_LINK_SOCKET_COUNT];
static uint8_t g_events;

static uint64_t SocketService_NowMs(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

static uint16_t SocketService_RingWrite(SocketService_Ring_t *ring,
                                        const uint8_t *data, uint16_t length)
{
    uint16_t accepted = 0U;
    while((accepted < length) && (ring->used < SOCKET_SERVICE_RING_SIZE))
    {
        ring->data[ring->head] = data[accepted++];
        ring->head = (uint16_t)((ring->head + 1U) % SOCKET_SERVICE_RING_SIZE);
        ring->used++;
    }
    return accepted;
}

static uint16_t SocketService_RingRead(SocketService_Ring_t *ring,
                                       uint8_t *data, uint16_t capacity)
{
    uint16_t count = ring->used;
    uint16_t i;
    if(count > capacity) count = capacity;
    for(i = 0U; i < count; i++)
    {
        data[i] = ring->data[ring->tail];
        ring->tail = (uint16_t)((ring->tail + 1U) % SOCKET_SERVICE_RING_SIZE);
        ring->used--;
    }
    return count;
}

static uint16_t SocketService_RingPeek(const SocketService_Ring_t *ring,
                                       uint8_t *data, uint16_t capacity)
{
    uint16_t count = ring->used;
    uint16_t contiguous = (uint16_t)(SOCKET_SERVICE_RING_SIZE - ring->tail);
    uint16_t i;
    if(count > capacity) count = capacity;
    if(count > contiguous) count = contiguous;
    for(i = 0U; i < count; i++) data[i] = ring->data[ring->tail + i];
    return count;
}

static void SocketService_RingConsume(SocketService_Ring_t *ring, uint16_t length)
{
    if(length > ring->used) length = ring->used;
    ring->tail = (uint16_t)((ring->tail + length) % SOCKET_SERVICE_RING_SIZE);
    ring->used = (uint16_t)(ring->used - length);
}

static void SocketService_CloseFd(int *fd)
{
    if(*fd >= 0)
    {
        (void)shutdown(*fd, SHUT_RDWR);
        (void)close(*fd);
        *fd = -1;
    }
}

static int SocketService_SetNonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if(flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void SocketService_CloseConnection(uint8_t socketId, uint8_t clearQueues)
{
    SocketService_Socket_t *socket = &g_sockets[socketId];
    uint8_t wasConnected = (socket->dataFd >= 0) && (socket->connecting == 0U);

    SocketService_CloseFd(&socket->dataFd);
    socket->connecting = 0U;
    if(clearQueues != 0U)
    {
        memset(&socket->rx, 0, sizeof(socket->rx));
        memset(&socket->tx, 0, sizeof(socket->tx));
    }
    if(wasConnected != 0U) g_events |= SOCKET_SERVICE_EVENT_CLOSED;
    if((socketId == ESP32_LINK_SOCKET_HOST) && (socket->type == ESP32_LINK_SOCK_OPEN_SERVER))
    {
        /* The listening socket remains available for the next host client. */
    }
}

static uint8_t SocketService_OpenServer(SocketService_Socket_t *slot)
{
    struct sockaddr_in address;
    int fd;
    int option = 1;

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(fd < 0) return 0U;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    if(SocketService_SetNonblocking(fd) < 0)
    {
        (void)close(fd);
        return 0U;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(slot->port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if((bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) ||
       (listen(fd, 1) < 0))
    {
        ESP_LOGW(TAG, "server bind/listen failed: errno=%d", errno);
        (void)close(fd);
        return 0U;
    }
    slot->listenFd = fd;
    return 1U;
}

static uint8_t SocketService_OpenClient(SocketService_Socket_t *slot)
{
    struct sockaddr_in address;
    int fd;
    int result;

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(fd < 0) return 0U;
    if(SocketService_SetNonblocking(fd) < 0)
    {
        (void)close(fd);
        return 0U;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(slot->port);
    address.sin_addr.s_addr = ((uint32_t)slot->ip[0] << 24U) |
                              ((uint32_t)slot->ip[1] << 16U) |
                              ((uint32_t)slot->ip[2] << 8U) |
                               (uint32_t)slot->ip[3];
    address.sin_addr.s_addr = htonl(address.sin_addr.s_addr);
    result = connect(fd, (struct sockaddr *)&address, sizeof(address));
    if((result < 0) && (errno != EINPROGRESS) && (errno != EWOULDBLOCK))
    {
        (void)close(fd);
        return 0U;
    }
    slot->dataFd = fd;
    slot->connecting = (result == 0) ? 0U : 1U;
    if(slot->connecting == 0U) g_events |= SOCKET_SERVICE_EVENT_CONNECTED;
    return 1U;
}

static void SocketService_StartWantedSocket(uint8_t socketId)
{
    SocketService_Socket_t *socket = &g_sockets[socketId];
    uint8_t opened;

    if((socket->wantsOpen == 0U) || (socket->dataFd >= 0) ||
       ((socket->type == ESP32_LINK_SOCK_OPEN_SERVER) && (socket->listenFd >= 0)) ||
       (WifiManager_IsNetworkReady() == 0U) || (SocketService_NowMs() < socket->retryAtMs))
        return;

    if(socket->type == ESP32_LINK_SOCK_OPEN_SERVER)
        opened = SocketService_OpenServer(socket);
    else
        opened = SocketService_OpenClient(socket);

    if(opened == 0U)
    {
        socket->retryAtMs = SocketService_NowMs() + SOCKET_SERVICE_RETRY_MS;
    }
}

void SocketService_Init(void)
{
    uint8_t i;
    memset(g_sockets, 0, sizeof(g_sockets));
    for(i = 0U; i < ESP32_LINK_SOCKET_COUNT; i++)
    {
        g_sockets[i].listenFd = -1;
        g_sockets[i].dataFd = -1;
    }
    g_events = 0U;
}

uint8_t SocketService_Open(uint8_t socketId, uint8_t type, const uint8_t ip[4], uint16_t port)
{
    SocketService_Socket_t *socket;
    if((socketId >= ESP32_LINK_SOCKET_COUNT) || (port == 0U) ||
       (type > ESP32_LINK_SOCK_OPEN_CLIENT) ||
       ((type == ESP32_LINK_SOCK_OPEN_CLIENT) && (ip == NULL)))
        return ESP32_LINK_RESULT_INVALID;

    socket = &g_sockets[socketId];
    SocketService_CloseConnection(socketId, 1U);
    SocketService_CloseFd(&socket->listenFd);
    socket->wantsOpen = 1U;
    socket->type = type;
    socket->port = port;
    memset(socket->ip, 0, sizeof(socket->ip));
    if(ip != NULL) memcpy(socket->ip, ip, sizeof(socket->ip));
    socket->retryAtMs = 0U;
    SocketService_StartWantedSocket(socketId);
    /* Opening a TCP client is asynchronous; the status bitmap confirms when
     * its connect completes. The accepted host connection is also separate. */
    return ESP32_LINK_RESULT_OK;
}

uint8_t SocketService_Close(uint8_t socketId)
{
    SocketService_Socket_t *socket;
    if(socketId >= ESP32_LINK_SOCKET_COUNT) return ESP32_LINK_RESULT_INVALID;
    socket = &g_sockets[socketId];
    socket->wantsOpen = 0U;
    SocketService_CloseConnection(socketId, 1U);
    SocketService_CloseFd(&socket->listenFd);
    socket->retryAtMs = 0U;
    return ESP32_LINK_RESULT_OK;
}

uint8_t SocketService_Send(uint8_t socketId, const uint8_t *data, uint16_t length,
                           uint16_t *accepted)
{
    SocketService_Socket_t *socket;
    if((socketId >= ESP32_LINK_SOCKET_COUNT) || (data == NULL) || (accepted == NULL))
        return ESP32_LINK_RESULT_INVALID;
    socket = &g_sockets[socketId];
    *accepted = 0U;
    if((socket->dataFd < 0) || (socket->connecting != 0U))
        return ESP32_LINK_RESULT_NOT_CONNECTED;
    *accepted = SocketService_RingWrite(&socket->tx, data, length);
    return ESP32_LINK_RESULT_OK;
}

uint16_t SocketService_Receive(uint8_t socketId, uint8_t *data, uint16_t capacity)
{
    if((socketId >= ESP32_LINK_SOCKET_COUNT) || (data == NULL)) return 0U;
    return SocketService_RingRead(&g_sockets[socketId].rx, data, capacity);
}

static void SocketService_CheckClientConnect(uint8_t socketId)
{
    SocketService_Socket_t *socket = &g_sockets[socketId];
    fd_set writable;
    fd_set exceptional;
    struct timeval timeout = { 0, 0 };
    int selected;
    int error = 0;
    socklen_t errorLength = sizeof(error);

    if((socket->dataFd < 0) || (socket->connecting == 0U)) return;

    FD_ZERO(&writable);
    FD_ZERO(&exceptional);
    FD_SET(socket->dataFd, &writable);
    FD_SET(socket->dataFd, &exceptional);
    selected = select(socket->dataFd + 1, NULL, &writable, &exceptional, &timeout);
    if(selected <= 0) return;

    if(getsockopt(socket->dataFd, SOL_SOCKET, SO_ERROR, &error, &errorLength) == 0)
    {
        if((error == 0) && FD_ISSET(socket->dataFd, &writable))
        {
            socket->connecting = 0U;
            g_events |= SOCKET_SERVICE_EVENT_CONNECTED;
        }
        else if(error != EINPROGRESS && error != EALREADY && error != EWOULDBLOCK)
        {
            SocketService_CloseConnection(socketId, 1U);
            socket->retryAtMs = SocketService_NowMs() + SOCKET_SERVICE_RETRY_MS;
        }
    }
}

static void SocketService_AcceptClient(uint8_t socketId)
{
    SocketService_Socket_t *socket = &g_sockets[socketId];
    struct sockaddr_in address;
    socklen_t addressLength = sizeof(address);
    int fd;

    if((socket->listenFd < 0) || (socket->dataFd >= 0)) return;
    fd = accept(socket->listenFd, (struct sockaddr *)&address, &addressLength);
    if(fd < 0) return;
    if(SocketService_SetNonblocking(fd) < 0)
    {
        (void)close(fd);
        return;
    }
    socket->dataFd = fd;
    socket->connecting = 0U;
    memset(&socket->rx, 0, sizeof(socket->rx));
    memset(&socket->tx, 0, sizeof(socket->tx));
    g_events |= SOCKET_SERVICE_EVENT_CONNECTED;
}

static void SocketService_PumpRx(uint8_t socketId)
{
    SocketService_Socket_t *socket = &g_sockets[socketId];
    uint8_t temporary[SOCKET_SERVICE_IO_CHUNK];
    uint16_t freeSpace;
    int received;

    if((socket->dataFd < 0) || (socket->connecting != 0U)) return;
    freeSpace = (uint16_t)(SOCKET_SERVICE_RING_SIZE - socket->rx.used);
    if(freeSpace == 0U) return;
    if(freeSpace > sizeof(temporary)) freeSpace = sizeof(temporary);
    received = (int)recv(socket->dataFd, temporary, freeSpace, 0);
    if(received > 0)
    {
        (void)SocketService_RingWrite(&socket->rx, temporary, (uint16_t)received);
    }
    else if(received == 0)
    {
        SocketService_CloseConnection(socketId, 0U);
        if(socket->type == ESP32_LINK_SOCK_OPEN_CLIENT)
            socket->retryAtMs = SocketService_NowMs() + SOCKET_SERVICE_RETRY_MS;
    }
    else if((errno != EWOULDBLOCK) && (errno != EAGAIN) && (errno != EINTR))
    {
        SocketService_CloseConnection(socketId, 0U);
        if(socket->type == ESP32_LINK_SOCK_OPEN_CLIENT)
            socket->retryAtMs = SocketService_NowMs() + SOCKET_SERVICE_RETRY_MS;
    }
}

static void SocketService_PumpTx(uint8_t socketId)
{
    SocketService_Socket_t *socket = &g_sockets[socketId];
    uint8_t temporary[SOCKET_SERVICE_IO_CHUNK];
    uint16_t count;
    int sent;

    if((socket->dataFd < 0) || (socket->connecting != 0U) || (socket->tx.used == 0U))
        return;
    count = SocketService_RingPeek(&socket->tx, temporary, sizeof(temporary));
    if(count == 0U) return;
    sent = (int)send(socket->dataFd, temporary, count, 0);
    if(sent > 0)
    {
        SocketService_RingConsume(&socket->tx, (uint16_t)sent);
    }
    else if((sent == 0) || ((errno != EWOULDBLOCK) && (errno != EAGAIN) && (errno != EINTR)))
    {
        SocketService_CloseConnection(socketId, 0U);
        if(socket->type == ESP32_LINK_SOCK_OPEN_CLIENT)
            socket->retryAtMs = SocketService_NowMs() + SOCKET_SERVICE_RETRY_MS;
    }
}

void SocketService_Task(void)
{
    uint8_t i;
    if(WifiManager_IsNetworkReady() == 0U)
    {
        for(i = 0U; i < ESP32_LINK_SOCKET_COUNT; i++)
        {
            SocketService_CloseConnection(i, 0U);
            SocketService_CloseFd(&g_sockets[i].listenFd);
        }
        return;
    }

    for(i = 0U; i < ESP32_LINK_SOCKET_COUNT; i++)
    {
        SocketService_Socket_t *socket = &g_sockets[i];
        SocketService_StartWantedSocket(i);
        if((socket->type == ESP32_LINK_SOCK_OPEN_SERVER) && (socket->listenFd >= 0))
            SocketService_AcceptClient(i);
        SocketService_CheckClientConnect(i);
        SocketService_PumpRx(i);
        SocketService_PumpTx(i);
    }
}

uint8_t SocketService_GetConnectedBitmap(void)
{
    uint8_t bitmap = 0U;
    uint8_t i;
    for(i = 0U; i < ESP32_LINK_SOCKET_COUNT; i++)
    {
        if((g_sockets[i].dataFd >= 0) && (g_sockets[i].connecting == 0U))
            bitmap |= (uint8_t)(1U << i);
    }
    return bitmap;
}

uint8_t SocketService_GetRxPendingBitmap(void)
{
    uint8_t bitmap = 0U;
    uint8_t i;
    for(i = 0U; i < ESP32_LINK_SOCKET_COUNT; i++)
    {
        if(g_sockets[i].rx.used != 0U) bitmap |= (uint8_t)(1U << i);
    }
    return bitmap;
}

uint8_t SocketService_TakeEventBitmap(void)
{
    uint8_t events = g_events;
    g_events = 0U;
    return events;
}

uint8_t SocketService_GetEventBitmap(void)
{
    return g_events;
}
