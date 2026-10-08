#ifndef RFID_CONTROLLER_HOST_TRANSPORT_H_
#define RFID_CONTROLLER_HOST_TRANSPORT_H_

#include <stdint.h>

#define HOST_TRANSPORT_USB       0U
#define HOST_TRANSPORT_ETHERNET  1U
#define HOST_TRANSPORT_NONE      2U
#define HOST_TRANSPORT_SERIAL    3U   /* RS232 / RS485 host port */

#define HOST_RX_QUEUE_SIZE       1024U


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

/*
 * Binary output on a byte-stream link (HOST_TRANSPORT_USB or
 * HOST_TRANSPORT_SERIAL), used by the vendor-tool paths that reply on
 * the link a request arrived on. Returns the number of bytes queued.
 */
uint16_t HOST_Transport_LinkSend(
    uint8_t link,
    const uint8_t *data,
    uint16_t length
);
uint16_t HOST_Transport_LinkGetFreeSpace(uint8_t link);

#endif /* RFID_CONTROLLER_HOST_TRANSPORT_H_ */
