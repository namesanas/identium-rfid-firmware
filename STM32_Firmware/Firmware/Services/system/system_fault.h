#ifndef RFID_CONTROLLER_SYSTEM_FAULT_H_
#define RFID_CONTROLLER_SYSTEM_FAULT_H_

#include <stdint.h>

#include "../../Common/error_codes.h"

void System_Fault_Init(void);

void System_Fault_Set(
    ErrorCode_t error
);

void System_Fault_Clear(void);

ErrorCode_t System_Fault_Get(void);

ErrorCode_t System_Fault_GetLast(void);

uint32_t System_Fault_GetCount(void);

uint32_t System_Fault_GetTimestamp(void);

/*
 * Degraded start-up: parts that failed to start. The reader keeps
 * running without them (and retries where it can) instead of halting.
 */
#define SYSTEM_DEGRADED_RFID      0x01U
#define SYSTEM_DEGRADED_ETHERNET  0x02U
#define SYSTEM_DEGRADED_WATCHDOG  0x04U

void System_Fault_SetDegraded(uint8_t part);
void System_Fault_ClearDegraded(uint8_t part);
uint8_t System_Fault_GetDegraded(void);

#endif /* RFID_CONTROLLER_SYSTEM_FAULT_H_ */
