#ifndef RFID_CONTROLLER_SILLION_APPLICATION_PORT_H_
#define RFID_CONTROLLER_SILLION_APPLICATION_PORT_H_

#include <stdint.h>
#include "../../Protocols/silion/silion_protocol.h"
#include "../../Drivers/uart/uart.h"
//i dont have these folders i only have application bsp common config devices drivers protocols services
#define SILION_APP_RX_QUEUE_SIZE 4096U

extern volatile uint8_t txComplete;
extern volatile uint8_t rxComplete;
extern volatile uint8_t rxORE;
extern volatile uint8_t rxFE;
extern volatile uint8_t rxNE;
extern volatile uint8_t rxPE;
extern volatile uint32_t silionAsyncPacketCount;
extern volatile uint32_t silionAsyncBadFrameCount;

void SILION_Application_Port_Init(
    Silion_Handle_t *pSilionHandle,
    USART_Handle_t *pUSARTHandle
);

void SILION_Application_Port_UartEvent(
    USART_Handle_t *pUSARTHandle,
    uint8_t AppEvent,
    uint8_t receivedByte
);

void SILION_ProcessRxQueue(void);
void SILION_ClearUartFlags(void);
void SILION_ClearRxQueue(void);
void SILION_DelayMs(uint32_t delayMs);
int SILION_WaitForResponse(uint32_t timeoutMs);
int SILION_WaitForTxComplete(uint32_t timeoutMs);

/* Raw module UART access for the Reader Manager module bridge. */
void SILION_SetRawMode(uint8_t enable);
uint16_t SILION_ReadRawBytes(uint8_t *buffer, uint16_t bufferSize);

/* Host/application output hooks. */
void HOST_SendString(const char *text);
void HOST_SendTag(const SILION_Tag_t *tag);

#endif /* RFID_CONTROLLER_SILLION_APPLICATION_PORT_H_ */
