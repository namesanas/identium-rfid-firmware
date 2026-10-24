#include "system_fault.h"

#include "../../Services/Time/time_service.h"

static volatile ErrorCode_t g_currentFault = ERROR_NONE;
static volatile ErrorCode_t g_lastFault = ERROR_NONE;

static volatile uint32_t g_faultCount = 0U;
static volatile uint32_t g_faultTimestamp = 0U;

static volatile uint8_t g_degraded = 0U;


void System_Fault_Init(void)
{
    g_currentFault = ERROR_NONE;
    g_lastFault = ERROR_NONE;

    g_faultCount = 0U;
    g_faultTimestamp = 0U;

    g_degraded = 0U;
}


void System_Fault_Set(
    ErrorCode_t error
)
{
    if(error == ERROR_NONE)
    {
        return;
    }

    g_currentFault = error;
    g_lastFault = error;

    g_faultCount++;

    g_faultTimestamp =
        Time_GetTickMs();
}


void System_Fault_Clear(void)
{
    g_currentFault = ERROR_NONE;
}


ErrorCode_t System_Fault_Get(void)
{
    return g_currentFault;
}


ErrorCode_t System_Fault_GetLast(void)
{
    return g_lastFault;
}


uint32_t System_Fault_GetCount(void)
{
    return g_faultCount;
}


uint32_t System_Fault_GetTimestamp(void)
{
    return g_faultTimestamp;
}


void System_Fault_SetDegraded(uint8_t part)
{
    g_degraded = (uint8_t)(g_degraded | part);
}


void System_Fault_ClearDegraded(uint8_t part)
{
    g_degraded = (uint8_t)(g_degraded & (uint8_t)~part);
}


uint8_t System_Fault_GetDegraded(void)
{
    return g_degraded;
}
