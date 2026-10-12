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

#endif /* RFID_CONTROLLER_SYSTEM_FAULT_H_ */
