#ifndef RFID_CONTROLLER_WIFI_SERVICE_H_
#define RFID_CONTROLLER_WIFI_SERVICE_H_

#include <stdint.h>

#include "../../Devices/esp32/esp32_link_protocol.h"

typedef void (*WiFi_RxByteCallback_t)(uint8_t receivedByte);

typedef struct
{
    uint8_t linkOnline;      /* ESP answered HELLO and is configured */
    uint8_t wifiState;       /* ESP32_LINK_WIFI_* */
    uint8_t ip[4];
    int8_t rssi;
    uint8_t hostConnected;   /* a client is on the WiFi host port */
    uint8_t uploadConnected; /* active-mode upload connection is up */
    uint32_t linkErrors;     /* failed I2C transactions since start-up */
    uint8_t rejectedCommand; /* last CONFIG/CONNECT/DISCONNECT the ESP refused (0 = none) */
    uint8_t rejectedResult;  /* its ESP32_LINK_RESULT_* code */
} WiFi_Status_t;

/* Snapshot for GET_WIFI. */
void WiFi_Service_GetStatus(WiFi_Status_t *status);

void WiFi_Service_Init(WiFi_RxByteCallback_t rxCallback);
void WiFi_Service_Task(void);
uint8_t WiFi_Service_IsAvailable(void);
uint8_t WiFi_Service_IsConnected(void);
uint8_t WiFi_Service_IsHostConnected(void);
uint8_t WiFi_Service_IsUploadConnected(void);
uint16_t WiFi_Service_HostSend(const uint8_t *data, uint16_t length);
uint16_t WiFi_Service_HostGetFreeSpace(void);
uint16_t WiFi_Service_UploadSend(const uint8_t *data, uint16_t length);
uint16_t WiFi_Service_UploadGetFreeSpace(void);
uint16_t WiFi_Service_UploadReceive(uint8_t *data, uint16_t capacity);
uint16_t WiFi_Service_UploadReceiveCount(void);
uint8_t WiFi_Service_CloseUploadSocket(void);

#endif /* RFID_CONTROLLER_WIFI_SERVICE_H_ */
