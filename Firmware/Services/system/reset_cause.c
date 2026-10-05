/*
 * reset_cause.c
 *
 *  Created on: 28-Sept-2026
 *      Author: Identium
 */

#include "reset_cause.h"

#include "stm32f429xx.h"


static volatile ResetCause_t g_resetCause =
    RESET_CAUSE_UNKNOWN;

static volatile uint32_t g_resetFlags =
    0U;


/*
 * ============================================================
 * CAPTURE RESET CAUSE
 * ============================================================
 */

void Reset_Cause_Init(void)
{
    uint32_t flags;

    /*
     * Read RCC_CSR before clearing the reset flags.
     */
    flags = RCC->CSR;

    /*
     * Store only actual reset-cause flags.
     */
    g_resetFlags =
        flags &
        (
            RCC_CSR_LPWRRSTF |
            RCC_CSR_WWDGRSTF |
            RCC_CSR_IWDGRSTF |
            RCC_CSR_SFTRSTF |
            RCC_CSR_PORRSTF  |
            RCC_CSR_PINRSTF  |
            RCC_CSR_BORRSTF
        );


    /*
     * Determine the primary cause.
     *
     * Watchdog/software causes are checked before
     * power/pin causes.
     */
    if(
        (g_resetFlags & RCC_CSR_IWDGRSTF) != 0U
    )
    {
        g_resetCause =
            RESET_CAUSE_IWDG;
    }
    else if(
        (g_resetFlags & RCC_CSR_WWDGRSTF) != 0U
    )
    {
        g_resetCause =
            RESET_CAUSE_WWDG;
    }
    else if(
        (g_resetFlags & RCC_CSR_SFTRSTF) != 0U
    )
    {
        g_resetCause =
            RESET_CAUSE_SOFTWARE;
    }
    else if(
        (g_resetFlags & RCC_CSR_BORRSTF) != 0U
    )
    {
        g_resetCause =
            RESET_CAUSE_BROWN_OUT;
    }
    else if(
        (g_resetFlags & RCC_CSR_PORRSTF) != 0U
    )
    {
        g_resetCause =
            RESET_CAUSE_POWER_ON;
    }
    else if(
        (g_resetFlags & RCC_CSR_PINRSTF) != 0U
    )
    {
        g_resetCause =
            RESET_CAUSE_PIN;
    }
    else if(
        (g_resetFlags & RCC_CSR_LPWRRSTF) != 0U
    )
    {
        g_resetCause =
            RESET_CAUSE_LOW_POWER;
    }
    else
    {
        g_resetCause =
            RESET_CAUSE_UNKNOWN;
    }


    /*
     * Clear all reset flags.
     *
     * RMVF is the reset-flag clear bit.
     */
    RCC->CSR |= RCC_CSR_RMVF;
}


/*
 * ============================================================
 * GET PRIMARY RESET CAUSE
 * ============================================================
 */

ResetCause_t Reset_Cause_Get(void)
{
    return g_resetCause;
}


/*
 * ============================================================
 * GET RAW FLAGS
 * ============================================================
 */

uint32_t Reset_Cause_GetRawFlags(void)
{
    return g_resetFlags;
}


/*
 * ============================================================
 * CHECK IWDG RESET
 * ============================================================
 */

uint8_t Reset_Cause_IsIWDG(void)
{
    if(
        (g_resetFlags & RCC_CSR_IWDGRSTF) != 0U
    )
    {
        return 1U;
    }

    return 0U;
}
