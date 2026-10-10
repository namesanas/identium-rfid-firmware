#ifndef RFID_CONTROLLER_WIFI_SERVICE_H_
#define RFID_CONTROLLER_WIFI_SERVICE_H_

#include <stdint.h>

#include "../../Devices/esp32/esp32_link_protocol.h"

typedef void (*WiFi_RxByteCallback_t)(uint8_t receivedByte);

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
