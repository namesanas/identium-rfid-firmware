#ifndef RFID_CONTROLLER_ETHERNET_SERVICE_H
#define RFID_CONTROLLER_ETHERNET_SERVICE_H

#include <stdint.h>

/*
 * Callback invoked once for every byte received from
 * the Ethernet TCP socket.
 */
typedef void (*Ethernet_RxByteCallback_t)(
    uint8_t receivedByte
);

/*
 * Initialize Ethernet service and start the TCP server.
 */
uint8_t Ethernet_Service_Init(
    Ethernet_RxByteCallback_t rxCallback
);

/*
 * Service the W5500 socket.
 *
 * This function is non-blocking at the service level:
 * it checks the socket state and processes available data.
 */
void Ethernet_Service_Task(void);

/*
 * Send data through the active TCP connection.
 *
 * Returns number of bytes accepted by the W5500.
 */
uint16_t Ethernet_Service_Send(
    const uint8_t *buffer,
    uint16_t length
);

/*
 * Returns 1 when Socket 0 is connected.
 */

/*
 * Send one complete application message without allowing the W5500 to
 * truncate it when TX space is temporarily insufficient.
 */
uint16_t Ethernet_Service_SendComplete(
    const uint8_t *buffer,
    uint16_t length
);

uint8_t Ethernet_Service_IsConnected(void);

#endif /* RFID_CONTROLLER_ETHERNET_SERVICE_H */
