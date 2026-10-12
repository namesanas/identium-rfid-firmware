/*
 * brownout.h
 *
 * Brown-out reset level.
 *
 * Out of the factory the STM32F4 brown-out reset (BOR) is off: the MCU
 * is only held in reset below the ~1.8 V power-down threshold, so a
 * sagging supply can make the core execute garbage or corrupt a flash
 * write long before it resets.
 *
 * The level depends on the board's supply (thresholds from the STM32F4
 * datasheets, rising edge, typical):
 *  - F411 custom board, 3.3 V rail: level 3 (~2.9 V).
 *  - F429 Discovery, 3.0 V rail (measures ~2.9 V): level 3 is too close
 *    to the rail and could hold the board in reset, so level 2 (~2.6 V).
 *
 * The level is an option byte: Brownout_Init() programs it once, when
 * it differs, and it then persists across resets and reflashing.
 */

#ifndef RFID_CONTROLLER_BROWNOUT_H_
#define RFID_CONTROLLER_BROWNOUT_H_

#include <stdint.h>

/* Returns 1 when the board's BOR level is active (already or after
 * programming). */
uint8_t Brownout_Init(void);

/* Active BOR level: 3, 2, 1, or 0 for off. */
uint8_t Brownout_GetLevel(void);

#endif /* RFID_CONTROLLER_BROWNOUT_H_ */
