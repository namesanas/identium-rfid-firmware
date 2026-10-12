#ifndef RFID_CONTROLLER_CRITICAL_SECTION_H_
#define RFID_CONTROLLER_CRITICAL_SECTION_H_

#include <stdint.h>
#include "../Config/firmware_config.h"

#if (TARGET_BOARD == BOARD_F429_DISCOVERY)

#include "stm32f429xx.h"

#elif (TARGET_BOARD == BOARD_F411_CUSTOM)

#include "stm32f411xe.h"

#else

#error "Unsupported TARGET_BOARD"

#endif


static inline uint32_t CriticalSection_Enter(void)
{
    uint32_t primask;

    primask = __get_PRIMASK();

    __disable_irq();

    return primask;
}


static inline void CriticalSection_Exit(uint32_t primask)
{
    __set_PRIMASK(primask);
}

#endif /* RFID_CONTROLLER_CRITICAL_SECTION_H_ */
