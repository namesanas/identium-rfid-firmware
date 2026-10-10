#include "stack_monitor.h"

#include "stm32f4xx.h"

#define STACK_MONITOR_FILL        0xA5A5A5A5UL
#define STACK_MONITOR_GUARD_BYTES 32U

/* Linker-script symbols: top of RAM and the stack reserve size. */
extern uint32_t _estack;
extern uint32_t _Min_Stack_Size;

static uint32_t g_guardBase = 0U;   /* bottom of the reserve */
static uint32_t g_stackTop = 0U;


static void Stack_Monitor_EnableGuard(uint32_t base)
{
    MPU->CTRL = 0U;

    MPU->RNR = 0U;
    MPU->RBAR = base & MPU_RBAR_ADDR_Msk;

    /* 32 bytes (SIZE = 4), no access, never executable. */
    MPU->RASR = (4UL << MPU_RASR_SIZE_Pos)
              | (0UL << MPU_RASR_AP_Pos)
              | MPU_RASR_XN_Msk
              | MPU_RASR_ENABLE_Msk;

    /* Default memory map everywhere else. */
    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;

    __DSB();
    __ISB();
}


void Stack_Monitor_Init(void)
{
    volatile uint32_t *word;
    uint32_t fillEnd;

    g_stackTop = (uint32_t)&_estack;

    /* MPU regions are aligned to their size. */
    g_guardBase =
        ((g_stackTop - (uint32_t)&_Min_Stack_Size)
         + (STACK_MONITOR_GUARD_BYTES - 1U))
        & ~(STACK_MONITOR_GUARD_BYTES - 1U);

    /* Paint from just above the guard up to a margin below the current
     * stack pointer (this function's own frame lives above it). */
    fillEnd = __get_MSP() - 64U;

    for(
        word = (volatile uint32_t *)(g_guardBase + STACK_MONITOR_GUARD_BYTES);
        (uint32_t)word < fillEnd;
        word++
    )
    {
        *word = STACK_MONITOR_FILL;
    }

    Stack_Monitor_EnableGuard(g_guardBase);
}


uint32_t Stack_Monitor_GetSize(void)
{
    if(g_stackTop == 0U)
    {
        return 0U;
    }

    return g_stackTop - (g_guardBase + STACK_MONITOR_GUARD_BYTES);
}


uint32_t Stack_Monitor_GetUsed(void)
{
    const volatile uint32_t *word;

    if(g_stackTop == 0U)
    {
        return 0U;
    }

    word = (const volatile uint32_t *)(g_guardBase + STACK_MONITOR_GUARD_BYTES);

    while(((uint32_t)word < g_stackTop) && (*word == STACK_MONITOR_FILL))
    {
        word++;
    }

    return g_stackTop - (uint32_t)word;
}
