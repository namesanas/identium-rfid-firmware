#ifndef RFID_CONTROLLER_ESP32_LINK_PROTOCOL_H_
#define RFID_CONTROLLER_ESP32_LINK_PROTOCOL_H_

#include <stdint.h>

/* This header is shared verbatim by the STM32 firmware and ESP-IDF project. */
#define ESP32_LINK_MAGIC_0              0x52U
#define ESP32_LINK_MAGIC_1              0x57U
#define ESP32_LINK_PROTOCOL_VERSION     1U
#define ESP32_LINK_I2C_ADDRESS          0x42U
#define ESP32_LINK_HEADER_SIZE          9U
#define ESP32_LINK_CRC_SIZE             2U
#define ESP32_LINK_MAX_PAYLOAD          220U
#define ESP32_LINK_MAX_FRAME_SIZE       (ESP32_LINK_HEADER_SIZE + ESP32_LINK_MAX_PAYLOAD + ESP32_LINK_CRC_SIZE)
#define ESP32_LINK_RESPONSE_SIZE        64U
#define ESP32_LINK_RESPONSE_MAX_PAYLOAD (ESP32_LINK_RESPONSE_SIZE - ESP32_LINK_HEADER_SIZE - ESP32_LINK_CRC_SIZE)

#define ESP32_LINK_FLAG_RESPONSE        0x01U
#define ESP32_LINK_FLAG_NOT_READY       0x02U
#define ESP32_LINK_FLAG_ERROR           0x04U

#define ESP32_LINK_SOCKET_HOST          0U
#define ESP32_LINK_SOCKET_UPLOAD        1U
#define ESP32_LINK_SOCKET_COUNT         2U

#define ESP32_LINK_WIFI_DISABLED        0U
#define ESP32_LINK_WIFI_IDLE            1U
#define ESP32_LINK_WIFI_CONNECTING      2U
#define ESP32_LINK_WIFI_CONNECTED       3U
#define ESP32_LINK_WIFI_AP_STARTED      4U
#define ESP32_LINK_WIFI_ERROR           5U

#define ESP32_LINK_MODE_STA             0U
#define ESP32_LINK_MODE_AP              1U

#define ESP32_LINK_IP_DHCP              1U
#define ESP32_LINK_IP_STATIC            0U

#define ESP32_LINK_CMD_HELLO             0x01U
#define ESP32_LINK_CMD_GET_STATUS        0x02U
#define ESP32_LINK_CMD_WIFI_CONFIG       0x10U
#define ESP32_LINK_CMD_WIFI_CONNECT      0x11U
#define ESP32_LINK_CMD_WIFI_DISCONNECT   0x12U
#define ESP32_LINK_CMD_SCAN_START        0x13U
#define ESP32_LINK_CMD_SCAN_RESULTS      0x14U
#define ESP32_LINK_CMD_SOCK_OPEN         0x20U
#define ESP32_LINK_CMD_SOCK_CLOSE        0x21U
#define ESP32_LINK_CMD_SOCK_SEND         0x22U
#define ESP32_LINK_CMD_SOCK_RECV         0x23U
#define ESP32_LINK_CMD_RESET             0x30U

#define ESP32_LINK_RESULT_OK             0U
#define ESP32_LINK_RESULT_FAILED         1U
#define ESP32_LINK_RESULT_INVALID        2U
#define ESP32_LINK_RESULT_BUSY           3U
#define ESP32_LINK_RESULT_NOT_CONNECTED  4U

/* WIFI_CONFIG payload offsets; fixed layout avoids compiler packing rules. */
#define ESP32_LINK_CFG_ENABLED_OFFSET    0U
#define ESP32_LINK_CFG_MODE_OFFSET       1U
#define ESP32_LINK_CFG_DHCP_OFFSET       2U
#define ESP32_LINK_CFG_AP_CHANNEL_OFFSET 3U
#define ESP32_LINK_CFG_STA_SSID_LEN      4U
#define ESP32_LINK_CFG_STA_PASS_LEN      5U
#define ESP32_LINK_CFG_AP_SSID_LEN       6U
#define ESP32_LINK_CFG_AP_PASS_LEN       7U
#define ESP32_LINK_CFG_STA_SSID          8U
#define ESP32_LINK_CFG_STA_PASS          40U
#define ESP32_LINK_CFG_AP_SSID           104U
#define ESP32_LINK_CFG_AP_PASS           136U
#define ESP32_LINK_CFG_STATIC_IP         200U
#define ESP32_LINK_CFG_NETMASK           204U
#define ESP32_LINK_CFG_GATEWAY           208U
#define ESP32_LINK_CFG_DNS               212U
#define ESP32_LINK_CFG_PAYLOAD_SIZE      216U

/* GET_STATUS response payload offsets. */
#define ESP32_LINK_STATUS_WIFI_STATE     1U
#define ESP32_LINK_STATUS_IPV4           2U
#define ESP32_LINK_STATUS_RSSI           6U
#define ESP32_LINK_STATUS_CONNECTED      7U
#define ESP32_LINK_STATUS_RX_PENDING     8U
#define ESP32_LINK_STATUS_EVENTS         9U
#define ESP32_LINK_STATUS_MAC            10U
#define ESP32_LINK_STATUS_PAYLOAD_SIZE   16U

/* SOCK_OPEN payload: type, port BE, IPv4 address (client only). */
#define ESP32_LINK_SOCK_OPEN_SERVER      0U
#define ESP32_LINK_SOCK_OPEN_CLIENT      1U
#define ESP32_LINK_SOCK_OPEN_TYPE        0U
#define ESP32_LINK_SOCK_OPEN_PORT_HI     1U
#define ESP32_LINK_SOCK_OPEN_PORT_LO     2U
#define ESP32_LINK_SOCK_OPEN_IPV4        3U
#define ESP32_LINK_SOCK_OPEN_CLIENT_SIZE 7U
#define ESP32_LINK_SOCK_OPEN_SERVER_SIZE 3U

/* A response payload begins with this application-level status byte. */
#define ESP32_LINK_REPLY_STATUS_OFFSET   0U
#define ESP32_LINK_REPLY_DATA_OFFSET     1U

/* CRC-16/CCITT-FALSE: poly 0x1021, initial 0xFFFF, no final xor. */
static inline uint16_t ESP32_LinkProtocol_Crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t bit;

    if (data == (const uint8_t *)0)
    {
        return 0U;
    }

    for (i = 0U; i < length; i++)
    {
        crc ^= (uint16_t)data[i] << 8U;
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 0x8000U) != 0U) ?
                (uint16_t)((crc << 1U) ^ 0x1021U) : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

#endif /* RFID_CONTROLLER_ESP32_LINK_PROTOCOL_H_ */
