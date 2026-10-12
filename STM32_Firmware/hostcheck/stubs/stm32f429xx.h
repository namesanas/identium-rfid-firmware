/*
 * Host-build stand-in for the STM32F429 device header.
 *
 * The real CMSIS headers carry ARM inline assembly, so the host check
 * replaces them with the few register types and functions that the
 * protocol sources reach through their includes.
 */
#ifndef HOSTCHECK_STM32F429XX_H
#define HOSTCHECK_STM32F429XX_H

#include <stdint.h>

typedef int IRQn_Type;

typedef struct
{
    volatile uint32_t SR;
    volatile uint32_t DR;
    volatile uint32_t BRR;
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t GTPR;
} USART_TypeDef;

void NVIC_SystemReset(void);

#endif /* HOSTCHECK_STM32F429XX_H */
