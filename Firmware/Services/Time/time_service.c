#include "time_service.h"
#include "stm32f429xx.h"

#define TIME_SYST_CSR (*(volatile uint32_t *)0xE000E010UL)
#define TIME_SYST_RVR (*(volatile uint32_t *)0xE000E014UL)
#define TIME_SYST_CVR (*(volatile uint32_t *)0xE000E018UL)

#define TIME_SYST_CSR_ENABLE    (1UL << 0)
#define TIME_SYST_CSR_TICKINT   (1UL << 1)
#define TIME_SYST_CSR_CLKSOURCE (1UL << 2)

static volatile uint32_t g_timeTickMs = 0U;

void Time_Service_Init(void)
{
    uint32_t coreClockHz;

    coreClockHz = SystemCoreClock;

    if(coreClockHz < 1000U)
    {
        coreClockHz = 16000000UL;
    }

    g_timeTickMs = 0U;

    TIME_SYST_RVR = (coreClockHz / 1000U) - 1U;
    TIME_SYST_CVR = 0U;
    TIME_SYST_CSR = TIME_SYST_CSR_CLKSOURCE |
                    TIME_SYST_CSR_TICKINT |
                    TIME_SYST_CSR_ENABLE;
}

uint32_t Time_GetTickMs(void)
{
    return g_timeTickMs;
}

void Time_DelayMs(uint32_t delayMs)
{
    uint32_t startTick;

    startTick = g_timeTickMs;

    while((g_timeTickMs - startTick) < delayMs)
    {
        /* Wait for the SysTick interrupt to advance the time base. */
    }
}

void Time_Service_SysTick_Handler(void)
{
	g_timeTickMs++;
}
