#ifndef RFID_CONTROLLER_CONFIGURATION_STORAGE_H_
#define RFID_CONTROLLER_CONFIGURATION_STORAGE_H_

#include <stdint.h>

#define CONFIGURATION_MAX_EVENTS                 6U
#define CONFIGURATION_MAX_CUS_PARAM_LENGTH       64U

/*
 * Persistent Identium configuration owned by the F429 prototype.
 * Only fields that have a real runtime consumer in Step 8 belong here.
 */
typedef struct
{
    uint32_t heartbeatSeconds;
    uint32_t sBufSize;
    uint32_t recvTimeoutSeconds;
    uint32_t clearBufferTimeSeconds;
    uint32_t dataAggregationTimeMs;
    uint32_t networkWatchdogMinutes;
    uint32_t inventoryWatchdogMinutes;

    uint8_t startInventoryAtBoot;
    uint8_t remoteCmdReply;
    uint8_t clientAck;
    uint8_t crcEnable;
    uint8_t dataAggregationMode;
    uint8_t hwInf;
    uint8_t session;

    uint8_t eventCount;
    uint8_t events[CONFIGURATION_MAX_EVENTS];

    char cusParam[CONFIGURATION_MAX_CUS_PARAM_LENGTH + 1U];
} Configuration_StorageData_t;

void Configuration_Storage_GetDefaults(Configuration_StorageData_t *data);
uint8_t Configuration_Storage_Load(Configuration_StorageData_t *data);
uint8_t Configuration_Storage_Save(const Configuration_StorageData_t *data);

#endif /* RFID_CONTROLLER_CONFIGURATION_STORAGE_H_ */
