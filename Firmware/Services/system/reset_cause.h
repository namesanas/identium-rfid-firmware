/*
 * reset_cause.h
 *
 *  Created on: 28-Sept-2026
 *      Author: Identium
 */

#ifndef RFID_CONTROLLER_RESET_CAUSE_H_
#define RFID_CONTROLLER_RESET_CAUSE_H_

#include <stdint.h>


typedef enum
{
    RESET_CAUSE_UNKNOWN = 0U,

    RESET_CAUSE_LOW_POWER,
    RESET_CAUSE_WWDG,
    RESET_CAUSE_IWDG,
    RESET_CAUSE_SOFTWARE,
    RESET_CAUSE_BROWN_OUT,
    RESET_CAUSE_POWER_ON,
    RESET_CAUSE_PIN

} ResetCause_t;


/*
 * Capture the reset cause and clear the MCU reset flags.
 *
 * This function should be called once during startup.
 */
void Reset_Cause_Init(void);


/*
 * Return the interpreted primary reset cause.
 */
ResetCause_t Reset_Cause_Get(void);


/*
 * Return the raw RCC_CSR reset flags captured at startup.
 */
uint32_t Reset_Cause_GetRawFlags(void);


/*
 * Return 1 when the captured reset was caused by IWDG.
 */
uint8_t Reset_Cause_IsIWDG(void);


#endif /* RFID_CONTROLLER_RESET_CAUSE_H_ */
