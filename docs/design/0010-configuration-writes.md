# 0010 — Configuration Writes

## Scope

This change adds safe configuration-write handling to the existing JSON-over-TCP Identium protocol path.

The company protocol requires configuration commands to be partial updates: only the supplied keys may change and omitted keys must remain unchanged. `set_runtime_conf` is persistent and restarts the reader. `set_static_conf` is persistent and supports the documented `reboot` and `reset` controls. `set_temp_static_conf` is live-only and requires inventory to be paused.

## Supported fields in this step

### `set_runtime_conf`
- `glob_params.hb_cylce` — 5 to 7200 seconds.
- `glob_params.start_inv_atbeg` — 0 or 1.

### `set_static_conf`
- `protocol.gen2.session` — 0 to 3.
- `reboot` — 0 or 1.
- `reset` — 0 or 1.

### `set_temp_static_conf`
- `protocol.gen2.session` — 0 to 3.
- Inventory must be stopped before the command is accepted.
- No persistent storage is written.

Fields outside this controlled subset are rejected rather than silently ignored. RF-region, antenna power, inventory timing, tag filtering, serial settings, network/IP/MAC and other StaticModelJson/ActiveModeJson fields remain for later implementation because they require additional hardware/application integration and validation.

## Runtime versus persistent configuration

The service keeps two copies: the persistent configuration loaded from Flash and the current live configuration. This is important for `set_temp_static_conf`: a temporary live change must not become persistent accidentally when a later permanent command is saved. A reboot reloads the persistent copy, so temporary settings disappear as required by the protocol.

## Persistence design

Two upper STM32F429ZI Flash sectors are reserved for configuration:
- Sector 22 at `0x081C0000`
- Sector 23 at `0x081E0000`

The linker now limits application Flash to 1792 KB so the final 256 KB remains reserved. Configuration records contain a magic value, version, sequence number, data-size field and CRC32. Saves alternate between the two sectors so a failed erase/program operation leaves the previous valid record available.

## Boot application

At boot the firmware:
1. Loads the persistent configuration over the compiled-safe defaults.
2. Applies the stored Gen2 session after the RFID module has completed its normal configuration sequence.
3. Starts inventory automatically when `start_inv_atbeg` is stored as 1.
4. Uses the stored heartbeat interval in the Identium protocol task.

## Reboot handling

Replies are sent before a configuration-triggered reboot. The delayed reboot task is executed even when the TCP connection has already closed, so the reset is not dependent on the socket remaining connected.

## Verification performed

Host-side validation performed before hardware testing:
- C syntax checks with `-Wall -Wextra -Werror` for the modified configuration/protocol source files.
- JSON parser/command integration tests for valid, invalid and pause-required operations.
- Flash storage simulation covering save, reload, alternating slots and corrupted newest-record fallback.
- Configuration-service + Flash persistence integration test covering runtime and static values across a simulated reboot.
- Python test utility syntax check.
- Explicit regression coverage that a temporary session change is not persisted by a later permanent runtime update.

Hardware validation should use `Tools/identium_config_write_test.py` after building/flashing in STM32CubeIDE. It checks persistent partial updates, unrelated-field preservation, invalid values, temp-config pause handling, temp non-persistence, reboot persistence and reset-to-default behavior.
