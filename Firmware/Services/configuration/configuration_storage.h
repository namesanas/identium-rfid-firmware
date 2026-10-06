#ifndef RFID_CONTROLLER_CONFIGURATION_STORAGE_H_
#define RFID_CONTROLLER_CONFIGURATION_STORAGE_H_

#include <stdint.h>

/* Persistent configuration deliberately kept small in this step. */
typedef struct
{
    uint32_t heartbeatSeconds;
    uint8_t startInventoryAtBoot;
    uint8_t session;
    uint8_t reserved[2];
} Configuration_StorageData_t;

void Configuration_Storage_GetDefaults(Configuration_StorageData_t *data);
uint8_t Configuration_Storage_Load(Configuration_StorageData_t *data);
uint8_t Configuration_Storage_Save(const Configuration_StorageData_t *data);

#endif /* RFID_CONTROLLER_CONFIGURATION_STORAGE_H_ */
