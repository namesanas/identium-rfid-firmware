#ifndef RFID_CONTROLLER_HOST_TRANSPORT_H_
#define RFID_CONTROLLER_HOST_TRANSPORT_H_

#include <stdint.h>

#define HOST_TRANSPORT_USB       0U
#define HOST_TRANSPORT_ETHERNET  1U
#define HOST_TRANSPORT_NONE      2U

#define HOST_RX_QUEUE_SIZE       256U


void HOST_Transport_Init(void);

void HOST_Transport_PushByte(
    uint8_t source,
    uint8_t byte
);

uint8_t HOST_Transport_ReadByte(
    uint8_t *source,
    uint8_t *byte
);

uint8_t HOST_Transport_GetActive(void);

void HOST_Transport_SetTagTransport(
    uint8_t transport
);

uint8_t HOST_Transport_GetTagTransport(void);

void HOST_Transport_Release(void);

void HOST_Transport_SendString(
    uint8_t transport,
    const char *text
);

uint32_t HOST_Transport_GetRxOverflowCount(void);

#endif /* RFID_CONTROLLER_HOST_TRANSPORT_H_ */
