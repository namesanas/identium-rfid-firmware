# Porting the firmware to the F411 custom board

How to bring the reader firmware from the F429 Discovery to the custom board (STM32F411CEU6, UFQFPN-48). The board's pin map is in `STM32_Firmware/Firmware/BSP/f411_custom/board_config.h`; it was traced from `docs/hardware/SCH_Schematic1_2026-10-08.pdf`.

The work splits in three:

1. **STM32CubeMX** produces the chip-level files: device header, startup file, linker script, clock setup and the USB peripheral setup.
2. **Code changes** that CubeMX cannot make: board pins, flash layout, UART roles, RS485/RS232 control lines and the inputs/outputs.
3. **Flashing and bring-up** on the board.

The firmware drives UART, SPI, I2C and GPIO through its own drivers (`Firmware/Drivers/`). Only the clock, USB and flash use the ST HAL. CubeMX therefore must **not** generate init code for USART, SPI, I2C or GPIO: those calls would fight with the firmware's own drivers.

## Board facts

| Item | Value |
|---|---|
| MCU | STM32F411CEU6: 512 KB flash, 128 KB RAM, 100 MHz max |
| HSE crystal (X1) | 8 MHz |
| LSE | none (PC14/PC15 not connected) |
| W5500 crystal (X2) | 25 MHz (W5500's own, nothing to configure) |
| Debug | SWD on CN7: PA13 SWDIO, PA14 SWCLK, NRST |
| RFID module (CN1) | USART1: PA9 → module RXD (pin 9), PA10 ← module TXD (pin 10). Module EN tied to 3V3 |
| Host RS232/RS485 | USART2 PA2/PA3 through mux U12. PB14 = mode (L = RS232, H = RS485), PB12 = RS485 DE, PB13 = RS485 RE# (active low), PA8 = RS232 transceiver power |
| ESP32-C3 | I2C1: PB6 SCL, PB7 SDA. PB9 = ESP EN. No IRQ line |
| W5500 | SPI1: PA5/PA6/PA7, CS PA4, INT PB0, RESET PB1 |
| USB | OTG_FS on PA11/PA12, no VBUS sense pin |
| Inputs 1–4 | PB3, PA15, PB4, PB15. Active low (NPN stage) |
| Outputs 1–3 | PB2, PB10, PA0 (relays, active high). There is no output 4 |
| LED / buzzer | PB8 status LED, PC13 buzzer (both active high) |

## Part 1: STM32CubeMX

Create the project **in a scratch folder outside the repo**, for example `C:\Users\Identium\Downloads\F411_cubemx`. You will copy a handful of files from it; never generate into `STM32_Firmware/`, because CubeMX would overwrite `main.c` and the USB files.

### 1.1 New project

1. File → New Project → MCU/MPU Selector → type **STM32F411CEU6** → select it → Start Project.
2. If it asks about the default mode of peripherals, answer **No** (start from a blank chip).

### 1.2 Pinout & Configuration

**System Core → SYS**
- Debug: **Serial Wire**. This reserves PA13/PA14 for SWD. If you leave it at "No Debug" you lock yourself out of the debugger after the first flash.
- Timebase Source: SysTick.

**System Core → RCC**
- High Speed Clock (HSE): **Crystal/Ceramic Resonator**.
- Low Speed Clock (LSE): **Disable** (the board has no 32 kHz crystal).

**Connectivity → USB_OTG_FS**
- Mode: **Device_Only**.
- Activate_VBUS: **unchecked** (the board has no VBUS sense pin; PA9 is the RFID UART).
- This puts USB on PA11/PA12, which matches the board.

**Middleware → USB_DEVICE**
- Class For FS IP: **Communication Device Class (Virtual Port Com)**.
- Leave its parameters at default. CubeMX is only used here to get the OTG_FS low-level setup; the firmware keeps its own CDC files.

**Optional: label the rest of the pins.** It's worth doing so the `.ioc` file documents the board and CubeMX warns about conflicts. Click each pin and set it as below, then right-click → Enter User Label:

| Pin | Set to | Label |
|---|---|---|
| PA9 / PA10 | USART1_TX / USART1_RX | RFID_TX / RFID_RX |
| PA2 / PA3 | USART2_TX / USART2_RX | HOST_TX / HOST_RX |
| PA4 | GPIO_Output | W5500_CS |
| PA5 / PA6 / PA7 | SPI1_SCK / SPI1_MISO / SPI1_MOSI | — |
| PB0 | GPIO_Input | W5500_INT |
| PB1 | GPIO_Output | W5500_RST |
| PB6 / PB7 | I2C1_SCL / I2C1_SDA | ESP_SCL / ESP_SDA |
| PB9 | GPIO_Output | ESP_EN |
| PB12 / PB13 | GPIO_Output | RS485_DE / RS485_RE_N |
| PB14 | GPIO_Output | SERIAL_MODE |
| PA8 | GPIO_Output | RS232_PWR |
| PB3, PA15, PB4, PB15 | GPIO_Input | IN1, IN2, IN3, IN4 |
| PB2, PB10, PA0 | GPIO_Output | OUT1, OUT2, OUT3 |
| PB8 | GPIO_Output | LED_STATUS |
| PC13 | GPIO_Output | BUZZER |
| PB5 | GPIO_Input | RFID_PIN11 |

If you add USART1, USART2, SPI1 and I2C1, set them to the mode shown (Asynchronous, Full-Duplex Master, I2C) but **do not** enable their DMA or NVIC interrupts.

### 1.3 Clock Configuration

Set the clock tree to **96 MHz**. USB needs exactly 48 MHz, which a 96 MHz system clock divides into cleanly; 100 MHz would not.

| Box | Value |
|---|---|
| Input frequency | **8** MHz |
| PLL Source Mux | **HSE** |
| /M | **4** |
| ×N | **96** |
| /P | **2** → SYSCLK 96 MHz |
| /Q | **4** → 48 MHz USB clock |
| System Clock Mux | **PLLCLK** |
| AHB Prescaler | /1 → HCLK 96 MHz |
| APB1 Prescaler | **/2** → 48 MHz (the APB1 limit is 50 MHz) |
| APB2 Prescaler | /1 → 96 MHz |

All boxes should be free of red. CubeMX picks flash latency 3 WS by itself.

### 1.4 Project Manager

**Project tab**
- Project Name: `F411_cubemx`.
- Toolchain / IDE: **STM32CubeIDE**. This gives the GCC startup file and linker script the Makefile needs.
- Minimum Heap Size: **0x200**; Minimum Stack Size: **0x4000**. These match the F429 linker script.

**Code Generator tab**
- STM32Cube MCU packages: **Copy only the necessary library files**.
- Generated files: tick **Generate peripheral initialization as a pair of '.c/.h' files per peripheral**.

**Advanced Settings tab**
- In the Generated Function Calls table, tick **Do Not Generate Function Call** for every row **except** `SystemClock_Config` and `MX_USB_DEVICE_Init`. In particular, tick it for `MX_GPIO_Init`, `MX_USART1_UART_Init`, `MX_USART2_UART_Init`, `MX_SPI1_Init` and `MX_I2C1_Init`.

Click **Generate Code**.

### 1.5 Files to take from the CubeMX project

| From the CubeMX project | Into `STM32_Firmware/` | Notes |
|---|---|---|
| `Drivers/CMSIS/Device/ST/STM32F4xx/Include/stm32f411xe.h` | same path | The device header. `stm32f4xx.h` and `system_stm32f4xx.h` there are the same files the repo already has |
| `Core/Startup/startup_stm32f411ceux.s` | `Startup/f411/` | Move the existing `startup_stm32f429zitx.s` to `Startup/f429/` (see 2.1) |
| `STM32F411CEUX_FLASH.ld` | project root, next to the F429 script | Needs the edits in 1.6 |
| `SystemClock_Config()` body from `Core/Src/main.c` | `Src/main.c` | Put it under `#if (TARGET_BOARD == BOARD_F411_CUSTOM)`, keep the F429 version under `#else` |
| `HAL_PCD_MspInit()` and the `hpcd_USB_OTG_FS` setup in `USBD_LL_Init()` from `USB_DEVICE/Target/usbd_conf.c` | `Firmware/USB_DEVICE/Target/usbd_conf.c` | Only the OTG_FS parts. Keep the firmware's own CDC files. The RX/TX FIFO sizes (`HAL_PCDEx_SetRxFiFo`, `HAL_PCDEx_SetTxFiFo`) must be the OTG_FS values CubeMX generates; the OTG_FS FIFO is smaller than OTG_HS |
| `OTG_FS_IRQHandler` from `Core/Src/stm32f4xx_it.c` | `Firmware/USB_DEVICE/Target/usb_irq.c` | Calls `HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS)` |

Leave `Inc/stm32f4xx_hal_conf.h` as it is: its `HSE_VALUE` is already 8 MHz, and the F411 uses the same HAL modules (RCC, FLASH, PWR, GPIO, CORTEX, PCD, DMA, EXTI). Also keep the repo's HAL driver sources; they are the same STM32F4 HAL.

### 1.6 Linker script edits

Make these four changes to `STM32F411CEUX_FLASH.ld`:

1. **Limit the program to 128 KB.** Settings live in flash sectors 5–7 (see 2.4), so the code must stay in sectors 0–4:
   ```
   FLASH (rx) : ORIGIN = 0x8000000, LENGTH = 128K
   ```
   With this limit, the linker refuses to build an image that would overwrite the settings sectors.
2. Keep `_Min_Heap_Size = 0x200;` and `_Min_Stack_Size = 0x4000;`. The stack monitor (`stack_monitor.c`) reads `_Min_Stack_Size`.
3. Add the `.noinit` section after `.bss`. The fault record (`fault_record.c`) is kept across a reset there:
   ```
   .noinit (NOLOAD) :
   {
     . = ALIGN(4);
     *(.noinit)
     *(.noinit*)
     . = ALIGN(4);
   } >RAM
   ```
4. There is no CCM RAM on the F411. Nothing in the firmware uses `.ccmram`, so there is nothing to carry over.

## Part 2: Code changes (not done by CubeMX)

`board_config.h` for the custom board already holds the traced pin map. The rest:

### 2.1 Build: select the board

In the `Makefile`, add a `BOARD` variable (`f429` default, `f411`):

| | f429 | f411 |
|---|---|---|
| `DEFS` | `-DSTM32F429xx -DTARGET_BOARD=1` | `-DSTM32F411xE -DTARGET_BOARD=2` |
| `LDSCRIPT` | `STM32F429ZITX_FLASH.ld` | `STM32F411CEUX_FLASH.ld` |
| startup | `Startup/f429/*.s` | `Startup/f411/*.s` |
| `BUILD` | `build/$(CONFIG)` | `build/f411-$(CONFIG)` |

The startup files must live in separate folders, because the Makefile currently links every `Startup/*.s`. Board-specific C files (`BSP/f429_discovery/board.c`, the new `BSP/f411_custom/board.c`) must be wrapped in `#if (TARGET_BOARD == ...)`, because the Makefile compiles every `.c` under `Firmware/`.

For the F411 debug build, use `-Og` instead of `-O0`. At `-O0` the image is 143 KB, which does not fit in the 128 KB program area; at `-Og` it is about 98 KB. Release (`-Os`) is about 83 KB.

Add the F411 build to `.github/workflows/build.yml` so CI builds both boards.

### 2.2 F429-only code

- Five files include `stm32f429xx.h` directly. Change them to `#include "stm32f4xx.h"`, which picks the right device from `-DSTM32F4xxxx`. The files are `Firmware/Protocol/identium_protocol.c`, `Firmware/Services/Time/time_service.c`, `Firmware/Services/system/reset_cause.c`, `Firmware/Services/watchdog/watchdog.c` and `Firmware/Common/critical_section.h`.
- `Firmware/Drivers/gpio/gpio.c` refers to GPIOF and GPIOG, which the F411 doesn't have (it has GPIOA–E and GPIOH). Wrap those lines in `#if defined(GPIOF)` / `#if defined(GPIOG)`, the same way GPIOI–K already are.
- `Firmware/Devices/esp32/esp32_link.c` and `Firmware/BSP/industrial_io.c` already pick the board config by `TARGET_BOARD`. `Firmware/Services/serial/serial_port.c` only includes the F429 config; add the F411 branch.
- Brownout (`brownout.c`) already selects BOR level 3 for the F411. That is right for a 3.3 V board.

### 2.3 Board init and UART roles

- Add `Firmware/BSP/f411_custom/board.c` with `Board_F411_Init()`, modelled on `f429_discovery/board.c`. It must configure:
  - USART1 on PA9/PA10 (AF7) as the RFID UART, and USART2 on PA2/PA3 (AF7) as the host UART;
  - SPI1 and the W5500 CS/RESET pins;
  - PB5 as an input with no pull. R32 already pulls it up; never drive it low;
  - PC13 (buzzer) and PB8 (LED) as outputs, driven low.
- `Firmware/Application/configuration/firmware_configuration.c` calls `Board_F429_Init(&usart3, &usart1, ...)` by name. Select the F411 call there, and swap the UART handle names to match (RFID = USART1, host = USART2).
- `Firmware/Common/uart_dispatch.c` hard-codes `USART1_IRQHandler` = host and `USART3_IRQHandler` = RFID. On the F411 it must be `USART2_IRQHandler` = host and `USART1_IRQHandler` = RFID. `BOARD_HOST_USART_IRQ` / `BOARD_RFID_USART_IRQ` in `board_config.h` already hold the new numbers.
- In `Src/main.c`, change the clock comment block to match: 8 MHz / 4 × 96 = 192 MHz VCO, /2 = 96 MHz, /4 = 48 MHz USB.

### 2.4 Flash layout for settings

The F429 build stores settings in sectors 21–23, which don't exist on the F411. The F411CE has eight sectors: 0–3 are 16 KB, 4 is 64 KB, and 5–7 are 128 KB each. The settings need three sectors of 128 KB, so they move to the last three. The program keeps sectors 0–4 (128 KB, matching the linker limit in 1.6).

| What | File | F429 | F411 |
|---|---|---|---|
| Reader settings log | `reader_settings.c` | sector 21, `0x081A0000` | **sector 5, `0x08020000`** |
| Config copy A | `configuration_storage.c` | sector 22, `0x081C0000` | **sector 6, `0x08040000`** |
| Config copy B | `configuration_storage.c` | sector 23, `0x081E0000` | **sector 7, `0x08060000`** |

Sector size is 128 KB in both cases, so only the sector numbers and addresses change. Put each set under `#if (TARGET_BOARD == ...)`.

### 2.5 Host serial port (RS232 / RS485)

`serial_port.c` currently knows one DE/RE pin and one mode pin. The board has more:

- **PB14 (mode)**: drive low for RS232, high for RS485.
- **PB12 (DE)**: high only while transmitting in RS485 mode. This is the existing `BOARD_RS485_DE_RE` pin.
- **PB13 (RE#)**: new. R20 pulls it high, which **switches the RS485 receiver off**. In RS485 mode the firmware must drive it low to receive, and high only while transmitting (or leave it low for echo). If you forget this pin, RS485 transmits but never receives.
- **PA8 (RS232 power)**: new. Drive it high in RS232 mode. It can go low in RS485 mode to save power.

### 2.6 Inputs, outputs, LED, buzzer

- `industrial_io.c` expects four outputs. Output 4 is `NULL` on this board, so skip `NULL` ports in the init and set functions, and report output 4 as absent.
- The inputs are active low (`BOARD_INPUTS_ACTIVE_LOW`). Invert the reading in `industrial_io.c` so a field input that is on still reads as 1 in the protocol.
- PB3, PA15 and PB4 are JTAG pins after reset. Configuring them as plain inputs is fine and doesn't affect SWD. Just make sure nothing re-enables JTAG.
- PB2 is also the BOOT1 strap. Its 10k pull-down keeps the relay off and boot normal during reset.
- The LED and buzzer have no driver yet (`SET_BUZZER` currently answers `ERROR`). A simple start: LED on at boot, blinking while inventory runs, and `SET_BUZZER` driving PC13.

## Part 3: Flashing and bring-up

### 3.1 Connections

Connect an ST-LINK to CN7 with SWDIO, SWCLK, NRST, GND and 3V3 (for target sensing). Power the board from its own supply, not from the ST-LINK.

### 3.2 Build and flash the STM32

```
cd STM32_Firmware
make BOARD=f411 CONFIG=release
STM32_Programmer_CLI -c port=SWD mode=UR -e all
STM32_Programmer_CLI -c port=SWD mode=UR -w build/f411-release/rfid_controller.elf -v -rst
```

Erase the whole chip on the first flash only (`-e all`), so the settings sectors start clean. Later flashes skip the erase and keep the settings.

### 3.3 Flash the ESP32-C3

```
cd ESP32_Firmware
idf.py -B build_esp32c3 -DIDF_TARGET=esp32c3 -DSDKCONFIG=build_esp32c3/sdkconfig build
```

1. Connect a 3.3 V USB-serial adapter to CN11 "WIFI_ESP": 5 = RX, 4 = TX, 3 = GND.
2. Fit jumper **J2** (IO9 to GND = download mode).
3. **Hold KEY1** (STM reset) while flashing. PB9 drives the ESP's EN pin, and a running STM can reset the ESP partway through a flash.
4. Run `idf.py -B build_esp32c3 -p COMx flash`.
5. Remove J2 and release KEY1.

### 3.4 Bring-up order

Check each step before the next:

1. **Power and SWD.** `STM32_Programmer_CLI -c port=SWD` connects and reads the device ID as `0x431`.
2. **Clock.** If the program runs but the UART bauds and USB are wrong, the HSE didn't start. Check X1 and its 22 pF caps.
3. **USB.** The board appears as a COM port. Send `GET_DIAG` and check the reset cause, BOR level 3 and the stack high-water mark.
4. **RFID module.** `VER` returns the module version over USART1. If nothing comes back, check the CN1 TX/RX direction.
5. **RS232 and RS485.** Test each with a USB adapter. No host command switches the mode yet: it comes from `PRODUCT_SERIAL_DEFAULT_MODE` at start-up, so build once per mode (or add a command first). In RS485 mode check both directions; a one-way link points at RE# (PB13).
6. **Inputs and outputs.** Toggle each relay with `SET_GPO,<n>,<state>,<duration>`. Apply 24 V to each input and read it with `GET_GPI`.
7. **Ethernet (W5500).** Plug in a cable, then ping the reader and connect on the host TCP port.
8. **WiFi.** Send `SET_WIFI,1,STA,<ssid>,<password>`, then `GET_WIFI`, and connect over TCP.
