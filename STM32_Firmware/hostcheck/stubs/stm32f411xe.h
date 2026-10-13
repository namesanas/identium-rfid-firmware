/*
 * Host-build stand-in for the STM32F411 device header.
 *
 * Only the register types and instance names that the tag log host
 * check reaches through its includes (SPI, GPIO, USART, board config).
 */
#ifndef HOSTCHECK_STM32F411XE_H
#define HOSTCHECK_STM32F411XE_H

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

typedef struct
{
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t SR;
    volatile uint32_t DR;
} SPI_TypeDef;

typedef struct
{
    volatile uint32_t MODER;
    volatile uint32_t OTYPER;
    volatile uint32_t OSPEEDR;
    volatile uint32_t PUPDR;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t LCKR;
    volatile uint32_t AFR[2];
} GPIO_TypeDef;

extern GPIO_TypeDef hostcheckGpioA;
#define GPIOA (&hostcheckGpioA)

void NVIC_SystemReset(void);

#endif /* HOSTCHECK_STM32F411XE_H */
