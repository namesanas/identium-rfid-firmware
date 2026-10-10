/*
 * i2c.c
 * Bare-metal I2C driver adapted to the RFID Controller driver's CMSIS/RCC style.
 */
#include "i2c.h"
#include "../rcc/rcc.h"
#include "../../Common/critical_section.h"

#if defined(__GNUC__)
#define I2C_WEAK __attribute__((weak))
#else
#define I2C_WEAK
#endif

#define I2C_CR1_PE_MASK       (1UL << I2C_CR1_PE_Pos)
#define I2C_CR1_ACK_MASK      (1UL << I2C_CR1_ACK_Pos)
#define I2C_CR1_START_MASK    (1UL << I2C_CR1_START_Pos)
#define I2C_CR1_STOP_MASK     (1UL << I2C_CR1_STOP_Pos)
#define I2C_CR1_POS_MASK      (1UL << I2C_CR1_POS_Pos)
#define I2C_CR2_ITEVTEN_MASK  (1UL << I2C_CR2_ITEVTEN_Pos)
#define I2C_CR2_ITBUFEN_MASK  (1UL << I2C_CR2_ITBUFEN_Pos)
#define I2C_CR2_ITERREN_MASK  (1UL << I2C_CR2_ITERREN_Pos)
#define I2C_SR2_MSL_MASK      (1UL << I2C_SR2_MSL_Pos)
#define I2C_SR2_TRA_MASK      (1UL << I2C_SR2_TRA_Pos)
#define I2C_SR1_ERROR_MASK    (I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_AF | \
                               I2C_FLAG_OVR | I2C_FLAG_TIMEOUT)

static void I2C_GenerateStartCondition(I2C_RegDef_t *pI2Cx);
static void I2C_ExecuteAddressPhaseWrite(I2C_RegDef_t *pI2Cx, uint8_t SlaveAddr);
static void I2C_ExecuteAddressPhaseRead(I2C_RegDef_t *pI2Cx, uint8_t SlaveAddr);
static void I2C_ClearADDRFlag(I2C_RegDef_t *pI2Cx);
static void I2C_ClearSTOPFlag(I2C_RegDef_t *pI2Cx);
static void I2C_ClearErrorFlags(I2C_RegDef_t *pI2Cx, uint32_t flags);
static I2C_Status_t I2C_WaitForSR1Flag(I2C_RegDef_t *pI2Cx,
                                       uint32_t flag,
                                       uint8_t expectedSet);
static I2C_Status_t I2C_WaitForBusFree(I2C_RegDef_t *pI2Cx);
static I2C_Status_t I2C_StatusFromErrorFlags(uint32_t sr1);
static void I2C_AbortMasterTransfer(I2C_Handle_t *pI2CHandle,
                                    I2C_Status_t status);
static void I2C_CompleteITTransfer(I2C_Handle_t *pI2CHandle,
                                   uint8_t isReceive);

static void I2C_GenerateStartCondition(I2C_RegDef_t *pI2Cx)
{
    pI2Cx->CR1 |= I2C_CR1_START_MASK;
}

static void I2C_ExecuteAddressPhaseWrite(I2C_RegDef_t *pI2Cx, uint8_t SlaveAddr)
{
    pI2Cx->DR = (uint32_t)((SlaveAddr & 0x7FU) << 1U); /* R/W = 0 */
}

static void I2C_ExecuteAddressPhaseRead(I2C_RegDef_t *pI2Cx, uint8_t SlaveAddr)
{
    pI2Cx->DR = (uint32_t)(((SlaveAddr & 0x7FU) << 1U) | 1U); /* R/W = 1 */
}

static void I2C_ClearADDRFlag(I2C_RegDef_t *pI2Cx)
{
    volatile uint32_t dummy;
    dummy = pI2Cx->SR1;
    dummy = pI2Cx->SR2;
    (void)dummy;
}

static void I2C_ClearSTOPFlag(I2C_RegDef_t *pI2Cx)
{
    volatile uint32_t dummy = pI2Cx->SR1;
    (void)dummy;
    pI2Cx->CR1 |= I2C_CR1_PE_MASK;
}

static void I2C_ClearErrorFlags(I2C_RegDef_t *pI2Cx, uint32_t flags)
{
    /* STM32F4 I2C error flags are cleared by writing 0 to the selected bit. */
    pI2Cx->SR1 = (uint16_t)(~flags);
}

static I2C_Status_t I2C_StatusFromErrorFlags(uint32_t sr1)
{
    if ((sr1 & I2C_FLAG_ARLO) != 0U)     return I2C_STATUS_ARBITRATION_LOST;
    if ((sr1 & I2C_FLAG_BERR) != 0U)     return I2C_STATUS_BUS_ERROR;
    if ((sr1 & I2C_FLAG_AF) != 0U)       return I2C_STATUS_NACK;
    if ((sr1 & I2C_FLAG_OVR) != 0U)        return I2C_STATUS_OVERRUN;
    if ((sr1 & I2C_FLAG_TIMEOUT) != 0U)    return I2C_STATUS_TIMEOUT;
    return I2C_STATUS_OK;
}

static I2C_Status_t I2C_WaitForSR1Flag(I2C_RegDef_t *pI2Cx,
                                       uint32_t flag,
                                       uint8_t expectedSet)
{
    volatile uint32_t remaining = I2C_WAIT_TIMEOUT_LOOPS;

    while (remaining-- > 0U)
    {
        uint32_t sr1 = pI2Cx->SR1;
        I2C_Status_t error = I2C_StatusFromErrorFlags(sr1);

        if (error != I2C_STATUS_OK)
            return error;

        if (expectedSet != 0U)
        {
            if ((sr1 & flag) != 0U) return I2C_STATUS_OK;
        }
        else
        {
            if ((sr1 & flag) == 0U) return I2C_STATUS_OK;
        }
    }

    return I2C_STATUS_TIMEOUT;
}

static I2C_Status_t I2C_WaitForBusFree(I2C_RegDef_t *pI2Cx)
{
    volatile uint32_t remaining = I2C_WAIT_TIMEOUT_LOOPS;

    while (remaining-- > 0U)
    {
        /* BUSY may remain set during a repeated START owned by this controller. */
        if ((pI2Cx->SR2 & (I2C_FLAG_BUSY & ~I2C_FLAG_SR2_MARKER)) == 0U)
            return I2C_STATUS_OK;
    }
    return I2C_STATUS_TIMEOUT;
}

void I2C_PeriClockControl(I2C_RegDef_t *pI2Cx, uint8_t EnorDi)
{
    uint32_t mask = 0U;

    if (pI2Cx == I2C1)
        mask = (1UL << RCC_APB1ENR_I2C1EN_Pos);
    else if (pI2Cx == I2C2)
        mask = (1UL << RCC_APB1ENR_I2C2EN_Pos);
#if defined(I2C3) && defined(RCC_APB1ENR_I2C3EN_Pos)
    else if (pI2Cx == I2C3)
        mask = (1UL << RCC_APB1ENR_I2C3EN_Pos);
#endif
    else
        return;

    if (EnorDi == ENABLE)
        RCC->APB1ENR |= mask;
    else
        RCC->APB1ENR &= ~mask;

    (void)RCC->APB1ENR; /* Complete the peripheral clock write. */
}

I2C_Status_t I2C_InitEx(I2C_Handle_t *pI2CHandle)
{
    uint32_t pclk1;
    uint32_t freqMHz;
    uint32_t ccr;
    uint32_t trise;
    uint32_t cr1;

    if ((pI2CHandle == 0) || (pI2CHandle->pI2Cx == 0))
        return I2C_STATUS_INVALID_ARGUMENT;

    if ((pI2CHandle->I2C_Config.I2C_DeviceAddress > 0x7FU) ||
        ((pI2CHandle->I2C_Config.I2C_ACKControl != I2C_ACK_ENABLE) &&
         (pI2CHandle->I2C_Config.I2C_ACKControl != I2C_ACK_DISABLE)) ||
        ((pI2CHandle->I2C_Config.I2C_FMDutyCycle != I2C_FM_DUTY_2) &&
         (pI2CHandle->I2C_Config.I2C_FMDutyCycle != I2C_FM_DUTY_16_9)) ||
        (pI2CHandle->I2C_Config.I2C_SCLSpeed == 0U) ||
        (pI2CHandle->I2C_Config.I2C_SCLSpeed > I2C_SCL_SPEED_FM4K))
    {
        pI2CHandle->LastError = I2C_STATUS_INVALID_ARGUMENT;
        return I2C_STATUS_INVALID_ARGUMENT;
    }

    pclk1 = RCC_GetPCLK1Value();
    freqMHz = pclk1 / 1000000UL;
    if ((freqMHz < 2U) || (freqMHz > 50U))
    {
        pI2CHandle->LastError = I2C_STATUS_INVALID_ARGUMENT;
        return I2C_STATUS_INVALID_ARGUMENT;
    }

    I2C_PeriClockControl(pI2CHandle->pI2Cx, ENABLE);
    I2C_PeripheralControl(pI2CHandle->pI2Cx, DISABLE);

    /* Reset the control registers before setting the requested mode. */
    pI2CHandle->pI2Cx->CR1 = 0U;
    pI2CHandle->pI2Cx->CR2 = freqMHz & 0x3FU;

    /* 7-bit own address; bit 14 is required to be kept at 1 on STM32F4. */
    pI2CHandle->pI2Cx->OAR1 = ((uint32_t)pI2CHandle->I2C_Config.I2C_DeviceAddress << 1U) |
                              (1UL << 14U);
    pI2CHandle->pI2Cx->OAR2 = 0U;

    if (pI2CHandle->I2C_Config.I2C_SCLSpeed <= I2C_SCL_SPEED_SM)
    {
        {
            uint32_t denominator = 2UL * pI2CHandle->I2C_Config.I2C_SCLSpeed;
            ccr = (pclk1 + denominator - 1UL) / denominator;
        }
        if (ccr < 4U) ccr = 4U;
        if (ccr > 0x0FFFU)
        {
            pI2CHandle->LastError = I2C_STATUS_INVALID_ARGUMENT;
            return I2C_STATUS_INVALID_ARGUMENT;
        }
        trise = freqMHz + 1U;
        pI2CHandle->pI2Cx->CCR = ccr & 0x0FFFU;
    }
    else
    {
        if (pI2CHandle->I2C_Config.I2C_FMDutyCycle == I2C_FM_DUTY_16_9)
        {
            uint32_t denominator = 25UL * pI2CHandle->I2C_Config.I2C_SCLSpeed;
            ccr = (pclk1 + denominator - 1UL) / denominator;
        }
        else
        {
            uint32_t denominator = 3UL * pI2CHandle->I2C_Config.I2C_SCLSpeed;
            ccr = (pclk1 + denominator - 1UL) / denominator;
        }

        if (ccr == 0U) ccr = 1U;
        if (ccr > 0x0FFFU)
        {
            pI2CHandle->LastError = I2C_STATUS_INVALID_ARGUMENT;
            return I2C_STATUS_INVALID_ARGUMENT;
        }
        pI2CHandle->pI2Cx->CCR = (1UL << I2C_CCR_FS_Pos) |
            ((pI2CHandle->I2C_Config.I2C_FMDutyCycle == I2C_FM_DUTY_16_9) ?
             (1UL << I2C_CCR_DUTY_Pos) : 0U) | (ccr & 0x0FFFU);
        trise = ((freqMHz * 300U) / 1000U) + 1U;
    }

    pI2CHandle->pI2Cx->TRISE = trise & 0x3FU;
    cr1 = (pI2CHandle->I2C_Config.I2C_ACKControl == I2C_ACK_ENABLE) ?
          I2C_CR1_ACK_MASK : 0U;
    pI2CHandle->pI2Cx->CR1 = cr1 | I2C_CR1_PE_MASK;

    pI2CHandle->pTxBuffer = 0;
    pI2CHandle->pRxBuffer = 0;
    pI2CHandle->TxLen = 0U;
    pI2CHandle->RxLen = 0U;
    pI2CHandle->RxSize = 0U;
    pI2CHandle->TxRxState = I2C_READY;
    pI2CHandle->LastError = I2C_STATUS_OK;
    return I2C_STATUS_OK;
}

void I2C_Init(I2C_Handle_t *pI2CHandle)
{
    (void)I2C_InitEx(pI2CHandle);
}

void I2C_DeInit(I2C_RegDef_t *pI2Cx)
{
    uint32_t mask = 0U;

    if (pI2Cx == I2C1)
        mask = (1UL << RCC_APB1RSTR_I2C1RST_Pos);
    else if (pI2Cx == I2C2)
        mask = (1UL << RCC_APB1RSTR_I2C2RST_Pos);
#if defined(I2C3) && defined(RCC_APB1RSTR_I2C3RST_Pos)
    else if (pI2Cx == I2C3)
        mask = (1UL << RCC_APB1RSTR_I2C3RST_Pos);
#endif
    else
        return;

    RCC->APB1RSTR |= mask;
    RCC->APB1RSTR &= ~mask;
}

uint8_t I2C_GetFlagStatus(I2C_RegDef_t *pI2Cx, uint32_t FlagName)
{
    uint32_t reg;

    if (pI2Cx == 0) return FLAG_RESET;

    reg = ((FlagName & I2C_FLAG_SR2_MARKER) != 0U) ? pI2Cx->SR2 : pI2Cx->SR1;
    return ((reg & FlagName & ~I2C_FLAG_SR2_MARKER) != 0U) ? FLAG_SET : FLAG_RESET;
}

void I2C_ManageAcking(I2C_RegDef_t *pI2Cx, uint8_t EnorDi)
{
    if (pI2Cx == 0) return;
    if (EnorDi == I2C_ACK_ENABLE) pI2Cx->CR1 |= I2C_CR1_ACK_MASK;
    else                          pI2Cx->CR1 &= ~I2C_CR1_ACK_MASK;
}

void I2C_PeripheralControl(I2C_RegDef_t *pI2Cx, uint8_t EnorDi)
{
    if (pI2Cx == 0) return;
    if (EnorDi == ENABLE) pI2Cx->CR1 |= I2C_CR1_PE_MASK;
    else                  pI2Cx->CR1 &= ~I2C_CR1_PE_MASK;
}

void I2C_GenerateStopCondition(I2C_RegDef_t *pI2Cx)
{
    if (pI2Cx != 0) pI2Cx->CR1 |= I2C_CR1_STOP_MASK;
}

void I2C_SlaveEnableDisableCallBackEvents(I2C_RegDef_t *pI2Cx, uint8_t EnorDi)
{
    if (pI2Cx == 0) return;
    if (EnorDi == ENABLE)
        pI2Cx->CR2 |= I2C_CR2_ITEVTEN_MASK | I2C_CR2_ITBUFEN_MASK | I2C_CR2_ITERREN_MASK;
    else
        pI2Cx->CR2 &= ~(I2C_CR2_ITEVTEN_MASK | I2C_CR2_ITBUFEN_MASK | I2C_CR2_ITERREN_MASK);
}

static void I2C_AbortMasterTransfer(I2C_Handle_t *pI2CHandle, I2C_Status_t status)
{
    I2C_RegDef_t *pI2Cx = pI2CHandle->pI2Cx;
    if ((pI2Cx->SR2 & I2C_SR2_MSL_MASK) != 0U)
        I2C_GenerateStopCondition(pI2Cx);

    I2C_ClearErrorFlags(pI2Cx, I2C_SR1_ERROR_MASK);
    pI2Cx->CR1 &= ~I2C_CR1_POS_MASK;
    if (pI2CHandle->I2C_Config.I2C_ACKControl == I2C_ACK_ENABLE)
        pI2Cx->CR1 |= I2C_CR1_ACK_MASK;
    pI2CHandle->LastError = status;
}

static I2C_Status_t I2C_MasterBegin(I2C_Handle_t *pI2CHandle, uint8_t SlaveAddr,
                                    uint8_t readDirection)
{
    I2C_RegDef_t *pI2Cx = pI2CHandle->pI2Cx;
    I2C_Status_t status;

    if (pI2CHandle->pI2Cx == 0 || SlaveAddr > 0x7FU)
        return I2C_STATUS_INVALID_ARGUMENT;
    if ((pI2Cx->CR1 & I2C_CR1_PE_MASK) == 0U)
        return I2C_STATUS_NOT_READY;

    /* If this controller does not already own a repeated-start sequence, wait
       for the physical bus to become free before starting a new transaction. */
    if ((pI2Cx->SR2 & I2C_SR2_MSL_MASK) == 0U)
    {
        status = I2C_WaitForBusFree(pI2Cx);
        if (status != I2C_STATUS_OK) return status;
    }

    I2C_ClearErrorFlags(pI2Cx, I2C_SR1_ERROR_MASK);
    I2C_GenerateStartCondition(pI2Cx);
    status = I2C_WaitForSR1Flag(pI2Cx, I2C_FLAG_SB, 1U);
    if (status != I2C_STATUS_OK) return status;

    if (readDirection != 0U) I2C_ExecuteAddressPhaseRead(pI2Cx, SlaveAddr);
    else                    I2C_ExecuteAddressPhaseWrite(pI2Cx, SlaveAddr);

    status = I2C_WaitForSR1Flag(pI2Cx, I2C_FLAG_ADDR, 1U);
    if (status != I2C_STATUS_OK) return status;

    return I2C_STATUS_OK;
}

I2C_Status_t I2C_MasterWrite(I2C_Handle_t *pI2CHandle,
                             const uint8_t *pTxBuffer,
                             uint32_t len,
                             uint8_t SlaveAddr,
                             uint8_t Sr)
{
    I2C_Status_t status;
    I2C_RegDef_t *pI2Cx;

    if (pI2CHandle == 0) return I2C_STATUS_INVALID_ARGUMENT;
    if ((pI2CHandle->pI2Cx == 0) || (pTxBuffer == 0) || (len == 0U) ||
        (SlaveAddr > 0x7FU))
    {
        pI2CHandle->LastError = I2C_STATUS_INVALID_ARGUMENT;
        return I2C_STATUS_INVALID_ARGUMENT;
    }
    if (pI2CHandle->TxRxState != I2C_READY)
    {
        pI2CHandle->LastError = I2C_STATUS_BUSY;
        return I2C_STATUS_BUSY;
    }

    pI2Cx = pI2CHandle->pI2Cx;
    status = I2C_MasterBegin(pI2CHandle, SlaveAddr, 0U);
    if (status != I2C_STATUS_OK)
    {
        I2C_AbortMasterTransfer(pI2CHandle, status);
        return status;
    }
    I2C_ClearADDRFlag(pI2Cx);

    while (len > 0U)
    {
        status = I2C_WaitForSR1Flag(pI2Cx, I2C_FLAG_TXE, 1U);
        if (status != I2C_STATUS_OK)
        {
            I2C_AbortMasterTransfer(pI2CHandle, status);
            return status;
        }
        pI2Cx->DR = *pTxBuffer++;
        --len;
    }

    status = I2C_WaitForSR1Flag(pI2Cx, I2C_FLAG_BTF, 1U);
    if (status != I2C_STATUS_OK)
    {
        I2C_AbortMasterTransfer(pI2CHandle, status);
        return status;
    }

    if (Sr == I2C_DISABLE_SR) I2C_GenerateStopCondition(pI2Cx);
    pI2CHandle->LastError = I2C_STATUS_OK;
    return I2C_STATUS_OK;
}

I2C_Status_t I2C_MasterRead(I2C_Handle_t *pI2CHandle,
                            uint8_t *pRxBuffer,
                            uint32_t len,
                            uint8_t SlaveAddr,
                            uint8_t Sr)
{
    I2C_Status_t status;
    I2C_RegDef_t *pI2Cx;

    if (pI2CHandle == 0) return I2C_STATUS_INVALID_ARGUMENT;
    if ((pI2CHandle->pI2Cx == 0) || (pRxBuffer == 0) || (len == 0U) ||
        (SlaveAddr > 0x7FU))
    {
        pI2CHandle->LastError = I2C_STATUS_INVALID_ARGUMENT;
        return I2C_STATUS_INVALID_ARGUMENT;
    }
    if (pI2CHandle->TxRxState != I2C_READY)
    {
        pI2CHandle->LastError = I2C_STATUS_BUSY;
        return I2C_STATUS_BUSY;
    }

    pI2Cx = pI2CHandle->pI2Cx;
    status = I2C_MasterBegin(pI2CHandle, SlaveAddr, 1U);
    if (status != I2C_STATUS_OK)
    {
        I2C_AbortMasterTransfer(pI2CHandle, status);
        return status;
    }

    if (len == 1U)
    {
        uint32_t primask;

        I2C_ManageAcking(pI2Cx, I2C_ACK_DISABLE);
        /* STOP must follow the ADDR clear before the byte is received. */
        primask = CriticalSection_Enter();
        I2C_ClearADDRFlag(pI2Cx);
        if (Sr == I2C_DISABLE_SR) I2C_GenerateStopCondition(pI2Cx);
        CriticalSection_Exit(primask);

        status = I2C_WaitForSR1Flag(pI2Cx, I2C_FLAG_RXNE, 1U);
        if (status != I2C_STATUS_OK)
        {
            I2C_AbortMasterTransfer(pI2CHandle, status);
            return status;
        }
        *pRxBuffer = (uint8_t)pI2Cx->DR;
    }
    else if (len == 2U)
    {
        /* POS/ACK must be configured before ADDR is cleared for a 2-byte read. */
        I2C_ManageAcking(pI2Cx, I2C_ACK_DISABLE);
        pI2Cx->CR1 |= I2C_CR1_POS_MASK;
        I2C_ClearADDRFlag(pI2Cx);
        status = I2C_WaitForSR1Flag(pI2Cx, I2C_FLAG_BTF, 1U);
        if (status != I2C_STATUS_OK)
        {
            I2C_AbortMasterTransfer(pI2CHandle, status);
            return status;
        }
        if (Sr == I2C_DISABLE_SR) I2C_GenerateStopCondition(pI2Cx);
        *pRxBuffer++ = (uint8_t)pI2Cx->DR;
        *pRxBuffer   = (uint8_t)pI2Cx->DR;
    }
    else
    {
        I2C_ManageAcking(pI2Cx, I2C_ACK_ENABLE);
        pI2Cx->CR1 &= ~I2C_CR1_POS_MASK;
        I2C_ClearADDRFlag(pI2Cx);

        while (len > 3U)
        {
            status = I2C_WaitForSR1Flag(pI2Cx, I2C_FLAG_RXNE, 1U);
            if (status != I2C_STATUS_OK)
            {
                I2C_AbortMasterTransfer(pI2CHandle, status);
                return status;
            }
            *pRxBuffer++ = (uint8_t)pI2Cx->DR;
            --len;
        }

        /* Last three bytes (RM0090): BTF = N-2 in DR and N-1 in the shift
         * register, clock stretched. NACK the last byte, then read N-2 so
         * byte N is received; at the next BTF, STOP and read N-1 and N. */
        status = I2C_WaitForSR1Flag(pI2Cx, I2C_FLAG_BTF, 1U);
        if (status != I2C_STATUS_OK)
        {
            I2C_AbortMasterTransfer(pI2CHandle, status);
            return status;
        }
        I2C_ManageAcking(pI2Cx, I2C_ACK_DISABLE);
        *pRxBuffer++ = (uint8_t)pI2Cx->DR;

        status = I2C_WaitForSR1Flag(pI2Cx, I2C_FLAG_BTF, 1U);
        if (status != I2C_STATUS_OK)
        {
            I2C_AbortMasterTransfer(pI2CHandle, status);
            return status;
        }
        /* Leave the bus owned when Sr is enabled; the next transfer issues START. */
        if (Sr == I2C_DISABLE_SR) I2C_GenerateStopCondition(pI2Cx);
        *pRxBuffer++ = (uint8_t)pI2Cx->DR;
        *pRxBuffer = (uint8_t)pI2Cx->DR;
    }

    pI2Cx->CR1 &= ~I2C_CR1_POS_MASK;
    if (pI2CHandle->I2C_Config.I2C_ACKControl == I2C_ACK_ENABLE)
        I2C_ManageAcking(pI2Cx, I2C_ACK_ENABLE);
    pI2CHandle->LastError = I2C_STATUS_OK;
    return I2C_STATUS_OK;
}

void I2C_MasterSendData(I2C_Handle_t *pI2CHandle, uint8_t *pTxbuffer,
                        uint32_t len, uint8_t SlaveAddr, uint8_t Sr)
{
    if (pI2CHandle == 0) return;
    pI2CHandle->LastError = I2C_MasterWrite(pI2CHandle, pTxbuffer, len, SlaveAddr, Sr);
}

void I2C_MasterReceiverData(I2C_Handle_t *pI2CHandle, uint8_t *pRxbuffer,
                            uint32_t len, uint8_t SlaveAddr, uint8_t Sr)
{
    if (pI2CHandle == 0) return;
    pI2CHandle->LastError = I2C_MasterRead(pI2CHandle, pRxbuffer, len, SlaveAddr, Sr);
}

uint8_t I2C_MasterSendDataIT(I2C_Handle_t *pI2CHandle, uint8_t *pTxbuffer,
                             uint32_t len, uint8_t SlaveAddr, uint8_t Sr)
{
    uint8_t previousState;
    if ((pI2CHandle == 0) || (pI2CHandle->pI2Cx == 0) || (pTxbuffer == 0) ||
        (len == 0U) || (SlaveAddr > 0x7FU))
        return (uint8_t)I2C_STATUS_INVALID_ARGUMENT;

    previousState = pI2CHandle->TxRxState;
    if (previousState != I2C_READY) return previousState;

    pI2CHandle->pTxBuffer = pTxbuffer;
    pI2CHandle->TxLen = len;
    pI2CHandle->RxLen = 0U;
    pI2CHandle->TxRxState = I2C_BUSY_IN_TX;
    pI2CHandle->DevAddr = SlaveAddr;
    pI2CHandle->Sr = Sr;
    pI2CHandle->LastError = I2C_STATUS_OK;

    pI2CHandle->pI2Cx->CR2 |= I2C_CR2_ITEVTEN_MASK | I2C_CR2_ITBUFEN_MASK |
                               I2C_CR2_ITERREN_MASK;
    I2C_GenerateStartCondition(pI2CHandle->pI2Cx);
    return I2C_READY; /* Matches the original driver convention. */
}

uint8_t I2C_MasterReceiverDataIT(I2C_Handle_t *pI2CHandle, uint8_t *pRxbuffer,
                                 uint32_t len, uint8_t SlaveAddr, uint8_t Sr)
{
    uint8_t previousState;
    if ((pI2CHandle == 0) || (pI2CHandle->pI2Cx == 0) || (pRxbuffer == 0) ||
        (len == 0U) || (SlaveAddr > 0x7FU))
        return (uint8_t)I2C_STATUS_INVALID_ARGUMENT;

    previousState = pI2CHandle->TxRxState;
    if (previousState != I2C_READY) return previousState;

    pI2CHandle->pRxBuffer = pRxbuffer;
    pI2CHandle->RxLen = len;
    pI2CHandle->RxSize = len;
    pI2CHandle->TxLen = 0U;
    pI2CHandle->TxRxState = I2C_BUSY_IN_RX;
    pI2CHandle->DevAddr = SlaveAddr;
    pI2CHandle->Sr = Sr;
    pI2CHandle->LastError = I2C_STATUS_OK;

    pI2CHandle->pI2Cx->CR2 |= I2C_CR2_ITEVTEN_MASK | I2C_CR2_ITBUFEN_MASK |
                               I2C_CR2_ITERREN_MASK;
    I2C_GenerateStartCondition(pI2CHandle->pI2Cx);
    return I2C_READY; /* Matches the original driver convention. */
}

void I2C_SlaveSendData(I2C_RegDef_t *pI2C, uint8_t data)
{
    if (pI2C != 0) pI2C->DR = data;
}

uint8_t I2C_SlaveReceiverData(I2C_RegDef_t *pI2C)
{
    return (pI2C != 0) ? (uint8_t)pI2C->DR : 0U;
}

void I2C_CloseSendData(I2C_Handle_t *pI2CHandle)
{
    if ((pI2CHandle == 0) || (pI2CHandle->pI2Cx == 0)) return;
    pI2CHandle->pI2Cx->CR2 &= ~(I2C_CR2_ITBUFEN_MASK | I2C_CR2_ITEVTEN_MASK |
                                 I2C_CR2_ITERREN_MASK);
    pI2CHandle->TxRxState = I2C_READY;
    pI2CHandle->pTxBuffer = 0;
    pI2CHandle->TxLen = 0U;
}

void I2C_CloseReceiveData(I2C_Handle_t *pI2CHandle)
{
    if ((pI2CHandle == 0) || (pI2CHandle->pI2Cx == 0)) return;
    pI2CHandle->pI2Cx->CR2 &= ~(I2C_CR2_ITBUFEN_MASK | I2C_CR2_ITEVTEN_MASK |
                                 I2C_CR2_ITERREN_MASK);
    pI2CHandle->TxRxState = I2C_READY;
    pI2CHandle->pRxBuffer = 0;
    pI2CHandle->RxLen = 0U;
    pI2CHandle->RxSize = 0U;
    pI2CHandle->pI2Cx->CR1 &= ~I2C_CR1_POS_MASK;
    if (pI2CHandle->I2C_Config.I2C_ACKControl == I2C_ACK_ENABLE)
        I2C_ManageAcking(pI2CHandle->pI2Cx, I2C_ACK_ENABLE);
}

static void I2C_CompleteITTransfer(I2C_Handle_t *pI2CHandle, uint8_t isReceive)
{
    if (isReceive != 0U)
    {
        I2C_CloseReceiveData(pI2CHandle);
        I2C_ApplicationEventCallback(pI2CHandle, I2C_EV_RX_CMPLT);
    }
    else
    {
        I2C_CloseSendData(pI2CHandle);
        I2C_ApplicationEventCallback(pI2CHandle, I2C_EV_TX_CMPLT);
    }
}

void I2C_EV_IRQHandling(I2C_Handle_t *pI2CHandle)
{
    I2C_RegDef_t *pI2Cx;
    uint32_t sr1, sr2, cr2;

    if ((pI2CHandle == 0) || (pI2CHandle->pI2Cx == 0)) return;
    pI2Cx = pI2CHandle->pI2Cx;
    cr2 = pI2Cx->CR2;
    sr1 = pI2Cx->SR1;

    if ((cr2 & I2C_CR2_ITEVTEN_MASK) == 0U) return;

    /* START condition: valid only while acting as the bus controller. */
    if ((sr1 & I2C_FLAG_SB) != 0U)
    {
        /* SR2 is only needed after SB has been observed. Reading SR2 while
         * ADDR is pending would clear ADDR before the address setup below. */
        sr2 = pI2Cx->SR2;
        if ((sr2 & I2C_SR2_MSL_MASK) != 0U)
        {
            if (pI2CHandle->TxRxState == I2C_BUSY_IN_TX)
                I2C_ExecuteAddressPhaseWrite(pI2Cx, pI2CHandle->DevAddr);
            else if (pI2CHandle->TxRxState == I2C_BUSY_IN_RX)
                I2C_ExecuteAddressPhaseRead(pI2Cx, pI2CHandle->DevAddr);
        }
    }

    sr1 = pI2Cx->SR1;
    if ((sr1 & I2C_FLAG_ADDR) != 0U)
    {
        /* Configure ACK/POS before the SR1 -> SR2 sequence clears ADDR.
         * TxRxState is BUSY only for these driver's master transactions. */
        if (pI2CHandle->TxRxState == I2C_BUSY_IN_RX)
        {
            if (pI2CHandle->RxLen == 1U)
            {
                I2C_ManageAcking(pI2Cx, I2C_ACK_DISABLE);
                pI2Cx->CR1 &= ~I2C_CR1_POS_MASK;
            }
            else if (pI2CHandle->RxLen == 2U)
            {
                I2C_ManageAcking(pI2Cx, I2C_ACK_DISABLE);
                pI2Cx->CR1 |= I2C_CR1_POS_MASK;
            }
            else
            {
                I2C_ManageAcking(pI2Cx, I2C_ACK_ENABLE);
                pI2Cx->CR1 &= ~I2C_CR1_POS_MASK;
            }
        }
        sr2 = pI2Cx->SR2; /* Clear ADDR after any receive ACK/POS setup. */
        if (((sr2 & I2C_SR2_MSL_MASK) != 0U) &&
            (pI2CHandle->TxRxState == I2C_BUSY_IN_RX) &&
            (pI2CHandle->RxLen == 1U) && (pI2CHandle->Sr == I2C_DISABLE_SR))
        {
            I2C_GenerateStopCondition(pI2Cx);
        }
    }

    sr1 = pI2Cx->SR1;
    sr2 = pI2Cx->SR2;

    /* The final 2/3 bytes of an STM32F4 legacy-I2C read are sequenced on BTF. */
    if ((sr1 & I2C_FLAG_BTF) != 0U && ((sr2 & I2C_SR2_MSL_MASK) != 0U))
    {
        if ((pI2CHandle->TxRxState == I2C_BUSY_IN_TX) && (pI2CHandle->TxLen == 0U))
        {
            if (pI2CHandle->Sr == I2C_DISABLE_SR) I2C_GenerateStopCondition(pI2Cx);
            I2C_CompleteITTransfer(pI2CHandle, 0U);
        }
        else if ((pI2CHandle->TxRxState == I2C_BUSY_IN_RX) &&
                 (pI2CHandle->RxLen == 2U))
        {
            if (pI2CHandle->Sr == I2C_DISABLE_SR) I2C_GenerateStopCondition(pI2Cx);
            *pI2CHandle->pRxBuffer++ = (uint8_t)pI2Cx->DR;
            *pI2CHandle->pRxBuffer++ = (uint8_t)pI2Cx->DR;
            pI2CHandle->RxLen = 0U;
            I2C_CompleteITTransfer(pI2CHandle, 1U);
        }
        else if ((pI2CHandle->TxRxState == I2C_BUSY_IN_RX) &&
                 (pI2CHandle->RxLen == 3U))
        {
            /* NACK the last byte, then read N-2 so byte N is received; the
               RxLen == 2 branch finishes on the next BTF. */
            I2C_ManageAcking(pI2Cx, I2C_ACK_DISABLE);
            *pI2CHandle->pRxBuffer++ = (uint8_t)pI2Cx->DR;
            --pI2CHandle->RxLen;
        }
    }

    /* Buffer events: master byte movement or application callbacks for a slave. */
    cr2 = pI2Cx->CR2;
    sr1 = pI2Cx->SR1;
    sr2 = pI2Cx->SR2;
    if ((cr2 & I2C_CR2_ITBUFEN_MASK) != 0U)
    {
        if ((sr1 & I2C_FLAG_TXE) != 0U)
        {
            if (((sr2 & I2C_SR2_MSL_MASK) != 0U) &&
                (pI2CHandle->TxRxState == I2C_BUSY_IN_TX))
            {
                if (pI2CHandle->TxLen > 0U)
                {
                    pI2Cx->DR = *pI2CHandle->pTxBuffer++;
                    --pI2CHandle->TxLen;
                }
                else
                {
                    pI2Cx->CR2 &= ~I2C_CR2_ITBUFEN_MASK;
                }
            }
            else if (((sr2 & I2C_SR2_MSL_MASK) == 0U) &&
                     ((sr2 & I2C_SR2_TRA_MASK) != 0U))
            {
                I2C_ApplicationEventCallback(pI2CHandle, I2C_EV_DATA_REQ);
            }
        }

        sr1 = pI2Cx->SR1;
        sr2 = pI2Cx->SR2;
        if ((sr1 & I2C_FLAG_RXNE) != 0U)
        {
            if (((sr2 & I2C_SR2_MSL_MASK) != 0U) &&
                (pI2CHandle->TxRxState == I2C_BUSY_IN_RX))
            {
                if ((pI2CHandle->RxLen > 3U) || (pI2CHandle->RxLen == 1U))
                {
                    *pI2CHandle->pRxBuffer++ = (uint8_t)pI2Cx->DR;
                    --pI2CHandle->RxLen;
                    if (pI2CHandle->RxLen == 0U)
                        I2C_CompleteITTransfer(pI2CHandle, 1U);
                }
                else
                {
                    /* Last 2-3 bytes are read on BTF; RXNE stays set until
                       then and would re-enter this handler continuously. */
                    pI2Cx->CR2 &= ~I2C_CR2_ITBUFEN_MASK;
                }
            }
            else if ((sr2 & I2C_SR2_MSL_MASK) == 0U)
            {
                I2C_ApplicationEventCallback(pI2CHandle, I2C_EV_DATA_RCV);
            }
        }
    }

    /* STOPF belongs to target/slave mode. */
    sr1 = pI2Cx->SR1;
    sr2 = pI2Cx->SR2;
    if (((sr1 & I2C_FLAG_STOPF) != 0U) && ((sr2 & I2C_SR2_MSL_MASK) == 0U))
    {
        I2C_ClearSTOPFlag(pI2Cx);
        I2C_ApplicationEventCallback(pI2CHandle, I2C_EV_STOP);
    }
}

void I2C_ER_IRQHandling(I2C_Handle_t *pI2CHandle)
{
    uint32_t sr1;
    I2C_Status_t status = I2C_STATUS_OK;
    uint8_t event = I2C_ERROR_BERR;

    if ((pI2CHandle == 0) || (pI2CHandle->pI2Cx == 0)) return;
    if ((pI2CHandle->pI2Cx->CR2 & I2C_CR2_ITERREN_MASK) == 0U) return;

    sr1 = pI2CHandle->pI2Cx->SR1;
    if ((sr1 & I2C_FLAG_BERR) != 0U)
    {
        status = I2C_STATUS_BUS_ERROR;
        event = I2C_ERROR_BERR;
        I2C_ClearErrorFlags(pI2CHandle->pI2Cx, I2C_FLAG_BERR);
    }
    else if ((sr1 & I2C_FLAG_ARLO) != 0U)
    {
        status = I2C_STATUS_ARBITRATION_LOST;
        event = I2C_ERROR_ARLO;
        I2C_ClearErrorFlags(pI2CHandle->pI2Cx, I2C_FLAG_ARLO);
    }
    else if ((sr1 & I2C_FLAG_AF) != 0U)
    {
        status = I2C_STATUS_NACK;
        event = I2C_ERROR_AF;
        I2C_ClearErrorFlags(pI2CHandle->pI2Cx, I2C_FLAG_AF);
    }
    else if ((sr1 & I2C_FLAG_OVR) != 0U)
    {
        status = I2C_STATUS_OVERRUN;
        event = I2C_ERROR_OVR;
        I2C_ClearErrorFlags(pI2CHandle->pI2Cx, I2C_FLAG_OVR);
    }
    else if ((sr1 & I2C_FLAG_TIMEOUT) != 0U)
    {
        status = I2C_STATUS_TIMEOUT;
        event = I2C_ERROR_TIMEOUT;
        I2C_ClearErrorFlags(pI2CHandle->pI2Cx, I2C_FLAG_TIMEOUT);
    }
    else
    {
        return;
    }

    pI2CHandle->LastError = status;
    if ((pI2CHandle->pI2Cx->SR2 & I2C_SR2_MSL_MASK) != 0U)
        I2C_GenerateStopCondition(pI2CHandle->pI2Cx);

    if (pI2CHandle->TxRxState == I2C_BUSY_IN_RX)
        I2C_CloseReceiveData(pI2CHandle);
    else if (pI2CHandle->TxRxState == I2C_BUSY_IN_TX)
        I2C_CloseSendData(pI2CHandle);

    I2C_ApplicationEventCallback(pI2CHandle, event);
}

void I2C_IRQInterruptConfig(uint8_t IRQNumber, uint8_t EnorDi)
{
    if (EnorDi == ENABLE) NVIC_EnableIRQ((IRQn_Type)IRQNumber);
    else                  NVIC_DisableIRQ((IRQn_Type)IRQNumber);
}

void I2C_IRQPriorityConfig(uint8_t IRQNumber, uint32_t IRQPriority)
{
    NVIC_SetPriority((IRQn_Type)IRQNumber, IRQPriority);
}

I2C_WEAK void I2C_ApplicationEventCallback(I2C_Handle_t *pI2CHandle, uint8_t AppEv)
{
    /* Weak no-op default prevents link failures until the application supplies
       its event handler. Override this function in an application source file. */
    (void)pI2CHandle;
    (void)AppEv;
}
