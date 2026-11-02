# STM32 reader firmware: developer guide

This guide describes the STM32 firmware in `STM32_Firmware/` as it is in the code on `main`. Paths are relative to the repository root. Where the code and an older design record disagree, the code wins and the difference is pointed out.

Related documents:

| Document | Contents |
|---|---|
| `docs/design/README.md`, `docs/design/0001`–`0017` | Protocol and design history, in the order the changes were made |
| `docs/notes/f411-custom-board-port.md` | How the F411 custom board port was made (CubeMX, code changes, bring-up order) |
| `docs/notes/usb-cdc-integration.md`, `docs/notes/usb-cdc-hal-fix.md` | Native USB CDC integration and the HAL build fix |
| `docs/reader-protocol-specification.pdf` | Identium reader protocol (TCP, HTTP, MQTT, configuration port 8080) |
| `docs/hardware/SCH_Schematic1_2026-10-08.pdf` | Custom board schematic |

Contents:

1. [Overview](#1-overview)
2. [Source tree](#2-source-tree)
3. [Start-up and main loop](#3-start-up-and-main-loop)
4. [Hardware map per board](#4-hardware-map-per-board)
5. [Communication paths and protocols](#5-communication-paths-and-protocols)
6. [Settings and flash storage](#6-settings-and-flash-storage)
7. [Diagnostics and faults](#7-diagnostics-and-faults)
8. [Building](#8-building)
9. [Flashing](#9-flashing)
10. [Testing](#10-testing)
11. [Common changes](#11-common-changes)
12. [Release flow](#12-release-flow)
13. [Known gaps and open points](#13-known-gaps-and-open-points)

---

## 1. Overview

The reader is an STM32 controller around a UHF RFID module. The STM32 owns everything above the radio: host command parsing, tag reporting, settings, uploads and the vendor-tool compatibility layer.

| Part | Role | Connection to the STM32 |
|---|---|---|
| STM32F429ZI (bench) / STM32F411CE (product) | Controller, protocols, settings | — |
| Silion E310 UHF RFID module | Inventory and tag access | UART, 115200 8N1, Silion `FF` frame protocol |
| WIZnet W5500 | Wired Ethernet, 4 hardware sockets used | SPI1 |
| ESP32 (DevKit on the bench) / ESP32-C3 (custom board) | WiFi coprocessor: WiFi + TCP sockets only | I2C, STM32 master, ESP slave at 0x42, polled |
| USB device (CDC) | Host link, shows as a virtual COM port | OTG_HS with internal FS PHY (F429) / OTG_FS (F411) |
| Host USART | RS232 / RS485 host link (custom board). On the Discovery it is the ST-LINK virtual COM port | USART1 (F429) / USART2 (F411) |
| Industrial I/O | 4 inputs, 4 outputs (F429 test pins) / 4 inputs, 3 relays (F411) | GPIO |

Two targets build from one source tree:

| Target | MCU | Clock | Flash used for code | Purpose |
|---|---|---|---|---|
| F429 Discovery (`BOARD=f429`, default) | STM32F429ZIT6, 2 MB flash, 192 KB RAM + 64 KB CCM | 168 MHz (8 MHz HSE) | 624 KB | Bench rig |
| F411 custom board (`BOARD=f411`) | STM32F411CEU6, 512 KB flash, 128 KB RAM | 96 MHz (8 MHz HSE) | 112 KB | Product board |

Board selection is a compile-time define: `TARGET_BOARD` (`Firmware/Config/firmware_config.h`, `BOARD_F429_DISCOVERY = 1`, `BOARD_F411_CUSTOM = 2`, default 1) plus the CMSIS device define (`STM32F429xx` or `STM32F411xE`).

The F411 port builds and links; per `docs/notes/f411-custom-board-port.md` it had not yet run on hardware when it was written. Treat F411 behaviour in this guide as "what the code does", not "what was measured".

---

## 2. Source tree

```
STM32_Firmware/
├── Makefile                      Command-line build (BOARD=, CONFIG=, flash, check-chip, clean)
├── .cproject / .project          STM32CubeIDE project: configurations Debug, Release (F429), F411
├── .mxproject, RFID_Controller_F429.ioc   F429 CubeMX project (reference only)
├── RFID_Controller_F429.launch   CubeIDE debug launch, loads Debug/RFID_Controller_F429.elf
├── RFID_Controller_F411.launch   CubeIDE debug launch, loads F411/RFID_Controller_F429_f411.elf
├── STM32F429ZITX_FLASH.ld        F429 linker script: FLASH 624 K from 0x08004000, 16 KB stack budget, .noinit
├── Bootloader/                   Sector-0 bootloader: signature check and install (design 0020)
├── STM32F429ZITX_RAM.ld          CubeIDE-generated RAM-run script; not used by the Makefile
├── Startup/startup_stm32f429zitx.s   F429 vector table / reset handler
├── Boards/f411/
│   ├── F411_custom.ioc           CubeMX project for the custom board (reopen to regenerate)
│   ├── STM32F411CEUX_FLASH.ld    F411 linker script: FLASH 112 K (sectors 1-4), .noinit
│   └── startup_stm32f411ceux.s   F411 vector table (kept out of Startup/ on purpose, see 8.4)
├── Inc/                          main.h, stm32f4xx_hal_conf.h (HSE_VALUE 8 MHz), stm32f4xx_it.h
├── Src/
│   ├── main.c                    HAL_Init, SystemClock_Config (per board), main loop, Error_Handler
│   ├── stm32f4xx_it.c            Fault handlers (naked, call the fault record), SysTick
│   ├── stm32f4xx_hal_msp.c, system_stm32f4xx.c, syscalls.c, sysmem.c   CubeMX / newlib glue
├── Drivers/                      ST HAL + CMSIS (canonical copy; used only for clock, USB, flash)
├── Firmware/
│   ├── Application/
│   │   ├── configuration/firmware_configuration.c   The whole start-up sequence
│   │   ├── Run_time/firmware_runtime.c              Thin wrapper: calls Firmware_Configuration_Init()
│   │   ├── Host/host_interface.c                    Legacy ASCII command parser (all text commands)
│   │   ├── Silion/sillion_application.c             Reader logic on top of the module protocol
│   │   ├── Silion/sillion_application_port.c        Module UART RX queue (4 KB), raw mode for the bridge
│   │   ├── Protocols                                Empty file (placeholder, nothing in it)
│   │   └── inventory/, reader/                      Empty directories (not in git)
│   ├── BSP/
│   │   ├── f429_discovery/board_config.h, board.c   F429 pin map and board init
│   │   ├── f411_custom/board_config.h, board.c      F411 pin map (traced from the schematic) and init
│   │   └── industrial_io.c                          Inputs/outputs for both boards
│   ├── Common/                   uart_dispatch.c (USART IRQ handlers), critical_section.h,
│   │                             error_codes.h, status.h, types.h, ring_buffer.c (8 lines, unused stub)
│   ├── Config/
│   │   ├── firmware_config.h     TARGET_BOARD selection
│   │   └── product_config.h      Product defaults: ports, server, reader name, RF defaults, VER bytes
│   ├── Devices/
│   │   ├── w5500/w5500.c         W5500 register driver; sockets 0-3
│   │   └── esp32/esp32_link.c, esp32_link_protocol.h   I2C link to the ESP32 (header shared with ESP32_Firmware/)
│   ├── Drivers/                  Own register-level drivers: gpio, uart, spi, i2c, rcc
│   │                             (dma/ and timer/ are empty directories)
│   ├── Middlewares/ST/STM32_USB_Device_Library/   ST USB device core + CDC class
│   ├── Protocol/
│   │   ├── identium_protocol.c   Identium JSON-over-TCP framing, events, tag queue, heartbeat
│   │   └── identium_command.c    JSON command dispatcher (pause/resume, sync_time, set_gpo, ...)
│   ├── Protocols/
│   │   ├── silion/silion_protocol.c   Silion module frame builder/parser, CRC, async tag queue
│   │   └── host/                      Empty directory (not in git)
│   ├── Services/
│   │   ├── host/host_transport.c      Host RX queue, host lock, vendor-tool router (USB + serial)
│   │   ├── USB/usb_cdc_transport.c    CDC TX queue (4 KB, 64-byte packets)
│   │   ├── serial/serial_port.c       RS232/RS485 host port, TX queue 2 KB, DE/RE control
│   │   ├── ethernet/ethernet_service.c   Active upload client on W5500 socket 0
│   │   ├── ethernet/host_tcp_server.c    Host text commands on W5500 socket 3, TCP 5000
│   │   ├── ethernet/http_upload.c        HTTP POST upload (active mode)
│   │   ├── upload/active_upload_transport.c   Chooses Ethernet or WiFi for uploads
│   │   ├── wifi/wifi_service.c        WiFi state machine on top of esp32_link
│   │   ├── configuration/
│   │   │   ├── configuration_port.c   TCP 8080 server (W5500 socket 1), EE requests
│   │   │   ├── module_bridge.c        Vendor-tool "mainboard" emulation, FF pass-through
│   │   │   ├── vendor_config.c        EE write commands 0x25 / 0x1F / 0x20 / 0x23
│   │   │   ├── discovery_service.c    UDP 15000 device search (W5500 socket 2)
│   │   │   ├── configuration_service.c   ActiveModeJson / StaticModelJson model
│   │   │   ├── configuration_storage.c   Identium config persistence, A/B sectors
│   │   │   └── reader_settings.c      Network / identity / upload / WiFi settings, append-only log
│   │   ├── GPIO/gpio_service.c        Output pulses, input change detection
│   │   ├── Time/time_service.c        1 ms tick (SysTick), epoch time from sync_time
│   │   ├── system/                    fault_record, stack_monitor, loop_monitor, brownout,
│   │   │                              reset_cause, system_fault, diagnostics (GET_DIAG)
│   │   ├── watchdog/watchdog.c        IWDG
│   │   └── io/, rfid/                 Empty directories (not in git)
│   └── USB_DEVICE/                    CDC app layer (usbd_cdc_if.c, usbd_desc.c) and target glue
│                                      (usbd_conf.c, usb_irq.c with per-board branches)
├── Tools/                        Python test scripts (see section 10)
├── hostcheck/identium_protocol_host_test.c   Host-PC unit test of the JSON framing (see 10.4)
├── Debug/, Release/, F411/       CubeIDE output folders (git-ignored)
└── build/                        Makefile output (git-ignored)
```

The only text-command parser is `Firmware/Application/Host/host_interface.c`. `Firmware/Protocols/host/`, `Firmware/Services/rfid/` and `Firmware/Application/Protocols` contain no code. The vendor-tool tokens (`VER`, `IOGET`, ...) are handled in `host_transport.c` and `module_bridge.c`, see 5.6.

Our own drivers in `Firmware/Drivers/` handle UART, SPI, I2C and GPIO. The ST HAL is used only for clock setup, USB and flash programming.

---

## 3. Start-up and main loop

### 3.1 Init order

`Src/main.c`:

1. `HAL_Init()`, `SystemClock_Config()` (per board, see 4.1).
2. `Fault_Record_Init()`: takes over the previous run's fault record and enables the separate MemManage/BusFault/UsageFault exceptions.
3. `Stack_Monitor_Init()`: paints the stack reserve and arms the MPU guard.
4. `Firmware_Runtime_Init()` → `Firmware_Configuration_Init()`. On failure: `Error_Handler()` (records the caller, then halts with interrupts off; it does not reset).
5. `Loop_Monitor_Init()`, then the main loop.

`Firmware/Application/configuration/firmware_configuration.c`, `Firmware_Configuration_Init()`:

| # | Call | Notes |
|---|---|---|
| 1 | `Time_Service_Init`, `Reset_Cause_Init`, `Brownout_Init`, `System_Fault_Init` | Brownout programs the BOR option byte once if it differs |
| 2 | `HOST_Transport_Init`, `Identium_Protocol_Init`, `Configuration_Service_Init` | Host queue exists before USB can deliver bytes |
| 3 | `Board_F429_Init` / `Board_F411_Init` | Pins, RFID UART, host UART, W5500 SPI |
| 4 | `Industrial_IO_Init`, `GPIO_Service_Init(Identium_Protocol_ReportGpiChange)` | Fails → `ERROR_GPIO_INIT` |
| 5 | `USB_CDC_Transport_Init`, `MX_USB_DEVICE_Init` | USB enumerates from here on |
| 6 | `USART_Dispatch_Init`, `Serial_Port_Init(hostUart, PRODUCT_SERIAL_DEFAULT_MODE)` | RS232 by default |
| 7 | `SILION_Init`, `SILION_Application_Port_Init`, UART IRQs (priority 5), start RX | |
| 8 | `Firmware_Configuration_ReaderDefaults`, `Configuration_Service_LoadPersistent` | Region 8, antenna 1/1, power 2500/2500, Gen2, session 0, then stored Identium config |
| 9 | `SILION_Application_Init`, `HOST_Interface_Init` | |
| 10 | `ReaderSettings_Init` | Loads network/identity settings; stored RF region/power override the defaults when `rfValid` |
| 11 | `W5500_Init`, `ReaderSettings_ApplyNetwork` | Fails → `ERROR_W5500_INIT` |
| 12 | `Configuration_Port_Init`, `Host_Tcp_Server_Init`, `Ethernet_Service_Init(Identium_Protocol_PushTcpByte)` | |
| 13 | `WiFi_Service_Init(Identium_Protocol_PushTcpByte)` | Never fails the boot; a missing ESP32 leaves WiFi offline |
| 14 | `SILION_Application_ConfigureReader()`, up to 3 attempts | Fails → `ERROR_RFID_INIT` |
| 15 | `SILION_Application_SetSession(readerConfig.session)` | Applies the stored Gen2 session |
| 16 | Start inventory if `start_inv_atbeg` = 1 and work state = ACTIVE | |
| 17 | `Watchdog_Init()` | The IWDG starts only after a successful start-up |

`SILION_Application_ConfigureReader()` (`Firmware/Application/Silion/sillion_application.c`):

1. Wait 200 ms (module settle).
2. Send Stop Async Inventory (`AA 49`), wait 300 ms, drop everything the module sent. An MCU reset does not reset the module, so it may still be streaming tags from before the reset.
3. `0x03` Get Version, `0x04` Boot Firmware, wait 500 ms, `0x0C` Get Run Phase (must report application, `0x12`).
4. `0x97` Set Region, `0x91` Set Antenna, `0x91` Set Power, `0x93` Set Tag Protocol (Gen2).
5. A warm-up single-tag inventory (`0x21`, 2000 ms module timeout). "No tags found" (`0x0400`) is accepted.

The fixed waits add up to 1 s; the warm-up inventory and the module transactions add the rest. On the bench the reader answers host commands about 2.7 s after reset. Scripts must wait for that (section 10).

### 3.2 Main loop

The firmware has no RTOS. `main()` runs one cooperative loop; each task must return quickly:

```c
while(1)
{
    LOOP_MONITOR_RUN(LOOP_TASK_ETHERNET,    Ethernet_Service_Task());     // upload client, socket 0
    LOOP_MONITOR_RUN(LOOP_TASK_WIFI,        WiFi_Service_Task());         // ESP32 link state machine
    LOOP_MONITOR_RUN(LOOP_TASK_CONFIG_PORT, Configuration_Port_Task());   // 8080, bridge, discovery, deferred saves
    LOOP_MONITOR_RUN(LOOP_TASK_PROTOCOL,    Identium_Protocol_Task());    // heartbeat, tag queue, reboot timer
    LOOP_MONITOR_RUN(LOOP_TASK_GPIO,        GPIO_Service_Task());         // pulses, input changes
    LOOP_MONITOR_RUN(LOOP_TASK_RFID,        SILION_Application_Task());   // module RX, async tags
    LOOP_MONITOR_RUN(LOOP_TASK_HOST,        HOST_Interface_Task());       // USB TX, TCP 5000, text commands
    Loop_Monitor_PassEnd();
    Watchdog_Refresh();
}
```

`Configuration_Port_Task()` also runs `ReaderSettings_Task()` (delayed network apply / reboot), `VendorConfig_Task()` (deferred config writes), `Discovery_Service_Task()` and `Module_Bridge_Poll()`. `HOST_Interface_Task()` first runs `USB_CDC_Transport_Task()` and `Host_Tcp_Server_Task()`.

Blocking code that does exist: module transactions in host commands (`SILION_Application_TransactionWithTimeout`, timeouts of 5–10 s, polling the module RX queue while it waits), W5500 register waits of a few ms, and flash erases when settings are saved. The bench figure in design 0017 is 8.4 ms for the longest loop pass during START/STOP.

### 3.3 Watchdog

`Firmware/Services/watchdog/watchdog.c`: IWDG, prescaler /256, reload `0xFFF`, about 32 s with a 32 kHz LSI (the header gives 22–62 s across the LSI tolerance). It is refreshed once per loop pass. The timeout is long on purpose because RFID transactions block and a 128 KB sector erase can take seconds.

### 3.4 Loop monitor

`Firmware/Services/system/loop_monitor.c`: `LOOP_MONITOR_RUN()` measures each task call with the DWT cycle counter and keeps the maximum per task and per pass. `GET_DIAG` reports them (`ETH_US`, `WIFI_US`, `CFG_US`, `PROTO_US`, `GPIO_US`, `RFID_US`, `HOST_US`, `TLOG_US`, `LOOP_MAX_US`, `PASSES`); `CLEAR_DIAG` resets the maxima.

---

## 4. Hardware map per board

Sources: `Firmware/BSP/f429_discovery/board_config.h`, `Firmware/BSP/f411_custom/board_config.h`, `Firmware/BSP/industrial_io.c`, `Firmware/USB_DEVICE/Target/usbd_conf.c`, `Src/main.c`.

### 4.1 Clocks

| | F429 Discovery | F411 custom |
|---|---|---|
| HSE | 8 MHz | 8 MHz (X1) |
| PLL | M=8, N=336, P=2, Q=7 | M=4, N=96, P=2, Q=4 |
| SYSCLK / HCLK | 168 MHz | 96 MHz |
| APB1 / APB2 | 42 MHz (/4) / 84 MHz (/2) | 48 MHz (/2) / 96 MHz (/1) |
| USB clock | 48 MHz | 48 MHz |
| Flash wait states | 5 | 3 |
| LSE | not used | none fitted |

UART baud rates and I2C timing are computed from the RCC registers at run time, so they follow the clock. W5500 SPI runs at APB2/8 (10.5 MHz on F429, 12 MHz on F411).

### 4.2 Pin and peripheral map

| Function | F429 Discovery | F411 custom board |
|---|---|---|
| RFID module UART | USART3: PB10 TX, PB11 RX (AF7), IRQ 39 | USART1: PA9 TX → module RXD (CN1-9), PA10 RX ← module TXD (CN1-10) (AF7), IRQ 37 |
| RFID module enable | PB0, driven high at init | none (module EN tied to 3V3) |
| RFID module pin 11 (RST) | — | PB5, left as input; R32 pull-up holds it high. Never drive it low |
| Host UART | USART1: PA9 TX, PA10 RX (AF7), IRQ 37. Wired to the ST-LINK virtual COM port | USART2: PA2 TX, PA3 RX (AF7), IRQ 38, through mux U12 |
| RS232/RS485 select | none | PB14: low = RS232 (MAX3232), high = RS485 (SP3485). R18 pulls it high |
| RS485 DE | PG13 (green LED LD3, lights while transmitting) | PB12 (active high) |
| RS485 RE# | — | PB13 (active low, follows DE) |
| RS232 transceiver power | — | PA8 (high in RS232 mode only) |
| W5500 SPI | SPI1: PA5 SCK, PA6 MISO, PA7 MOSI (AF5) | SPI1: PA5, PA6, PA7 (AF5) |
| W5500 CS / RESET | PB12 / PC0 | PA4 / PB1 |
| W5500 INT | not used | PB0, input with pull-up (firmware polls, does not use it) |
| ESP32 I2C | I2C3: PA8 SCL, PC9 SDA (AF4) | I2C1: PB6 SCL (→ C3 IO6), PB7 SDA (→ C3 IO10) (AF4) |
| ESP32 EN | PA15, open drain | PB9, open drain |
| USB | OTG_HS with internal FS PHY on PB14/PB15, no VBUS sensing | OTG_FS on PA11/PA12, no VBUS sensing |
| Inputs 1–4 | PE4, PE5, PE6, PD2 (pull-down, active high) | PB3, PA15, PB4, PB15 (no pull, active low, inverted in software) |
| Outputs 1–4 | PE0, PE1, PE2, PE3 (push-pull) | PB2, PB10, PA0 (relays, active high); no output 4 |
| Status LED | — | PB8, switched on in board init |
| Buzzer | — (`SET_BUZZER` answers ERROR, `BUZZER=0`) | PC13, active high; driven by `SET_BUZZER` (`BUZZER=1`) |
| SWD | on-board ST-LINK | CN7: PA13 SWDIO, PA14 SWCLK, NRST |

Notes:

- F429: PE7 is avoided because the Discovery uses it for SDRAM (`FMC_D4`). I2C3 (PA8/PC9) is shared with the Discovery's touch controller (STMPE811 at 0x41) and PA15 is that controller's interrupt line, which is why ESP EN is open drain.
- F411: PB3, PA15 and PB4 are JTAG pins after reset; using them as inputs does not affect SWD.
- F411: output 4 does not exist. `GET_CAPABILITIES` reports `GPO=3` (`Industrial_IO_GetOutputCount()`), and `SET_GPO,4,...` fails.

### 4.3 Memory

| | F429 | F411 |
|---|---|---|
| Bootloader | sector 0, 0x08000000, 16 KB | sector 0, 0x08000000, 16 KB |
| Code region (linker) | 0x08004000, 624 KB (sectors 1–11) | 0x08004000, 112 KB (sectors 1–4) |
| Update staging | sectors 12–20, 0x08100000, 640 KB | SPI flash 0x200000, 2 MB |
| Reader settings | sector 21, 0x081A0000 | sector 5, 0x08020000 |
| Identium config A / B | sector 22, 0x081C0000 / sector 23, 0x081E0000 | sector 6, 0x08040000 / sector 7, 0x08060000 |
| RAM | 192 KB at 0x20000000 (+ 64 KB CCM, unused) | 128 KB at 0x20000000 |
| Stack budget | `_Min_Stack_Size = 0x4000` (16 KB) | 16 KB |
| `.noinit` | fault record | fault record |

Static RAM plus the 16 KB stack reserve is about 113 KB (88.5 % of the F411, release build), which leaves about 15 KB. That figure includes the tag backup log, and the Identium tag queue packed into an 8 KB ring (design 0019). The largest RAM users are now the HTTP upload queue (12 KB), the HTTP request buffer (8.7 KB), the tag batch buffer (8 KB) and the Identium tag ring (8 KB).

---

## 5. Communication paths and protocols

### 5.1 Overview

| Path | Physical | Port / address | Protocols carried | Code |
|---|---|---|---|---|
| USB CDC | USB virtual COM port | VID 0x0483, PID 0x5740 ("STM32 Virtual ComPort") | ASCII text commands, vendor-tool FF / EE / board tokens | `usbd_cdc_if.c`, `usb_cdc_transport.c`, `host_transport.c` |
| Host serial | RS232 / RS485 (F411), ST-LINK VCP (F429) | 115200 8N1 | Same as USB CDC | `serial_port.c`, `host_transport.c` |
| Host TCP (Ethernet) | W5500 socket 3 | TCP 5000, server, one client | ASCII text commands only | `host_tcp_server.c` |
| Host TCP (WiFi) | ESP32 socket 0 | TCP 5000, server, one client | ASCII text commands only | `wifi_service.c` |
| Upload (Ethernet) | W5500 socket 0 | client to `serverIp:serverPort` (default 192.168.150.100:9000), source port 40001; HTTP: 50000–50999 | Identium JSON lines or HTTP POST | `ethernet_service.c`, `http_upload.c` |
| Upload (WiFi) | ESP32 socket 1 | client, same target | Same | `wifi_service.c`, `active_upload_transport.c` |
| Configuration port | W5500 socket 1 | TCP 8080, server | EE configuration requests, or module-bridge session | `configuration_port.c`, `module_bridge.c`, `vendor_config.c` |
| Discovery | W5500 socket 2 | UDP 15000 | Vendor-tool device search | `discovery_service.c` |
| RFID module | UART | 115200 8N1 | Silion `FF` frames | `silion_protocol.c`, `sillion_application*.c` |
| ESP32 link | I2C 400 kHz | slave 0x42 | `RW` link frames | `esp32_link.c`, `esp32_link_protocol.h` |

Default reader network settings (`reader_settings.c`): IP 192.168.150.50, mask 255.255.255.0, gateway 192.168.150.1, DNS 0.0.0.0, MAC `02:1D:xx:xx:xx:xx` derived from a CRC32 of the MCU unique ID, reader name `RFID-F429-DEV`, work state ACTIVE, upload interface AUTO, WiFi off.

### 5.2 Data flow

```
 PC / server side                              STM32                                      Radio side
 ──────────────────                            ─────                                      ──────────

 USB CDC  ──(bytes)──┐
                     ├─► host RX queue (1 KB, tagged with source)
 Host UART ─(IRQ)────┘          │
                                ▼
                    vendor-tool router (USB and serial only, per link)
                     │ line starts with FF / IOGET IOSET IPGET IPSET GIO SIO VER
                     │        └──────────────────────────────► module_bridge ──┐
                     │ line starts with EE 00 len cmd flags ──► configuration   │
                     │                                          port dispatch   │
                     ▼ everything else                                          │
 TCP 5000 (W5500 S3) ──────► host lock (first printable byte claims it)        │
 TCP 5000 (ESP32 S0) ──────►        │                                           │
                                    ▼                                           │
                          host_interface.c  ── ASCII commands ──► sillion_application ──► RFID UART ──► E310
                                    ▲                                    │                    ▲
               replies: active transport;                                │ tags              │ raw FF frames
               TAG lines: tag transport ◄────── HOST_SendTag ◄───────────┤                    │ (bridge owns UART)
                                                                         ▼                    │
                                                         Identium_Protocol_ReportTag          │
                                                          (tag queue, 256 entries)            │
                                                                         │                    │
 Server ◄── JSON lines / HTTP POST ◄── active_upload_transport ◄─────────┘                    │
 (default 192.168.150.100:9000)       (Ethernet S0 or WiFi S1)                                │
 Server ──► JSON commands ──► Identium_Protocol_PushTcpByte ──► identium_command.c            │
                                                                                              │
 Reader Manager / ReaderSense ── TCP 8080 (S1) ── first byte EE → configuration_port          │
                                                  anything else → module_bridge ──────────────┘
                              ── UDP 15000 (S2) ── discovery_service

 ESP32 ◄── I2C 0x42 (poll) ──► esp32_link ──► wifi_service (host socket 0, upload socket 1)
```

### 5.3 Host transports and the host lock

`Firmware/Services/host/host_transport.c`.

Transport IDs: `HOST_TRANSPORT_USB` 0, `ETHERNET` 1, `NONE` 2, `SERIAL` 3, `WIFI` 4. All four push received bytes into one 1024-byte queue, each byte tagged with its source. Overflow is counted and reported as `ERROR_HOST_RX_OVERFLOW` (system fault, not a reply).

The text parser can only listen to one transport at a time (the "host lock"):

- While the lock is free, the first printable byte (0x20–0x7E) from any transport claims it. Control characters and CR/LF cannot claim it, so a trailing LF after `RELEASE` or RS485 line noise does not lock the others out.
- While it is held, bytes from every other transport are dropped silently.
- Replies (`HOST_SendString`) go to the transport that holds the lock.
- `START`, `SINGLE_POLL` and `SYNC_POLL` set the tag transport to the transport they came from; `HOST_SendTag` sends `TAG,...` lines there.

The lock is released by:

| Event | Code |
|---|---|
| `RELEASE` command (also stops async inventory, flushes the RX queue, clears the tag transport) | `host_interface.c`, `HOST_Transport_Release()` |
| Ethernet host client disconnects while it holds the lock | `host_tcp_server.c` |
| WiFi host client gone or ESP32 link down while WiFi holds the lock | `wifi_service.c` |

USB CDC and the serial port have no close event, so they never release the lock by themselves. Always end a test script or tool session on USB or serial with `RELEASE`.

The vendor-tool router (5.6) runs before the lock check, so `FF`, `EE` and board tokens on USB or serial are handled even while another transport holds the lock.

Output queues:

| Transport | TX buffer | Behaviour when full |
|---|---|---|
| USB CDC | 4096 bytes, sent in 64-byte packets from task context | dropped, counted; nothing is sent until the port is enumerated |
| Serial | 2048 bytes, interrupt driven | dropped, counted |
| TCP 5000 | 2048-byte ring | rest of the message dropped |
| WiFi host | 2048-byte queue, 200-byte SOCK_SEND chunks | — |

RS485 (`serial_port.c`): DE goes high (and RE# high, receiver off) before the first start bit and is released on the USART transmission-complete interrupt. Bytes received while driving are discarded. The power-up mode comes from `PRODUCT_SERIAL_DEFAULT_MODE` (0 = RS232, 1 = RS485, overridable with `-D`). No host command switches the mode yet.

### 5.4 ASCII text command set

Parser: `HOST_Interface_Task()` in `Firmware/Application/Host/host_interface.c`.

Framing:

- A command ends with CR. LF is ignored, so `CMD\r` and `CMD\r\n` are the same. An empty line is ignored.
- Non-printable bytes are ignored. The buffer holds 319 characters; extra characters are dropped.
- Names are case sensitive.
- Unknown command: `ERROR,INVALID_COMMAND`.
- Every reply ends with `\r\n`.

Module commands (`GET_VERSION` ... `WRITE_TAG`, `SET_POWER` ...) only work while the module is idle (not inventorying, no bridge session); otherwise they answer `ERROR,<CMD>`. When a module transaction fails, one diagnostic line is sent first: `DEBUG,TX_TIMEOUT,TXEVENTS=n`, `DEBUG,RX_TIMEOUT`, `DEBUG,FRAME_ERROR`, `DEBUG,RX_UART_ERROR`, `DEBUG,CMD_MISMATCH,EXP=xx,GOT=xx` or `DEBUG,STATUS=xxxx` (module status, hex).

The RF set commands are saved to flash and restored at boot (design 0018): `SET_POWER` and `SET_REGION` in the reader settings (`rfValid`, the same fields the vendor tools write), `SET_ANTENNA` / `SET_ANTENNAS` and `SET_GEN2` in the reader settings (record version 4), and `SET_SESSION` in the configuration service record. A command answers `OK` only when the module accepted the value and it was saved.

#### Session and inventory

| Command | Purpose | Reply |
|---|---|---|
| `RELEASE` | Stop async inventory, free the host lock, flush host input | `OK,RELEASE` |
| `START` | Start async inventory (module `AA 48`); tags stream as `TAG` lines on this transport and as JSON `tag_read` on the upload link | `OK,START` / `ERROR,START` (also when already running) |
| `STOP` | Stop async inventory; retries up to 3 times; OK when already idle | `OK,STOP` / `ERROR,STOP` |
| `SINGLE_POLL[,<ms>]` | Single-tag inventory (`0x21`), timeout 1–65535 ms (default 1000), up to 3 attempts | `TAG,...` or `NO_TAG`, then `OK,SINGLE_POLL`; `ERROR,SINGLE_POLL` |
| `SYNC_POLL[,<ms>]` | Synchronous inventory (`0x22`) then read the tag buffer (`0x29`) | one `TAG,...` per tag, then `OK,SYNC_POLL`; `ERROR,SYNC_POLL` |
| `GET_STATUS` | Inventory state | `STATUS,INVENTORY` / `STATUS,IDLE` |

Tag line format (`HOST_SendTag`):

```
TAG,EPC=E2806894000050328410E5CD,RSSI=-59,ANT=1,FREQ=865700,TIME=123456\r\n
```

`FREQ` is in kHz, `TIME` is the MCU tick in ms.

#### Module information

| Command | Reply |
|---|---|
| `GET_VERSION` | `VERSION,BTVER=xxxxxxxx,HWVER=xxxxxxxx,FWDATE=xxxxxxxx,FWVER=xxxxxxxx,PROTO=xxxxxxxx` |
| `GET_SERIAL` | `SERIAL,YEAR=<n>,SN=<16 hex>` |
| `GET_TEMPERATURE` | `TEMP,<°C>` |
| `GET_REGION` | `REGION,<hex>` |
| `GET_REGIONS` | `REGIONS,<hex>,<hex>,...` |
| `GET_ANTENNA` | `ANTENNA,TX=<n>,RX=<n>` |
| `GET_POWER` | `POWER,ANT=<n>,READ=<n>,WRITE=<n>` |
| `GET_PROTOCOL` | `PROTOCOL,<4 hex>` |
| `GET_SESSION` | `SESSION,<n>` |
| `GET_FREQUENCY` | `FREQ,<kHz>,<kHz>,...` (hop table) |

Each answers `ERROR,<CMD>` on failure.

#### Module settings

| Command | Rules | Reply |
|---|---|---|
| `SET_POWER,<p>` | Same value for read and write power, 0–5000 (module units; default 2500) | `OK,SET_POWER` / `ERROR,SET_POWER` |
| `SET_ANTENNA,<tx>,<rx>` | 1–255 each | `OK,SET_ANTENNA` / `ERROR,SET_ANTENNA` |
| `SET_REGION,<hex>` | Only `01` (North America), `06` (China 1), `08` (India), `0A` (China 2) | `OK,SET_REGION` / `ERROR,SET_REGION` |
| `SET_SESSION,<0-3>` | Gen2 session | `OK,SET_SESSION` / `ERROR,SET_SESSION` |

#### Tag access

| Command | Rules | Reply |
|---|---|---|
| `READ_TAG,<epc>,<bank>,<addr>,<words>` | EPC hex, even length up to 124 chars; words 1–96 | `READ_DATA,BANK=<b>,ADDR=<a>,WORDS=<w>,DATA=<hex>` then `OK,READ_TAG`; `ERROR,READ_TAG` |
| `WRITE_TAG,<epc>,<bank>,<addr>,<accessPwdHex>,<dataHex>` | bank 0–3, data up to 64 hex chars, whole words | `OK,WRITE_TAG` / `ERROR,WRITE_TAG` |
| `WRITE_EPC,<oldEpc>,<newEpc>` | both hex, even number of bytes, at most 62 hex characters each | `OK,WRITE_EPC` / `ERROR,WRITE_EPC` |
| `LOCK_TAG,<epc>,<accessPwdHex>,<maskHex>,<actionHex>` | mask/action ≤ 0x3FF, EPC ≤ 31 bytes | `OK,LOCK_TAG` / `ERROR,LOCK_TAG` |
| `KILL_TAG,<epc>,<killPwdHex>` | EPC ≤ 31 bytes | `OK,KILL_TAG` / `ERROR,KILL_TAG` |

#### Board, network and work mode

| Command | Purpose | Reply |
|---|---|---|
| `GET_CAPABILITIES` | Feature list for the desktop app | `CAPABILITIES,GPI=4,GPO=<3\|4>,ANTENNAS=1,HTTP=1,VSWR=1,SET_GPO=1,SET_HTTP=1,SET_GEN2=1,BUZZER=<0\|1>` |
| `GET_GEN2` | Gen2 target, Q and RF mode read back from the module (`PROFILE` is left out if the module does not answer that query) | `GEN2,TARGET=<A\|B\|AB>,Q=<DYNAMIC\|0..15>,PROFILE=<n>` |
| `GET_GPI` | Input states | `GPI,GPI1=<0/1>,GPI2=..,GPI3=..,GPI4=..` |
| `SET_GPO,<n>,<state>,<seconds>` | Output n (1–4), state 0/1, duration 0–3600 s (0 = stay); a timed pulse restores the previous state; rejected while a timed pulse is running | `OK,SET_GPO` / `ERROR,SET_GPO` |
| `GET_NETWORK` | Stored Ethernet settings | `NETWORK,IP=a.b.c.d,MASK=..,GATEWAY=..,DNS=..,PORT=8080,MAC=xx:..` (PORT is the configuration port) |
| `GET_WORK_MODE` | | `WORK_MODE,PASSIVE` / `WORK_MODE,ACTIVE` |
| `SET_WORK_MODE,PASSIVE\|ACTIVE` | Persisted. PASSIVE answers OK only once inventory has stopped. ACTIVE starts inventory if "start at boot" is set | `OK,SET_WORK_MODE` / `ERROR,SET_WORK_MODE` |
| `SET_HTTP,<url>,<timeoutS>,<startAtBoot>` | Store HTTP upload settings (URL up to 127 chars, no `"` or `\`); work state unchanged | `OK,SET_HTTP` / `ERROR,SET_HTTP` |
| `SET_HTTP,<url>,<timeoutS>,<active>,<startAtBoot>` | Older form; also sets the work state | same |
| `GET_WIFI` | WiFi link and settings | `WIFI,LINK=ONLINE\|OFFLINE,STATE=DISABLED\|IDLE\|CONNECTING\|CONNECTED\|AP\|ERROR,IP=..,RSSI=..,HOST=0\|1,UPLOAD=0\|1,ENABLE=0\|1,MODE=STA\|AP,SSID=..,IF=ETH\|WIFI\|AUTO,ERRORS=<n>,REJECTED=<cmd hex>/<result>` |
| `SET_WIFI,<0\|1>,<STA\|AP>,<ssid>,<password>` | Persisted and applied. SSID without commas; password = rest of the line, empty or 8–64 chars; `****` keeps the stored password. `SET_WIFI,0,STA,,` disables WiFi and clears the station credentials | `OK,SET_WIFI` / `ERROR,SET_WIFI` |
| `SET_UPLOAD_IF,ETH\|WIFI\|AUTO` | Interface for active uploads (persisted) | `OK,SET_UPLOAD_IF` / `ERROR,SET_UPLOAD_IF` |
| `SET_BUZZER,<ms>[,<tone>]` | Sound the buzzer for 1..10000 ms (fixed-tone buzzer, `<tone>` ignored). Boards without a buzzer answer ERROR | `OK,SET_BUZZER` / `ERROR,SET_BUZZER` |
| `SET_GEN2,<A\|B\|A/B>,<q>[,<profile>,<fast>,<hop>]` | Gen2 target and Q (`-1` = dynamic, 0..15 static). `profile` = Gen2 RF mode, the module's gen2tagEncoding (103, 105, 107, 112, 113, the modes the E310 supports; others are rejected; 0 = leave the module as it is), sent as module frame `9B 05 02 <value>`. `fast=1` = Reader Manager Fast Mode: forces Session 0 (saved) and Target A/B. `hop` must be 0. Target, Q and profile are saved (`gen2Profile` uses the former always-zero `reserved` byte, so the record version is unchanged) and re-applied at boot | `OK,SET_GEN2` / `ERROR,SET_GEN2` |
| `SET_ANTENNAS,<port>[,<port>...]` | Inventory antenna; the list must name one port (repeats allowed), same as `SET_ANTENNA,p,p`; saved | `OK,SET_ANTENNAS` / `ERROR,SET_ANTENNAS` |

#### Diagnostics

| Command | Reply |
|---|---|
| `GET_DIAG` | `DIAG,...` line, see 7.5 |
| `CLEAR_DIAG` | `OK,CLEAR_DIAG` (resets the loop-timing maxima only) |
| `REBOOT` | `OK,REBOOT`, then a software reset 500 ms later (`RESET=SOFTWARE` in `GET_DIAG` afterwards). Tag-log records still queued in RAM are lost |
| `TAGLOG_STATUS` / `TAGLOG_READ,<seq>,<count>` / `TAGLOG_CLEAR` | Tag backup log in the F411's SPI flash, see design note 0019. `ABSENT` on the F429 |
| `FW_STATUS` / `FW_BEGIN` / `FW_DATA` / `FW_END` / `FW_APPLY` / `FW_ABORT` | Signed firmware update, see design note 0020. `Tools/fw_update.py` sends the whole sequence |
| `DEBUG_FAULT,USAGE\|BUS\|STACK` | Debug builds only (`DEBUG` defined). Crashes the MCU on purpose; no reply. Unknown argument: `ERROR,DEBUG_FAULT`. In a release build the command does not exist: `ERROR,INVALID_COMMAND` |

### 5.5 Identium JSON-over-TCP protocol (active mode)

Code: `Firmware/Protocol/identium_protocol.c` (framing, events, tag queue) and `Firmware/Protocol/identium_command.c` (dispatcher).

The protocol specification (`docs/reader-protocol-specification.pdf`, Part C) defines a binary TCP framing (`FF`, name length, data length, message ID, flags, status, name, payload, optional CRC). **This firmware does not implement the binary framing.** It implements the JSON-over-TCP variant from Part C.11: newline-delimited JSON objects on a TCP connection that the reader opens to the server. The same JSON envelopes are used for HTTP (Part D). MQTT is not implemented.

Connection (`ethernet_service.c` / `wifi_service.c`):

- Only in work state ACTIVE. In PASSIVE, socket 0 stays closed.
- Target: `upload.tcp.ser_ip:ser_port` from reader settings (default 192.168.150.100:9000), IPv4 address only.
- Reconnect with exponential backoff, 1 s doubling to 30 s. W5500 keep-alive 30 s.
- The interface is chosen by `upload.interface` (`SET_UPLOAD_IF`): ETH, WIFI, or AUTO (Ethernet while the W5500 PHY link is up, otherwise WiFi).

Receive framing (`Identium_Protocol_PushTcpByte`):

- CR is ignored, LF ends a line.
- Leading spaces/tabs and any garbage before the first `{` are skipped.
- The line buffer is 2048 bytes; a longer line is discarded up to its LF.

Command envelope (server → reader):

```json
{"command_type":"set_gpo","command_data":{"gpo":[...]}}
```

| `command_type` | Behaviour | Reply `err_code` / `err_string` |
|---|---|---|
| `pause_inventory` | Stop async inventory (OK if already stopped) | 0 `ok` / 3 `inventory stop failed` |
| `resume_inventory` | Start async inventory | 0 `ok`, 0 `reader is inventorying now` / 3 `inventory start failed` |
| `sync_time` | `command_data` = epoch ms, must be ≥ 1000000000000 | 0 `ok` / 3 `invalid epoch milliseconds` |
| `reboot` | Reply, then reset after 100 ms | 0 `ok` |
| `set_gpo` | `gpo`, `state`, `duration` (seconds, 0–3600), 1–4 outputs, no duplicates; rejected while a timed pulse runs | 0 `ok`; 20003 / 20004 on invalid or missing fields |
| `get_gpi` | Sends a `gpi_changed` event with all 4 inputs, then the reply | 0 `ok` |
| `set_runtime_conf` | Partial ActiveMode update, persisted, then reset after 100 ms | from the configuration service |
| `set_static_conf` | Partial StaticModel update, persisted; `reboot`/`reset` controls | from the configuration service |
| `set_temp_static_conf` | Live only; inventory must be stopped | from the configuration service |
| `update_fw_by_http` | Not supported | 2 `not supported operation` |
| anything else / no `command_type` | | 1 `invalid command_type` |

The writable fields and their ranges are in design records 0010 and 0012 (`hb_cylce` 5–7200 s, `s_buf_size` 1500–8192, `data_aggr.mode` 0/1, `data_aggr.timeval` 50–86400000 ms, events 1/2/3/6, `cus_param` ≤ 64 chars, `protocol.gen2.session` 0–3, ...). Unsupported fields are rejected, not ignored.

Events (reader → server), each one JSON object plus `\n`:

| Event | Code | When | Shape |
|---|---|---|---|
| `heart_beat` | 2 | On connect, then every `hb_cylce` s (default 10) | `{"reader_name":"..","event_type":"heart_beat","event_data":<counter>}` |
| `tag_read` | 1 | From the tag queue | `{"reader_name":"..","event_type":"tag_read","event_data":[{"ep":"<EPC hex>","at":<ant>,"rc":<count>,"ri":<rssi>,"fq":<kHz>},...]}` |
| `gpi_changed` | 3 | Input change, or `get_gpi` | `{..,"event_data":{"gpi_states":[{"gpi":1,"state":0},..4],"timestamp":<epoch ms>}}` |
| `sync_time_req` | 6 | Once per boot, on the first successful connection | `{"reader_name":"..","event_type":"sync_time_req"}` |
| `command_reply` | — | After each command, if `remote_cmd_reply` = 1 | `{..,"event_data":{"command_type":"..","err_code":n,"err_string":".."}}` |
| `reader_exception` | — | `Identium_Protocol_ReportReaderException()` | `{..,"event_data":{"err_code":n,"err_string":".."}}` |

Events are sent only if their code is in the configured `events` list (default `[1,2,3]`).

Tag queue: tags from every inventory path (`START`, `SINGLE_POLL`, `SYNC_POLL`, `resume_inventory`, start at boot) go into a 256-entry queue. Tags older than `clr_r_buf_time` (default 8 s) are discarded. A message is never larger than `s_buf_size` (default 1580 bytes). `data_aggr.mode` 0 sends tags as they come (at most 4 messages per loop pass); mode 1 batches over `data_aggr.timeval` ms. A message is written only when the whole message fits in the TX buffer (`Ethernet_Service_SendComplete`), so JSON is never truncated.

Recovery timers `thwdt_net_timeout` and `thwdt_inv_timeout` exist but default to 0 (off).

### 5.6 Configuration port 8080, vendor tools and discovery

Reader Manager 2.2 and ReaderSense 4.0.0 (both built on `ModuleAPI.dll`) treat the reader as an "ARM7 mainboard". Design 0013 has the full background. The firmware reproduces it on three paths:

| Path | How a session starts |
|---|---|
| TCP 8080 (W5500 socket 1) | First byte of the connection: `EE` → one configuration request; anything else → module-bridge session |
| USB CDC | Per-line router in `host_transport.c` |
| Host serial port | Same router, separate instance |

The serial-link router looks at the start of each line:

| Line starts with | Goes to |
|---|---|
| `FF` | module bridge (session starts) |
| `IOGET`, `IOSET`, `IPGET`, `IPSET`, `GIO`, `SIO`, `VER` | module bridge (session starts) |
| `EE 00 <lenHi> <lenLo> <cmd> <flags> ...` (+2-byte CRC16-Modbus when flags bit 1 is set; the CRC is not checked) | configuration request, reply on the same link |
| anything else | legacy ASCII parser |

A partly received token or EE frame is given up after 500 ms of silence.

#### EE configuration frames

Request (big-endian length):

```
EE 00 LEN_HI LEN_LO CMD FLAGS DATA[LEN]
```

Reply:

```
FF 00 LEN_HI LEN_LO CMD 00 STATUS(4 bytes, BE) CONTENT[LEN]
```

| CMD | Meaning | Request data | Reply content |
|---|---|---|---|
| `0x22` | Get work state | 1 byte | 1 byte: 1 passive, 3 active |
| `0x24` | Get ActiveModeJson | 2 bytes | JSON, no NUL padding (ReaderSense calls `JSON.parse`) |
| `0x1E` | Get StaticModelJson | 2 bytes | JSON |
| `0x25` | Set ActiveModeJson | JSON | 1 byte (work state); switches to ACTIVE |
| `0x1F` | Set StaticModelJson | JSON | 1 byte; `ethernet.ip/nm/gw/dns`, `rf.region`, `rf.tx_powers[0].rp/wp`, `protocol.gen2.session`; `reboot:1` restarts |
| `0x20` | Clear parameters | `00 50` | 1 byte; config to defaults, work state PASSIVE |
| `0x23` | Switch work state | `01` | 1 byte; toggles PASSIVE ↔ ACTIVE |
| other | | | status 1 |

Status: 0 OK, 1 invalid, 2 failed. On TCP each request uses its own connection; the reader waits for the client to close, then reopens the listener. Writes are answered first and applied in `VendorConfig_Task()` on the next loop pass, because Reader Manager waits only 1000 ms and a sector erase can take longer. Fields the reader cannot act on are ignored (list in design 0013).

#### Module bridge

`Firmware/Services/configuration/module_bridge.c`:

- `FF` module frames (`FF LEN CMD DATA CRC16`) are forwarded unchanged to the E310 UART; module output is relayed back unchanged.
- While a session is open the bridge owns the module (`SILION_APP_BRIDGE`, UART in raw mode). Local inventory is stopped and restarted 5 s after the session ends. Module commands from the text parser and JSON (`resume_inventory`, ...) fail during that time.
- Board commands answered locally, inserted only between module frames:

| Request | Reply |
|---|---|
| `IOGET` | `IOGET` + GPI bitmap (bit 0 = GPI1; never 0x80) |
| `IOSET 01 <pin> <state>` | `IOSETOK` (GPO 1–4; pin 5 = buzzer, acknowledged without hardware) |
| `GIO` during fast inventory | `FF 04 AA 00 00 'G' 'I' 'O' <bitmap> CRC` |
| `SIO 01 <pin> <state>` during fast inventory | `FF 03 AA 00 00 'S' 'I' 'O' CRC` |
| `VER` | 4 bytes: `PRODUCT_MAINBOARD_VERSION_0..3` (currently 1.0.0.8) |
| `IPGET` | `IPGET` ip(4) mask(4) gw(4) mac(6) port(BE16) |
| `IPSET` ip mask gw mac port | no reply; saved, applied after 1.5 s |
| 32 × `FF` (module reset) | discarded |

- Only one session owns the module. A session on another transport takes over after the current one's PC has sent nothing for 3 s (module output does not count); a taken-over TCP client is disconnected.
- A serial-link session (USB or serial) ends after 30 s with no traffic in either direction. While it is open, all bytes on that link go to the bridge, except that a printable byte at a frame boundary that cannot start a board token ends the session and goes to the text parser.
- The tools' "USB" address type is USB HID (VID 0x2E88 / PID 0x4605) and is not supported; use the COM address.
- Firmware update from the tools (`INITUPFV`) is not supported.

#### Discovery (UDP 15000)

`discovery_service.c`, W5500 socket 2. Request: `EE 06 .. .. 50 ..` + 6-byte target MAC at offset 6 (all zero = any reader), at least 12 bytes. Reply, 47 bytes, unicast to the sender:

| Offset | Content |
|---|---|
| 0–5 | `FF 06 00 1F 50 00` |
| 6–9 | status (0) |
| 10–15 | MAC |
| 16 | DHCP flag (0) |
| 17–32 | IP, netmask, gateway, DNS |
| 33–34 | configuration port, BE (8080) |
| 35 | board type 1 (Hc32f46x, as the tools expect) |
| 38–41 | mainboard version |
| 46 | work state |

### 5.7 Silion module protocol and tag flow

`Firmware/Protocols/silion/silion_protocol.c`:

```
Host → module:  FF | LEN | CMD | DATA[LEN] | CRC16 (BE)
Module → host:  FF | LEN | CMD | STATUS_HI STATUS_LO | DATA[LEN] | CRC16 (BE)
```

CRC-16/CCITT, polynomial 0x1021, initial 0xFFFF, over LEN through the last data byte (header `FF` and CRC excluded). Status `0x0000` = success, `0x0400` = no tags found.

Commands used:

| Code | Command | Code | Command |
|---|---|---|---|
| 0x03 | Get version | 0x61 | Get antenna ports / power |
| 0x04 | Boot firmware | 0x63 | Get tag protocol |
| 0x0C | Get run phase | 0x65 | Get frequency hopping table |
| 0x10 | Get serial number | 0x67 | Get current region |
| 0x21 | Single-tag inventory | 0x6B | Get protocol config (session) |
| 0x22 | Synchronous inventory | 0x71 | Get available regions |
| 0x23 | Write EPC | 0x72 | Get temperature |
| 0x24 | Write tag data | 0x91 | Set antenna ports / power |
| 0x25 | Lock tag | 0x93 | Set tag protocol |
| 0x26 | Kill tag | 0x97 | Set region |
| 0x28 | Read tag data | 0x9B | Set protocol config (session) |
| 0x29 | Get tag buffer | 0xAA | "Moduletech" sub-commands: `AA 48` start / `AA 49` stop async inventory |

Async inventory (`START`): `FF` + `AA` frame with the ASCII marker `Moduletech`, sub-command `AA 48`, metadata flags `0x00BF`, option 0, search flags 0, sub-CRC (byte sum). The module then streams `AA` tag frames until `AA 49`.

Tag flow:

1. USART RX interrupt → `SILION_Application_Port_UartEvent` → 4 KB RX queue (`sillion_application_port.c`).
2. `SILION_Application_Task()` → `SILION_ProcessRxQueue()` parses frames; async tag frames go into a 32-entry tag queue in the Silion handle.
3. One tag per loop pass is popped and sent to both `Identium_Protocol_ReportTag()` (JSON upload queue) and `HOST_SendTag()` (`TAG,...` line on the tag transport).
4. `SINGLE_POLL` and `SYNC_POLL` call the same two functions directly.

In bridge mode the UART is switched to raw mode and module bytes go to the bridge instead of the parser.

VSWR: the desktop app's return-loss test uses module request `AA 4A` through the bridge. Status `0xAA4A` with no samples means no antenna on that port (`0x0105` wrong antenna, `0x0500` wrong region). The antenna-detect query `0x61 05` reports "not detected" even with an antenna, so do not use it as a pre-check (design 0015).

### 5.8 Active mode and upload

Work state (`reader_settings.c`): PASSIVE (1) or ACTIVE (3), persisted.

| | PASSIVE | ACTIVE |
|---|---|---|
| Upload connection (socket 0 / ESP socket 1) | closed | open to the configured server |
| Inventory at boot | no | if `start_inv_atbeg` = 1 |
| Host commands, 8080, discovery | yes | yes |

Set it with `SET_WORK_MODE`, EE `0x23`/`0x20`/`0x25`, or the older `SET_HTTP` form.

Upload mode (`ReaderSettings_t.uploadMode`): TCP (JSON lines, 5.5) or HTTP. HTTP is selected by `upload.http` in a set-ActiveModeJson request or by `SET_HTTP`; `upload.tcp` switches back.

HTTP upload (`Firmware/Services/ethernet/http_upload.c`):

- URL: `http://` + numeric IPv4 + optional port (default 80) + path. No `https://`, no host names (no TLS, no DNS).
- Each event becomes one `POST <path>` with exact `Content-Length` and `Connection: keep-alive`. `reader_name` is sent as `name/reader-ip`.
- One request in flight; others wait in a 12 KB queue.
- A non-empty 2xx response body is executed as one JSON command (same dispatcher as 5.5); its `command_reply` goes out as the next POST. Non-2xx bodies are not executed. Content-Length, chunked and close-delimited bodies are accepted.
- Timeout: `upload.http.timeout`, else `upload.recv_timeout` (default 10 s), clamped to 2–120 s. Up to 3 attempts on a new connection, then the event is dropped.
- Source port 50000–50999, new for each connection.

Upload interface (`active_upload_transport.c`): `ETH` → W5500 socket 0, `WIFI` → ESP socket 1, `AUTO` → Ethernet while the PHY link is up, otherwise WiFi. HTTP formatting stays on the STM32 for both.

### 5.9 ESP32 I2C link

Code: `Firmware/Devices/esp32/esp32_link.c`, `Firmware/Services/wifi/wifi_service.c`. The wire format is in `Firmware/Devices/esp32/esp32_link_protocol.h`, which the ESP-IDF project includes directly.

- I2C 400 kHz (fast mode), STM32 master, ESP slave at 7-bit address `0x42`. STM32 own address `0x7F` so a bus glitch cannot make it a slave receiver.
- No interrupt line: the STM32 polls status about every 5 ms.
- Interrupt-driven, non-blocking transfers; the WiFi service is a state machine. A dead ESP never stops the main loop.

Frame (little-endian fields, except the TCP port in `SOCK_OPEN`, which is big-endian):

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | magic `0x52 0x57` ("RW") |
| 2 | 1 | protocol version 1 |
| 3 | 1 | sequence number (echoed) |
| 4 | 1 | command |
| 5 | 1 | socket (0 host, 1 upload) |
| 6 | 2 | payload length |
| 8 | 1 | flags (`0x01` RESPONSE, `0x02` NOT_READY, `0x04` ERROR) |
| 9 | N | payload (request ≤ 220 bytes) |
| 9+N | 2 | CRC-16/CCITT-FALSE (0x1021, init 0xFFFF), low byte first |

Responses are always read as 64 bytes (≤ 53 payload bytes, zero padded). The first payload byte is a result: 0 OK, 1 FAILED, 2 INVALID, 3 BUSY, 4 NOT_CONNECTED.

Commands: `0x01` HELLO, `0x02` GET_STATUS, `0x10` WIFI_CONFIG, `0x11` WIFI_CONNECT, `0x12` WIFI_DISCONNECT, `0x13` SCAN_START, `0x14` SCAN_RESULTS, `0x20` SOCK_OPEN, `0x21` SOCK_CLOSE, `0x22` SOCK_SEND, `0x23` SOCK_RECV, `0x30` RESET.

Timing (`esp32_link.c`): wait ≥ 2 ms between write and read; replies are awaited for up to 1.5 s; 2 retries with the same sequence number (the ESP replays its last reply instead of executing twice); after 3 failures: STOP, up to 9 SCL pulses, I2C re-init, EN low 15 ms, 800 ms boot wait, new HELLO, settings re-sent, sockets reopened. EN is pulsed once at start-up.

WiFi sockets: socket 0 is a TCP server on port 5000 (same port as the Ethernet host server), bytes go to the host router as `HOST_TRANSPORT_WIFI`; socket 1 is the upload client. Credentials are not stored on the ESP; the STM32 sends them after each handshake.

Bench results and fixes for the classic ESP32 DevKit are in design 0016.

### 5.10 W5500 Ethernet

`Firmware/Devices/w5500/w5500.c`:

| Socket | Use | Mode |
|---|---|---|
| 0 | Active upload client (JSON TCP or HTTP) | TCP client, keep-alive 30 s |
| 1 | Configuration port 8080 / module bridge | TCP server |
| 2 | Discovery, UDP 15000 | UDP |
| 3 | Host text commands, TCP 5000 | TCP server, one client |

The W5500 is reset through its RESET pin at init (`Time_DelayMs(2)` low, `50` ms settle) and polled; the INT pin is not used. Socket sends wait until the previous SEND has completed (required by the W5500). The network settings (IP, mask, gateway, MAC) are written by `ReaderSettings_ApplyNetwork()`; DHCP is not supported on Ethernet.

---

## 6. Settings and flash storage

Two independent stores, both in the last flash sectors, which the linker scripts keep out of the code region.

### 6.1 Reader settings (`Firmware/Services/configuration/reader_settings.c`)

What: IP, mask, gateway, DNS, MAC, configuration port, upload server IP/port, work state, RF region and read/write power (`rfValid`), reader name (≤ 32 chars), upload mode, HTTP URL/timeout, WiFi (enable, STA/AP, DHCP, AP channel, SSIDs, passwords, static IP set), upload interface.

How:

- One 128 KB sector holds an append-only log of fixed-size records: `magic "RST1"`, `version` (4), `dataSize`, `sequence`, `ReaderSettings_t`, CRC32.
- Load takes the last valid record that passes `ReaderSettings_IsSane()`; otherwise defaults (5.1).
- Save appends to the next erased slot. When the sector is full, it is erased and the new record is written at the start.
- Version 1 and 2 records are read once and migrated in RAM; the next save erases the sector and starts a version-3 log.
- WiFi passwords are never returned by readback; settings JSON shows `"****"`, and sending `****` back keeps the stored password.

| Board | Sector | Address |
|---|---|---|
| F429 | 21 | 0x081A0000 |
| F411 | 5 | 0x08020000 |

Limitation (design 0017): there is one copy. A power cut between erasing a full sector and writing the new record loses the settings (they fall back to defaults).

### 6.2 Identium configuration (`configuration_storage.c`, `configuration_service.c`)

What: heartbeat, `s_buf_size`, `recv_timeout`, `clr_r_buf_time`, data aggregation mode/time, network and inventory watchdog minutes, `start_inv_atbeg`, `remote_cmd_reply`, `client_ack`, `crc_enable`, `hw_inf`, Gen2 session, event list (≤ 6), `cus_param`.

How:

- Two sectors, A and B. Each record: `magic "CFG1"`, version (2; version 1 is still read and migrated on the next save), size, sequence, data, CRC32. Saves alternate between the sectors, so a failed erase/program leaves the older record valid. The newest valid record wins.
- The service keeps a persistent copy and a live copy, so `set_temp_static_conf` changes are never saved by a later permanent save. A reboot reloads the persistent copy.

| Board | Sector A | Sector B |
|---|---|---|
| F429 | 22, 0x081C0000 | 23, 0x081E0000 |
| F411 | 6, 0x08040000 | 7, 0x08060000 |

### 6.3 Keeping or clearing the settings

- A normal flash (`-w file.elf`) erases only the sectors the image covers, so settings survive.
- `-e all` wipes everything, including settings. Use it for the first flash of a new board.
- `0x20` (clear) over the configuration port resets the Identium configuration and sets PASSIVE; it does not reset the network settings.

---

## 7. Diagnostics and faults

### 7.1 Fault record (`Firmware/Services/system/fault_record.c`)

- `NMI`, `HardFault`, `MemManage`, `BusFault`, `UsageFault` handlers in `Src/stm32f4xx_it.c` are `naked` and run `FAULT_RECORD_ENTER(type)`: choose MSP/PSP from EXC_RETURN, switch to a private 512-byte fault stack, call `Fault_Record_Capture()`.
- The capture stores PC, LR, xPSR (only if the stacked frame is valid and in RAM), CFSR, HFSR, MMFAR, BFAR and uptime in `.noinit`, then resets the MCU at once.
- A magic word (`"FLTR"`) and check word separate a real record from power-on RAM contents. A record survives every reset except a power cycle; a new firmware image may move the section.
- `Error_Handler()` records its caller as `ERROR_HANDLER` but halts instead of resetting, so a start-up failure (no RFID module, no W5500) does not become a reset loop. The watchdog is not running yet at that point.

Find the source line of a fault:

```sh
arm-none-eabi-addr2line -f -e build/debug/rfid_controller.elf 0x08001234
```

### 7.2 System fault codes (`Services/system/system_fault.c`, `Common/error_codes.h`)

A RAM-only "current / last fault" with a count and timestamp, set by init failures and `ERROR_HOST_RX_OVERFLOW`. Codes: `ERROR_GPIO_INIT`, `ERROR_W5500_INIT`, `ERROR_ETHERNET_INIT`, `ERROR_RFID_INIT`, `ERROR_WATCHDOG_INIT`, `ERROR_HOST_RX_OVERFLOW` and others in `error_codes.h`. No host command reads these values at the moment; inspect them over SWD (`g_lastFault`, `g_faultCount` in `system_fault.c`).

### 7.3 Reset cause and brown-out

- `reset_cause.c` reads RCC CSR once at start-up: `POWER_ON`, `PIN`, `BROWN_OUT`, `SOFTWARE`, `IWDG`, `WWDG`, `LOW_POWER`. Power-on is checked before brown-out because a power-on also sets BORRSTF.
- `brownout.c` programs the BOR option byte once if it differs: level 2 (~2.6 V) on the F429, because the Discovery's 3 V rail measures about 2.88 V and level 3 could hold it in reset; level 3 (~2.9 V) on the F411 (3.3 V board). It skips the write while a debugger is attached or when RDP is not level 0: an option-byte write cut short by a reset leaves the chip at RDP level 1 with PCROP on every sector, and debugging fails with "Error finishing flash operation". Recovery (mass-erases flash, including the saved settings, network address and WiFi credentials): PCROP is only cleared by a single OPTCR write that takes RDP from level 1 to level 0 with SPRMOD=0. STM32CubeProgrammer's `-ob RDP=0xAA SPRMOD=0` programs RDP on its own and leaves PCROP on, and its `-w32` fails on FLASH_OPTKEYR (it reads the address back), so write the registers through GDB:
  1. `"$PROG" -c port=SWD mode=UR -ob RDP=0xBB` (level 1).
  2. Start `ST-LINK_gdbserver -p 61234 -d -k -cp <STM32CubeProgrammer/bin>`, then in `arm-none-eabi-gdb`: `target extended-remote :61234`, `set *(unsigned*)0x40023C08=0x08192A3B`, `set *(unsigned*)0x40023C08=0x4C5D6E7F`, `set *(unsigned*)0x40023C14=0x0FFFAAE4`, `set *(unsigned*)0x40023C14=0x0FFFAAE6`. OPTCR `0x0FFFAAE4` is the factory value with BOR level 2 (F429); use `0x0FFFAAE0` for level 3 (F411).
  3. Wait about 20 s for the mass erase, power-cycle the board, then check with `"$PROG" -c port=SWD -ob displ` (RDP 0xAA, SPRMOD 0).

### 7.4 Stack monitor (`stack_monitor.c`)

- The linker reserves 16 KB (`_Min_Stack_Size = 0x4000`); the link fails if static data leaves less.
- The lowest 32 bytes of the reserve are a no-access MPU region (default map elsewhere). Overflow → MemManage → fault record → reset.
- The rest is painted with `0xA5A5A5A5`; `STACK_USED` is the deepest point reached. `_sbrk()` stops at the same limit.

### 7.5 GET_DIAG fields

```
DIAG,RESET=POWER_ON,UPTIME_MS=123456,FAULTS=0,LAST_FAULT=NONE,LOOP_MAX_US=1700,PASSES=987654,
ETH_US=..,WIFI_US=..,CFG_US=..,PROTO_US=..,GPIO_US=..,RFID_US=..,HOST_US=..,TLOG_US=..,TAG_DROPPED=0,TAG_EXPIRED=0,STACK_USED=1600,STACK_SIZE=16384,BOR=2
```

(one line, wrapped here). With a recorded fault, `LAST_FAULT=<type>` is followed by `PC=0x..,LR=0x..,CFSR=0x..,HFSR=0x..,MMFAR=0x..,BFAR=0x..,AT_MS=..`.

| Field | Meaning |
|---|---|
| `RESET` | Cause of the last reset |
| `UPTIME_MS` | ms since start-up |
| `FAULTS` | Faults recorded since power-on |
| `LAST_FAULT` | `NONE`, `HARDFAULT`, `MEMMANAGE`, `BUSFAULT`, `USAGEFAULT`, `NMI`, `ERROR_HANDLER` (the fault that ended the previous run) |
| `LOOP_MAX_US`, `PASSES` | Longest loop pass, number of passes |
| `<TASK>_US` | Longest single call of each task |
| `TAG_DROPPED`, `TAG_EXPIRED` | Tags lost before upload since boot: dropped because the 256-tag upload queue was full, or discarded after waiting longer than `clear_buf_time` (default 8 s). Non-zero means the server link is slower than the reads |
| `STACK_USED`, `STACK_SIZE` | Stack high-water mark and budget, bytes |
| `BOR` | BOR level read from the option bytes |

### 7.6 DEBUG_FAULT (debug builds only)

`DEBUG_FAULT,USAGE` runs `__builtin_trap()`, `DEBUG_FAULT,BUS` reads `0x2FFFFFF0`, `DEBUG_FAULT,STACK` recurses until the MPU guard hits. The reader resets within milliseconds; check `GET_DIAG` afterwards. Expected CFSR values from design 0017: USAGE `0x00010000`, BUS `0x00008200` with BFAR `0x2FFFFFF0`, STACK `0x00000092`.

---

## 8. Building

### 8.1 Toolchain

- STM32CubeCLT 1.22 at `C:\ST\STM32CubeCLT_1.22.0` (GCC, make, STM32_Programmer_CLI).
- Run `make` from **Git Bash**, not PowerShell: the Makefile uses `find`, and PowerShell finds Windows `find.exe` instead, so no `.c` files are found and only the startup file is linked.

```sh
export PATH="/c/ST/STM32CubeCLT_1.22.0/GNU-tools-for-STM32/bin:/c/ST/STM32CubeCLT_1.22.0/Make/bin:$PATH"
arm-none-eabi-gcc --version
```

### 8.2 Makefile

```sh
cd STM32_Firmware
make                               # F429 debug   (-O0 -g3, DEBUG defined)
make CONFIG=release                # F429 release (-Os -g)
make BOARD=f411                    # F411 debug   (-Og -g3, DEBUG defined)
make BOARD=f411 CONFIG=release     # F411 release (-Os -g)
make -j8 EXTRA_CFLAGS=-Werror      # what CI does
make clean                         # removes build/
```

| Variable | Values | Default |
|---|---|---|
| `BOARD` | `f429`, `f411` | `f429` |
| `CONFIG` | `debug`, `release` | `debug` |
| `EXTRA_CFLAGS` | extra compiler flags | empty |
| `PROGRAMMER` | path to `STM32_Programmer_CLI` | `STM32_Programmer_CLI` |

| Build | Output |
|---|---|
| F429 debug / release | `build/debug/rfid_controller.elf` / `build/release/rfid_controller.elf` |
| F411 debug / release | `build/f411-debug/rfid_controller_f411.elf` / `build/f411-release/rfid_controller_f411.elf` |

Each build also writes `.hex`, `.bin`, `.map` and per-file `.su` stack-usage files, and prints the memory usage.

F411 debug uses `-Og` because at `-O0` the image (about 143 KB) overflows the 128 KB code region. The linker refuses an image that would reach the settings sectors.

Compiled sources: every `.c` under `Src/`, `Drivers/` and `Firmware/` (found with `find`), plus `Startup/*.s` (F429) or `Boards/f411/startup_stm32f411ceux.s` (F411). A new `.c` file anywhere in those folders is picked up automatically. F411-only code must therefore be guarded so it compiles to nothing on the F429, and the other way round.

Defines: F429 `-DUSE_HAL_DRIVER -DSTM32F429xx`; F411 `-DUSE_HAL_DRIVER -DSTM32F411xE -DTARGET_BOARD=2`; debug adds `-DDEBUG`. CPU flags: `-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard`; link with `nano.specs`, `nosys.specs`, `--gc-sections`.

### 8.3 Other targets

```sh
make check-chip                        # read the device ID over SWD (hot-plug, no reset)
make flash                             # build, check-chip, then program and reset
make BOARD=f411 CONFIG=release flash
```

`check-chip` refuses to continue when the chip does not match the board (`0x419` F429, `0x431` F411).

### 8.4 STM32CubeIDE

Build configurations (hammer drop-down, or Project → Build Configurations → Set Active):

| Configuration | Board | Optimisation | Output |
|---|---|---|---|
| Debug | F429 | default (-O0) | `Debug/RFID_Controller_F429.elf` |
| Release | F429 | -Os | `Release/RFID_Controller_F429.elf` |
| F411 | F411 (`STM32F411xE`, `TARGET_BOARD=2`) | -Og | `F411/RFID_Controller_F429_f411.elf` |

Launch files: `RFID_Controller_F429.launch` (loads the Debug output) and `RFID_Controller_F411.launch` (loads the F411 output). Both use ST-LINK GDB server with "connect under reset".

The F411 startup file and linker script live in `Boards/f411/`, not `Startup/`: CubeIDE compiles everything in `Startup/`, and a second startup file there would break the F429 build.

Importing:

- File → Import → Existing Projects into Workspace → select `STM32_Firmware/` in this repo, with **"Copy projects into workspace" unticked**, so CubeIDE builds the files that git tracks.
- An earlier copy of the project under `Documents` broke because files were copied while CubeIDE had the project open. Do not keep a second copy.
- Never let CubeMX generate into `STM32_Firmware/`; it would overwrite `main.c` and the USB files. Generate into a scratch folder and copy the pieces across (see `docs/notes/f411-custom-board-port.md`, section 1.5).

### 8.5 CI

`.github/workflows/build.yml`: on every push and pull request, Ubuntu 24.04 with `gcc-arm-none-eabi` from apt, matrix `board: [f429, f411]` × `config: [debug, release]`, `make BOARD=.. CONFIG=.. EXTRA_CFLAGS=-Werror`. Artifacts `firmware-<board>-<config>` contain `.elf`, `.hex`, `.bin`, `.map`. Build locally with `EXTRA_CFLAGS=-Werror` before pushing; the apt GCC version differs from CubeCLT's and may warn differently.

---

## 9. Flashing

`STM32_Programmer_CLI` may not be on the Git Bash PATH. Set a variable once per shell:

```sh
PROG="/c/ST/STM32CubeCLT_1.22.0/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"
```

### 9.1 Commands

| Task | Command |
|---|---|
| Flash bootloader + application | `make CONFIG=release flash PROGRAMMER="$PROG"` |
| Flash only the application | `make CONFIG=release flash-app PROGRAMMER="$PROG"` or `"$PROG" -c port=SWD mode=UR -w build/release/rfid_controller.elf -v -rst` |
| Signed update image | `make CONFIG=release image`, then `python Tools/fw_update.py --port COM3 build/release/rfid_controller-<version>.tlfw` (full instructions: [how-to-build-a-firmware-update-image](how-to-build-a-firmware-update-image.md)) |
| Flash F411 | `make BOARD=f411 CONFIG=release flash PROGRAMMER="$PROG"` |
| Device ID only (no reset) | `make check-chip PROGRAMMER="$PROG"` or `"$PROG" -c port=SWD mode=HOTPLUG` |
| Full chip erase (also erases settings) | `"$PROG" -c port=SWD mode=UR -e all` |
| Hard reset | `"$PROG" -c port=SWD mode=UR -hardRst` |
| Read one RAM word live | `"$PROG" -c port=SWD mode=HOTPLUG -r32 <addr> 1` |
| Show option bytes (BOR level) | `"$PROG" -c port=SWD -ob displ` |

Device IDs: `0x419` = STM32F42x/43x (Discovery), `0x431` = STM32F411.

The application starts at `0x08004000`, after the bootloader in sector 0 (design 0020). Programming only the application (`flash-app`, CubeIDE) onto a board without the bootloader leaves it unable to start: run `make flash` once per board.

Use `-hardRst` for resets during tests. A `mode=HOTPLUG -rst` soft reset was once seen to leave the MCU stuck in start-up; the cause was not confirmed.

### 9.2 Reading variables live

```sh
arm-none-eabi-nm build/debug/rfid_controller.elf | grep -w hostActiveTransport
# 20000abc b hostActiveTransport
"$PROG" -c port=SWD mode=HOTPLUG -r32 0x20000ABC 1
```

`-r32` reads a whole 32-bit word; for a `uint8_t` take the byte at the symbol's offset (little endian). Peripheral registers work the same way, for example the F429 RS485 DE pin, GPIOG ODR bit 13: `-r32 0x40021814 1`.

### 9.3 BOR option byte

The firmware sets it on first boot (7.3). Expected: level 2 on the Discovery, level 3 on the F411. Do not raise the Discovery to level 3: its rail measures about 2.88 V.

### 9.4 Flashing the custom board with the Discovery's ST-LINK

1. On the Discovery, remove both ST-LINK/DISCO jumpers (CN4). This disconnects the on-board ST-LINK from the F429.
2. Wire the Discovery's 6-pin SWD header to the custom board's CN7:

| Discovery SWD pin | Signal | Custom board |
|---|---|---|
| 1 | VDD_TARGET (sense) | CN7-2 (3V3) |
| 2 | SWCLK | CN7-5 |
| 3 | GND | CN7-3 |
| 4 | SWDIO | CN7-6 |
| 5 | NRST | CN7-4 |
| 6 | SWO | not connected |

3. Power the custom board from its own supply, not from the ST-LINK.
4. Check the chip, erase once, flash:

```sh
cd STM32_Firmware
make BOARD=f411 CONFIG=release check-chip PROGRAMMER="$PROG"   # must report 0x431
"$PROG" -c port=SWD mode=UR -e all                              # first flash only
make BOARD=f411 CONFIG=release flash PROGRAMMER="$PROG"
```

5. Refit both CN4 jumpers to use the Discovery itself again.

The CN7 pinout is from the schematic drawing (`docs/hardware/SCH_Schematic1_2026-10-08.pdf`, header "SWD_DEBUG", B6B-EH): 1 = NC, 2 = 3V3, 3 = GND, 4 = RST, 5 = SWCLK, 6 = SWDIO.

---

## 10. Testing

### 10.1 Bench setup

| Port | What it is |
|---|---|
| COM3 | ST-LINK virtual COM port = the F429's host serial port (USART1, PA9/PA10) |
| COM8 | The firmware's native USB CDC port |

COM numbers are what this PC assigned; check Device Manager on another PC. The PC has no wired Ethernet to the reader, so Ethernet paths (TCP 5000, 8080, UDP 15000, JSON upload) can only be reached over WiFi or from another machine.

Python: `pyserial` 3.5 (`python -m pip install pyserial`) for the serial scripts; the TCP scripts use only the standard library.

### 10.2 Gotchas

- Wait about 2.7 s after a reset before sending commands; the main loop does not run until start-up has finished.
- Any `VER`, `IOGET` or `FF` frame on USB or serial opens a module-bridge session. It takes that link's traffic until the link has been idle for 30 s (a printable text line at a frame boundary also ends it). Run text-command tests before bridge tests, or wait.
- The host lock goes to whichever port sent a printable byte first. Send `RELEASE` at the end of every test script, or other ports get no answers.
- `RELEASE` also stops async inventory.
- An MCU reset does not reset the RFID module. If inventory was running, the module keeps streaming tags; start-up stops it, but tools that connect during that time see tag frames.
- Module status `0xAA4A` on the VSWR ReturnLossTest means no antenna on that port.
- Module commands (`GET_VERSION`, `READ_TAG`, ...) answer `ERROR,...` while inventory is running. Send `STOP` first.

### 10.3 Scripts in `STM32_Firmware/Tools/`

Run from `STM32_Firmware/`.

| Script | Tests | Needs | Command |
|---|---|---|---|
| `serial_link_test.py` | On one serial link: `GET_STATUS`/`RELEASE`, EE `0x22`, EE `0x24`, `VER`, `IOGET`, module pass-through `FF 00 03 1D 0C` | USB CDC or serial port | `python Tools/serial_link_test.py --port COM3` (add `--skip-module` without an RFID module, `--baud`) |
| `reader_manager_connect_test.py` | Replays Reader Manager's connect sequence on 8080: IOGET/IOSET/IPGET, module probes with CRC check, EE reads (valid JSON, no NUL), UDP discovery; optional work-state toggles and GIO/SIO during fast inventory | Network path to the reader | `python Tools/reader_manager_connect_test.py --host 192.168.150.50 [--write-tests] [--async-gpio]` |
| `identium_config_port_test.py` | One EE read on 8080 | Network | `python Tools/identium_config_port_test.py --host 192.168.150.50 --cmd state\|active\|static` |
| `identium_tcp_json_test_server.py` | Interactive JSON server on port 9000: prints events, sends `resume`, `pause`, `sync`, `gpo`, `gpio2`, `gpio3`, `gpi`, `setrt`, `setstatic`, `settemp`, `reset`, `reboot` | Reader in ACTIVE, uploading to this PC | `python Tools/identium_tcp_json_test_server.py` |
| `identium_config_write_test.py` | `set_runtime_conf` / `set_static_conf` / `set_temp_static_conf` persistence, partial updates, invalid values, reboot persistence; leaves heartbeat 10 s, start-at-boot 0, session 0 | Reader uploading to this PC on 9000, plus 8080 | `python Tools/identium_config_write_test.py --reader-ip 192.168.150.50` |
| `identium_active_mode_test.py` | Extended ActiveMode fields, atomic rejection, duplicate keys, ranges, events, `sync_time_req` after reboot; restores the default profile | Same | `python Tools/identium_active_mode_test.py --reader-ip 192.168.150.50` |
| `identium_tcp_hardening_test.py` | Fragmented frames, two frames per write, garbage prefix, oversized line, whitespace/CRLF, reconnect; optional tag buffering across an outage | Reader uploading to this PC on 9000 | `python Tools/identium_tcp_hardening_test.py [--require-tags] [--outage-seconds 3]` |

The four port-9000 scripts need the reader in ACTIVE work state and its upload target (`serverIp`, default 192.168.150.100) pointing at the PC running the script.

Quick manual check over USB or serial:

```sh
python -c "import serial,time; s=serial.Serial('COM8',115200,timeout=1); s.write(b'GET_DIAG\r'); time.sleep(0.5); print(s.read(2000).decode()); s.write(b'RELEASE\r'); time.sleep(0.2); print(s.read(200).decode())"
```

### 10.4 hostcheck

`STM32_Firmware/hostcheck/identium_protocol_host_test.c` is a host-PC test of `identium_protocol.c` with stubbed dependencies: framing (garbage prefix, fragments, oversized line), the tag queue and heartbeat order, and stale-tag disposal after a disconnect. It prints `HOST PROTOCOL TEST: PASS`.

Run it with `make hostcheck` from `STM32_Firmware/` (uses the PC compiler `HOSTCC`, default `gcc`; CI runs it on every push). The real CMSIS headers contain ARM assembly, so `hostcheck/stubs/stm32f429xx.h` stands in for the device header. On this PC there is no gcc on the PATH; CLion ships one:

```bash
PATH="/c/Program Files/JetBrains/CLion 2026.1.4/bin/mingw/bin:$PATH" make hostcheck
```

### 10.5 Bring-up checklist (new board or new build)

1. `make check-chip` reports the right ID.
2. Flash, wait 3 s, `GET_DIAG` over USB: reset cause, `LAST_FAULT=NONE`, BOR level, stack use.
3. `GET_VERSION` returns the module version (module UART direction is right).
4. `python Tools/serial_link_test.py --port <COM>` on USB and on the serial port.
5. Inputs/outputs: `SET_GPO,<n>,1,2` and `GET_GPI`.
6. Ethernet: ping 192.168.150.50, connect to TCP 5000, run `reader_manager_connect_test.py`.
7. WiFi: `SET_WIFI,1,STA,<ssid>,<password>`, then `GET_WIFI`, then connect to TCP 5000 on the WiFi IP.
8. End with `RELEASE`.

---

## 11. Common changes

### 11.1 Add a host text command

1. In `Firmware/Application/Host/host_interface.c`, add an `else if` branch in `HOST_Interface_Task()` before the final `else` (unknown command). Use `strcmp` for exact commands, `strncmp(hostRxBuffer, "NAME,", n)` for commands with arguments.
2. Reply with `HOST_SendString("OK,NAME\r\n")` / `"ERROR,NAME\r\n"`, or a data line `NAME,...\r\n`. Always answer, even for unsupported features: the desktop app keeps a command queue.
3. Do not start a command name with a vendor token: `VER`, `IOGET`, `IOSET`, `IPGET`, `IPSET`, `GIO`, `SIO`. On USB and serial the router would take it for a bridge session (a command called `VERIFY` would never reach the parser). Bytes `0xFF` / `0xEE` at line start are also taken.
4. Keep the line under 319 characters. If you need more, raise `HOST_RX_BUFFER_SIZE` and check RAM on the F411.
5. If it talks to the module, check `SILION_Application_GetState() == SILION_APP_IDLE` (the application functions already do) and keep timeouts well under the ~32 s watchdog.
6. If the command is board specific, make `GET_CAPABILITIES` say so.
7. Add it to the command table in section 5.4 of this guide and to a test script.

### 11.2 Add a setting

Network, identity, upload or WiFi value (reader settings):

1. Add the field at the **end** of `ReaderSettings_t` in `reader_settings.h`.
2. In `reader_settings.c`: bump `READER_SETTINGS_VERSION` (now 4) by one; copy the current `ReaderSettings_t` layout into a `ReaderSettings_V<n>_t` + record type, and add a `ReaderSettings_LoadV<n>()` that copies the old data over defaults and sets `g_nextSlot = READER_SETTINGS_SLOT_COUNT` (forces an erase on the next save), following `LoadV3`; call it from `ReaderSettings_Init()` for that version.
3. Set the default in `ReaderSettings_GetDefaults()` and validate it in `ReaderSettings_IsSane()` (an invalid record is ignored, so a bad check silently drops all settings).
4. Change it with the copy pattern used by the WiFi commands: `ReaderSettings_t s = *ReaderSettings_Get(); s.field = ...; ReaderSettings_Save(&s);`.
5. If it must reach the vendor tools, add it to `configuration_service.c` (readback JSON) and `vendor_config.c` (write).

Identium ActiveMode value (configuration storage): add it to `Configuration_StorageData_t`, bump `CONFIG_STORAGE_VERSION` with a migration like the existing V1 path, set the default in `Configuration_Storage_GetDefaults()`, and parse/validate it in `configuration_service.c` (`set_runtime_conf`) with the same partial-update and atomic-rejection rules.

Flash writes erase a 128 KB sector at times: never save from an interrupt, and do not save on every loop pass.

### 11.3 Add a board pin

1. Add `BOARD_<NAME>_PORT` / `BOARD_<NAME>_PIN` to `Firmware/BSP/<board>/board_config.h` for **each** board. On a board without the pin, use `NULL` / `0xFFU` as the existing entries do, and make the user code skip `NULL` ports.
2. Configure it in that board's `board.c` (`Board_<B>_GPIO_Init`) or in the service that owns it (the serial port configures its own DE/mode pins).
3. Use only the `BOARD_...` names in application code, never `GPIOx` directly.
4. For an input or output channel, extend the tables in `industrial_io.c`; F429 test pins are defined there, F411 pins in its `board_config.h`.
5. Check the pin against the other `board_config.h` entries, SWD (PA13/PA14) and, on the Discovery, the SDRAM/LCD pins.

### 11.4 Add a new board

1. `Firmware/Config/firmware_config.h`: add `BOARD_<NEW> 3U` and allow it in the `#error` check.
2. `Firmware/BSP/<new>/board_config.h`, `board.h`, `board.c` guarded by `#if (TARGET_BOARD == BOARD_<NEW>)`, with the same set of `BOARD_...` macros as the existing boards.
3. Add an `#elif` for the new board everywhere a board is selected. Files that select per board today: `firmware_configuration.c`, `industrial_io.c`, `serial_port.c`, `esp32_link.c`, `uart_dispatch.c` (IRQ handler names), `reader_settings.c` and `configuration_storage.c` (flash sectors), `brownout.c` (BOR level), `reset_cause.c`, `watchdog.c`, `time_service.c`, `identium_protocol.c` (device header), `usbd_conf.c`, `usb_irq.c`, `Src/main.c` (clock).
4. A different MCU: generate a CubeMX project in a scratch folder, copy the device header, startup file and linker script into `Boards/<new>/`, and set the code region so it ends before the settings sectors. Keep `_Min_Stack_Size = 0x4000` and the `.noinit` section.
5. Makefile: add a `BOARD=<new>` branch (`TARGET`, `CHIP_ID`, `BUILD`, `LDSCRIPT`, `S_SRCS`, `DEFS`, `DEBUG_OPT`). CI: add the board to the matrix and the `OUT` expression. CubeIDE: add a build configuration and a launch file.

### 11.5 Keep the F429 output unchanged

When adding code for the F411 only, guard it with `#if defined(STM32F411xE)` or `#if (TARGET_BOARD == BOARD_F411_CUSTOM)`, so the F429 image does not change. To confirm, compare the F429 `.bin` before and after:

```sh
make clean && make && make CONFIG=release
sha1sum build/debug/rfid_controller.bin build/release/rfid_controller.bin   # note the values
# apply the change, then:
make clean && make && make CONFIG=release
sha1sum build/debug/rfid_controller.bin build/release/rfid_controller.bin   # must match
```

Build both boards with `EXTRA_CFLAGS=-Werror` before committing, as CI does.

---

## 12. Release flow

Repository: `https://github.com/namesanas/identium-rfid-firmware` (private). Versions are tags `vX.Y.Z` (`v0.1.0` ... `v0.6.0` so far).

1. Work on a feature branch, push it.
2. Merge into `main` (fast-forward), push `main`.
3. Build the release images (see 8.2), both boards if both changed.
4. Create an annotated tag and push it:

```sh
git tag -a v0.7.0 -m "v0.7.0 - <short summary>"
git push origin v0.7.0
```

5. Collect the assets in one folder and write the checksums:

```sh
mkdir -p /tmp/rel && cp STM32_Firmware/build/release/rfid_controller.{elf,hex,bin} /tmp/rel/
cd /tmp/rel && sha256sum * > SHA256SUMS
```

6. Create the GitHub release through the REST API. There is no `gh` CLI on this PC. The token comes from the git credential store; do not print it. A small Python helper works (piping the credential through PowerShell did not):

```python
import json, os, subprocess, urllib.request
cred = subprocess.run(["git", "credential", "fill"], input="protocol=https\nhost=github.com\n\n",
                      capture_output=True, text=True).stdout
token = dict(l.split("=", 1) for l in cred.splitlines() if "=" in l)["password"]
api = "https://api.github.com/repos/namesanas/identium-rfid-firmware"
hdr = {"Authorization": f"token {token}", "Accept": "application/vnd.github+json"}

body = {"tag_name": "v0.7.0", "name": "v0.7.0 - <summary>", "body": open("notes.md").read()}
req = urllib.request.Request(f"{api}/releases", json.dumps(body).encode(), hdr, method="POST")
rel = json.load(urllib.request.urlopen(req))

for name in os.listdir("/tmp/rel"):
    data = open(os.path.join("/tmp/rel", name), "rb").read()
    up = urllib.request.Request(
        f"https://uploads.github.com/repos/namesanas/identium-rfid-firmware/releases/{rel['id']}/assets?name={name}",
        data, {**hdr, "Content-Type": "application/octet-stream"}, method="POST")
    urllib.request.urlopen(up)
```

Previous releases carried the firmware `.elf`/`.hex`/`.bin`, the TAGlink desktop `.exe`, the TAGlink Mobile APK, the ESP32 coprocessor binaries and `SHA256SUMS`. State in the release notes whether the firmware images are debug or release builds.

---

## 13. Known gaps and open points

Verified in the code, listed so nobody relies on them:

| Item | State |
|---|---|
| Identium binary TCP framing (spec Part C.3–C.10) | Not implemented; JSON-over-TCP only |
| MQTT | Not implemented |
| `update_fw_by_http`, field firmware update, bootloader | Not implemented |
| `tag_coming` (event 5), empty-data event (4), `gpo_act`, `gpi_trigger`, `tag_json_format` | Rejected over JSON, ignored from the vendor tools |
| Positive `client_ack`, `crc_enable` | Only 0 accepted |
| RS232/RS485 mode switching at run time | No command; `PRODUCT_SERIAL_DEFAULT_MODE` at build time |
| Gen2 frequency hopping (`SET_GEN2` field 5) | Only 0 accepted |
| Multi-antenna inventory (`SET_ANTENNAS` with several ports) | Not implemented; one port only |
| Buzzer on the F411 board | Implemented, not yet run on hardware |
| Ethernet DHCP | Not supported (static only; `ip = 0.0.0.0` from the tools keeps the current address) |
| USB identity | Still ST's example VID/PID 0x0483/0x5740 |
| Reader settings | Single copy; power loss during a full-sector rewrite loses them |
| Watchdog | ~32 s; not shortened until the longest blocking path is measured |
| F411 board | Code complete, not yet run on hardware at the time of the porting note |

Design records 0014 and 0016 were updated for the F411 pin map; `Firmware/BSP/f411_custom/board_config.h` is the reference.
