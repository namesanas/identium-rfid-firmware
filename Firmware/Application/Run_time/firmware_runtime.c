#include "firmware_runtime.h"

#include "../configuration/firmware_configuration.h"

Status_t Firmware_Runtime_Init(void)
{
    return Firmware_Configuration_Init();
}
