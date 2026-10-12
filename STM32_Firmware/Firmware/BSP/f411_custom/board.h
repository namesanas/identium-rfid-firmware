#ifndef RFID_CONTROLLER_BSP_F411_CUSTOM_H
#define RFID_CONTROLLER_BSP_F411_CUSTOM_H

#include "board_config.h"

#include "../../Drivers/gpio/gpio.h"
#include "../../Drivers/uart/uart.h"
#include "../../Drivers/spi/spi.h"

void Board_F411_GPIO_Init(void);
void Board_F411_RFID_UART_GPIO_Init(void);
void Board_F411_Host_UART_GPIO_Init(void);
void Board_F411_W5500_GPIO_Init(void);

void Board_F411_RFID_UART_Init(USART_Handle_t *pUSART);
void Board_F411_Host_UART_Init(USART_Handle_t *pUSART);
void Board_F411_W5500_SPI_Init(SPI_Handle_t *pSPI);

void Board_F411_Init(
    USART_Handle_t *pRFIDUart,
    USART_Handle_t *pHostUart,
    SPI_Handle_t *pW5500Spi
);

#endif /* RFID_CONTROLLER_BSP_F411_CUSTOM_H */
