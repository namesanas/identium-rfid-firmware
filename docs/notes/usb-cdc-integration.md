# Native USB CDC integration — batched steps 2–6

This source snapshot is based on the current F429 firmware plus the already-tested CubeMX USB CDC stack.

## Batched changes

**Step 2 — Native RX path**
- CDC OUT callback forwards every received byte to `HOST_Transport_PushByte(HOST_TRANSPORT_USB, ...)`.
- RX OUT endpoint is always re-armed, even if the host RX queue is full.

**Step 3 — Native TX queue**
- Added `Services/USB/usb_cdc_transport.c/.h`.
- 4096-byte application TX queue.
- Full-speed CDC data is sent in 64-byte chunks.
- The queue is drained from task context, not from the USB ISR.
- USB TX completion callback releases the in-flight state.
- TX overflow is counted.

**Step 4 — Host transport output routing**
- `HOST_Transport_SendString()` now prefers native CDC once enumerated/configured.
- The existing F429 ST-LINK VCP path remains a fallback before native USB enumeration, preserving the existing prototype debug path.

**Step 5 — USB initialization**
- `USB_CDC_Transport_Init()` + `MX_USB_DEVICE_Init()` are called from `Firmware_Configuration_Init()` after `Board_F429_Init()`.
- CubeMX `main.c`, `HAL_Init()`, `SystemClock_Config()` and CubeMX GPIO init are intentionally NOT merged.

**Step 6 — Runtime service / IRQ**
- `USB_CDC_Transport_Task()` is called by the existing `HOST_Interface_Task()`.
- Dedicated `USB_DEVICE/Target/usb_irq.c` provides `OTG_HS_IRQHandler()` so the existing UART IRQ dispatch remains untouched.

## Files added

- `Services/USB/usb_cdc_transport.c`
- `Services/USB/usb_cdc_transport.h`
- `USB_DEVICE/App/*`
- `USB_DEVICE/Target/*`
- `Middlewares/ST/STM32_USB_Device_Library/*`
- Cube HAL driver package under `Drivers/STM32F4xx_HAL_Driver/*`

## Files modified from the RFID firmware

- `Application/configuration/firmware_configuration.c`
- `Application/Host/host_interface.c`
- `Services/host/host_transport.c`

## Important clock condition

The F429 firmware now provides the USB clock directly: 8 MHz HSE -> PLLM=8, PLLN=336, PLLP=2, PLLQ=7 -> 168 MHz SYSCLK and exact 48 MHz USB clock for OTG_HS with the internal FS PHY. The existing application remains the owner of the system clock; no second HAL_Init/SystemClock_Config is introduced.

## Important build note

The current `Firmware(5).zip` contains source modules but not the complete CubeIDE project/startup/linker files, so a full application link cannot be honestly claimed here. The supplied source tree has been structurally integrated, but the final CubeIDE build must be performed in the actual project.

## Final hardware/runtime test order

1. Build/flash the combined F429 application.
2. Connect native USB and confirm Windows creates the CDC COM port.
3. Send `START\r` -> `OK,START\r\n`.
4. Send `RELEASE\r` -> `OK,RELEASE\r\n`.
5. Reconnect and repeat START/RELEASE.
6. Run `SINGLE_POLL,1000\r` and verify response on native USB.
7. Start inventory and verify `TAG,...` messages on native USB.
8. Disconnect/reconnect USB and verify clean re-enumeration.
9. Test Ethernet separately to ensure its host path is unchanged.

FW-11 remains open until the combined application actually builds, flashes, enumerates as CDC, and passes these host protocol tests.

## Production USB identity

The descriptor is still the previously-tested ST CDC example identity. A released product should use the vendor/product identity authorized for the actual product.

## Additional integration hardening

- The host transport manager is initialized before the USB device stack starts, preventing an early CDC packet from entering an uninitialized transport state.
- The F429 ST-LINK VCP remains only a pre-enumeration fallback. Once native CDC is configured, USART1 RX no longer feeds the logical USB host path, avoiding two physical inputs sharing one transport while native USB is active.
- HAL USB source modules now include their own module headers directly. This keeps the intentionally minimal `stm32f4xx_hal_conf.h` isolated from the existing custom GPIO-driver macro namespace while allowing the USB HAL sources to compile normally.

## Source-level validation performed

Using the STM32F429 CMSIS device headers from the previously tested USB project as the validation include set, all 43 C translation units in this integrated source snapshot pass GCC `-fsyntax-only` with `STM32F429xx` and `USE_HAL_DRIVER` defined.

The compiler is the host x86 GCC in this environment, so Cortex-M pointer-size warnings are expected during this check. This is a syntax/translation-unit validation, not a replacement for the target CubeIDE compile and link.

## What remains outside this snapshot

The supplied RFID firmware archive does not contain the application's CubeIDE project metadata, linker script, startup/vector file, or `main.c`. Those remain owned by the real project. The USB IRQ handler supplied here must be present in the final target build, and the real startup vector table must map `OTG_HS_IRQHandler` to it.

The existing clock configuration must provide the USB 48 MHz clock for the selected OTG_HS internal-FS-PHY configuration. Do not add a second CubeMX clock initializer or `HAL_Init()` to the existing firmware.
