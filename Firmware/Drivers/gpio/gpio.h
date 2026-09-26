#ifndef RFID_CONTROLLER_DRIVER_GPIO_H
#define RFID_CONTROLLER_DRIVER_GPIO_H

#include <stdint.h>
#include "../../Config/firmware_config.h"

#if (TARGET_BOARD == BOARD_F429_DISCOVERY)
#include "stm32f429xx.h"
typedef GPIO_TypeDef GPIO_RegDef_t;
#elif (TARGET_BOARD == BOARD_F411_CUSTOM)
#include "stm32f411xe.h"
typedef GPIO_TypeDef GPIO_RegDef_t;
#else
#error "Unsupported TARGET_BOARD"
#endif

typedef enum
{
    GPIO_PIN_NO_0 = 0U,
    GPIO_PIN_NO_1,
    GPIO_PIN_NO_2,
    GPIO_PIN_NO_3,
    GPIO_PIN_NO_4,
    GPIO_PIN_NO_5,
    GPIO_PIN_NO_6,
    GPIO_PIN_NO_7,
    GPIO_PIN_NO_8,
    GPIO_PIN_NO_9,
    GPIO_PIN_NO_10,
    GPIO_PIN_NO_11,
    GPIO_PIN_NO_12,
    GPIO_PIN_NO_13,
    GPIO_PIN_NO_14,
    GPIO_PIN_NO_15
} GPIO_PinNumber_t;

typedef enum
{
    GPIO_MODE_IN = 0U,
    GPIO_MODE_OUT = 1U,
    GPIO_MODE_ALTFN = 2U,
    GPIO_MODE_ANALOG = 3U,
    GPIO_MODE_IT_FT = 4U,
    GPIO_MODE_IT_RT = 5U,
    GPIO_MODE_IT_RFT = 6U
} GPIO_PinMode_t;

typedef enum
{
    GPIO_SPEED_LOW = 0U,
    GPIO_SPEED_MEDIUM = 1U,
    GPIO_SPEED_FAST = 2U,
    GPIO_SPEED_HIGH = 3U
} GPIO_PinSpeed_t;

typedef enum
{
    GPIO_NO_PUPD = 0U,
    GPIO_PIN_PU = 1U,
    GPIO_PIN_PD = 2U
} GPIO_PuPd_t;

typedef enum
{
    GPIO_OP_TYPE_PP = 0U,
    GPIO_OP_TYPE_OD = 1U
} GPIO_OutputType_t;

typedef struct
{
    uint8_t GPIO_PinNumber;
    uint8_t GPIO_PinMode;
    uint8_t GPIO_PinSpeed;
    uint8_t GPIO_PuPdControl;
    uint8_t GPIO_PinOPType;
    uint8_t GPIO_PinAltFunMode;
} GPIO_PinConfig_t;

typedef struct
{
    GPIO_RegDef_t *pGPIOx;
    GPIO_PinConfig_t GPIO_PinConfig;
} GPIO_Handle_t;

#define GPIO_PIN_SET       1U
#define GPIO_PIN_RESET     0U

#define GPIO_EXTI_PORTA    0U
#define GPIO_EXTI_PORTB    1U
#define GPIO_EXTI_PORTC    2U
#define GPIO_EXTI_PORTD    3U
#define GPIO_EXTI_PORTE    4U
#define GPIO_EXTI_PORTF    5U
#define GPIO_EXTI_PORTG    6U
#define GPIO_EXTI_PORTH    7U
#define GPIO_EXTI_PORTI    8U

#ifndef ENABLE
#define ENABLE             1U
#endif
#ifndef DISABLE
#define DISABLE            0U
#endif

#ifndef FLAG_SET
#define FLAG_SET           1U
#endif
#ifndef FLAG_RESET
#define FLAG_RESET         0U
#endif

void GPIO_PeriClockControl(GPIO_RegDef_t *pGPIOx, uint8_t EnorDi);
void GPIO_Init(GPIO_Handle_t *pGPIOHandle);
void GPIO_DeInit(GPIO_RegDef_t *pGPIOx);
uint8_t GPIO_ReadFromInputPin(GPIO_RegDef_t *pGPIOx, uint8_t PinNumber);
uint16_t GPIO_ReadFromInputPort(GPIO_RegDef_t *pGPIOx);
void GPIO_WriteToOutputPin(GPIO_RegDef_t *pGPIOx, uint8_t PinNumber, uint8_t Value);
void GPIO_WriteToOutputPort(GPIO_RegDef_t *pGPIOx, uint16_t Value);
void GPIO_ToggleOutputPin(GPIO_RegDef_t *pGPIOx, uint8_t PinNumber);
void GPIO_IRQInterruptConfig(uint8_t IRQNumber, uint8_t EnorDi);
void GPIO_IRQPriorityConfig(uint8_t IRQNumber, uint32_t IRQPriority);
void GPIO_IRQHandling(uint8_t PinNumber);

#endif /* RFID_CONTROLLER_DRIVER_GPIO_H */
