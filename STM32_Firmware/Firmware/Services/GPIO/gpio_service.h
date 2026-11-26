#ifndef RFID_CONTROLLER_GPIO_SERVICE_H_
#define RFID_CONTROLLER_GPIO_SERVICE_H_

#include <stdint.h>
#include "../../BSP/industrial_io.h"

#define GPIO_SERVICE_OK                  0U
#define GPIO_SERVICE_ERR_INVALID         1U
#define GPIO_SERVICE_ERR_BUSY            2U
#define GPIO_SERVICE_ERR_NOT_SUPPORTED   3U

/*
 * Called when one or more input levels change.
 * states contains the complete GPI1..GPI4 snapshot.
 */
typedef void (*GPIO_ServiceInputChangeCallback_t)(
    const uint8_t states[INDUSTRIAL_IO_COUNT]
);

void GPIO_Service_Init(
    GPIO_ServiceInputChangeCallback_t inputChangeCallback
);

void GPIO_Service_Task(void);

/* Validate all requested outputs before applying a set_gpo operation.
 * F411 has three outputs; F429 has four. */
uint8_t GPIO_Service_SetOutputs(
    const uint8_t outputNumbers[],
    const uint8_t states[],
    const uint32_t durationsSeconds[],
    uint8_t count,
    uint32_t *errorCode,
    const char **errorString
);

void GPIO_Service_GetAllInputs(
    uint8_t states[INDUSTRIAL_IO_COUNT]
);

#define GPIO_SERVICE_BUZZER_MAX_MS       10000U

/* Sound the buzzer for durationMs (1..GPIO_SERVICE_BUZZER_MAX_MS); it is
 * switched off by GPIO_Service_Task. Returns GPIO_SERVICE_OK,
 * GPIO_SERVICE_ERR_INVALID or GPIO_SERVICE_ERR_NOT_SUPPORTED (no buzzer). */
uint8_t GPIO_Service_Beep(uint32_t durationMs);

#endif /* RFID_CONTROLLER_GPIO_SERVICE_H_ */
