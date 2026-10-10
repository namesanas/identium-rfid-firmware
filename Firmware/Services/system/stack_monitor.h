/*
 * stack_monitor.h
 *
 * Main-stack budget, overflow guard and high-water mark.
 *
 * The linker script reserves _Min_Stack_Size bytes below _estack for the
 * main stack, which the main loop and every interrupt share.
 * Stack_Monitor_Init():
 *  - fills the unused part of that reserve with a pattern, so the deepest
 *    point the stack has reached can be measured later;
 *  - makes the lowest 32 bytes of the reserve a no-access MPU region, so
 *    running past the budget raises a MemManage fault (saved by
 *    fault_record, then reset) instead of silently overwriting .bss.
 */

#ifndef RFID_CONTROLLER_STACK_MONITOR_H_
#define RFID_CONTROLLER_STACK_MONITOR_H_

#include <stdint.h>

void Stack_Monitor_Init(void);

/* Usable stack bytes (reserve minus the guard). */
uint32_t Stack_Monitor_GetSize(void);

/* Deepest stack use since start-up, in bytes. Scans the reserve; call
 * on request, not every loop pass. */
uint32_t Stack_Monitor_GetUsed(void);

#endif /* RFID_CONTROLLER_STACK_MONITOR_H_ */
