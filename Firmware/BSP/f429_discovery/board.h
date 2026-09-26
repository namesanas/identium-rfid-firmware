#ifndef RFID_CONTROLLER_BSP_F429_DISCOVERY_H
#define RFID_CONTROLLER_BSP_F429_DISCOVERY_H

#include "stm32f429xx_driver_gpio.h"
#include "stm32f429xx_driver_uart.h"
#include "stm32f429xx_driver_spi.h"


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
    SPI_Handle_t *pW5500Spi
);

#endif /* RFID_CONTROLLER_BSP_F429_DISCOVERY_H */
