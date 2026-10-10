#include "loop_monitor.h"

#include "stm32f4xx.h"

/* Longest durations in CPU cycles. The 32-bit counter wraps after ~25 s
 * at 168 MHz; any single task taking that long has hung and is caught
 * by the watchdog instead. */
static uint32_t g_taskMaxCycles[LOOP_TASK_COUNT];
static uint32_t g_passMaxCycles = 0U;
static uint32_t g_passStart = 0U;
static uint32_t g_passCount = 0U;

static const char *const g_taskNames[LOOP_TASK_COUNT] =
{
    "ETH",
    "CFG",
    "PROTO",
    "GPIO",
    "RFID",
    "HOST"
};


static uint32_t Loop_Monitor_CyclesToUs(uint32_t cycles)
{
    uint32_t cyclesPerUs = SystemCoreClock / 1000000U;

    if(cyclesPerUs == 0U)
    {
        return 0U;
    }

    return cycles / cyclesPerUs;
}


void Loop_Monitor_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    Loop_Monitor_Clear();
}


uint32_t Loop_Monitor_Now(void)
{
    return DWT->CYCCNT;
}


void Loop_Monitor_Record(LoopTask_t task, uint32_t startCycles)
{
    uint32_t elapsed = DWT->CYCCNT - startCycles;

    if((uint32_t)task >= (uint32_t)LOOP_TASK_COUNT)
    {
        return;
    }

    if(elapsed > g_taskMaxCycles[task])
    {
        g_taskMaxCycles[task] = elapsed;
    }
}


void Loop_Monitor_PassEnd(void)
{
    uint32_t now = DWT->CYCCNT;
    uint32_t elapsed = now - g_passStart;

    if(elapsed > g_passMaxCycles)
    {
        g_passMaxCycles = elapsed;
    }

    g_passStart = now;
    g_passCount++;
}


uint32_t Loop_Monitor_GetTaskMaxUs(LoopTask_t task)
{
    if((uint32_t)task >= (uint32_t)LOOP_TASK_COUNT)
    {
        return 0U;
    }

    return Loop_Monitor_CyclesToUs(g_taskMaxCycles[task]);
}


uint32_t Loop_Monitor_GetPassMaxUs(void)
{
    return Loop_Monitor_CyclesToUs(g_passMaxCycles);
}


uint32_t Loop_Monitor_GetPassCount(void)
{
    return g_passCount;
}


const char *Loop_Monitor_GetTaskName(LoopTask_t task)
{
    if((uint32_t)task >= (uint32_t)LOOP_TASK_COUNT)
    {
        return "?";
    }

    return g_taskNames[task];
}


void Loop_Monitor_Clear(void)
{
    uint32_t i;

    for(i = 0U; i < (uint32_t)LOOP_TASK_COUNT; i++)
    {
        g_taskMaxCycles[i] = 0U;
    }

    g_passMaxCycles = 0U;
    g_passCount = 0U;
    g_passStart = DWT->CYCCNT;
}
