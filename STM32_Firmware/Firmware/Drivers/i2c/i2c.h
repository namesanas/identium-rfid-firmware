/*
 * i2c.h
 * Bare-metal I2C driver for the current RFID Controller firmware.
 * Supports the STM32F429 Discovery target and the future STM32F411 target.
 *
 * Notes:
 * - I2C pins must be configured separately as alternate-function open-drain
 *   with suitable external pull-ups. Pin routing is intentionally kept in BSP.
 * - Blocking master calls use a bounded poll-loop timeout; it is not a wall-
 *   clock timeout. Use the returned status from I2C_MasterWrite/Read.
 * - Slave byte handling is callback-driven in interrupt mode.
 */
#ifndef RFID_CONTROLLER_DRIVERS_I2C_I2C_H_
#define RFID_CONTROLLER_DRIVERS_I2C_I2C_H_

#include <stdint.h>
#include "../../Config/firmware_config.h"

#if (TARGET_BOARD == BOARD_F429_DISCOVERY)
#include "stm32f429xx.h"
typedef I2C_TypeDef I2C_RegDef_t;
#elif (TARGET_BOARD == BOARD_F411_CUSTOM)
#include "stm32f411xe.h"
typedef I2C_TypeDef I2C_RegDef_t;
#else
#error "Unsupported TARGET_BOARD for I2C driver"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t I2C_SCLSpeed;
    uint8_t  I2C_DeviceAddress; /* Own 7-bit address when acting as a slave. */
    uint8_t  I2C_ACKControl;
    uint16_t I2C_FMDutyCycle;
} I2C_Config_t;

typedef enum
{
    I2C_STATUS_OK = 0U,
    I2C_STATUS_BUSY,
    I2C_STATUS_TIMEOUT,
    I2C_STATUS_NACK,
    I2C_STATUS_BUS_ERROR,
    I2C_STATUS_ARBITRATION_LOST,
    I2C_STATUS_OVERRUN,
    I2C_STATUS_INVALID_ARGUMENT,
    I2C_STATUS_NOT_READY
} I2C_Status_t;

typedef struct
{
    I2C_RegDef_t *pI2Cx;
    I2C_Config_t  I2C_Config;

    uint8_t  *pTxBuffer;
    uint8_t  *pRxBuffer;
    uint32_t  TxLen;
    uint32_t  RxLen;
    volatile uint8_t TxRxState;
    uint8_t   DevAddr;       /* Unshifted 7-bit address. */
    uint32_t  RxSize;         /* Original receive length, used by ISR. */
    uint8_t   Sr;             /* I2C_ENABLE_SR leaves transfer open for restart. */
    volatile I2C_Status_t LastError;
} I2C_Handle_t;

#define I2C_SCL_SPEED_SM       100000UL
#define I2C_SCL_SPEED_FM2K     200000UL
#define I2C_SCL_SPEED_FM4K     400000UL
#define I2C_SCL_SPEED_FM_200K  I2C_SCL_SPEED_FM2K
#define I2C_SCL_SPEED_FM_400K  I2C_SCL_SPEED_FM4K

#define I2C_ACK_ENABLE         1U
#define I2C_ACK_DISABLE        0U
#define I2C_FM_DUTY_2          0U
#define I2C_FM_DUTY_16_9       1U

#define I2C_READY              0U
#define I2C_BUSY_IN_RX         1U
#define I2C_BUSY_IN_TX         2U

#define I2C_DISABLE_SR         0U
#define I2C_ENABLE_SR          1U

/* Register flag masks use CMSIS *_Pos symbols, matching this project. */
#define I2C_FLAG_TXE            (1UL << I2C_SR1_TXE_Pos)
#define I2C_FLAG_RXNE           (1UL << I2C_SR1_RXNE_Pos)
#define I2C_FLAG_SB             (1UL << I2C_SR1_SB_Pos)
#define I2C_FLAG_OVR            (1UL << I2C_SR1_OVR_Pos)
#define I2C_FLAG_AF             (1UL << I2C_SR1_AF_Pos)
#define I2C_FLAG_ARLO           (1UL << I2C_SR1_ARLO_Pos)
#define I2C_FLAG_BERR           (1UL << I2C_SR1_BERR_Pos)
#define I2C_FLAG_STOPF          (1UL << I2C_SR1_STOPF_Pos)
#define I2C_FLAG_ADD10          (1UL << I2C_SR1_ADD10_Pos)
#define I2C_FLAG_BTF            (1UL << I2C_SR1_BTF_Pos)
#define I2C_FLAG_ADDR           (1UL << I2C_SR1_ADDR_Pos)
#define I2C_FLAG_TIMEOUT        (1UL << I2C_SR1_TIMEOUT_Pos)

/* SR2 flags carry this marker so I2C_GetFlagStatus() reads the right
 * register (SR2 BUSY is the same bit position as SR1 ADDR). */
#define I2C_FLAG_SR2_MARKER     (1UL << 31)
#define I2C_FLAG_BUSY           (I2C_FLAG_SR2_MARKER | (1UL << I2C_SR2_BUSY_Pos))

#define I2C_EV_TX_CMPLT         0U
#define I2C_EV_RX_CMPLT         1U
#define I2C_EV_STOP             2U
#define I2C_ERROR_BERR          3U
#define I2C_ERROR_ARLO          4U
#define I2C_ERROR_AF            5U
#define I2C_ERROR_OVR           6U
#define I2C_ERROR_TIMEOUT       7U
#define I2C_EV_DATA_REQ         8U
#define I2C_EV_DATA_RCV         9U

#ifndef ENABLE
#define ENABLE                  1U
#endif
#ifndef DISABLE
#define DISABLE                 0U
#endif
#ifndef FLAG_SET
#define FLAG_SET                1U
#endif
#ifndef FLAG_RESET
#define FLAG_RESET              0U
#endif

/* Default finite polling budget. Override at build time if required. */
#ifndef I2C_WAIT_TIMEOUT_LOOPS
#define I2C_WAIT_TIMEOUT_LOOPS  1000000UL
#endif

void I2C_PeriClockControl(I2C_RegDef_t *pI2Cx, uint8_t EnorDi);
I2C_Status_t I2C_InitEx(I2C_Handle_t *pI2CHandle);
void I2C_Init(I2C_Handle_t *pI2CHandle); /* Legacy-compatible wrapper. */
void I2C_DeInit(I2C_RegDef_t *pI2Cx);

/* Status-returning blocking APIs recommended for new code. SlaveAddr is 7-bit. */
I2C_Status_t I2C_MasterWrite(I2C_Handle_t *pI2CHandle,
                             const uint8_t *pTxBuffer,
                             uint32_t len,
                             uint8_t SlaveAddr,
                             uint8_t Sr);
I2C_Status_t I2C_MasterRead(I2C_Handle_t *pI2CHandle,
                            uint8_t *pRxBuffer,
                            uint32_t len,
                            uint8_t SlaveAddr,
                            uint8_t Sr);

/* Original API retained so existing driver exercises/call sites remain valid. */
void I2C_MasterSendData(I2C_Handle_t *pI2CHandle, uint8_t *pTxbuffer,
                        uint32_t len, uint8_t SlaveAddr, uint8_t Sr);
void I2C_MasterReceiverData(I2C_Handle_t *pI2CHandle, uint8_t *pRxbuffer,
                           uint32_t len, uint8_t SlaveAddr, uint8_t Sr);
uint8_t I2C_MasterSendDataIT(I2C_Handle_t *pI2CHandle, uint8_t *pTxbuffer,
                             uint32_t len, uint8_t SlaveAddr, uint8_t Sr);
uint8_t I2C_MasterReceiverDataIT(I2C_Handle_t *pI2CHandle, uint8_t *pRxbuffer,
                                 uint32_t len, uint8_t SlaveAddr, uint8_t Sr);

void I2C_SlaveSendData(I2C_RegDef_t *pI2C, uint8_t data);
uint8_t I2C_SlaveReceiverData(I2C_RegDef_t *pI2C);
void I2C_CloseSendData(I2C_Handle_t *pI2CHandle);
void I2C_CloseReceiveData(I2C_Handle_t *pI2CHandle);
void I2C_EV_IRQHandling(I2C_Handle_t *pI2CHandle);
void I2C_ER_IRQHandling(I2C_Handle_t *pI2CHandle);
void I2C_IRQInterruptConfig(uint8_t IRQNumber, uint8_t EnorDi);
void I2C_IRQPriorityConfig(uint8_t IRQNumber, uint32_t IRQPriority);
void I2C_PeripheralControl(I2C_RegDef_t *pI2Cx, uint8_t EnorDi);
/* FlagName: an I2C_FLAG_* value, SR1 or SR2 (I2C_FLAG_BUSY). Reading SR2
 * right after SR1 clears ADDR, as on the hardware. */
uint8_t I2C_GetFlagStatus(I2C_RegDef_t *pI2Cx, uint32_t FlagName);
void I2C_ManageAcking(I2C_RegDef_t *pI2Cx, uint8_t EnorDi);
void I2C_GenerateStopCondition(I2C_RegDef_t *pI2Cx);
void I2C_SlaveEnableDisableCallBackEvents(I2C_RegDef_t *pI2Cx, uint8_t EnorDi);
void I2C_ApplicationEventCallback(I2C_Handle_t *pI2CHandle, uint8_t AppEv);

#ifdef __cplusplus
}
#endif
#endif /* RFID_CONTROLLER_DRIVERS_I2C_I2C_H_ */
