/*
 * uart.h
 *
 *  Created on: 26-Sept-2026
 *      Author: Identium
 */

#ifndef UART_UART_H_
#define UART_UART_H_


#include <stdint.h>
#include "../../Config/firmware_config.h"

#if (TARGET_BOARD == BOARD_F429_DISCOVERY)
#include "stm32f429xx.h"
typedef USART_TypeDef USART_RegDef_t;
#elif (TARGET_BOARD == BOARD_F411_CUSTOM)
#include "stm32f411xe.h"
typedef USART_TypeDef USART_RegDef_t;
#else
#error "Unsupported TARGET_BOARD"
#endif

typedef struct
{
    uint8_t USART_Mode;
    uint32_t USART_Baud;
    uint8_t USART_NoOfStopBits;
    uint8_t USART_WordLength;
    uint8_t USART_ParityControl;
    uint8_t USART_HWFlowControl;
    uint8_t USART_OverSampling;
} USART_Config_t;

typedef struct
{
    USART_RegDef_t *pUSARTx;
    USART_Config_t USART_Config;
    uint8_t *pTxBuffer;
    uint32_t TxLen;
    uint8_t TxBusyState;
    uint8_t *pRxBuffer;
    uint32_t RxLen;
    uint8_t RxBusyState;
} USART_Handle_t;

#define USART_MODE_ONLY_TX            0U
#define USART_MODE_ONLY_RX            1U
#define USART_MODE_TXRX               2U

#define USART_WORDLEN_8BITS           0U
#define USART_WORDLEN_9BITS           1U

#define USART_PARITY_DISABLE          0U
#define USART_PARITY_EN_EVEN          1U
#define USART_PARITY_EN_ODD           2U

#define USART_STOPBITS_1              0U
#define USART_STOPBITS_0_5            1U
#define USART_STOPBITS_2              2U
#define USART_STOPBITS_1_5            3U

#define USART_HW_FLOW_CTRL_NONE       0U
#define USART_HW_FLOW_CTRL_CTS        1U
#define USART_HW_FLOW_CTRL_RTS        2U
#define USART_HW_FLOW_CTRL_CTSRTS     3U

#define USART_OVERSAMPLING_16         0U
#define USART_OVERSAMPLING_8          1U

#define USART_STD_BAUD_1200           1200U
#define USART_STD_BAUD_2400           2400U
#define USART_STD_BAUD_4800           4800U
#define USART_STD_BAUD_9600           9600U
#define USART_STD_BAUD_19200          19200U
#define USART_STD_BAUD_38400          38400U
#define USART_STD_BAUD_57600          57600U
#define USART_STD_BAUD_115200         115200U
#define USART_STD_BAUD_230400         230400U

#define USART_BUSY_IN_TX              0U
#define USART_BUSY_IN_RX              0U
#define USART_READY                   1U

#define USART_FLAG_PE                 0U
#define USART_FLAG_FE                 1U
#define USART_FLAG_NE                 2U
#define USART_FLAG_ORE                3U
#define USART_FLAG_IDLE               4U
#define USART_FLAG_RXNE               5U
#define USART_FLAG_TC                 6U
#define USART_FLAG_TXE                7U

#define UART_CR1_RE_BIT               USART_CR1_RE_Pos
#define UART_CR1_TE_BIT               USART_CR1_TE_Pos
#define UART_CR1_IDLEIE_BIT           USART_CR1_IDLEIE_Pos
#define UART_CR1_RXNEIE_BIT           USART_CR1_RXNEIE_Pos
#define UART_CR1_TCIE_BIT             USART_CR1_TCIE_Pos
#define UART_CR1_TXEIE_BIT            USART_CR1_TXEIE_Pos
#define UART_CR1_PCE_BIT              USART_CR1_PCE_Pos
#define UART_CR1_PS_BIT               USART_CR1_PS_Pos
#define UART_CR1_M_BIT                USART_CR1_M_Pos
#define UART_CR1_OVER8_BIT            USART_CR1_OVER8_Pos
#define UART_CR1_UE_BIT              USART_CR1_UE_Pos
#define UART_CR2_STOP_BIT             USART_CR2_STOP_Pos
#define UART_CR3_RTSE_BIT             USART_CR3_RTSE_Pos
#define UART_CR3_CTSE_BIT             USART_CR3_CTSE_Pos

#define USART_IRQ_RXNE                UART_CR1_RXNEIE_BIT
#define USART_IRQ_TC                  UART_CR1_TCIE_BIT
#define USART_IRQ_TXE                 UART_CR1_TXEIE_BIT

#define USART_EVENT_TX_CMPLT          1U
#define USART_EVENT_RX_CMPLT          2U
#define USART_EVENT_RX_BYTE           3U
#define USART_EVENT_ORE               4U
#define USART_EVENT_FE                5U
#define USART_EVENT_NE                6U
#define USART_EVENT_PE                7U

#ifndef ENABLE
#define ENABLE                        1U
#endif
#ifndef DISABLE
#define DISABLE                       0U
#endif
#ifndef FLAG_SET
#define FLAG_SET                      1U
#endif
#ifndef FLAG_RESET
#define FLAG_RESET                    0U
#endif

#define NO_PR_BITS_IMPLEMENTED        4U
#define NVIC_PR_BASE_ADDR             ((volatile uint32_t *)0xE000E400UL)

void USART_PeriClockControl(USART_RegDef_t *pUSARTx, uint8_t EnorDi);
void USART_Init(USART_Handle_t *pUSARTHandle);
void USART_DeInit(USART_RegDef_t *pUSARTx);
uint8_t USART_GetFlagStatus(USART_RegDef_t *pUSARTx, uint8_t StatusFlagName);
void USART_IRQInterruptConfig(uint8_t IRQNumber, uint8_t EnorDi);
void USART_IRQPriorityConfig(uint8_t IRQNumber, uint32_t IRQPriority);
void USART_SendData(USART_Handle_t *pUSARTHandle, uint8_t *pTxBuffer, uint32_t Len);
void USART_ReceiveData(USART_Handle_t *pUSARTHandle, uint8_t *pRxBuffer, uint32_t Len);
uint8_t USART_SendDataIT(USART_Handle_t *pUSARTHandle, uint8_t *pTxBuffer, uint32_t Len);
uint8_t USART_ReceiveDataIT(USART_Handle_t *pUSARTHandle, uint8_t *pRxBuffer, uint32_t Len);
uint8_t USART_ReceiveByteIT(USART_Handle_t *pUSARTHandle);
void USART_IRQHandling(USART_Handle_t *pUSARTHandle);
void USART_SetBaudRate(USART_RegDef_t *pUSARTx, uint32_t BaudRate);
void USART_ApplicationEventCallback(USART_Handle_t *pUSARTHandle, uint8_t AppEvent, uint8_t receivedByte);




#endif /* UART_UART_H_ */
