/*
 * serial_port.h
 *
 * Host serial port (RS232 / RS485) on the board's host USART.
 *
 * The port is a byte stream like the native USB CDC port: received bytes
 * enter the host router as HOST_TRANSPORT_SERIAL, and replies are queued
 * here and sent by interrupt. In RS485 mode the transceiver driver is
 * enabled only while bytes are on the wire and released on the USART
 * transmission-complete interrupt, after the last stop bit.
 */

#ifndef RFID_CONTROLLER_SERIAL_PORT_H_
#define RFID_CONTROLLER_SERIAL_PORT_H_

#include <stdint.h>

#include "../../Drivers/uart/uart.h"

#define SERIAL_PORT_MODE_RS232      0U
#define SERIAL_PORT_MODE_RS485      1U

#define SERIAL_PORT_TX_BUFFER_SIZE  2048U

void Serial_Port_Init(USART_Handle_t *pUSARTHandle, uint8_t mode);

void Serial_Port_SetMode(uint8_t mode);
uint8_t Serial_Port_GetMode(void);

/* Queues bytes for transmission. Returns the number accepted; bytes that
 * do not fit are dropped and counted. */
uint16_t Serial_Port_Send(const uint8_t *data, uint16_t length);
uint16_t Serial_Port_GetFreeSpace(void);

uint32_t Serial_Port_GetTxOverflowCount(void);
uint32_t Serial_Port_GetRxOverrunCount(void);

void Serial_Port_UartEvent(
    USART_Handle_t *pUSARTHandle,
    uint8_t AppEvent,
    uint8_t receivedByte
);

#endif /* RFID_CONTROLLER_SERIAL_PORT_H_ */
