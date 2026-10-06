#ifndef RFID_CONTROLLER_CONFIGURATION_SERVICE_H_
#define RFID_CONTROLLER_CONFIGURATION_SERVICE_H_

#include <stdint.h>

#define CONFIGURATION_SERVICE_MAX_JSON 1400U

#define CONFIGURATION_SERVICE_ERR_OK            0U
#define CONFIGURATION_SERVICE_ERR_UNSUPPORTED  2U
#define CONFIGURATION_SERVICE_ERR_INVALID      3U
#define CONFIGURATION_SERVICE_ERR_EXECUTION    3U

void Configuration_Service_Init(void);

/* Load the persistent configuration after readerConfig defaults are set. */
uint8_t Configuration_Service_LoadPersistent(void);

/* Runtime values currently owned by the Identium configuration service. */
uint32_t Configuration_Service_GetHeartbeatSeconds(void);
uint8_t Configuration_Service_GetStartInventoryAtBoot(void);

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

/* Apply a permanent ActiveModeJson partial update. */
uint8_t Configuration_Service_ApplyRuntimeConfig(
    const char *json,
    uint32_t *errorCode,
    const char **errorString
);

/* Apply a permanent StaticModelJson partial update. */
uint8_t Configuration_Service_ApplyStaticConfig(
    const char *json,
    uint32_t *errorCode,
    const char **errorString,
    uint8_t *rebootRequested
);

/* Apply a live, non-persistent StaticModelJson subset. */
uint8_t Configuration_Service_ApplyTempStaticConfig(
    const char *json,
    uint32_t *errorCode,
    const char **errorString
);

#endif /* RFID_CONTROLLER_CONFIGURATION_SERVICE_H_ */
