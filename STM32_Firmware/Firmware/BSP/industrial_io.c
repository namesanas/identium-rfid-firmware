#include "industrial_io.h"

#include "../Config/firmware_config.h"
#include "../Drivers/gpio/gpio.h"

#if (TARGET_BOARD == BOARD_F429_DISCOVERY)
#include "f429_discovery/board_config.h"
#elif (TARGET_BOARD == BOARD_F411_CUSTOM)
#include "f411_custom/board_config.h"
#else
#error "Unsupported TARGET_BOARD"
#endif

#include <stddef.h>

#if (TARGET_BOARD == BOARD_F429_DISCOVERY)
/*
 * F429 prototype-only test mapping.
 * These pins are brought out on the Discovery headers. Avoid PE7 here because
 * the F429I-DISCO connects PE7 to the onboard SDRAM data bus (FMC_D4).
 * The F411 mapping is in f411_custom/board_config.h.
 */
#define BOARD_INPUT1_PORT GPIOE
#define BOARD_INPUT1_PIN  4U
#define BOARD_INPUT2_PORT GPIOE
#define BOARD_INPUT2_PIN  5U
#define BOARD_INPUT3_PORT GPIOE
#define BOARD_INPUT3_PIN  6U
#define BOARD_INPUT4_PORT GPIOD
#define BOARD_INPUT4_PIN  2U

#define BOARD_OUTPUT1_PORT GPIOE
#define BOARD_OUTPUT1_PIN  0U
#define BOARD_OUTPUT2_PORT GPIOE
#define BOARD_OUTPUT2_PIN  1U
#define BOARD_OUTPUT3_PORT GPIOE
#define BOARD_OUTPUT3_PIN  2U
#define BOARD_OUTPUT4_PORT GPIOE
#define BOARD_OUTPUT4_PIN  3U
#endif

/*
 * F411 custom board: the inputs read LOW when the field input is on
 * (NPN stage), and output 4 does not exist (three relays only).
 */
#if defined(BOARD_INPUTS_ACTIVE_LOW) && (BOARD_INPUTS_ACTIVE_LOW != 0U)
#define INDUSTRIAL_IO_INPUT_LEVEL(raw)  ((uint8_t)((raw) ^ 1U))
#else
#define INDUSTRIAL_IO_INPUT_LEVEL(raw)  (raw)
#endif

typedef struct
{
    GPIO_RegDef_t *port;
    uint8_t pin;
} Industrial_IO_Pin_t;

static const Industrial_IO_Pin_t industrialOutputs[INDUSTRIAL_IO_COUNT] =
{
    {BOARD_OUTPUT1_PORT, BOARD_OUTPUT1_PIN},
    {BOARD_OUTPUT2_PORT, BOARD_OUTPUT2_PIN},
    {BOARD_OUTPUT3_PORT, BOARD_OUTPUT3_PIN},
    {BOARD_OUTPUT4_PORT, BOARD_OUTPUT4_PIN}
};

static const Industrial_IO_Pin_t industrialInputs[INDUSTRIAL_IO_COUNT] =
{
    {BOARD_INPUT1_PORT, BOARD_INPUT1_PIN},
    {BOARD_INPUT2_PORT, BOARD_INPUT2_PIN},
    {BOARD_INPUT3_PORT, BOARD_INPUT3_PIN},
    {BOARD_INPUT4_PORT, BOARD_INPUT4_PIN}
};

static void Industrial_IO_ConfigureOutput(
    const Industrial_IO_Pin_t *pin
)
{
    GPIO_Handle_t gpio;

    gpio.pGPIOx = pin->port;
    gpio.GPIO_PinConfig.GPIO_PinNumber = pin->pin;
    gpio.GPIO_PinConfig.GPIO_PinMode = GPIO_DRV_MODE_OUT;
    gpio.GPIO_PinConfig.GPIO_PinSpeed = GPIO_DRV_SPEED_LOW;
    gpio.GPIO_PinConfig.GPIO_PuPdControl = GPIO_NO_PUPD;
    gpio.GPIO_PinConfig.GPIO_PinOPType = GPIO_OP_TYPE_PP;
    gpio.GPIO_PinConfig.GPIO_PinAltFunMode = 0U;

    GPIO_Init(&gpio);
}

static void Industrial_IO_ConfigureInput(
    const Industrial_IO_Pin_t *pin
)
{
    GPIO_Handle_t gpio;

    gpio.pGPIOx = pin->port;
    gpio.GPIO_PinConfig.GPIO_PinNumber = pin->pin;
    gpio.GPIO_PinConfig.GPIO_PinMode = GPIO_DRV_MODE_IN;
    gpio.GPIO_PinConfig.GPIO_PinSpeed = GPIO_DRV_SPEED_LOW;
#if (TARGET_BOARD == BOARD_F429_DISCOVERY)
    /* Stable default for bench tests with a floating jumper input. */
    gpio.GPIO_PinConfig.GPIO_PuPdControl = GPIO_PIN_PD;
#else
    /* Production board has its own external input conditioning. */
    gpio.GPIO_PinConfig.GPIO_PuPdControl = GPIO_NO_PUPD;
#endif
    gpio.GPIO_PinConfig.GPIO_PinOPType = GPIO_OP_TYPE_PP;
    gpio.GPIO_PinConfig.GPIO_PinAltFunMode = 0U;

    GPIO_Init(&gpio);
}

uint8_t Industrial_IO_Init(void)
{
    uint8_t i;

    for(i = 0U; i < INDUSTRIAL_IO_COUNT; i++)
    {
        if(industrialOutputs[i].port == NULL)
        {
#if (TARGET_BOARD == BOARD_F411_CUSTOM)
            continue;
#else
            return 0U;
#endif
        }

        Industrial_IO_ConfigureOutput(
            &industrialOutputs[i]
        );

        GPIO_WriteToOutputPin(
            industrialOutputs[i].port,
            industrialOutputs[i].pin,
            GPIO_PIN_RESET
        );
    }

    for(i = 0U; i < INDUSTRIAL_IO_COUNT; i++)
    {
        if(industrialInputs[i].port == NULL)
        {
            return 0U;
        }

        Industrial_IO_ConfigureInput(
            &industrialInputs[i]
        );
    }

    return 1U;
}

uint8_t Industrial_IO_SetOutput(
    uint8_t outputNumber,
    uint8_t state
)
{
    if(
        (outputNumber < 1U) ||
        (outputNumber > INDUSTRIAL_IO_COUNT) ||
        (state > 1U)
    )
    {
        return 0U;
    }

#if (TARGET_BOARD == BOARD_F411_CUSTOM)
    if(industrialOutputs[outputNumber - 1U].port == NULL)
    {
        return 0U;
    }
#endif

    GPIO_WriteToOutputPin(
        industrialOutputs[outputNumber - 1U].port,
        industrialOutputs[outputNumber - 1U].pin,
        state
    );

    return 1U;
}

uint8_t Industrial_IO_GetOutput(
    uint8_t outputNumber,
    uint8_t *state
)
{
    uint8_t value;

    if(
        (outputNumber < 1U) ||
        (outputNumber > INDUSTRIAL_IO_COUNT) ||
        (state == NULL)
    )
    {
        return 0U;
    }

#if (TARGET_BOARD == BOARD_F411_CUSTOM)
    if(industrialOutputs[outputNumber - 1U].port == NULL)
    {
        return 0U;
    }
#endif

    value = (uint8_t)(
        (industrialOutputs[outputNumber - 1U].port->ODR >>
         industrialOutputs[outputNumber - 1U].pin) &
        1UL
    );

    *state = value;
    return 1U;
}

uint8_t Industrial_IO_GetInput(
    uint8_t inputNumber
)
{
    if(
        (inputNumber < 1U) ||
        (inputNumber > INDUSTRIAL_IO_COUNT)
    )
    {
        return 0U;
    }

    return INDUSTRIAL_IO_INPUT_LEVEL(GPIO_ReadFromInputPin(
        industrialInputs[inputNumber - 1U].port,
        industrialInputs[inputNumber - 1U].pin
    ));
}

void Industrial_IO_GetAllInputs(
    uint8_t states[INDUSTRIAL_IO_COUNT]
)
{
    uint8_t i;

    if(states == NULL)
    {
        return;
    }

    for(i = 0U; i < INDUSTRIAL_IO_COUNT; i++)
    {
        states[i] = INDUSTRIAL_IO_INPUT_LEVEL(GPIO_ReadFromInputPin(
            industrialInputs[i].port,
            industrialInputs[i].pin
        ));
    }
}
