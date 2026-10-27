/*
 * boot_startup.c
 *
 * Vector table and reset handler. Only the 16 core exceptions: the
 * bootloader enables no peripheral interrupts. Any fault resets the
 * MCU.
 */

#include "stm32f4xx.h"

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

int main(void);

void Reset_Handler(void);
static void Boot_FaultHandler(void);

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    uint32_t *dst;

    for(dst = &_sdata; dst < &_edata; dst++, src++)
        *dst = *src;

    for(dst = &_sbss; dst < &_ebss; dst++)
        *dst = 0U;

    (void)main();

    for(;;)
    {
    }
}

static void Boot_FaultHandler(void)
{
    NVIC_SystemReset();
}

__attribute__((section(".isr_vector"), used))
static void (*const g_bootVectors[16])(void) =
{
    (void (*)(void))&_estack,
    Reset_Handler,
    Boot_FaultHandler,      /* NMI */
    Boot_FaultHandler,      /* HardFault */
    Boot_FaultHandler,      /* MemManage */
    Boot_FaultHandler,      /* BusFault */
    Boot_FaultHandler,      /* UsageFault */
    0, 0, 0, 0,
    Boot_FaultHandler,      /* SVCall */
    Boot_FaultHandler,      /* DebugMon */
    0,
    Boot_FaultHandler,      /* PendSV */
    Boot_FaultHandler       /* SysTick */
};
