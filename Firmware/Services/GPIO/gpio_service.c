#include "gpio_service.h"

#include "../Time/time_service.h"

#include <stddef.h>
#include <string.h>

#define GPIO_SERVICE_MAX_OUTPUTS INDUSTRIAL_IO_COUNT

static GPIO_ServiceInputChangeCallback_t gpioInputChangeCallback = NULL;

static uint8_t gpioInputStates[INDUSTRIAL_IO_COUNT];
static uint8_t gpioInputInitialized = 0U;

static uint8_t gpioOutputStates[INDUSTRIAL_IO_COUNT];
static uint8_t gpioPulseActive[INDUSTRIAL_IO_COUNT];
static uint32_t gpioPulseDeadlineMs[INDUSTRIAL_IO_COUNT];
static uint8_t gpioPulsePreviousState[INDUSTRIAL_IO_COUNT];

static uint8_t GPIO_Service_AnyPulseActive(void)
{
    uint8_t i;

    for(i = 0U; i < GPIO_SERVICE_MAX_OUTPUTS; i++)
    {
        if(gpioPulseActive[i] != 0U)
        {
            return 1U;
        }
    }

    return 0U;
}

static void GPIO_Service_SetError(
    uint32_t *errorCode,
    const char **errorString,
    uint32_t code,
    const char *string
)
{
    if(errorCode != NULL)
    {
        *errorCode = code;
    }

    if(errorString != NULL)
    {
        *errorString = string;
    }
}

void GPIO_Service_Init(
    GPIO_ServiceInputChangeCallback_t inputChangeCallback
)
{
    uint8_t i;

    gpioInputChangeCallback = inputChangeCallback;
    gpioInputInitialized = 0U;

    memset(
        gpioInputStates,
        0,
        sizeof(gpioInputStates)
    );

    memset(
        gpioOutputStates,
        0,
        sizeof(gpioOutputStates)
    );

    memset(
        gpioPulseActive,
        0,
        sizeof(gpioPulseActive)
    );

    memset(
        gpioPulseDeadlineMs,
        0,
        sizeof(gpioPulseDeadlineMs)
    );

    memset(
        gpioPulsePreviousState,
        0,
        sizeof(gpioPulsePreviousState)
    );

    /* Hardware outputs are already driven low by Industrial_IO_Init(). */
    for(i = 0U; i < GPIO_SERVICE_MAX_OUTPUTS; i++)
    {
        gpioOutputStates[i] = 0U;
    }
}

void GPIO_Service_Task(void)
{
    uint8_t currentInputs[INDUSTRIAL_IO_COUNT];
    uint8_t changed = 0U;
    uint8_t i;
    uint32_t nowMs;

    nowMs = Time_GetTickMs();

    for(i = 0U; i < GPIO_SERVICE_MAX_OUTPUTS; i++)
    {
        if(
            (gpioPulseActive[i] != 0U) &&
            ((int32_t)(nowMs - gpioPulseDeadlineMs[i]) >= 0)
        )
        {
            gpioPulseActive[i] = 0U;

            (void)Industrial_IO_SetOutput(
                (uint8_t)(i + 1U),
                gpioPulsePreviousState[i]
            );

            gpioOutputStates[i] = gpioPulsePreviousState[i];
        }
    }

    Industrial_IO_GetAllInputs(
        currentInputs
    );

    if(gpioInputInitialized == 0U)
    {
        memcpy(
            gpioInputStates,
            currentInputs,
            sizeof(gpioInputStates)
        );

        gpioInputInitialized = 1U;
        return;
    }

    for(i = 0U; i < INDUSTRIAL_IO_COUNT; i++)
    {
        if(currentInputs[i] != gpioInputStates[i])
        {
            changed = 1U;
            gpioInputStates[i] = currentInputs[i];
        }
    }

    if(
        (changed != 0U) &&
        (gpioInputChangeCallback != NULL)
    )
    {
        gpioInputChangeCallback(
            gpioInputStates
        );
    }
}

uint8_t GPIO_Service_SetOutputs(
    const uint8_t outputNumbers[],
    const uint8_t states[],
    const uint32_t durationsSeconds[],
    uint8_t count,
    uint32_t *errorCode,
    const char **errorString
)
{
    uint8_t seenMask = 0U;
    uint8_t i;
    uint8_t outputNumber;
    uint8_t state;
    uint32_t durationSeconds;
    uint32_t nowMs;
    uint32_t durationMs;
    uint8_t index;

    GPIO_Service_SetError(
        errorCode,
        errorString,
        0U,
        "ok"
    );

    if(
        (outputNumbers == NULL) ||
        (states == NULL) ||
        (durationsSeconds == NULL) ||
        (count < 1U) ||
        (count > GPIO_SERVICE_MAX_OUTPUTS)
    )
    {
        GPIO_Service_SetError(
            errorCode,
            errorString,
            20003U,
            "invalid set_gpo item"
        );
        return GPIO_SERVICE_ERR_INVALID;
    }

    if(GPIO_Service_AnyPulseActive() != 0U)
    {
        GPIO_Service_SetError(
            errorCode,
            errorString,
            20003U,
            "gpo op is running"
        );
        return GPIO_SERVICE_ERR_BUSY;
    }

    for(i = 0U; i < count; i++)
    {
        outputNumber = outputNumbers[i];
        state = states[i];
        durationSeconds = durationsSeconds[i];

        if(
            (outputNumber < 1U) ||
            (outputNumber > GPIO_SERVICE_MAX_OUTPUTS)
        )
        {
            /* GPO 5 is legal on reference readers as a buzzer, but this
             * four-output controller does not have that physical output. */
            GPIO_Service_SetError(
                errorCode,
                errorString,
                2U,
                "not supported operation"
            );
            return GPIO_SERVICE_ERR_NOT_SUPPORTED;
        }

        if(state > 1U)
        {
            GPIO_Service_SetError(
                errorCode,
                errorString,
                20003U,
                "invalid set_gpo item"
            );
            return GPIO_SERVICE_ERR_INVALID;
        }

        if(durationSeconds > 3600U)
        {
            GPIO_Service_SetError(
                errorCode,
                errorString,
                20003U,
                "invalid set_gpo item"
            );
            return GPIO_SERVICE_ERR_INVALID;
        }

        index = (uint8_t)(outputNumber - 1U);

        if((seenMask & (uint8_t)(1U << index)) != 0U)
        {
            GPIO_Service_SetError(
                errorCode,
                errorString,
                20003U,
                "repetitive gpo number"
            );
            return GPIO_SERVICE_ERR_INVALID;
        }

        seenMask |= (uint8_t)(1U << index);
    }

    nowMs = Time_GetTickMs();

    for(i = 0U; i < count; i++)
    {
        outputNumber = outputNumbers[i];
        state = states[i];
        durationSeconds = durationsSeconds[i];
        index = (uint8_t)(outputNumber - 1U);

        gpioPulsePreviousState[index] = gpioOutputStates[index];
        gpioOutputStates[index] = state;

        if(Industrial_IO_SetOutput(outputNumber, state) == 0U)
        {
            GPIO_Service_SetError(
                errorCode,
                errorString,
                20003U,
                "invalid set_gpo item"
            );
            return GPIO_SERVICE_ERR_INVALID;
        }

        if(durationSeconds != 0U)
        {
            durationMs = durationSeconds * 1000U;
            gpioPulseDeadlineMs[index] = nowMs + durationMs;
            gpioPulseActive[index] = 1U;
        }
        else
        {
            gpioPulseDeadlineMs[index] = 0U;
            gpioPulseActive[index] = 0U;
        }
    }

    return GPIO_SERVICE_OK;
}

void GPIO_Service_GetAllInputs(
    uint8_t states[INDUSTRIAL_IO_COUNT]
)
{
    Industrial_IO_GetAllInputs(states);
}
