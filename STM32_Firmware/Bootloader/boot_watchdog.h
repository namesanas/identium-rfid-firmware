/*
 * boot_watchdog.h
 *
 * The application starts the independent watchdog (about 32 s) and a
 * software reset does not stop it, so the bootloader reloads it during
 * long erase and copy steps. Reloading a watchdog that is not running
 * has no effect.
 */

#ifndef RFID_CONTROLLER_BOOT_WATCHDOG_H
#define RFID_CONTROLLER_BOOT_WATCHDOG_H

#include "stm32f4xx.h"

static inline void BootWatchdog_Kick(void)
{
    IWDG->KR = 0xAAAAU;
}

#endif /* RFID_CONTROLLER_BOOT_WATCHDOG_H */
