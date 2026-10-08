# 0003 — Hardware I/O Commands

## Goal

Migrate the first real hardware I/O commands to the company protocol while keeping the existing legacy ASCII/USB path intact for rollback.

## Edited files and reasons

### `Firmware/BSP/industrial_io.h` — new
Defines a small board-independent interface for the four controller inputs and four outputs. This keeps physical pin details out of the protocol/application code.

### `Firmware/BSP/industrial_io.c` — new
Implements the hardware mapping.

F429 prototype test mapping:
- GPO1 = PE0
- GPO2 = PE1
- GPO3 = PE2
- GPO4 = PE3
- GPI1 = PE4
- GPI2 = PE5
- GPI3 = PE6
- GPI4 = PE7

Production F411 uses the already-defined `BOARD_INPUT*` / `BOARD_OUTPUT*` mappings in `f411_custom/board_config.h`.

### `Firmware/Services/GPIO/gpio_service.h` — new
Defines the logical GPIO service used by the protocol dispatcher. It owns output states, timed pulses, input-state tracking and change callbacks.

### `Firmware/Services/GPIO/gpio_service.c` — new
Implements:
- four-output manual control
- output pulse timing
- restore of the previous output state when a pulse expires
- rejection while any timed GPO operation is active
- duplicate-GPO rejection
- GPO/state/duration validation
- four-input polling and change detection

### `Firmware/Protocol/identium_command.c`
Added parsing and execution of `set_gpo` and `get_gpi`.

`set_gpo` follows the documented JSON fields (`gpo`, `state`, `duration`) and validates the documented ranges. The public protocol uses seconds for `set_gpo.duration`; the GPIO service converts the duration to milliseconds for its internal timer.

GPO 5 is rejected because this controller has four physical outputs; the reference firmware documents GPO 5 as its onboard buzzer.

For JSON `get_gpi`, this stage reports a current `gpi_changed` snapshot followed by the normal `command_reply`. The specification explicitly defines `get_gpi` as TCP-only and clearly defines the binary 0x16 response, but does not spell out the JSON-mode response object, so this behavior is provisional and must be checked against existing company software before being treated as final.

### `Firmware/Protocol/identium_protocol.h` / `.c`
Added `Identium_Protocol_ReportGpiChange()` to emit the company's `gpi_changed` event with all four inputs and a timestamp.

### `Firmware/Application/configuration/firmware_configuration.c`
Initializes the industrial I/O hardware and GPIO service after the F429 BSP initialization. GPIO service reports input changes through the Identium protocol callback.

### `Src/main.c`
Services the GPIO state machine in the main foreground loop so pulses expire and input changes are detected.

### `Firmware/Common/error_codes.h`
Added `ERROR_GPIO_INIT` for initialization failure reporting.

### `Tools/identium_tcp_json_test_server.py`
Added `gpio2`, `gpio3` and `gpi` test commands.

## Protocol behavior covered

- `set_gpo` — one or more outputs, state 0/1, duration 0-3600 seconds.
- duplicate output numbers are rejected.
- a new `set_gpo` is rejected while a timed GPO operation is active.
- `gpi_changed` reports all four input states.
- `get_gpi` can request the current snapshot over the JSON-over-TCP path.

The protocol specification documents these GPO/GPI rules and the distinction between `set_gpo.duration` in seconds and `gpo_act.dur` in milliseconds.

## Verification performed

- Python test server passes `py_compile`.
- Modified F429 C files pass host-side syntax checking with zero errors.
- Existing F429 legacy USB/ASCII path has not been removed.

## Hardware test plan

Use a multimeter on the F429 prototype test pins:
- GPO1 PE0
- GPO2 PE1
- GPO3 PE2
- GPO4 PE3

Set an output high and verify approximately the MCU I/O supply level, then verify it returns to the previous state after the requested duration.

For inputs, the F429 test inputs use pull-downs. Momentarily connect GPI1-GPI4 to 3.3 V one at a time and watch for `gpi_changed` events.

## Not implemented yet

- `gpo_act` tag-triggered output actions
- `gpi_trigger` inventory start/stop automation
- `tag_eas` matching actions
- persistent ActiveModeJson/StaticModelJson configuration
- TCP 8080 configuration port
- HTTP transport
- MQTT transport
- binary TCP transport
- removal of legacy ASCII

