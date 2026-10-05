#ifndef RFID_CONTROLLER_VCP_TRANSPORT_H_
#define RFID_CONTROLLER_VCP_TRANSPORT_H_

#include <stdint.h>
#include "../../Drivers/uart/uart.h"
#include "../../Protocols/silion/silion_protocol.h"

#define VCP_TX_BUFFER_SIZE 2048U

void VCP_Transport_Init(USART_Handle_t *pUSARTHandle);

void VCP_Transport_UartEvent(
    USART_Handle_t *pUSARTHandle,
    uint8_t AppEvent,
    uint8_t receivedByte
);

void VCP_SendString(const char *text);
void VCP_SendTag(const SILION_Tag_t *tag);

#endif /* RFID_CONTROLLER_VCP_TRANSPORT_H_ */
