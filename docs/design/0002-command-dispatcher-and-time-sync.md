# Identium Protocol Migration — Step 2

## Scope

This step separates the company-protocol command dispatcher from the TCP JSON transport and adds the documented `sync_time` command. The working native USB CDC + legacy ASCII path remains untouched for rollback.

## Edited files

- `Firmware/Protocol/identium_protocol.c` — now handles JSON line framing/events and delegates inbound commands to the common command dispatcher; command replies remain on the same protocol transport.
- `Firmware/Protocol/identium_command.h` — new protocol-independent command dispatcher interface and temporary generic error values used by the current migration stage.
- `Firmware/Protocol/identium_command.c` — new common command dispatcher for `pause_inventory`, `resume_inventory`, `sync_time`, and `reboot`; unsupported company commands are explicitly rejected.
- `Firmware/Services/Time/time_service.h` — added epoch set/get APIs required by `sync_time`.
- `Firmware/Services/Time/time_service.c` — added an epoch base synchronized to the incoming epoch-millisecond command while preserving the existing monotonic tick API.
- `Tools/identium_tcp_json_test_server.py` — added a `sync` test command that sends the current epoch time in milliseconds.

## Company-protocol behaviour added

- `sync_time` accepts the command envelope with a bare epoch-millisecond `command_data`.
- Values below `1000000000000` are rejected, matching the specification's sanity check.
- A valid value updates the firmware's epoch time base and returns a `command_reply`.
- Command handling is now isolated from the TCP transport so HTTP/MQTT can reuse the same dispatcher later.

## Not changed yet

- No controller GPIO implementation.
- No `set_gpo` / `get_gpi` hardware execution yet.
- No HTTP/MQTT transport.
- No binary TCP framing.
- No configuration manager / TCP 8080 configuration port.
- Legacy ASCII/native USB remains available during migration.
