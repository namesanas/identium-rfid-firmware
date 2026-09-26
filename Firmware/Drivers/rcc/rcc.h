#ifndef RFID_CONTROLLER_DRIVER_RCC_H
#define RFID_CONTROLLER_DRIVER_RCC_H

#include <stdint.h>
#include "../../Config/firmware_config.h"

#if (TARGET_BOARD == BOARD_F429_DISCOVERY)
#include "stm32f429xx.h"
#elif (TARGET_BOARD == BOARD_F411_CUSTOM)
#include "stm32f411xe.h"
#else
#error "Unsupported TARGET_BOARD"
#endif

#ifndef BOARD_HSE_FREQUENCY_HZ
#define BOARD_HSE_FREQUENCY_HZ 8000000UL
#endif

uint32_t RCC_GetPLLOutputClock(void);
uint32_t RCC_GetSystemClockValue(void);
uint32_t RCC_GetPCLK1Value(void);
uint32_t RCC_GetPCLK2Value(void);

#endif /* RFID_CONTROLLER_DRIVER_RCC_H */
