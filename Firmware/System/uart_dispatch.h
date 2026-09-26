#ifndef RFID_CONTROLLER_UART_DISPATCH_H_
#define RFID_CONTROLLER_UART_DISPATCH_H_

#include <stdint.h>
#include "../Drivers/uart/uart.h"

void USART_Dispatch_Init(
    USART_Handle_t *pRFIDUart,
    USART_Handle_t *pHostUart
);

void USART_ApplicationEventCallback(
    USART_Handle_t *pUSARTHandle,
    uint8_t AppEvent,
    uint8_t receivedByte
);

void USART1_IRQHandler(void);
void USART3_IRQHandler(void);

#endif /* RFID_CONTROLLER_UART_DISPATCH_H_ */
