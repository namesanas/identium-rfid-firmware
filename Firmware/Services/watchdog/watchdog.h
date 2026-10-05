/*
 * watchdog.h
 *
 *  Created on: 28-Sept-2026
 *      Author: Identium
 */

#ifndef RFID_CONTROLLER_WATCHDOG_H_
#define RFID_CONTROLLER_WATCHDOG_H_

#include <stdint.h>


/*
 * ============================================================
 * WATCHDOG CONFIGURATION
 * ============================================================
 *
 * IWDG clock = LSI
 *
 * Prescaler = /256
 * Reload    = 0xFFF
 *
 * Approximate timeout:
 *
 * LSI 17 kHz -> ~61.7 s
 * LSI 32 kHz -> ~32.8 s
 * LSI 47 kHz -> ~22.3 s
 *
 * This intentionally uses a long timeout during the first
 * watchdog integration stage because the current firmware
 * contains blocking RFID transactions.
 *
 * ============================================================
 */

#define WATCHDOG_IWDG_PRESCALER      6U
#define WATCHDOG_IWDG_RELOAD         0x0FFFU


/*
 * Initialize and start the independent watchdog.
 *
 * Once started, the IWDG cannot be stopped except by reset.
 */
uint8_t Watchdog_Init(void);


/*
 * Refresh the watchdog counter.
 */
void Watchdog_Refresh(void);


/*
 * Return 1 if the watchdog service has been initialized.
 */
uint8_t Watchdog_IsInitialized(void);


#endif /* RFID_CONTROLLER_WATCHDOG_H_ */
