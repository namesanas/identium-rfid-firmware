#include "fault_record.h"

#include "stm32f4xx.h"

#include "../../Services/Time/time_service.h"

#define FAULT_RECORD_MAGIC       0x464C5452UL   /* "FLTR" */

/* Kept across resets: placed in .noinit, which start-up does not clear. */
typedef struct
{
    uint32_t magic;
    FaultRecord_t record;
    uint32_t pending;    /* 1 = record not yet taken over after a reset */
    uint32_t count;      /* faults since power-on */
    uint32_t check;      /* ~magic ^ count, guards against random RAM */
} FaultRecordStore_t;

static FaultRecordStore_t g_store __attribute__((section(".noinit")));

static FaultRecord_t g_lastRecord;
static uint8_t g_lastValid = 0U;
static uint32_t g_faultCount = 0U;

/* Private stack for the capture path. Global: referenced from the
 * FAULT_RECORD_ENTER() assembly. */
uint32_t fault_record_stack[FAULT_RECORD_STACK_BYTES / 4]
    __attribute__((aligned(8)));

/* Linker-script symbols bounding main RAM. */
extern uint32_t _sdata;
extern uint32_t _estack;


static uint8_t Fault_Record_StoreValid(void)
{
    return (uint8_t)(
        (g_store.magic == FAULT_RECORD_MAGIC)
        &&
        (g_store.check == (~FAULT_RECORD_MAGIC ^ g_store.count))
    );
}


static void Fault_Record_Seal(void)
{
    g_store.magic = FAULT_RECORD_MAGIC;
    g_store.check = ~FAULT_RECORD_MAGIC ^ g_store.count;
}


static void Fault_Record_Save(FaultRecord_t *record)
{
    if(Fault_Record_StoreValid() == 0U)
    {
        g_store.count = 0U;
    }

    record->cfsr = SCB->CFSR;
    record->hfsr = SCB->HFSR;
    record->mmfar = SCB->MMFAR;
    record->bfar = SCB->BFAR;
    record->uptimeMs = Time_GetTickMs();

    g_store.record = *record;
    g_store.pending = 1U;
    g_store.count++;

    Fault_Record_Seal();
}


void Fault_Record_Init(void)
{
    if(Fault_Record_StoreValid() != 0U)
    {
        g_faultCount = g_store.count;

        if(g_store.pending != 0U)
        {
            g_lastRecord = g_store.record;
            g_lastValid = 1U;
            g_store.pending = 0U;
        }
    }
    else
    {
        /* Power-on: RAM holds random data. */
        g_store.count = 0U;
        g_store.pending = 0U;
        Fault_Record_Seal();
    }

    /* Report MemManage / BusFault / UsageFault as themselves instead of
     * escalating every fault to HardFault. */
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk
                | SCB_SHCSR_BUSFAULTENA_Msk
                | SCB_SHCSR_USGFAULTENA_Msk;
}


uint8_t Fault_Record_GetLast(FaultRecord_t *record)
{
    if((g_lastValid == 0U) || (record == 0))
    {
        return 0U;
    }

    *record = g_lastRecord;

    return 1U;
}


uint32_t Fault_Record_GetCount(void)
{
    return g_faultCount;
}


const char *Fault_Record_TypeName(uint32_t type)
{
    switch(type)
    {
        case FAULT_TYPE_HARD:          return "HARDFAULT";
        case FAULT_TYPE_MEM_MANAGE:    return "MEMMANAGE";
        case FAULT_TYPE_BUS:           return "BUSFAULT";
        case FAULT_TYPE_USAGE:         return "USAGEFAULT";
        case FAULT_TYPE_NMI:           return "NMI";
        case FAULT_TYPE_ERROR_HANDLER: return "ERROR_HANDLER";
        default:                       return "NONE";
    }
}


void Fault_Record_SaveErrorHandler(uint32_t callerAddress)
{
    FaultRecord_t record = {0};

    record.type = FAULT_TYPE_ERROR_HANDLER;
    record.lr = callerAddress;

    Fault_Record_Save(&record);
}


void Fault_Record_Capture(const uint32_t *frame, uint32_t type)
{
    FaultRecord_t record = {0};
    uint32_t frameAddress = (uint32_t)frame;
    uint32_t stackingFailed;

    record.type = type;
    record.sp = frameAddress;

    /* The frame can only be read when the hardware managed to push it,
     * and it lies in RAM (a corrupted SP may point anywhere). */
    stackingFailed = SCB->CFSR & (SCB_CFSR_MSTKERR_Msk | SCB_CFSR_STKERR_Msk);

    if(
        (stackingFailed == 0U)
        &&
        ((frameAddress & 3U) == 0U)
        &&
        (frameAddress >= (uint32_t)&_sdata)
        &&
        (frameAddress <= ((uint32_t)&_estack - 32U))
    )
    {
        record.lr = frame[5];
        record.pc = frame[6];
        record.psr = frame[7];
    }

    Fault_Record_Save(&record);

    __DSB();
    NVIC_SystemReset();

    for(;;)
    {
    }
}
