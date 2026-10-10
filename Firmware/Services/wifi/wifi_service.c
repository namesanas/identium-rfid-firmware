#include "wifi_service.h"

#include "../../Config/product_config.h"
#include "../../Devices/esp32/esp32_link.h"
#include "../configuration/reader_settings.h"
#include "../ethernet/http_upload.h"
#include "../host/host_transport.h"
#include "../Time/time_service.h"

#include <stddef.h>
#include <string.h>

#define WIFI_SERVICE_TX_QUEUE_SIZE       2048U
#define WIFI_SERVICE_UPLOAD_RX_SIZE      1024U
#define WIFI_SERVICE_SEND_CHUNK          200U
#define WIFI_SERVICE_RECV_CHUNK          48U
#define WIFI_SERVICE_POLL_INTERVAL_MS    5U
#define WIFI_SERVICE_RETRY_INTERVAL_MS   1000U
#define WIFI_SERVICE_HELLO_MIN_LENGTH    10U

typedef enum
{
    WIFI_COMMAND_NONE = 0U,
    WIFI_COMMAND_HELLO,
    WIFI_COMMAND_CONFIG,
    WIFI_COMMAND_CONNECT,
    WIFI_COMMAND_DISCONNECT,
    WIFI_COMMAND_STATUS,
    WIFI_COMMAND_OPEN_HOST,
    WIFI_COMMAND_OPEN_UPLOAD,
    WIFI_COMMAND_CLOSE_HOST,
    WIFI_COMMAND_CLOSE_UPLOAD,
    WIFI_COMMAND_RECV_HOST,
    WIFI_COMMAND_RECV_UPLOAD,
    WIFI_COMMAND_SEND_HOST,
    WIFI_COMMAND_SEND_UPLOAD
} WiFi_CommandPurpose_t;

typedef struct
{
    uint8_t data[WIFI_SERVICE_TX_QUEUE_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t used;
} WiFi_TxQueue_t;

typedef struct
{
    uint8_t data[WIFI_SERVICE_UPLOAD_RX_SIZE];
    uint16_t head;
    uint16_t tail;
    uint16_t used;
} WiFi_RxQueue_t;

static WiFi_RxByteCallback_t g_rxCallback;
static WiFi_TxQueue_t g_hostTx;
static WiFi_TxQueue_t g_uploadTx;
static WiFi_RxQueue_t g_uploadRx;
static uint8_t g_sendPending[WIFI_SERVICE_SEND_CHUNK];
static uint16_t g_sendPendingLength;
static uint8_t g_response[ESP32_LINK_RESPONSE_MAX_PAYLOAD];
static uint16_t g_responseLength;
static WiFi_CommandPurpose_t g_commandPurpose;
static uint8_t g_commandInFlight;
static uint8_t g_linkOnline;
static uint8_t g_configured;
static uint8_t g_wifiConnectIssued;
static uint8_t g_hostSocketOpened;
static uint8_t g_uploadSocketOpened;
static uint8_t g_wifiState;
static uint8_t g_connectedBitmap;
static uint8_t g_rxPendingBitmap;
static uint8_t g_eventBitmap;
static int8_t g_rssi;
static uint8_t g_ip[4];
static uint8_t g_mac[6];
static uint8_t g_settingsSnapshotValid;
static ReaderSettings_t g_settingsSnapshot;
static uint8_t g_uploadWasConnected;
static uint8_t g_hostWasConnected;
static uint32_t g_nextHelloMs;
static uint32_t g_nextStatusMs;
static uint32_t g_nextUploadOpenMs;
static uint32_t g_linkFailureCount;
static uint8_t g_available;
static uint8_t g_needsConfiguration = 1U;
static uint8_t g_uploadTargetIp[4];
static uint16_t g_uploadTargetPort;
static uint8_t g_uploadTargetValid;

static uint16_t WiFi_QueueWrite(WiFi_TxQueue_t *queue, const uint8_t *data, uint16_t length)
{
    uint16_t accepted = 0U;
    if ((queue == NULL) || (data == NULL)) return 0U;
    while ((accepted < length) && (queue->used < WIFI_SERVICE_TX_QUEUE_SIZE))
    {
        queue->data[queue->head] = data[accepted++];
        queue->head = (uint16_t)((queue->head + 1U) % WIFI_SERVICE_TX_QUEUE_SIZE);
        queue->used++;
    }
    return accepted;
}

static uint16_t WiFi_QueuePeek(const WiFi_TxQueue_t *queue, uint8_t *data, uint16_t capacity)
{
    uint16_t i;
    uint16_t count;
    if ((queue == NULL) || (data == NULL)) return 0U;
    count = queue->used;
    if (count > capacity) count = capacity;
    for (i = 0U; i < count; i++)
        data[i] = queue->data[(queue->tail + i) % WIFI_SERVICE_TX_QUEUE_SIZE];
    return count;
}

static void WiFi_QueueConsume(WiFi_TxQueue_t *queue, uint16_t length)
{
    if (queue == NULL) return;
    if (length > queue->used) length = queue->used;
    queue->tail = (uint16_t)((queue->tail + length) % WIFI_SERVICE_TX_QUEUE_SIZE);
    queue->used = (uint16_t)(queue->used - length);
}

static uint16_t WiFi_RxWrite(const uint8_t *data, uint16_t length)
{
    uint16_t i;
    uint16_t accepted = 0U;
    if (data == NULL) return 0U;
    for (i = 0U; (i < length) && (g_uploadRx.used < WIFI_SERVICE_UPLOAD_RX_SIZE); i++)
    {
        g_uploadRx.data[g_uploadRx.head] = data[i];
        g_uploadRx.head = (uint16_t)((g_uploadRx.head + 1U) % WIFI_SERVICE_UPLOAD_RX_SIZE);
        g_uploadRx.used++;
        accepted++;
    }
    return accepted;
}

uint16_t WiFi_Service_UploadReceive(uint8_t *data, uint16_t capacity)
{
    uint16_t i;
    uint16_t count;
    if (data == NULL) return 0U;
    count = g_uploadRx.used;
    if (count > capacity) count = capacity;
    for (i = 0U; i < count; i++)
    {
        data[i] = g_uploadRx.data[g_uploadRx.tail];
        g_uploadRx.tail = (uint16_t)((g_uploadRx.tail + 1U) % WIFI_SERVICE_UPLOAD_RX_SIZE);
        g_uploadRx.used--;
    }
    return count;
}

uint16_t WiFi_Service_UploadReceiveCount(void)
{
    return g_uploadRx.used;
}

static uint8_t WiFi_SettingsEqual(const ReaderSettings_t *a, const ReaderSettings_t *b)
{
    return (a->wifiEnabled == b->wifiEnabled) &&
           (a->wifiMode == b->wifiMode) &&
           (a->wifiDhcp == b->wifiDhcp) &&
           (a->wifiApChannel == b->wifiApChannel) &&
           (memcmp(a->wifiSsid, b->wifiSsid, sizeof(a->wifiSsid)) == 0) &&
           (memcmp(a->wifiPassword, b->wifiPassword, sizeof(a->wifiPassword)) == 0) &&
           (memcmp(a->wifiStaticIp, b->wifiStaticIp, sizeof(a->wifiStaticIp)) == 0) &&
           (memcmp(a->wifiNetmask, b->wifiNetmask, sizeof(a->wifiNetmask)) == 0) &&
           (memcmp(a->wifiGateway, b->wifiGateway, sizeof(a->wifiGateway)) == 0) &&
           (memcmp(a->wifiDns, b->wifiDns, sizeof(a->wifiDns)) == 0) &&
           (memcmp(a->wifiApSsid, b->wifiApSsid, sizeof(a->wifiApSsid)) == 0) &&
           (memcmp(a->wifiApPassword, b->wifiApPassword, sizeof(a->wifiApPassword)) == 0);
}

static uint8_t WiFi_SettingsTargetChanged(const ReaderSettings_t *a, const ReaderSettings_t *b)
{
    return (a->uploadInterface != b->uploadInterface) ||
           (a->uploadMode != b->uploadMode) ||
           (memcmp(a->serverIp, b->serverIp, sizeof(a->serverIp)) != 0) ||
           (a->serverPort != b->serverPort) ||
           (a->httpTimeout != b->httpTimeout) ||
           (strcmp(a->httpUrl, b->httpUrl) != 0);
}

static void WiFi_CopyTargetSettings(ReaderSettings_t *destination,
                                    const ReaderSettings_t *source)
{
    destination->uploadInterface = source->uploadInterface;
    destination->uploadMode = source->uploadMode;
    memcpy(destination->serverIp, source->serverIp, sizeof(destination->serverIp));
    destination->serverPort = source->serverPort;
    destination->httpTimeout = source->httpTimeout;
    memcpy(destination->httpUrl, source->httpUrl, sizeof(destination->httpUrl));
}

static uint8_t WiFi_UploadSelected(void)
{
    uint8_t interface = ReaderSettings_GetUploadInterface();
    if (interface == READER_SETTINGS_UPLOAD_IF_WIFI) return 1U;
    if (interface == READER_SETTINGS_UPLOAD_IF_ETH) return 0U;
    /* The auto decision is made by the STM32; the ESP is only the fallback. */
    extern uint8_t W5500_IsLinkUp(void);
    return (W5500_IsLinkUp() == 0U) ? 1U : 0U;
}

static uint8_t WiFi_BuildConfigPayload(uint8_t *payload, uint16_t capacity)
{
    const ReaderSettings_t *settings = ReaderSettings_Get();
    size_t length;
    if ((payload == NULL) || (capacity < ESP32_LINK_CFG_PAYLOAD_SIZE)) return 0U;
    memset(payload, 0, ESP32_LINK_CFG_PAYLOAD_SIZE);
    payload[ESP32_LINK_CFG_ENABLED_OFFSET] = settings->wifiEnabled;
    payload[ESP32_LINK_CFG_MODE_OFFSET] = settings->wifiMode;
    payload[ESP32_LINK_CFG_DHCP_OFFSET] = settings->wifiDhcp;
    payload[ESP32_LINK_CFG_AP_CHANNEL_OFFSET] = settings->wifiApChannel;

    length = strlen(settings->wifiSsid);
    if (length > READER_SETTINGS_WIFI_SSID_MAX) length = READER_SETTINGS_WIFI_SSID_MAX;
    payload[ESP32_LINK_CFG_STA_SSID_LEN] = (uint8_t)length;
    memcpy(&payload[ESP32_LINK_CFG_STA_SSID], settings->wifiSsid, length);

    length = strlen(settings->wifiPassword);
    if (length > READER_SETTINGS_WIFI_PASSWORD_MAX) length = READER_SETTINGS_WIFI_PASSWORD_MAX;
    payload[ESP32_LINK_CFG_STA_PASS_LEN] = (uint8_t)length;
    memcpy(&payload[ESP32_LINK_CFG_STA_PASS], settings->wifiPassword, length);

    length = strlen(settings->wifiApSsid);
    if (length > READER_SETTINGS_WIFI_SSID_MAX) length = READER_SETTINGS_WIFI_SSID_MAX;
    payload[ESP32_LINK_CFG_AP_SSID_LEN] = (uint8_t)length;
    memcpy(&payload[ESP32_LINK_CFG_AP_SSID], settings->wifiApSsid, length);

    length = strlen(settings->wifiApPassword);
    if (length > READER_SETTINGS_WIFI_PASSWORD_MAX) length = READER_SETTINGS_WIFI_PASSWORD_MAX;
    payload[ESP32_LINK_CFG_AP_PASS_LEN] = (uint8_t)length;
    memcpy(&payload[ESP32_LINK_CFG_AP_PASS], settings->wifiApPassword, length);

    memcpy(&payload[ESP32_LINK_CFG_STATIC_IP], settings->wifiStaticIp, 4U);
    memcpy(&payload[ESP32_LINK_CFG_NETMASK], settings->wifiNetmask, 4U);
    memcpy(&payload[ESP32_LINK_CFG_GATEWAY], settings->wifiGateway, 4U);
    memcpy(&payload[ESP32_LINK_CFG_DNS], settings->wifiDns, 4U);
    return 1U;
}

static uint8_t WiFi_GetUploadTarget(uint8_t ip[4], uint16_t *port)
{
    const ReaderSettings_t *settings = ReaderSettings_Get();
    if (settings->uploadMode == READER_SETTINGS_UPLOAD_HTTP)
    {
        return HttpUpload_GetTarget(ip, port);
    }
    if ((port == NULL) || (ip == NULL) || (settings->serverPort == 0U)) return 0U;
    memcpy(ip, settings->serverIp, 4U);
    *port = settings->serverPort;
    return 1U;
}

static uint8_t WiFi_TargetSameAsOpened(void)
{
    uint8_t ip[4];
    uint16_t port;
    if (WiFi_GetUploadTarget(ip, &port) == 0U) return 0U;
    return (g_uploadTargetValid != 0U) && (port == g_uploadTargetPort) &&
           (memcmp(ip, g_uploadTargetIp, sizeof(ip)) == 0);
}

static uint8_t WiFi_Submit(uint8_t command, uint8_t socket,
                           const uint8_t *payload, uint16_t length,
                           WiFi_CommandPurpose_t purpose)
{
    if (g_commandInFlight != 0U) return 0U;
    if (ESP32_Link_Submit(command, socket, payload, length) == 0U) return 0U;
    g_commandPurpose = purpose;
    g_commandInFlight = 1U;
    return 1U;
}

static void WiFi_HandleReceiveResponse(uint8_t socket)
{
    uint16_t count;
    uint16_t i;
    if ((g_responseLength < 2U) || (g_response[0] != ESP32_LINK_RESULT_OK)) return;
    count = g_response[1];
    if (count > (uint16_t)(g_responseLength - 2U))
        count = (uint16_t)(g_responseLength - 2U);
    if (socket == ESP32_LINK_SOCKET_HOST)
    {
        for (i = 0U; i < count; i++)
            HOST_Transport_PushByte(HOST_TRANSPORT_WIFI, g_response[2U + i]);
    }
    else if (ReaderSettings_GetUploadMode() == READER_SETTINGS_UPLOAD_HTTP)
    {
        (void)WiFi_RxWrite(&g_response[2], count);
    }
    else if (g_rxCallback != NULL)
    {
        for (i = 0U; i < count; i++) g_rxCallback(g_response[2U + i]);
    }
    g_rxPendingBitmap &= (uint8_t)~(1U << socket);
}

static void WiFi_HandleCompletedCommand(void)
{
    uint8_t command;
    uint8_t socket;
    uint16_t length = 0U;
    ESP32_LinkState_t state;

    state = ESP32_Link_GetResult(&command, &socket, g_response, &length,
                                 sizeof(g_response));
    if (state == ESP32_LINK_STATE_BUSY || state == ESP32_LINK_STATE_IDLE) return;

    if (state == ESP32_LINK_STATE_FAILED)
    {
        g_linkFailureCount++;
        if (g_commandPurpose == WIFI_COMMAND_HELLO)
        {
            g_linkOnline = 0U;
            g_configured = 0U;
            g_nextHelloMs = Time_GetTickMs() + WIFI_SERVICE_RETRY_INTERVAL_MS;
        }
        else if (g_commandPurpose == WIFI_COMMAND_OPEN_UPLOAD)
        {
            g_uploadSocketOpened = 0U;
            g_nextUploadOpenMs = Time_GetTickMs() + WIFI_SERVICE_RETRY_INTERVAL_MS;
        }
        else if (g_commandPurpose == WIFI_COMMAND_OPEN_HOST)
        {
            g_hostSocketOpened = 0U;
        }
        else if (g_commandPurpose == WIFI_COMMAND_STATUS)
        {
            g_nextStatusMs = Time_GetTickMs() + WIFI_SERVICE_POLL_INTERVAL_MS;
        }
        else if (g_commandPurpose == WIFI_COMMAND_CONFIG)
        {
            g_configured = 0U;
            g_needsConfiguration = 1U;
        }
        else if (g_commandPurpose == WIFI_COMMAND_DISCONNECT)
        {
            g_wifiConnectIssued = 0U;
            g_nextHelloMs = Time_GetTickMs() + WIFI_SERVICE_RETRY_INTERVAL_MS;
        }
        else if (g_commandPurpose == WIFI_COMMAND_SEND_HOST)
        {
            g_sendPendingLength = 0U;
        }
        else if (g_commandPurpose == WIFI_COMMAND_SEND_UPLOAD)
        {
            g_sendPendingLength = 0U;
        }
        else if (g_commandPurpose == WIFI_COMMAND_RECV_HOST)
        {
            g_rxPendingBitmap &= (uint8_t)~(1U << ESP32_LINK_SOCKET_HOST);
            g_nextStatusMs = Time_GetTickMs();
        }
        else if (g_commandPurpose == WIFI_COMMAND_RECV_UPLOAD)
        {
            g_rxPendingBitmap &= (uint8_t)~(1U << ESP32_LINK_SOCKET_UPLOAD);
            g_nextStatusMs = Time_GetTickMs();
        }
        ESP32_Link_ConsumeResult();
        g_commandInFlight = 0U;
        g_commandPurpose = WIFI_COMMAND_NONE;
        return;
    }

    g_responseLength = length;
    if ((length == 0U) || (g_response[0] != ESP32_LINK_RESULT_OK))
    {
        if (g_commandPurpose == WIFI_COMMAND_HELLO)
        {
            g_linkOnline = 0U;
            g_nextHelloMs = Time_GetTickMs() + WIFI_SERVICE_RETRY_INTERVAL_MS;
        }
        else if (g_commandPurpose == WIFI_COMMAND_OPEN_UPLOAD)
        {
            g_uploadSocketOpened = 0U;
            g_nextUploadOpenMs = Time_GetTickMs() + WIFI_SERVICE_RETRY_INTERVAL_MS;
        }
        else if (g_commandPurpose == WIFI_COMMAND_OPEN_HOST)
        {
            g_hostSocketOpened = 0U;
        }
        else if (g_commandPurpose == WIFI_COMMAND_STATUS)
        {
            g_nextStatusMs = Time_GetTickMs() + WIFI_SERVICE_POLL_INTERVAL_MS;
        }
        else if (g_commandPurpose == WIFI_COMMAND_RECV_HOST)
        {
            g_rxPendingBitmap &= (uint8_t)~(1U << ESP32_LINK_SOCKET_HOST);
            g_nextStatusMs = Time_GetTickMs();
        }
        else if (g_commandPurpose == WIFI_COMMAND_RECV_UPLOAD)
        {
            g_rxPendingBitmap &= (uint8_t)~(1U << ESP32_LINK_SOCKET_UPLOAD);
            g_nextStatusMs = Time_GetTickMs();
        }
        ESP32_Link_ConsumeResult();
        g_commandInFlight = 0U;
        g_commandPurpose = WIFI_COMMAND_NONE;
        return;
    }

    switch (g_commandPurpose)
    {
        case WIFI_COMMAND_HELLO:
            if ((length >= WIFI_SERVICE_HELLO_MIN_LENGTH) &&
                (g_response[1] == ESP32_LINK_PROTOCOL_VERSION))
            {
                g_linkOnline = 1U;
                g_needsConfiguration = 1U;
                g_configured = 0U;
                g_linkFailureCount = 0U;
            }
            else
            {
                g_linkOnline = 0U;
                g_nextHelloMs = Time_GetTickMs() + WIFI_SERVICE_RETRY_INTERVAL_MS;
            }
            break;

        case WIFI_COMMAND_CONFIG:
            g_configured = 1U;
            g_needsConfiguration = 0U;
            g_settingsSnapshot = *ReaderSettings_Get();
            g_settingsSnapshotValid = 1U;
            g_wifiConnectIssued = 0U;
            break;

        case WIFI_COMMAND_CONNECT:
            g_wifiConnectIssued = 1U;
            break;

        case WIFI_COMMAND_DISCONNECT:
            g_wifiConnectIssued = 2U;
            g_wifiState = ESP32_LINK_WIFI_DISABLED;
            g_hostSocketOpened = 0U;
            g_uploadSocketOpened = 0U;
            break;

        case WIFI_COMMAND_STATUS:
            if (length >= ESP32_LINK_STATUS_PAYLOAD_SIZE)
            {
                uint8_t previousConnected = g_connectedBitmap;
                g_wifiState = g_response[ESP32_LINK_STATUS_WIFI_STATE];
                memcpy(g_ip, &g_response[ESP32_LINK_STATUS_IPV4], sizeof(g_ip));
                g_rssi = (int8_t)g_response[ESP32_LINK_STATUS_RSSI];
                g_connectedBitmap = g_response[ESP32_LINK_STATUS_CONNECTED];
                g_rxPendingBitmap = g_response[ESP32_LINK_STATUS_RX_PENDING];
                g_eventBitmap = g_response[ESP32_LINK_STATUS_EVENTS];
                memcpy(g_mac, &g_response[ESP32_LINK_STATUS_MAC], sizeof(g_mac));
                g_hostWasConnected = (uint8_t)((previousConnected & 0x01U) != 0U);
                g_uploadWasConnected = (uint8_t)((previousConnected & 0x02U) != 0U);
                if ((g_hostWasConnected != 0U) && ((g_connectedBitmap & 0x01U) == 0U) &&
                    (HOST_Transport_GetActive() == HOST_TRANSPORT_WIFI))
                {
                    HOST_Transport_Release();
                }
                if ((g_uploadWasConnected != 0U) && ((g_connectedBitmap & 0x02U) == 0U) &&
                    (ReaderSettings_GetUploadMode() == READER_SETTINGS_UPLOAD_HTTP))
                {
                    HttpUpload_OnDisconnected();
                }
                g_nextStatusMs = Time_GetTickMs() + WIFI_SERVICE_POLL_INTERVAL_MS;
            }
            else
            {
                g_nextStatusMs = Time_GetTickMs() + WIFI_SERVICE_POLL_INTERVAL_MS;
            }
            break;

        case WIFI_COMMAND_OPEN_HOST:
            g_hostSocketOpened = 1U;
            break;

        case WIFI_COMMAND_OPEN_UPLOAD:
            g_uploadSocketOpened = 1U;
            g_uploadTargetValid = 1U;
            break;

        case WIFI_COMMAND_CLOSE_HOST:
            g_hostSocketOpened = 0U;
            break;

        case WIFI_COMMAND_CLOSE_UPLOAD:
            g_uploadSocketOpened = 0U;
            g_uploadTargetValid = 0U;
            g_sendPendingLength = 0U;
            if (ReaderSettings_GetUploadMode() == READER_SETTINGS_UPLOAD_HTTP)
                HttpUpload_OnDisconnected();
            break;

        case WIFI_COMMAND_RECV_HOST:
            WiFi_HandleReceiveResponse(ESP32_LINK_SOCKET_HOST);
            break;

        case WIFI_COMMAND_RECV_UPLOAD:
            WiFi_HandleReceiveResponse(ESP32_LINK_SOCKET_UPLOAD);
            break;

        case WIFI_COMMAND_SEND_HOST:
            if (length >= 3U)
            {
                uint16_t accepted = ((uint16_t)g_response[1] << 8U) | g_response[2];
                if (accepted > g_sendPendingLength) accepted = g_sendPendingLength;
                WiFi_QueueConsume(&g_hostTx, accepted);
            }
            g_sendPendingLength = 0U;
            break;

        case WIFI_COMMAND_SEND_UPLOAD:
            if (length >= 3U)
            {
                uint16_t accepted = ((uint16_t)g_response[1] << 8U) | g_response[2];
                if (accepted > g_sendPendingLength) accepted = g_sendPendingLength;
                WiFi_QueueConsume(&g_uploadTx, accepted);
            }
            g_sendPendingLength = 0U;
            break;

        default:
            break;
    }

    ESP32_Link_ConsumeResult();
    g_commandInFlight = 0U;
    g_commandPurpose = WIFI_COMMAND_NONE;
}

static uint8_t WiFi_SubmitSocketOpen(uint8_t socket, uint8_t type,
                                     const uint8_t ip[4], uint16_t port,
                                     WiFi_CommandPurpose_t purpose)
{
    uint8_t payload[ESP32_LINK_SOCK_OPEN_CLIENT_SIZE];
    uint16_t length;
    memset(payload, 0, sizeof(payload));
    payload[ESP32_LINK_SOCK_OPEN_TYPE] = type;
    payload[ESP32_LINK_SOCK_OPEN_PORT_HI] = (uint8_t)(port >> 8U);
    payload[ESP32_LINK_SOCK_OPEN_PORT_LO] = (uint8_t)(port & 0xFFU);
    if (type == ESP32_LINK_SOCK_OPEN_CLIENT)
    {
        if (ip == NULL) return 0U;
        memcpy(&payload[ESP32_LINK_SOCK_OPEN_IPV4], ip, 4U);
        length = ESP32_LINK_SOCK_OPEN_CLIENT_SIZE;
    }
    else
    {
        length = ESP32_LINK_SOCK_OPEN_SERVER_SIZE;
    }
    return WiFi_Submit(ESP32_LINK_CMD_SOCK_OPEN, socket, payload, length, purpose);
}

static uint8_t WiFi_SubmitNextSend(uint8_t socket)
{
    WiFi_TxQueue_t *queue = (socket == ESP32_LINK_SOCKET_HOST) ? &g_hostTx : &g_uploadTx;
    uint16_t count = WiFi_QueuePeek(queue, g_sendPending, WIFI_SERVICE_SEND_CHUNK);
    if (count == 0U) return 0U;
    g_sendPendingLength = count;
    return WiFi_Submit(ESP32_LINK_CMD_SOCK_SEND, socket, g_sendPending, count,
                       (socket == ESP32_LINK_SOCKET_HOST) ? WIFI_COMMAND_SEND_HOST : WIFI_COMMAND_SEND_UPLOAD);
}

static uint8_t WiFi_SubmitReceive(uint8_t socket)
{
    uint8_t maxLength = WIFI_SERVICE_RECV_CHUNK;
    return WiFi_Submit(ESP32_LINK_CMD_SOCK_RECV, socket, &maxLength, 1U,
                       (socket == ESP32_LINK_SOCKET_HOST) ? WIFI_COMMAND_RECV_HOST : WIFI_COMMAND_RECV_UPLOAD);
}

void WiFi_Service_Init(WiFi_RxByteCallback_t rxCallback)
{
    memset(&g_hostTx, 0, sizeof(g_hostTx));
    memset(&g_uploadTx, 0, sizeof(g_uploadTx));
    memset(&g_uploadRx, 0, sizeof(g_uploadRx));
    memset(&g_settingsSnapshot, 0, sizeof(g_settingsSnapshot));
    g_rxCallback = rxCallback;
    g_commandPurpose = WIFI_COMMAND_NONE;
    g_commandInFlight = 0U;
    g_linkOnline = 0U;
    g_configured = 0U;
    g_wifiConnectIssued = 0U;
    g_hostSocketOpened = 0U;
    g_uploadSocketOpened = 0U;
    g_wifiState = ESP32_LINK_WIFI_DISABLED;
    g_connectedBitmap = 0U;
    g_rxPendingBitmap = 0U;
    g_eventBitmap = 0U;
    g_rssi = 0;
    memset(g_ip, 0, sizeof(g_ip));
    memset(g_mac, 0, sizeof(g_mac));
    g_settingsSnapshotValid = 0U;
    g_uploadWasConnected = 0U;
    g_hostWasConnected = 0U;
    g_nextHelloMs = Time_GetTickMs();
    g_nextStatusMs = Time_GetTickMs();
    g_nextUploadOpenMs = Time_GetTickMs();
    g_linkFailureCount = 0U;
    g_needsConfiguration = 1U;
    g_uploadTargetValid = 0U;
    g_sendPendingLength = 0U;
    g_available = ESP32_Link_Init();
}

void WiFi_Service_Task(void)
{
    uint32_t now;
    const ReaderSettings_t *settings;
    uint8_t configPayload[ESP32_LINK_CFG_PAYLOAD_SIZE];
    uint8_t targetIp[4];
    uint16_t targetPort;

    ESP32_Link_Task();
    now = Time_GetTickMs();
    settings = ReaderSettings_Get();

    if (g_commandInFlight != 0U)
    {
        WiFi_HandleCompletedCommand();
        if (g_commandInFlight != 0U) return;
    }

    if (ESP32_Link_IsSupported() == 0U)
    {
        g_available = 0U;
        return;
    }
    if (ESP32_Link_IsBooted() == 0U)
    {
        /* EN/bus recovery restarts the coprocessor without a separate IRQ.
         * Force a fresh HELLO/config/socket sequence after it boots again. */
        g_available = 0U;
        g_linkOnline = 0U;
        g_configured = 0U;
        g_settingsSnapshotValid = 0U;
        g_needsConfiguration = 1U;
        g_wifiConnectIssued = 0U;
        g_hostSocketOpened = 0U;
        g_uploadSocketOpened = 0U;
        g_uploadTargetValid = 0U;
        return;
    }
    g_available = 1U;

    if ((g_linkOnline == 0U) && ((int32_t)(now - g_nextHelloMs) >= 0))
    {
        (void)WiFi_Submit(ESP32_LINK_CMD_HELLO, ESP32_LINK_SOCKET_HOST, NULL, 0U, WIFI_COMMAND_HELLO);
        return;
    }

    if (g_linkOnline == 0U) return;

    if (g_settingsSnapshotValid != 0U)
    {
        if (WiFi_SettingsEqual(settings, &g_settingsSnapshot) == 0U)
        {
            g_needsConfiguration = 1U;
            g_configured = 0U;
            g_hostSocketOpened = 0U;
            g_uploadSocketOpened = 0U;
            g_uploadTargetValid = 0U;
            g_nextHelloMs = now;
        }
        else if (WiFi_SettingsTargetChanged(settings, &g_settingsSnapshot) != 0U)
        {
            g_uploadTargetValid = 0U;
            WiFi_CopyTargetSettings(&g_settingsSnapshot, settings);
            if (g_uploadSocketOpened != 0U)
            {
                (void)WiFi_Submit(ESP32_LINK_CMD_SOCK_CLOSE, ESP32_LINK_SOCKET_UPLOAD,
                                  NULL, 0U, WIFI_COMMAND_CLOSE_UPLOAD);
                return;
            }
        }
    }

    if (g_needsConfiguration != 0U)
    {
        if (WiFi_BuildConfigPayload(configPayload, sizeof(configPayload)) != 0U)
        {
            (void)WiFi_Submit(ESP32_LINK_CMD_WIFI_CONFIG, ESP32_LINK_SOCKET_HOST,
                              configPayload, ESP32_LINK_CFG_PAYLOAD_SIZE, WIFI_COMMAND_CONFIG);
        }
        return;
    }

    if (g_configured == 0U) return;

    if ((settings->wifiEnabled == 0U) && (g_wifiConnectIssued != 2U))
    {
        (void)WiFi_Submit(ESP32_LINK_CMD_WIFI_DISCONNECT, ESP32_LINK_SOCKET_HOST,
                          NULL, 0U, WIFI_COMMAND_DISCONNECT);
        return;
    }
    if ((settings->wifiEnabled != 0U) && (g_wifiConnectIssued != 1U))
    {
        (void)WiFi_Submit(ESP32_LINK_CMD_WIFI_CONNECT, ESP32_LINK_SOCKET_HOST,
                          NULL, 0U, WIFI_COMMAND_CONNECT);
        return;
    }

    /* Consume pending socket bytes before the next status poll. */
    if ((g_rxPendingBitmap & (1U << ESP32_LINK_SOCKET_HOST)) != 0U &&
        (g_hostSocketOpened != 0U) && ((g_connectedBitmap & 0x01U) != 0U))
    {
        (void)WiFi_SubmitReceive(ESP32_LINK_SOCKET_HOST);
        return;
    }
    if ((g_rxPendingBitmap & (1U << ESP32_LINK_SOCKET_UPLOAD)) != 0U &&
        (g_uploadSocketOpened != 0U) && ((g_connectedBitmap & 0x02U) != 0U))
    {
        (void)WiFi_SubmitReceive(ESP32_LINK_SOCKET_UPLOAD);
        return;
    }

    if ((settings->wifiEnabled != 0U) &&
        ((g_wifiState == ESP32_LINK_WIFI_CONNECTED) || (g_wifiState == ESP32_LINK_WIFI_AP_STARTED)) &&
        (g_hostSocketOpened == 0U))
    {
        (void)WiFi_SubmitSocketOpen(ESP32_LINK_SOCKET_HOST, ESP32_LINK_SOCK_OPEN_SERVER,
                                    NULL, PRODUCT_ETHERNET_TCP_PORT, WIFI_COMMAND_OPEN_HOST);
        return;
    }

    if ((WiFi_UploadSelected() == 0U) || (settings->workState != READER_SETTINGS_WORK_ACTIVE))
    {
        if (g_uploadSocketOpened != 0U)
        {
            (void)WiFi_Submit(ESP32_LINK_CMD_SOCK_CLOSE, ESP32_LINK_SOCKET_UPLOAD,
                              NULL, 0U, WIFI_COMMAND_CLOSE_UPLOAD);
            return;
        }
        g_uploadTargetValid = 0U;
    }
    else if ((settings->wifiEnabled != 0U) &&
             ((g_wifiState == ESP32_LINK_WIFI_CONNECTED) || (g_wifiState == ESP32_LINK_WIFI_AP_STARTED)))
    {
        if ((g_uploadSocketOpened != 0U) && (WiFi_TargetSameAsOpened() == 0U))
        {
            (void)WiFi_Submit(ESP32_LINK_CMD_SOCK_CLOSE, ESP32_LINK_SOCKET_UPLOAD,
                              NULL, 0U, WIFI_COMMAND_CLOSE_UPLOAD);
            return;
        }
        if ((g_uploadSocketOpened == 0U) && ((int32_t)(now - g_nextUploadOpenMs) >= 0) &&
            (WiFi_GetUploadTarget(targetIp, &targetPort) != 0U))
        {
            if (WiFi_SubmitSocketOpen(ESP32_LINK_SOCKET_UPLOAD, ESP32_LINK_SOCK_OPEN_CLIENT,
                                      targetIp, targetPort, WIFI_COMMAND_OPEN_UPLOAD) != 0U)
            {
                memcpy(g_uploadTargetIp, targetIp, sizeof(g_uploadTargetIp));
                g_uploadTargetPort = targetPort;
            }
            return;
        }
    }

    if ((settings->uploadMode == READER_SETTINGS_UPLOAD_HTTP) &&
        (WiFi_UploadSelected() != 0U) && ((g_connectedBitmap & 0x02U) != 0U))
    {
        HttpUpload_Task();
    }

    /* Status has a bounded cadence even while either socket is busy. */
    if ((int32_t)(now - g_nextStatusMs) >= 0)
    {
        (void)WiFi_Submit(ESP32_LINK_CMD_GET_STATUS, ESP32_LINK_SOCKET_HOST,
                          NULL, 0U, WIFI_COMMAND_STATUS);
        return;
    }

    if (((g_connectedBitmap & 0x01U) != 0U) && (g_hostTx.used != 0U))
    {
        (void)WiFi_SubmitNextSend(ESP32_LINK_SOCKET_HOST);
        return;
    }

    if (((g_connectedBitmap & 0x02U) != 0U) && (g_uploadTx.used != 0U) &&
        (WiFi_UploadSelected() != 0U) && (settings->workState == READER_SETTINGS_WORK_ACTIVE))
    {
        (void)WiFi_SubmitNextSend(ESP32_LINK_SOCKET_UPLOAD);
    }
}

uint8_t WiFi_Service_IsAvailable(void)
{
    return (g_available != 0U) && (g_linkOnline != 0U);
}

uint8_t WiFi_Service_IsConnected(void)
{
    return ((g_wifiState == ESP32_LINK_WIFI_CONNECTED) ||
            (g_wifiState == ESP32_LINK_WIFI_AP_STARTED)) ? 1U : 0U;
}

uint8_t WiFi_Service_IsHostConnected(void)
{
    return ((g_connectedBitmap & 0x01U) != 0U) ? 1U : 0U;
}

uint8_t WiFi_Service_IsUploadConnected(void)
{
    return ((g_connectedBitmap & 0x02U) != 0U) ? 1U : 0U;
}

uint16_t WiFi_Service_HostSend(const uint8_t *data, uint16_t length)
{
    if (WiFi_Service_IsHostConnected() == 0U) return 0U;
    return WiFi_QueueWrite(&g_hostTx, data, length);
}

uint16_t WiFi_Service_HostGetFreeSpace(void)
{
    return (uint16_t)(WIFI_SERVICE_TX_QUEUE_SIZE - g_hostTx.used);
}

uint16_t WiFi_Service_UploadSend(const uint8_t *data, uint16_t length)
{
    if (WiFi_Service_IsUploadConnected() == 0U) return 0U;
    return WiFi_QueueWrite(&g_uploadTx, data, length);
}

uint16_t WiFi_Service_UploadGetFreeSpace(void)
{
    return (uint16_t)(WIFI_SERVICE_TX_QUEUE_SIZE - g_uploadTx.used);
}

uint8_t WiFi_Service_CloseUploadSocket(void)
{
    if ((g_linkOnline == 0U) || (g_uploadSocketOpened == 0U)) return 0U;
    return WiFi_Submit(ESP32_LINK_CMD_SOCK_CLOSE, ESP32_LINK_SOCKET_UPLOAD,
                       NULL, 0U, WIFI_COMMAND_CLOSE_UPLOAD);
}

void WiFi_Service_GetStatus(WiFi_Status_t *status)
{
    if (status == NULL) return;
    status->linkOnline = (uint8_t)((g_linkOnline != 0U) && (g_configured != 0U));
    status->wifiState = g_wifiState;
    memcpy(status->ip, g_ip, sizeof(status->ip));
    status->rssi = g_rssi;
    status->hostConnected = WiFi_Service_IsHostConnected();
    status->uploadConnected = WiFi_Service_IsUploadConnected();
    status->linkErrors = ESP32_Link_GetErrorCount();
}
