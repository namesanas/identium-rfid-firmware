#include "diagnostics.h"

#include <stdio.h>
#include <string.h>

#include "brownout.h"
#include "fault_record.h"
#include "loop_monitor.h"
#include "reset_cause.h"
#include "stack_monitor.h"
#include "../../Services/Time/time_service.h"


static const char *Diagnostics_ResetName(ResetCause_t cause)
{
    switch(cause)
    {
        case RESET_CAUSE_LOW_POWER: return "LOW_POWER";
        case RESET_CAUSE_WWDG:      return "WWDG";
        case RESET_CAUSE_IWDG:      return "IWDG";
        case RESET_CAUSE_SOFTWARE:  return "SOFTWARE";
        case RESET_CAUSE_BROWN_OUT: return "BROWN_OUT";
        case RESET_CAUSE_POWER_ON:  return "POWER_ON";
        case RESET_CAUSE_PIN:       return "PIN";
        default:                    return "UNKNOWN";
    }
}


/* Appends with snprintf; keeps *pos within the buffer on truncation. */
static void Diagnostics_Append(
    char *buffer,
    uint16_t size,
    uint16_t *pos,
    int written
)
{
    if(written < 0)
    {
        return;
    }

    if((uint32_t)*pos + (uint32_t)written >= size)
    {
        *pos = (uint16_t)(size - 1U);
    }
    else
    {
        *pos = (uint16_t)(*pos + (uint16_t)written);
    }
}


uint16_t Diagnostics_Format(char *buffer, uint16_t size)
{
    FaultRecord_t fault;
    uint16_t pos = 0U;
    uint32_t task;

    if((buffer == 0) || (size < 4U))
    {
        return 0U;
    }

    Diagnostics_Append(buffer, size, &pos,
        snprintf(&buffer[pos], size - pos,
            "DIAG,RESET=%s,UPTIME_MS=%lu,FAULTS=%lu",
            Diagnostics_ResetName(Reset_Cause_Get()),
            (unsigned long)Time_GetTickMs(),
            (unsigned long)Fault_Record_GetCount()));

    if(Fault_Record_GetLast(&fault) != 0U)
    {
        Diagnostics_Append(buffer, size, &pos,
            snprintf(&buffer[pos], size - pos,
                ",LAST_FAULT=%s,PC=0x%08lX,LR=0x%08lX,CFSR=0x%08lX,"
                "HFSR=0x%08lX,MMFAR=0x%08lX,BFAR=0x%08lX,AT_MS=%lu",
                Fault_Record_TypeName(fault.type),
                (unsigned long)fault.pc,
                (unsigned long)fault.lr,
                (unsigned long)fault.cfsr,
                (unsigned long)fault.hfsr,
                (unsigned long)fault.mmfar,
                (unsigned long)fault.bfar,
                (unsigned long)fault.uptimeMs));
    }
    else
    {
        Diagnostics_Append(buffer, size, &pos,
            snprintf(&buffer[pos], size - pos, ",LAST_FAULT=NONE"));
    }

    Diagnostics_Append(buffer, size, &pos,
        snprintf(&buffer[pos], size - pos,
            ",LOOP_MAX_US=%lu,PASSES=%lu",
            (unsigned long)Loop_Monitor_GetPassMaxUs(),
            (unsigned long)Loop_Monitor_GetPassCount()));

    for(task = 0U; task < (uint32_t)LOOP_TASK_COUNT; task++)
    {
        Diagnostics_Append(buffer, size, &pos,
            snprintf(&buffer[pos], size - pos,
                ",%s_US=%lu",
                Loop_Monitor_GetTaskName((LoopTask_t)task),
                (unsigned long)Loop_Monitor_GetTaskMaxUs((LoopTask_t)task)));
    }

    Diagnostics_Append(buffer, size, &pos,
        snprintf(&buffer[pos], size - pos,
            ",STACK_USED=%lu,STACK_SIZE=%lu,BOR=%u\r\n",
            (unsigned long)Stack_Monitor_GetUsed(),
            (unsigned long)Stack_Monitor_GetSize(),
            (unsigned int)Brownout_GetLevel()));

    return pos;
}


void Diagnostics_Clear(void)
{
    Loop_Monitor_Clear();
}


#ifdef DEBUG
/* Burns 128 bytes of stack per level until the MPU guard is hit. */
static uint32_t Diagnostics_Recurse(uint32_t depth)
{
    volatile uint8_t pad[128];

    pad[0] = (uint8_t)depth;

    if(depth > 100000U)
    {
        return pad[0];
    }

    return Diagnostics_Recurse(depth + 1U) + pad[0];
}


uint8_t Diagnostics_TriggerFault(const char *kind)
{
    if(strcmp(kind, "USAGE") == 0)
    {
        __builtin_trap();
    }
    else if(strcmp(kind, "BUS") == 0)
    {
        /* SRAM region, past the end of the F4's 192 KB of SRAM. */
        (void)*(volatile uint32_t *)0x2FFFFFF0UL;
    }
    else if(strcmp(kind, "STACK") == 0)
    {
        (void)Diagnostics_Recurse(0U);
    }

    return 0U;
}
#endif
