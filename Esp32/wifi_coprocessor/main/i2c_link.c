#include "i2c_link.h"

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

#include "esp32_link_protocol.h"
#include "socket_service.h"
#include "wifi_manager.h"

#define I2C_LINK_RX_BUFFER_SIZE 512U
#define I2C_LINK_RESET_DELAY_MS 200U

#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define I2C_LINK_SCL_PIN GPIO_NUM_6
#define I2C_LINK_SDA_PIN GPIO_NUM_10
#define I2C_LINK_CHIP_ID   2U
#elif defined(CONFIG_IDF_TARGET_ESP32)
#define I2C_LINK_SCL_PIN GPIO_NUM_22
#define I2C_LINK_SDA_PIN GPIO_NUM_21
#define I2C_LINK_CHIP_ID   1U
#else
#error "wifi_coprocessor supports only esp32 and esp32c3 targets"
#endif

static const char *TAG = "i2c_link";
static uint8_t g_rxStream[I2C_LINK_RX_BUFFER_SIZE];
static size_t g_rxUsed;
static uint8_t g_replyFrame[ESP32_LINK_RESPONSE_SIZE];
static uint8_t g_replyPending;
static uint16_t g_replyOffset;
static uint64_t g_restartAtMs;
static uint8_t g_cachedStatus[ESP32_LINK_STATUS_PAYLOAD_SIZE];

/* The last executed request and its reply. The master resends a request
 * with the same sequence number when it lost the reply; replaying the reply
 * keeps SOCK_SEND / SOCK_RECV from being executed twice. */
static uint8_t g_lastValid;
static uint8_t g_lastSequence;
static uint8_t g_lastCommand;
static uint8_t g_lastSocket;
static uint8_t g_lastReply[ESP32_LINK_RESPONSE_SIZE];

static uint64_t I2cLink_NowMs(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

static void I2cLink_RemoveRxPrefix(size_t count)
{
    if(count >= g_rxUsed)
    {
        g_rxUsed = 0U;
        return;
    }
    memmove(g_rxStream, &g_rxStream[count], g_rxUsed - count);
    g_rxUsed -= count;
}

static void I2cLink_RefreshCachedStatus(void)
{
    WifiManager_Status_t status;
    WifiManager_GetStatus(&status);
    memset(g_cachedStatus, 0, sizeof(g_cachedStatus));
    g_cachedStatus[0] = ESP32_LINK_RESULT_OK;
    g_cachedStatus[ESP32_LINK_STATUS_WIFI_STATE] = status.state;
    memcpy(&g_cachedStatus[ESP32_LINK_STATUS_IPV4], status.ip, sizeof(status.ip));
    g_cachedStatus[ESP32_LINK_STATUS_RSSI] = (uint8_t)status.rssi;
    g_cachedStatus[ESP32_LINK_STATUS_CONNECTED] = SocketService_GetConnectedBitmap();
    g_cachedStatus[ESP32_LINK_STATUS_RX_PENDING] = SocketService_GetRxPendingBitmap();
    g_cachedStatus[ESP32_LINK_STATUS_EVENTS] = (uint8_t)(status.events | SocketService_GetEventBitmap());
    memcpy(&g_cachedStatus[ESP32_LINK_STATUS_MAC], status.mac, sizeof(status.mac));
}

static void I2cLink_BuildReply(uint8_t sequence, uint8_t command, uint8_t socketId,
                               uint8_t flags, const uint8_t *payload, uint16_t length)
{
    uint16_t crc;
    uint16_t crcIndex;

    if(length > ESP32_LINK_RESPONSE_MAX_PAYLOAD)
    {
        length = 1U;
        flags |= ESP32_LINK_FLAG_ERROR;
    }
    memset(g_replyFrame, 0, sizeof(g_replyFrame));
    g_replyFrame[0] = ESP32_LINK_MAGIC_0;
    g_replyFrame[1] = ESP32_LINK_MAGIC_1;
    g_replyFrame[2] = ESP32_LINK_PROTOCOL_VERSION;
    g_replyFrame[3] = sequence;
    g_replyFrame[4] = command;
    g_replyFrame[5] = socketId;
    g_replyFrame[6] = (uint8_t)(length & 0xFFU);
    g_replyFrame[7] = (uint8_t)(length >> 8U);
    g_replyFrame[8] = (uint8_t)(ESP32_LINK_FLAG_RESPONSE | flags);
    if((payload != NULL) && (length > 0U))
        memcpy(&g_replyFrame[ESP32_LINK_HEADER_SIZE], payload, length);
    crcIndex = (uint16_t)(ESP32_LINK_HEADER_SIZE + length);
    crc = ESP32_LinkProtocol_Crc16(g_replyFrame, crcIndex);
    g_replyFrame[crcIndex] = (uint8_t)(crc & 0xFFU);
    g_replyFrame[crcIndex + 1U] = (uint8_t)(crc >> 8U);
    g_replyPending = 1U;
    g_replyOffset = 0U;

    /* A NOT_READY reply is not replayed: the request was not executed. */
    g_lastValid = ((flags & ESP32_LINK_FLAG_NOT_READY) == 0U) ? 1U : 0U;
    g_lastSequence = sequence;
    g_lastCommand = command;
    g_lastSocket = socketId;
    memcpy(g_lastReply, g_replyFrame, sizeof(g_lastReply));
}

static void I2cLink_SetStatusOnly(uint8_t sequence, uint8_t command, uint8_t socketId,
                                  uint8_t status)
{
    uint8_t payload[1] = { status };
    uint8_t flags = 0U;
    if(status == ESP32_LINK_RESULT_BUSY)
        flags = ESP32_LINK_FLAG_NOT_READY;
    else if(status != ESP32_LINK_RESULT_OK)
        flags = ESP32_LINK_FLAG_ERROR;
    I2cLink_BuildReply(sequence, command, socketId, flags, payload, sizeof(payload));
}

static void I2cLink_HandleRequest(const uint8_t *frame, uint16_t payloadLength)
{
    const uint8_t sequence = frame[3];
    const uint8_t command = frame[4];
    const uint8_t socketId = frame[5];
    const uint8_t *payload = &frame[ESP32_LINK_HEADER_SIZE];
    uint8_t reply[ESP32_LINK_RESPONSE_MAX_PAYLOAD];
    uint16_t replyLength = 1U;
    uint8_t status = ESP32_LINK_RESULT_OK;
    uint8_t flags = 0U;

    if((g_lastValid != 0U) && (sequence == g_lastSequence) &&
       (command == g_lastCommand) && (socketId == g_lastSocket))
    {
        memcpy(g_replyFrame, g_lastReply, sizeof(g_replyFrame));
        g_replyPending = 1U;
        g_replyOffset = 0U;
        return;
    }

    memset(reply, 0, sizeof(reply));
    reply[0] = ESP32_LINK_RESULT_OK;

    switch(command)
    {
        case ESP32_LINK_CMD_HELLO:
        {
            WifiManager_Status_t wifiStatus;
            WifiManager_GetStatus(&wifiStatus);
            reply[1] = ESP32_LINK_PROTOCOL_VERSION;
            reply[2] = I2C_LINK_CHIP_ID;
            memcpy(&reply[3], wifiStatus.mac, sizeof(wifiStatus.mac));
            reply[9] = ESP32_LINK_SOCKET_COUNT;
            replyLength = 10U;
            break;
        }

        case ESP32_LINK_CMD_GET_STATUS:
            I2cLink_RefreshCachedStatus();
            memcpy(reply, g_cachedStatus, sizeof(g_cachedStatus));
            replyLength = sizeof(g_cachedStatus);
            (void)WifiManager_TakeEvents();
            (void)SocketService_TakeEventBitmap();
            break;

        case ESP32_LINK_CMD_WIFI_CONFIG:
            status = WifiManager_ApplyConfig(payload, payloadLength);
            I2cLink_SetStatusOnly(sequence, command, socketId, status);
            return;

        case ESP32_LINK_CMD_WIFI_CONNECT:
            status = WifiManager_Connect();
            I2cLink_SetStatusOnly(sequence, command, socketId, status);
            return;

        case ESP32_LINK_CMD_WIFI_DISCONNECT:
            status = WifiManager_Disconnect();
            I2cLink_SetStatusOnly(sequence, command, socketId, status);
            return;

        case ESP32_LINK_CMD_SCAN_START:
            status = WifiManager_ScanStart();
            I2cLink_SetStatusOnly(sequence, command, socketId, status);
            return;

        case ESP32_LINK_CMD_SCAN_RESULTS:
        {
            WifiManager_ScanResult_t scan;
            uint8_t count = 0U;
            uint8_t complete = 0U;
            uint8_t index = (payloadLength >= 1U) ? payload[0] : 0U;
            status = WifiManager_ScanGetResult(index, &scan, &count, &complete);
            reply[0] = status;
            if(status == ESP32_LINK_RESULT_OK)
            {
                size_t ssidLength = strnlen(scan.ssid, sizeof(scan.ssid));
                reply[1] = count;
                reply[2] = index;
                reply[3] = complete;
                reply[4] = (uint8_t)ssidLength;
                reply[5] = (uint8_t)scan.rssi;
                reply[6] = scan.authMode;
                reply[7] = scan.channel;
                memcpy(&reply[8], scan.ssid, ssidLength);
                replyLength = (uint16_t)(8U + ssidLength);
            }
            else
            {
                replyLength = 1U;
                if(status == ESP32_LINK_RESULT_BUSY) flags |= ESP32_LINK_FLAG_NOT_READY;
            }
            break;
        }

        case ESP32_LINK_CMD_SOCK_OPEN:
        {
            uint8_t type;
            uint16_t port;
            const uint8_t *ip = NULL;
            if((socketId >= ESP32_LINK_SOCKET_COUNT) || (payloadLength < ESP32_LINK_SOCK_OPEN_SERVER_SIZE))
            {
                status = ESP32_LINK_RESULT_INVALID;
                break;
            }
            type = payload[ESP32_LINK_SOCK_OPEN_TYPE];
            port = (uint16_t)(((uint16_t)payload[ESP32_LINK_SOCK_OPEN_PORT_HI] << 8U) |
                              payload[ESP32_LINK_SOCK_OPEN_PORT_LO]);
            if(type == ESP32_LINK_SOCK_OPEN_CLIENT)
            {
                if(payloadLength < ESP32_LINK_SOCK_OPEN_CLIENT_SIZE)
                {
                    status = ESP32_LINK_RESULT_INVALID;
                    break;
                }
                ip = &payload[ESP32_LINK_SOCK_OPEN_IPV4];
            }
            status = SocketService_Open(socketId, type, ip, port);
            break;
        }

        case ESP32_LINK_CMD_SOCK_CLOSE:
            status = SocketService_Close(socketId);
            break;

        case ESP32_LINK_CMD_SOCK_SEND:
        {
            uint16_t accepted = 0U;
            if((socketId >= ESP32_LINK_SOCKET_COUNT) || (payloadLength == 0U))
            {
                status = ESP32_LINK_RESULT_INVALID;
                break;
            }
            status = SocketService_Send(socketId, payload, payloadLength, &accepted);
            reply[1] = (uint8_t)(accepted >> 8U);
            reply[2] = (uint8_t)(accepted & 0xFFU);
            replyLength = 3U;
            break;
        }

        case ESP32_LINK_CMD_SOCK_RECV:
        {
            uint16_t maximum;
            uint16_t received;
            if((socketId >= ESP32_LINK_SOCKET_COUNT) || (payloadLength < 1U))
            {
                status = ESP32_LINK_RESULT_INVALID;
                break;
            }
            maximum = payload[0];
            if(maximum > (ESP32_LINK_RESPONSE_MAX_PAYLOAD - 2U))
                maximum = ESP32_LINK_RESPONSE_MAX_PAYLOAD - 2U;
            received = SocketService_Receive(socketId, &reply[2], maximum);
            reply[1] = (uint8_t)received;
            replyLength = (uint16_t)(2U + received);
            break;
        }

        case ESP32_LINK_CMD_RESET:
            g_restartAtMs = I2cLink_NowMs() + I2C_LINK_RESET_DELAY_MS;
            break;

        default:
            status = ESP32_LINK_RESULT_INVALID;
            break;
    }

    reply[0] = status;
    if(status == ESP32_LINK_RESULT_BUSY) flags |= ESP32_LINK_FLAG_NOT_READY;
    if(status != ESP32_LINK_RESULT_OK) flags |= ESP32_LINK_FLAG_ERROR;
    I2cLink_BuildReply(sequence, command, socketId, flags, reply, replyLength);
}

static void I2cLink_ParseRxStream(void)
{
    while(g_rxUsed >= ESP32_LINK_HEADER_SIZE)
    {
        uint16_t payloadLength;
        uint16_t frameLength;
        uint16_t crcExpected;
        uint16_t crcActual;
        size_t offset = 0U;

        while((offset + 1U < g_rxUsed) &&
              ((g_rxStream[offset] != ESP32_LINK_MAGIC_0) ||
               (g_rxStream[offset + 1U] != ESP32_LINK_MAGIC_1)))
            offset++;
        if(offset != 0U)
        {
            I2cLink_RemoveRxPrefix(offset);
            if(g_rxUsed < ESP32_LINK_HEADER_SIZE) return;
        }
        if((g_rxStream[0] != ESP32_LINK_MAGIC_0) || (g_rxStream[1] != ESP32_LINK_MAGIC_1))
        {
            I2cLink_RemoveRxPrefix(1U);
            continue;
        }
        if(g_rxStream[2] != ESP32_LINK_PROTOCOL_VERSION)
        {
            I2cLink_RemoveRxPrefix(1U);
            continue;
        }
        payloadLength = (uint16_t)g_rxStream[6] | ((uint16_t)g_rxStream[7] << 8U);
        if(payloadLength > ESP32_LINK_MAX_PAYLOAD)
        {
            I2cLink_RemoveRxPrefix(1U);
            continue;
        }
        frameLength = (uint16_t)(ESP32_LINK_HEADER_SIZE + payloadLength + ESP32_LINK_CRC_SIZE);
        if(g_rxUsed < frameLength) return;
        crcExpected = (uint16_t)g_rxStream[ESP32_LINK_HEADER_SIZE + payloadLength] |
                      ((uint16_t)g_rxStream[ESP32_LINK_HEADER_SIZE + payloadLength + 1U] << 8U);
        crcActual = ESP32_LinkProtocol_Crc16(g_rxStream,
                    (uint16_t)(ESP32_LINK_HEADER_SIZE + payloadLength));
        if(crcExpected == crcActual)
        {
            I2cLink_HandleRequest(g_rxStream, payloadLength);
        }
        else
        {
            ESP_LOGW(TAG, "request CRC mismatch");
        }
        I2cLink_RemoveRxPrefix(frameLength);
    }
}

static void I2cLink_StageReply(void)
{
    int written;
    if(g_replyPending == 0U) return;
    written = i2c_slave_write_buffer(I2C_NUM_0, &g_replyFrame[g_replyOffset],
                                     (int)(ESP32_LINK_RESPONSE_SIZE - g_replyOffset), 0);
    if(written > 0)
    {
        g_replyOffset = (uint16_t)(g_replyOffset + (uint16_t)written);
        if(g_replyOffset >= ESP32_LINK_RESPONSE_SIZE)
        {
            g_replyPending = 0U;
            g_replyOffset = 0U;
        }
    }
}

esp_err_t I2cLink_Init(void)
{
    i2c_config_t config;
    esp_err_t result;

    memset(&config, 0, sizeof(config));
    config.mode = I2C_MODE_SLAVE;
    config.sda_io_num = I2C_LINK_SDA_PIN;
    config.scl_io_num = I2C_LINK_SCL_PIN;
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.slave.slave_addr = ESP32_LINK_I2C_ADDRESS;
    config.slave.addr_10bit_en = 0;
    config.slave.maximum_speed = 400000U;
    result = i2c_param_config(I2C_NUM_0, &config);
    if(result != ESP_OK) return result;
    result = i2c_driver_install(I2C_NUM_0, I2C_MODE_SLAVE,
                                I2C_LINK_RX_BUFFER_SIZE, I2C_LINK_RX_BUFFER_SIZE, 0);
    if(result != ESP_OK) return result;

    g_rxUsed = 0U;
    g_replyPending = 0U;
    g_replyOffset = 0U;
    g_restartAtMs = 0U;
    g_lastValid = 0U;
    memset(g_cachedStatus, 0, sizeof(g_cachedStatus));
    I2cLink_RefreshCachedStatus();
    ESP_LOGI(TAG, "I2C slave address 0x%02X, SCL=%d SDA=%d",
             ESP32_LINK_I2C_ADDRESS, (int)I2C_LINK_SCL_PIN, (int)I2C_LINK_SDA_PIN);
    return ESP_OK;
}

void I2cLink_Task(void)
{
    uint8_t temporary[256];
    int received;

    WifiManager_Task();
    SocketService_Task();
    I2cLink_RefreshCachedStatus();

    received = i2c_slave_read_buffer(I2C_NUM_0, temporary, sizeof(temporary), 0);
    if(received > 0)
    {
        size_t amount = (size_t)received;
        if(amount > sizeof(g_rxStream) - g_rxUsed)
        {
            g_rxUsed = 0U;
            amount = (amount > sizeof(g_rxStream)) ? sizeof(g_rxStream) : amount;
        }
        memcpy(&g_rxStream[g_rxUsed], temporary, amount);
        g_rxUsed += amount;
        I2cLink_ParseRxStream();
    }

    I2cLink_StageReply();
    if((g_restartAtMs != 0U) && (I2cLink_NowMs() >= g_restartAtMs))
    {
        esp_restart();
    }
}
