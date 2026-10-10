# 0017 — Fault Handling and Runtime Diagnostics

## Scope

Make a crash diagnosable and self-recovering, give the stack a hard budget, measure how responsive the super-loop really is, protect against a sagging supply, and build the firmware from the command line and in CI. Nothing here changes the protocol, settings or network behaviour.

## Fault record (`Services/system/fault_record.c`)

Before this change, HardFault / MemManage / BusFault / UsageFault / NMI spun in `while(1)` until the watchdog reset the MCU (~32 s), and nothing said why.

- The handlers in `Src/stm32f4xx_it.c` are declared `naked` (in a USER CODE block, so CubeMX regeneration keeps them) and run `FAULT_RECORD_ENTER(type)`. It picks MSP or PSP from EXC_RETURN, switches to a private 512-byte fault stack (the main stack may be the cause) and jumps to `Fault_Record_Capture()`.
- `Fault_Record_Capture()` stores the stacked PC / LR / xPSR, CFSR, HFSR, MMFAR, BFAR and the uptime in a `.noinit` section. That section is new in `STM32F429ZITX_FLASH.ld` and start-up does not clear it. Then it resets the MCU at once.
- The stacked frame is only read when the hardware managed to push it (no MSTKERR / STKERR) and it lies in RAM.
- A magic word and a check word tell a record left by a reset from the random RAM contents left by power-on. A record survives every reset except a power cycle and is lost when the firmware image changes (the section moves).
- `Fault_Record_Init()` runs first in `main()`. It takes over the previous record and enables the separate MemManage / BusFault / UsageFault exceptions, so faults are reported as their real type.
- `Error_Handler()` records its caller (`ERROR_HANDLER`) but keeps halting. A start-up failure, such as no RFID module, must not turn into a reset loop.

## Stack budget and overflow guard (`Services/system/stack_monitor.c`)

The linker script reserved only 1 KB for the stack (`_Min_Stack_Size = 0x400`), while `HOST_Interface_Task()` alone uses a 664-byte frame. The stack in fact grew into unused RAM, and an overflow would have overwritten `.bss` without any sign.

- `_Min_Stack_Size` is now 16 KB, so the link fails if static data leaves less than that.
- The lowest 32 bytes of the reserve are a no-access MPU region (`PRIVDEFENA`, default map elsewhere). Running past the budget raises MemManage, which is recorded and then resets the MCU.
- The rest of the reserve is filled with `0xA5A5A5A5` at start-up. `Stack_Monitor_GetUsed()` reports the deepest point reached.
- The heap cannot reach the guard: `_sbrk()` stops at the same `_estack - _Min_Stack_Size` limit. `malloc` is only linked in by newlib's stdio exit path and is not called.

## Main-loop timing (`Services/system/loop_monitor.c`)

Every task call in `main()` is wrapped in `LOOP_MONITOR_RUN()`. The DWT cycle counter gives the longest duration of each task and of a whole loop pass.

## Brown-out reset (`Services/system/brownout.c`)

BOR is off from the factory. `Brownout_Init()` programs the option byte once, when it differs.

| Board | Supply | BOR level |
|---|---|---|
| F411 custom | 3.3 V | 3 (~2.9 V rising) |
| F429 Discovery | 3.0 V (ST-LINK measures 2.88 V) | 2 (~2.6 V rising) |

Level 3 is too close to the Discovery's rail: it could hold the board in reset for good.

## Reset cause fix (`Services/system/reset_cause.c`)

A power-on reset sets BORRSTF as well as PORRSTF. The old order checked BOR first, so every power-up was reported as a brown-out. POR is now checked first.

## Host commands

| Command | Reply |
|---|---|
| `GET_DIAG` | `DIAG,RESET=…,UPTIME_MS=…,FAULTS=…,LAST_FAULT=…[,PC=…,LR=…,CFSR=…,HFSR=…,MMFAR=…,BFAR=…,AT_MS=…],LOOP_MAX_US=…,PASSES=…,ETH_US=…,CFG_US=…,PROTO_US=…,GPIO_US=…,RFID_US=…,HOST_US=…,STACK_USED=…,STACK_SIZE=…,BOR=…` |
| `CLEAR_DIAG` | `OK,CLEAR_DIAG`; restarts the loop-timing maxima |
| `DEBUG_FAULT,USAGE\|BUS\|STACK` | Debug builds only (`DEBUG` defined): crashes the reader on purpose to test the record. A release build answers `ERROR,INVALID_COMMAND`. |

`FAULTS` counts faults since power-on. `LAST_FAULT` is the fault that ended the previous run. To find the source line: `arm-none-eabi-addr2line -f -e rfid_controller.elf <PC>`.

## Build (`Makefile`, `.github/workflows/build.yml`)

- `make` builds a debug image (`-O0 -g3`).
- `make CONFIG=release` builds a release image (`-Os -g`).
- `make flash` programs over SWD.
- Output goes to `build/<config>/`: `.elf`, `.hex`, `.bin`, `.map`, plus `.su` stack-usage files.
- The STM32CubeIDE project is unchanged and still builds.
- CI builds both configurations with `-Werror` on every push and keeps the images as artifacts.

## Bench results (F429 Discovery)

| | Debug | Release |
|---|---|---|
| Flash | 124.6 KB | 72.5 KB |
| Static RAM (+16 KB stack reserve) | 105 KB | 105 KB |
| Stack used, idle and inventory | 1.6 KB | 1.2 KB |
| Longest loop pass, idle | 1.7 ms (`CFG`) | 1.3 ms (`CFG`) |
| Longest loop pass, inventory START / STOP | 8.4 ms (`HOST`) | 8.4 ms (`HOST`) |

- **Fault tests (debug):** `USAGE` → USAGEFAULT, CFSR `0x00010000` (UNDEFINSTR), PC resolves to the trap line. `BUS` → BUSFAULT, CFSR `0x00008200`, BFAR `0x2FFFFFF0`. `STACK` → MEMMANAGE, CFSR `0x00000092` (MSTKERR), MMFAR inside the guard. Each time the reader reset within milliseconds and `FAULTS` counted 1, 2, 3.
- **Serial link test:** passes on both builds.
- **Inventory START / STOP over USB:** works on both builds.
- **RAM on the F411:** static RAM plus the 16 KB stack reserve is 121 KB, which fits the F411's 128 KB with ~7 KB to spare. The largest users are `identiumTagQueue` (22.5 KB), the HTTP upload queue (12 KB) and the request buffer (8.7 KB).

## Not changed yet

- **Watchdog timeout (~32 s):** the longest measured loop pass is 8.4 ms, but flash sector erases (settings save) can take seconds. Shortening the timeout needs that path measured first.
- **Field firmware update:** there is no bootloader yet.
- **Settings stored as a single copy:** a power cut during a settings write can lose the record. Two copies with the newer valid one used would fix this.
