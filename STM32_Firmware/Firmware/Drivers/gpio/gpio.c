#include "gpio.h"
#include <stddef.h>

static void GPIO_ClockEnable(GPIO_RegDef_t *pGPIOx, uint8_t enable)
{
    uint32_t bit;

    if (pGPIOx == GPIOA) bit = 0U;
    else if (pGPIOx == GPIOB) bit = 1U;
    else if (pGPIOx == GPIOC) bit = 2U;
    else if (pGPIOx == GPIOD) bit = 3U;
    else if (pGPIOx == GPIOE) bit = 4U;
#if defined(GPIOF)
    else if (pGPIOx == GPIOF) bit = 5U;
#endif
#if defined(GPIOG)
    else if (pGPIOx == GPIOG) bit = 6U;
#endif
    else if (pGPIOx == GPIOH) bit = 7U;
#if defined(GPIOI)
    else if (pGPIOx == GPIOI) bit = 8U;
#endif
#if defined(GPIOJ)
    else if (pGPIOx == GPIOJ) bit = 9U;
#endif
#if defined(GPIOK)
    else if (pGPIOx == GPIOK) bit = 10U;
#endif
    else return;

    if (enable) RCC->AHB1ENR |= (1UL << bit);
    else        RCC->AHB1ENR &= ~(1UL << bit);
}

void GPIO_PeriClockControl(GPIO_RegDef_t *pGPIOx, uint8_t EnorDi)
{
    GPIO_ClockEnable(pGPIOx, EnorDi == ENABLE);
}

void GPIO_Init(GPIO_Handle_t *pGPIOHandle)
{
    GPIO_RegDef_t *gpio;
    uint8_t pin;
    uint8_t mode;

    if (pGPIOHandle == NULL || pGPIOHandle->pGPIOx == NULL)
        return;

    gpio = pGPIOHandle->pGPIOx;
    pin = pGPIOHandle->GPIO_PinConfig.GPIO_PinNumber;
    mode = pGPIOHandle->GPIO_PinConfig.GPIO_PinMode;

    if (pin > 15U)
        return;

    GPIO_PeriClockControl(gpio, ENABLE);

    if (mode <= GPIO_DRV_MODE_ANALOG)
    {
        gpio->MODER &= ~(3UL << (2U * pin));
        gpio->MODER |= ((uint32_t)mode << (2U * pin));
    }
    else
    {
        uint8_t exticrIndex;
        uint8_t exticrSection;
        uint8_t portCode = 0U;

        if (mode == GPIO_DRV_MODE_IT_FT)
        {
            EXTI->FTSR |= (1UL << pin);
            EXTI->RTSR &= ~(1UL << pin);
        }
        else if (mode == GPIO_DRV_MODE_IT_RT)
        {
            EXTI->RTSR |= (1UL << pin);
            EXTI->FTSR &= ~(1UL << pin);
        }
        else if (mode == GPIO_DRV_MODE_IT_RFT)
        {
            EXTI->RTSR |= (1UL << pin);
            EXTI->FTSR |= (1UL << pin);
        }

        gpio->MODER &= ~(3UL << (2U * pin));
        EXTI->IMR |= (1UL << pin);

        RCC->APB2ENR |= (1UL << 14U); /* SYSCFG clock */

        exticrIndex = pin / 4U;
        exticrSection = pin % 4U;

        if (gpio == GPIOA) portCode = GPIO_EXTI_PORTA;
        else if (gpio == GPIOB) portCode = GPIO_EXTI_PORTB;
        else if (gpio == GPIOC) portCode = GPIO_EXTI_PORTC;
        else if (gpio == GPIOD) portCode = GPIO_EXTI_PORTD;
        else if (gpio == GPIOE) portCode = GPIO_EXTI_PORTE;
#if defined(GPIOF)
        else if (gpio == GPIOF) portCode = GPIO_EXTI_PORTF;
#endif
#if defined(GPIOG)
        else if (gpio == GPIOG) portCode = GPIO_EXTI_PORTG;
#endif
        else if (gpio == GPIOH) portCode = GPIO_EXTI_PORTH;
#if defined(GPIOI)
        else if (gpio == GPIOI) portCode = GPIO_EXTI_PORTI;
#endif
        else return;

        SYSCFG->EXTICR[exticrIndex] &= ~(0xFUL << (4U * exticrSection));
        SYSCFG->EXTICR[exticrIndex] |= ((uint32_t)portCode << (4U * exticrSection));
    }

    gpio->OTYPER &= ~(1UL << pin);
    gpio->OTYPER |= ((uint32_t)pGPIOHandle->GPIO_PinConfig.GPIO_PinOPType << pin);

    gpio->OSPEEDR &= ~(3UL << (2U * pin));
    gpio->OSPEEDR |= ((uint32_t)pGPIOHandle->GPIO_PinConfig.GPIO_PinSpeed << (2U * pin));

    gpio->PUPDR &= ~(3UL << (2U * pin));
    gpio->PUPDR |= ((uint32_t)pGPIOHandle->GPIO_PinConfig.GPIO_PuPdControl << (2U * pin));

    if (mode == GPIO_DRV_MODE_ALTFN)
    {
        uint8_t afrIndex = pin / 8U;
        uint8_t afrSection = pin % 8U;

        gpio->AFR[afrIndex] &= ~(0xFUL << (4U * afrSection));
        gpio->AFR[afrIndex] |= ((uint32_t)pGPIOHandle->GPIO_PinConfig.GPIO_PinAltFunMode << (4U * afrSection));
    }
}

void GPIO_DeInit(GPIO_RegDef_t *pGPIOx)
{
    /* Preserve the original driver's reset intent using the MCU RCC reset bits. */
    if (pGPIOx == GPIOA) RCC->AHB1RSTR |= (1UL << 0U), RCC->AHB1RSTR &= ~(1UL << 0U);
    else if (pGPIOx == GPIOB) RCC->AHB1RSTR |= (1UL << 1U), RCC->AHB1RSTR &= ~(1UL << 1U);
    else if (pGPIOx == GPIOC) RCC->AHB1RSTR |= (1UL << 2U), RCC->AHB1RSTR &= ~(1UL << 2U);
    else if (pGPIOx == GPIOD) RCC->AHB1RSTR |= (1UL << 3U), RCC->AHB1RSTR &= ~(1UL << 3U);
    else if (pGPIOx == GPIOE) RCC->AHB1RSTR |= (1UL << 4U), RCC->AHB1RSTR &= ~(1UL << 4U);
#if defined(GPIOF)
    else if (pGPIOx == GPIOF) RCC->AHB1RSTR |= (1UL << 5U), RCC->AHB1RSTR &= ~(1UL << 5U);
#endif
#if defined(GPIOG)
    else if (pGPIOx == GPIOG) RCC->AHB1RSTR |= (1UL << 6U), RCC->AHB1RSTR &= ~(1UL << 6U);
#endif
    else if (pGPIOx == GPIOH) RCC->AHB1RSTR |= (1UL << 7U), RCC->AHB1RSTR &= ~(1UL << 7U);
}

uint8_t GPIO_ReadFromInputPin(GPIO_RegDef_t *pGPIOx, uint8_t PinNumber)
{
    return (uint8_t)((pGPIOx->IDR >> PinNumber) & 1UL);
}

uint16_t GPIO_ReadFromInputPort(GPIO_RegDef_t *pGPIOx)
{
    return (uint16_t)pGPIOx->IDR;
}

void GPIO_WriteToOutputPin(GPIO_RegDef_t *pGPIOx, uint8_t PinNumber, uint8_t Value)
{
    if (Value == GPIO_PIN_SET) pGPIOx->ODR |= (1UL << PinNumber);
    else                       pGPIOx->ODR &= ~(1UL << PinNumber);
}

void GPIO_WriteToOutputPort(GPIO_RegDef_t *pGPIOx, uint16_t Value)
{
    pGPIOx->ODR = Value;
}

void GPIO_ToggleOutputPin(GPIO_RegDef_t *pGPIOx, uint8_t PinNumber)
{
    pGPIOx->ODR ^= (1UL << PinNumber);
}

void GPIO_IRQInterruptConfig(uint8_t IRQNumber, uint8_t EnorDi)
{
    IRQn_Type irqn = (IRQn_Type)IRQNumber;
    if (EnorDi == ENABLE) NVIC_EnableIRQ(irqn);
    else                   NVIC_DisableIRQ(irqn);
}

void GPIO_IRQPriorityConfig(uint8_t IRQNumber, uint32_t IRQPriority)
{
    NVIC_SetPriority((IRQn_Type)IRQNumber, IRQPriority);
}

void GPIO_IRQHandling(uint8_t PinNumber)
{
    if (EXTI->PR & (1UL << PinNumber))
        EXTI->PR = (1UL << PinNumber);
}
