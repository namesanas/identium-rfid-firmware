/*
 * watchdog.c
 *
 *  Created on: 28-Sept-2026
 *      Author: Identium
 */

#include "watchdog.h"

#include "stm32f429xx.h"

#include "../Time/time_service.h"


static volatile uint8_t g_watchdogInitialized =
    0U;


/*
 * ============================================================
 * INITIALIZE IWDG
 * ============================================================
 */

uint8_t Watchdog_Init(void)
{
    uint32_t startTime;


    /*
     * --------------------------------------------------------
     * Enable LSI
     * --------------------------------------------------------
     *
     * IWDG uses LSI as its clock source.
     * Explicitly enable it so that we can verify readiness.
     * --------------------------------------------------------
     */

    RCC->CSR |= RCC_CSR_LSION;


    /*
     * Wait for LSI to become ready.
     *
     * A failure here means the independent clock source did
     * not become ready within the startup timeout.
     */

    startTime =
        Time_GetTickMs();

    while(
        (RCC->CSR & RCC_CSR_LSIRDY) == 0U
    )
    {
        if(
            (Time_GetTickMs() - startTime)
            >= 100U
        )
        {
            return 0U;
        }
    }


    /*
     * --------------------------------------------------------
     * Enable IWDG
     * --------------------------------------------------------
     *
     * 0xCCCC starts the independent watchdog.
     * --------------------------------------------------------
     */

    IWDG->KR =
        0xCCCCU;


    /*
     * --------------------------------------------------------
     * Enable access to IWDG configuration registers
     * --------------------------------------------------------
     */

    IWDG->KR =
        0x5555U;


    /*
     * --------------------------------------------------------
     * Set prescaler
     * --------------------------------------------------------
     *
     * PR = 6 -> divide LSI by 256.
     * --------------------------------------------------------
     */

    IWDG->PR =
        WATCHDOG_IWDG_PRESCALER;


    /*
     * --------------------------------------------------------
     * Set maximum reload value
     * --------------------------------------------------------
     */

    IWDG->RLR =
        WATCHDOG_IWDG_RELOAD;


    /*
     * --------------------------------------------------------
     * Wait until prescaler/reload updates complete.
     * --------------------------------------------------------
     */

    startTime =
        Time_GetTickMs();

    while(
        IWDG->SR != 0U
    )
    {
        if(
            (Time_GetTickMs() - startTime)
            >= 100U
        )
        {
            return 0U;
        }
    }


    /*
     * --------------------------------------------------------
     * Initial watchdog refresh
     * --------------------------------------------------------
     */

    IWDG->KR =
        0xAAAAU;


    g_watchdogInitialized =
        1U;


    return 1U;
}


/*
 * ============================================================
 * REFRESH WATCHDOG
 * ============================================================
 */

void Watchdog_Refresh(void)
{
    if(
        g_watchdogInitialized == 0U
    )
    {
        return;
    }


    /*
     * Reload the IWDG counter.
     */
    IWDG->KR =
        0xAAAAU;
}


/*
 * ============================================================
 * GET INITIALIZATION STATE
 * ============================================================
 */

uint8_t Watchdog_IsInitialized(void)
{
    return g_watchdogInitialized;
}
