#ifndef WIFI_COPROCESSOR_SOCKET_SERVICE_H_
#define WIFI_COPROCESSOR_SOCKET_SERVICE_H_

#include <stddef.h>
#include <stdint.h>

#include "esp32_link_protocol.h"

void SocketService_Init(void);
void SocketService_Task(void);
uint8_t SocketService_Open(uint8_t socketId, uint8_t type, const uint8_t ip[4], uint16_t port);
uint8_t SocketService_Close(uint8_t socketId);
uint8_t SocketService_Send(uint8_t socketId, const uint8_t *data, uint16_t length,
                           uint16_t *accepted);
uint16_t SocketService_Receive(uint8_t socketId, uint8_t *data, uint16_t capacity);
uint8_t SocketService_GetConnectedBitmap(void);
uint8_t SocketService_GetRxPendingBitmap(void);
uint8_t SocketService_GetEventBitmap(void);
uint8_t SocketService_TakeEventBitmap(void);

#endif
