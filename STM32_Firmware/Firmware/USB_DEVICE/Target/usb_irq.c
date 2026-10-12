#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_pcd.h"

#if defined(STM32F411xE)
extern PCD_HandleTypeDef hpcd_USB_OTG_FS;

void OTG_FS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
}
#else
extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

void OTG_HS_IRQHandler(void)
{
    HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS);
}
#endif
