#ifndef FIRMWARE_CONFIGURATION_H_
#define FIRMWARE_CONFIGURATION_H_

#include <stdint.h>
#include "../../Common/status.h"
Status_t  Firmware_Configuration_Init(void);

/* Main-loop task: retries the RFID module bring-up while it is degraded. */
void Firmware_Configuration_Task(void);

#endif /* FIRMWARE_CONFIGURATION_H_ */
