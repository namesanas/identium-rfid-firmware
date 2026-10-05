# USB CDC / HAL build fix

## Root cause fixed
The project had a duplicate STM32F4xx HAL tree under `Firmware/Drivers/STM32F4xx_HAL_Driver`
and a stale CubeIDE folder configuration that generated a separate build group for that tree.
The canonical HAL is `Drivers/STM32F4xx_HAL_Driver` and now owns the USB PCD/LL sources.

The canonical USB-related HAL sources also had the low-level headers included before
`stm32f4xx_hal.h`, which exposed a recursive include path through `stm32f4xx_hal_def.h`
and caused `HAL_StatusTypeDef` / `HAL_LockTypeDef` to be undefined. The sources now follow
ST's normal pattern with `stm32f4xx_hal.h` first.

## Files changed
- `Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pcd.c`
- `Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_usb.c`
- `Firmware/USB_DEVICE/Target/usbd_conf.c`
- `.cproject`
- Removed duplicate `Firmware/Drivers/STM32F4xx_HAL_Driver/`
- Removed generated `Debug/`

## Intentionally not changed
- `Inc/stm32f4xx_hal_conf.h` include-order hacks were not added.
- System clock configuration was not changed yet; that is a separate runtime/USB-clock checkpoint.
