#include "rcc.h"

static const uint16_t AHB_PreScaler[8] =
{
    2U, 4U, 8U, 16U, 64U, 128U, 256U, 512U
};

static const uint16_t APB_PreScaler[4] =
{
    2U, 4U, 8U, 16U
};

uint32_t RCC_GetPLLOutputClock(void)
{
    uint32_t pllm;
    uint32_t plln;
    uint32_t pllp;
    uint32_t vcoInputFrequency;
    uint32_t vcoOutputFrequency;
    uint32_t pllsrc;

    pllm = RCC->PLLCFGR & 0x3FU;
    plln = (RCC->PLLCFGR >> 6U) & 0x1FFU;
    pllp = (RCC->PLLCFGR >> 16U) & 0x03U;
    pllp = (pllp * 2U) + 2U;
    pllsrc = (RCC->PLLCFGR >> 22U) & 0x01U;

    if (pllm == 0U)
        return 0U;

    if (pllsrc == 0U)
        vcoInputFrequency = 16000000UL / pllm;
    else
        vcoInputFrequency = BOARD_HSE_FREQUENCY_HZ / pllm;

    vcoOutputFrequency = vcoInputFrequency * plln;

    return vcoOutputFrequency / pllp;
}

uint32_t RCC_GetSystemClockValue(void)
{
    uint32_t clockSource;

    clockSource = (RCC->CFGR >> 2U) & 0x03U;

    if (clockSource == 0U)
        return 16000000UL;
    if (clockSource == 1U)
        return BOARD_HSE_FREQUENCY_HZ;
    if (clockSource == 2U)
        return RCC_GetPLLOutputClock();

    return 16000000UL;
}

static uint32_t RCC_GetAHBPrescaler(void)
{
    uint32_t temp = (RCC->CFGR >> 4U) & 0x0FU;

    if (temp < 8U)
        return 1U;

    return AHB_PreScaler[temp - 8U];
}

static uint32_t RCC_GetAPBPrescaler(uint32_t shift)
{
    uint32_t temp = (RCC->CFGR >> shift) & 0x07U;

    if (temp < 4U)
        return 1U;

    return APB_PreScaler[temp - 4U];
}

uint32_t RCC_GetPCLK1Value(void)
{
    return RCC_GetSystemClockValue() /
           RCC_GetAHBPrescaler() /
           RCC_GetAPBPrescaler(10U);
}

uint32_t RCC_GetPCLK2Value(void)
{
    return RCC_GetSystemClockValue() /
           RCC_GetAHBPrescaler() /
           RCC_GetAPBPrescaler(13U);
}
