/* Forced include for hostcheck/firmware_update_host_test.c: the
 * bootloader info block lives in RAM instead of at 0x08000100. */
#ifndef HOSTCHECK_FW_UPDATE_HOST_H
#define HOSTCHECK_FW_UPDATE_HOST_H

#include "../../Firmware/Common/firmware_image.h"

extern FwBoot_Info_t hostcheckBootInfo;

#define FW_UPDATE_BOOT_INFO (&hostcheckBootInfo)

#endif
