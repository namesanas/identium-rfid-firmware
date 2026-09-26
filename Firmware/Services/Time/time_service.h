#ifndef RFID_CONTROLLER_TIME_SERVICE_H_
#define RFID_CONTROLLER_TIME_SERVICE_H_

#include <stdint.h>

void Time_Service_Init(void);
uint32_t Time_GetTickMs(void);
void Time_DelayMs(uint32_t delayMs);

#endif /* RFID_CONTROLLER_TIME_SERVICE_H_ */
