# Porting the firmware to the F411 custom board

How to bring the reader firmware from the F429 Discovery to the custom board (STM32F411CEU6, UFQFPN-48). The board's pin map is in `STM32_Firmware/Firmware/BSP/f411_custom/board_config.h`; it was traced from `docs/hardware/SCH_Schematic1_2026-10-08.pdf`.

**Status:** the port is implemented on branch `f411-custom-board` and builds for both boards; it has not yet run on hardware. Build it with:

```
cd STM32_Firmware
make BOARD=f411                  # debug (-Og), build/f411-debug/
make BOARD=f411 CONFIG=release   # release (-Os), build/f411-release/
```

`make` with no `BOARD` still builds the F429 Discovery, and the F429 output is byte-for-byte the same as before the port. All F411 code sits behind `#if defined(STM32F411xE)` or `#if (TARGET_BOARD == BOARD_F411_CUSTOM)`.

The work had three parts:

1. **STM32CubeMX** produced the chip-level files: device header, startup file, linker script, clock setup and USB setup. The project is kept as `STM32_Firmware/Boards/f411/F411_custom.ioc`.
2. **Code changes** that CubeMX cannot make: board pins, flash layout, UART roles, RS485/RS232 control lines and the inputs/outputs.
3. **Flashing and bring-up** on the board, still to do when the board arrives.

The firmware drives UART, SPI, I2C and GPIO through its own drivers (`Firmware/Drivers/`). Only the clock, USB and flash use the ST HAL, so only those were taken from CubeMX.

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

### 1.5 What was taken from the CubeMX project

| CubeMX file | Now in `STM32_Firmware/` |
|---|---|
| `stm32f411xe.h` | `Drivers/CMSIS/Device/ST/STM32F4xx/Include/` (the other CMSIS files and `system_stm32f4xx.c` were identical to the repo's) |
| `startup_stm32f411ceux.s` | `Boards/f411/` |
| `STM32F411CEUX_FLASH.ld` | `Boards/f411/`, edited as in 1.6 |
| `F411_custom.ioc` | `Boards/f411/`, to reopen in CubeMX later |
| `SystemClock_Config()` | `Src/main.c`, F411 branch |
| OTG_FS `HAL_PCD_MspInit/DeInit`, `USBD_LL_Init` | `Firmware/USB_DEVICE/Target/usbd_conf.c`, F411 branch |
| `OTG_FS_IRQHandler` | `Firmware/USB_DEVICE/Target/usb_irq.c`, F411 branch |

The F411 startup file and linker script live in `Boards/f411/`, not `Startup/`. The STM32CubeIDE project (`.cproject`) compiles everything under `Startup/`, so a second startup file there would break the F429 build in CubeIDE.

`Inc/stm32f4xx_hal_conf.h` is unchanged: `HSE_VALUE` was already 8 MHz.

To regenerate after changing the `.ioc`, generate into a scratch folder outside the repo and copy the same pieces across. Never generate into `STM32_Firmware/`: CubeMX would overwrite `main.c` and the USB files.

### 1.6 Linker script edits

The F411 linker script differs from CubeMX's output in three places:

1. **The program is limited to 128 KB** (`FLASH LENGTH = 128K`, sectors 0–4). Sectors 5–7 hold the settings, and with this limit the linker refuses an image that would overwrite them.
2. **`_Min_Stack_Size = 0x4000`**, the same as the F429. The stack monitor (`stack_monitor.c`) reads it.
3. **A `.noinit` section** after `.bss`, which keeps the fault record (`fault_record.c`) across a reset.

The F411 has no CCM RAM. Nothing in the firmware uses `.ccmram`, so nothing was lost.

## Part 2: Code changes

| Area | File | F411 behaviour |
|---|---|---|
| Board select | `Makefile` | `BOARD=f411` sets `-DSTM32F411xE -DTARGET_BOARD=2`, the `Boards/f411` startup and linker script, output in `build/f411-<config>/`, and `-Og` for debug (at `-O0` the image is 143 KB, too big for 128 KB) |
| CI | `.github/workflows/build.yml` | Builds f429 and f411, debug and release, with `-Werror` |
| Device header | `identium_protocol.c`, `time_service.c`, `reset_cause.c`, `watchdog.c` | Include `stm32f411xe.h` on the F411 |
| Missing peripherals | `Drivers/gpio/gpio.c`, `Drivers/uart/uart.c` | GPIOF, GPIOG and USART3 compiled only where they exist |
| Clock | `Src/main.c` | 8 MHz HSE → 96 MHz, USB 48 MHz, APB1 48 MHz, APB2 96 MHz, 3 wait states |
| USB | `usbd_conf.c`, `usb_irq.c` | OTG_FS on PA11/PA12, no VBUS sense, CubeMX FIFO sizes. The app layer keeps its `HS` names |
| Settings flash | `reader_settings.c`, `configuration_storage.c` | Reader settings in sector 5 (`0x08020000`), config A in sector 6 (`0x08040000`), config B in sector 7 (`0x08060000`) |
| Board init | `BSP/f411_custom/board.c`, `board.h` | RFID UART USART1 PA9/PA10, host UART USART2 PA2/PA3, SPI1 + W5500 CS/RESET, W5500 INT input with pull-up, PB5 (module RST) left as input, buzzer off, status LED on once board init runs |
| Board select | `firmware_configuration.c` | Calls `Board_F411_Init()`. The UART handles are now named `rfidUart` / `hostUart` on both boards |
| UART interrupts | `Common/uart_dispatch.c` | `USART2_IRQHandler` = host, `USART1_IRQHandler` = RFID |
| RS232 / RS485 | `Services/serial/serial_port.c` | PB14 selects the mode. PB12 (DE) and PB13 (RE#) switch together: receiver on and driver off while idle, driver on and receiver off while sending. PA8 powers the RS232 transceiver in RS232 mode only |
| Inputs / outputs | `BSP/industrial_io.c` | Inputs are inverted, so an active field input reads 1. Output 4 doesn't exist: init skips it, and `SET_GPO,4` and reads of output 4 fail |
| ESP32-C3 link | `BSP/f411_custom/board_config.h` | Enabled on I2C1 PB6/PB7, EN on PB9 |

Unchanged on purpose:
- **Brownout:** `brownout.c` already selects BOR level 3 for the F411, which is right for a 3.3 V board.
- **UART/I2C timing:** both are computed from the RCC registers at run time, so they follow the 96 MHz clock.

Still open, not needed for first bring-up:
- **RS232/RS485 switching:** no host command switches the mode yet. It comes from `PRODUCT_SERIAL_DEFAULT_MODE` at start-up.
- **LED and buzzer:** they have no driver beyond init (`SET_BUZZER` still answers `ERROR`).
- **PB3, PA15, PB4 (inputs):** these are JTAG pins after reset. Using them as inputs doesn't affect SWD.

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

The same ESP-IDF project builds the DevKit (`esp32`) and the custom board (`esp32c3`) from one source; the C3 pins (IO6 = SCL, IO10 = SDA) are already set in `main/i2c_link.c`. Build and merge into one file:

```
cd ESP32_Firmware
idf.py -B build_esp32c3 -DIDF_TARGET=esp32c3 -DSDKCONFIG=build_esp32c3/sdkconfig build
cd build_esp32c3
python -m esptool --chip esp32c3 merge-bin -o wifi_coprocessor_esp32c3_merged.bin "@flash_args"
```

1. Connect a 3.3 V USB-serial adapter to CN11 "WIFI_ESP": 5 = RX, 4 = TX, 3 = GND.
2. Fit jumper **J2** (IO9 to GND = download mode).
3. **Hold KEY1** (STM reset) while flashing. PB9 drives the ESP's EN pin, and a running STM can reset the ESP partway through a flash.
4. Run `python -m esptool --chip esp32c3 -p COMx write-flash 0x0 wifi_coprocessor_esp32c3_merged.bin`.
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
