/*
 * rcc.h
 *
 *  Created on: 26-Sept-2026
 *      Author: Identium
 */

#ifndef RCC_RCC_H_
#define RCC_RCC_H_



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


#endif /* RCC_RCC_H_ */
