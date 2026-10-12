#ifndef RFID_CONTROLLER_CONFIGURATION_PORT_H_
#define RFID_CONTROLLER_CONFIGURATION_PORT_H_

#include <stdint.h>

/* Vendor-tool TCP port (Reader Manager / ReaderSense). */
#define CONFIGURATION_PORT_TCP_LISTEN_PORT 8080U

void Configuration_Port_Init(void);
void Configuration_Port_Task(void);

/* EE configuration request received on a serial link (COM route):
 * link = HOST_TRANSPORT_USB or HOST_TRANSPORT_SERIAL. The reply is sent
 * back on the same link. frame = complete request; a trailing serial
 * CRC, if present, is ignored. */
void Configuration_Port_HandleLinkRequest(
    uint8_t link,
    const uint8_t *frame,
    uint16_t length
);

#endif /* RFID_CONTROLLER_CONFIGURATION_PORT_H_ */
