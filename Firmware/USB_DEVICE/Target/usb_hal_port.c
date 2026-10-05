#include "stm32f4xx_hal.h"

#include "../../Services/Time/time_service.h"

/*
 * The RFID firmware already owns SysTick/timekeeping and the system clock.
 * These small HAL compatibility hooks let the USB PCD layer use that same
 * infrastructure without calling HAL_Init() or HAL_RCC_ClockConfig().
 */
uint32_t HAL_GetTick(void)
{
    return Time_GetTickMs();
}

void HAL_Delay(uint32_t Delay)
{
    Time_DelayMs(Delay);
}

