# 0015 — Work Mode Control and Start-up Recovery

## Scope

Make the passive / active work state an explicit host setting, independent of the HTTP upload settings, and make sure a reader that was inventorying in active mode can always be brought back under control — after a reboot, after a lost STOP reply, or from a host that lost track of the state.

## Host commands (`Application/Host/host_interface.c`)

| Command | Reply | Notes |
|---|---|---|
| `GET_WORK_MODE` | `WORK_MODE,PASSIVE` / `WORK_MODE,ACTIVE` | |
| `SET_WORK_MODE,PASSIVE\|ACTIVE` | `OK,SET_WORK_MODE` / `ERROR,SET_WORK_MODE` | Persisted. `PASSIVE` answers OK only once inventory has stopped. `ACTIVE` starts inventory when "start inventory at boot" is set. |
| `SET_HTTP,url,timeoutSeconds,startAtBoot` | `OK,SET_HTTP` / `ERROR,SET_HTTP` | Stores the HTTP upload settings for the next active session; the work state is not changed. |
| `SET_HTTP,url,timeoutSeconds,active,startAtBoot` | as above | Older form, still accepted: also sets the work state. |
| `STOP` | `OK,STOP` / `ERROR,STOP` | Idempotent: an idle reader answers OK. A stop whose reply is lost in a fast tag stream is retried (3 attempts). |

`SET_HTTP` is applied through the same code as a Reader Manager *set ActiveModeJson* request (`VendorConfig_HandleActiveSettings()`), with one difference: the vendor request switches the reader to active mode (what Reader Manager and ReaderSense expect after "Set"), the host command does not.

## Start-up with a running module (`Application/Silion/sillion_application.c`)

An MCU reset does not reset the RFID module. After a reset in active mode the module is still streaming async tag frames, so the start-up handshake (version / boot firmware / run phase) read tag frames instead of replies, `Firmware_Configuration_Init()` failed and `main()` stopped in `Error_Handler()` — before the watchdog was started, so the reader stayed unresponsive until the next reset. On the bench this happened on every second reset in active mode.

- `SILION_Application_ConfigureReader()` first sends *Stop Async Inventory* (`AA 49`), waits 300 ms and drops the module output. A module that is not inventorying answers with an error, dropped with the rest.
- `Firmware_Configuration_Init()` retries `ConfigureReader()` up to three times before reporting `ERROR_RFID_INIT`.

Bench result: 6 of 6 resets in active mode came up and answered (previously 3 of 6).

## VSWR (return loss) on the module

The module answers the ReturnLossTest request (`AA 4A`) with status `0xAA4A` and no samples when no antenna is connected to the requested port; all parameters are valid in that case (a wrong antenna gives `0x0105`, a wrong region `0x0500`). The module's antenna-detect query (`0x61 05`) reported "not detected" even with an antenna connected, so it is not usable as a pre-check. The desktop application reports `0xAA4A` as a missing antenna.
