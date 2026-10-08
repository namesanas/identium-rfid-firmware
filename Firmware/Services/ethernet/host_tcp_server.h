#ifndef RFID_CONTROLLER_HOST_TCP_SERVER_H
#define RFID_CONTROLLER_HOST_TCP_SERVER_H

#include <stdint.h>

/*
 * Legacy ASCII host commands over TCP (W5500 socket 3,
 * PRODUCT_ETHERNET_TCP_PORT): the same command set as the USB and
 * RS232/RS485 ports, for the desktop GUI. One client at a time; its
 * bytes enter the host router as HOST_TRANSPORT_ETHERNET.
 *
 * Independent of the active-mode connection (socket 0, reader as TCP
 * client) and of the vendor-tool port 8080 (socket 1).
 */

void Host_Tcp_Server_Init(void);

/* Call every main-loop pass. */
void Host_Tcp_Server_Task(void);

/* Queue output for the connected client. Returns the number of bytes
 * accepted (0 when no client is connected or the queue is full). */
uint16_t Host_Tcp_Server_Send(const uint8_t *data, uint16_t length);

uint8_t Host_Tcp_Server_IsConnected(void);

#endif /* RFID_CONTROLLER_HOST_TCP_SERVER_H */
