#ifndef RFID_CONTROLLER_DRIVER_SPI_H
#define RFID_CONTROLLER_DRIVER_SPI_H

#include <stdint.h>
#include "../../Config/firmware_config.h"

#if (TARGET_BOARD == BOARD_F429_DISCOVERY)
#include "stm32f429xx.h"
typedef SPI_TypeDef SPI_RegDef_t;
#elif (TARGET_BOARD == BOARD_F411_CUSTOM)
#include "stm32f411xe.h"
typedef SPI_TypeDef SPI_RegDef_t;
#else
#error "Unsupported TARGET_BOARD"
#endif

typedef struct
{
    uint8_t SPI_DeviceMode;
    uint8_t SPI_BusConfig;
    uint8_t SPI_SclkSpeed;
    uint8_t SPI_DFF;
    uint8_t SPI_CPOL;
    uint8_t SPI_CPHA;
    uint8_t SPI_SSM;
} SPI_Config_t;

typedef struct
{
    SPI_RegDef_t *pSPIx;
    SPI_Config_t SPIConfig;
    uint8_t *pTxBuffer;
    uint8_t *pRxBuffer;
    uint32_t TxLen;
    uint32_t RxLen;
    uint8_t TxState;
    uint8_t RxState;
} SPI_Handle_t;

#define SPI_DEVICE_MODE_SLAVE              0U
#define SPI_DEVICE_MODE_MASTER             1U
#define SPI_BUS_CONFIG_FD                  0U
#define SPI_BUS_CONFIG_HD                  1U
#define SPI_BUS_CONFIG_SIMPLEX_RXONLY     2U
#define SPI_SCLK_SPEED_DIV2                0U
#define SPI_SCLK_SPEED_DIV4                1U
#define SPI_SCLK_SPEED_DIV8                2U
#define SPI_SCLK_SPEED_DIV16               3U
#define SPI_SCLK_SPEED_DIV32               4U
#define SPI_SCLK_SPEED_DIV64               5U
#define SPI_SCLK_SPEED_DIV128              6U
#define SPI_SCLK_SPEED_DIV256              7U
#define SPI_DFF_8BITS                      0U
#define SPI_DFF_16BITS                     1U
#define SPI_CPOL_LOW                       0U
#define SPI_CPOL_HIGH                      1U
#define SPI_CPHA_LOW                       0U
#define SPI_CPHA_HIGH                      1U
#define SPI_SSM_DI                         0U
#define SPI_SSM_EN                         1U
#define SPI_BUSY_IN_TX                     0U
#define SPI_BUSY_IN_RX                     0U
#define SPI_READY                          1U
#define SPI_TXE_FLAG                       (1UL << 1U)
#define SPI_RXNE_FLAG                      (1UL << 0U)

#define SPI_CR1_CPHA                       0U
#define SPI_CR1_CPOL                       1U
#define SPI_CR1_MSTR                       2U
#define SPI_CR1_BR                         3U
#define SPI_CR1_SPE                        6U
#define SPI_CR1_SSI                        8U
#define SPI_CR1_SSM                        9U
#define SPI_CR1_RXONLY                     10U
#define SPI_CR1_DFF                        11U
#define SPI_CR1_BIDIMODE                   15U
#define SPI_CR2_ERRIE                      5U
#define SPI_CR2_RXNEIE                     6U
#define SPI_CR2_TXEIE                      7U
#define SPI_CR2_SSOE                       2U
#define SPI_SR_TXE                         1U
#define SPI_SR_RXNE                        0U
#define SPI_SR_OVR                         6U
#define SPI_SR_BSY                         7U

#define SPI_EVENT_TX_CMPLT                 1U
#define SPI_EVENT_RX_CMPLT                 2U
#define SPI_EVENT_OVR_ERR                 3U

#ifndef ENABLE
#define ENABLE                             1U
#endif
#ifndef DISABLE
#define DISABLE                            0U
#endif
#ifndef FLAG_SET
#define FLAG_SET                           1U
#endif
#ifndef FLAG_RESET
#define FLAG_RESET                         0U
#endif

void SPI_PeriClockControl(SPI_RegDef_t *pSPIx, uint8_t EnorDi);
void SPI_Init(SPI_Handle_t *pSPIHandle);
void SPI_DeInit(SPI_RegDef_t *pSPIx);
uint8_t SPI_GetFlagStatus(SPI_RegDef_t *pSPIx, uint32_t FlagName);
void SPI_SendData(SPI_RegDef_t *pSPIx, uint8_t *pTxBuffer, uint32_t Len);
void SPI_ReceiveData(SPI_RegDef_t *pSPIx, uint8_t *pRxBuffer, uint32_t Len);
uint8_t SPI_SendDataIT(SPI_Handle_t *pSPIHandle, uint8_t *pTxBuffer, uint32_t Len);
uint8_t SPI_ReceiveDataIT(SPI_Handle_t *pSPIHandle, uint8_t *pRxBuffer, uint32_t Len);
void SPI_IRQConfig(uint8_t IRQNumber, uint8_t EnorDi);
void SPI_IRQPriorityConfig(uint8_t IRQNumber, uint32_t IRQPriority);
void SPI_IRQHandling(SPI_Handle_t *pHandle);
void SPI_CloseTransmission(SPI_Handle_t *pSPIHandle);
void SPI_CloseReception(SPI_Handle_t *pSPIHandle);
void SPI_ClearOVRFlag(SPI_RegDef_t *pSPIx);
void SPI_PeripheralControl(SPI_RegDef_t *pSPIx, uint8_t EnorDi);
void SPI_SSIConfig(SPI_RegDef_t *pSPIx, uint8_t EnorDi);
void SPI_SSOEConfig(SPI_RegDef_t *pSPIx, uint8_t EnorDi);
void SPI_ApplicationEventCallback(SPI_Handle_t *pSPIHandle, uint8_t AppEv);

#endif /* RFID_CONTROLLER_DRIVER_SPI_H */
