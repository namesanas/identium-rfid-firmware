/*
 * fault_record.h
 *
 * Post-mortem record of CPU faults.
 *
 * The Cortex-M fault handlers (HardFault, MemManage, BusFault,
 * UsageFault, NMI) save the faulting PC, LR, xPSR and the fault status
 * registers into RAM that start-up code does not clear, then reset the
 * MCU at once instead of spinning until the watchdog fires. After the
 * reset, Fault_Record_Init() takes the record over so the host can read
 * why the previous run ended (GET_DIAG).
 *
 * A record survives every reset except a power cycle: power-on leaves
 * random RAM contents, which fail the magic / check-word test.
 */

#ifndef RFID_CONTROLLER_FAULT_RECORD_H_
#define RFID_CONTROLLER_FAULT_RECORD_H_

#include <stdint.h>

/* Fault types. Plain numbers: FAULT_RECORD_ENTER() pastes them into
 * assembly. */
#define FAULT_TYPE_NONE           0
#define FAULT_TYPE_HARD           1
#define FAULT_TYPE_MEM_MANAGE     2
#define FAULT_TYPE_BUS            3
#define FAULT_TYPE_USAGE          4
#define FAULT_TYPE_NMI            5
#define FAULT_TYPE_ERROR_HANDLER  6

typedef struct
{
    uint32_t type;
    uint32_t pc;        /* stacked PC (faulting instruction), 0 if unknown */
    uint32_t lr;        /* stacked LR (caller), 0 if unknown */
    uint32_t psr;       /* stacked xPSR */
    uint32_t sp;        /* stack pointer at fault entry */
    uint32_t cfsr;      /* SCB->CFSR */
    uint32_t hfsr;      /* SCB->HFSR */
    uint32_t mmfar;     /* SCB->MMFAR (valid when CFSR.MMARVALID) */
    uint32_t bfar;      /* SCB->BFAR  (valid when CFSR.BFARVALID) */
    uint32_t uptimeMs;  /* time since start-up when the fault occurred */
} FaultRecord_t;

/*
 * Call once, early in start-up. Takes over a record left by the
 * previous run and enables the separate MemManage / BusFault /
 * UsageFault exceptions so faults are reported by their real type.
 */
void Fault_Record_Init(void);

/* Copies the record of the previous run. Returns 1 when the previous
 * run ended in a fault, 0 otherwise. */
uint8_t Fault_Record_GetLast(FaultRecord_t *record);

/* Faults since the last power-on (survives fault resets). */
uint32_t Fault_Record_GetCount(void);

const char *Fault_Record_TypeName(uint32_t type);

/*
 * Error_Handler() path: records the caller and returns. Error_Handler()
 * keeps its halt behaviour; the record is read after the next reset.
 */
void Fault_Record_SaveErrorHandler(uint32_t callerAddress);

/*
 * Entered from FAULT_RECORD_ENTER() with r0 = exception frame,
 * r1 = fault type, already running on the private fault stack.
 * Saves the record and resets the MCU.
 */
void Fault_Record_Capture(const uint32_t *frame, uint32_t type)
    __attribute__((noreturn));

/* Size of the private stack Fault_Record_Capture() runs on. */
#define FAULT_RECORD_STACK_BYTES  512

#define FAULT_RECORD_STR_(x) #x
#define FAULT_RECORD_STR(x)  FAULT_RECORD_STR_(x)

/*
 * Body of a naked fault handler. Picks the active stack from EXC_RETURN,
 * switches to the private fault stack (the main stack may be the reason
 * for the fault) and branches to Fault_Record_Capture().
 */
#define FAULT_RECORD_ENTER(type)                                        \
    __asm volatile(                                                     \
        "tst   lr, #4                                     \n"           \
        "ite   eq                                         \n"           \
        "mrseq r0, msp                                    \n"           \
        "mrsne r0, psp                                    \n"           \
        "movs  r1, #" FAULT_RECORD_STR(type) "            \n"           \
        "movw  r2, #:lower16:fault_record_stack           \n"           \
        "movt  r2, #:upper16:fault_record_stack           \n"           \
        "add   r2, r2, #" FAULT_RECORD_STR(FAULT_RECORD_STACK_BYTES) "\n" \
        "mov   sp, r2                                     \n"           \
        "b     Fault_Record_Capture                       \n")

#endif /* RFID_CONTROLLER_FAULT_RECORD_H_ */
