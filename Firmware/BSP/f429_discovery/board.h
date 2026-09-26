#ifndef RFID_CONTROLLER_BSP_F429_DISCOVERY_H
#define RFID_CONTROLLER_BSP_F429_DISCOVERY_H

#include "../../Drivers/gpio/gpio.h"
#include "../../Drivers/uart/uart.h"
#include "../../Drivers/spi/spi.h"

#include "impinj.h"
#include "sillion_application.h"

void Board_F429_GPIO_Init(void);
void Board_F429_RFID_UART_GPIO_Init(void);
void Board_F429_Host_UART_GPIO_Init(void);
void Board_F429_W5500_GPIO_Init(void);

void Board_F429_RFID_UART_Init(USART_Handle_t *pUSART);
void Board_F429_Host_UART_Init(USART_Handle_t *pUSART);
void Board_F429_W5500_SPI_Init(SPI_Handle_t *pSPI);

void Board_F429_Init(
    USART_Handle_t *pRFIDUart,
    USART_Handle_t *pHostUart,
    SPI_Handle_t *pW5500Spi,
    Silion_Handle_t *pSilion
);

#endif /* RFID_CONTROLLER_BSP_F429_DISCOVERY_H */
