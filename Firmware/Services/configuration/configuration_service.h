#ifndef RFID_CONTROLLER_CONFIGURATION_SERVICE_H_
#define RFID_CONTROLLER_CONFIGURATION_SERVICE_H_

#include <stdint.h>

#define CONFIGURATION_SERVICE_MAX_JSON 1400U

void Configuration_Service_Init(void);

/* Build the live ActiveModeJson document. */
uint16_t Configuration_Service_BuildActiveJson(
    char *buffer,
    uint16_t bufferSize
);

/* Build the live StaticModelJson document. */
uint16_t Configuration_Service_BuildStaticJson(
    char *buffer,
    uint16_t bufferSize
);

/* 1 = active upload mode, 0 = passive. */
uint8_t Configuration_Service_GetWorkState(void);

#endif /* RFID_CONTROLLER_CONFIGURATION_SERVICE_H_ */
