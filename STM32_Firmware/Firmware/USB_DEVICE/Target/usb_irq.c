#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_pcd.h"

extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

void OTG_HS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS);
}
