#ifndef RFID_CONTROLLER_HOST_TRANSPORT_PORT_H_
#define RFID_CONTROLLER_HOST_TRANSPORT_PORT_H_

#include <stdint.h>

#define HOST_TRANSPORT_USB       0U
#define HOST_TRANSPORT_ETHERNET  1U
#define HOST_TRANSPORT_NONE      2U
#define HOST_RX_QUEUE_SIZE       256U

extern volatile uint8_t hostRxQueue[HOST_RX_QUEUE_SIZE];
extern volatile uint16_t hostRxHead;
extern volatile uint16_t hostRxTail;
extern volatile uint16_t hostRxCount;
extern volatile uint32_t hostRxOverflow;
extern volatile uint8_t hostRxSourceQueue[HOST_RX_QUEUE_SIZE];
extern volatile uint8_t hostActiveTransport;

void HOST_Transport_Reset(void);
void HOST_Transport_PushByte(uint8_t source, uint8_t byte);

#endif /* RFID_CONTROLLER_HOST_TRANSPORT_PORT_H_ */
