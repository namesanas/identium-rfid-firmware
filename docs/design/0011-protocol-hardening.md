# Identium Protocol Hardening — Step 7A

## Scope

Step 7 follows the protocol specification's hardening stage. The first tranche hardens the JSON-over-TCP transport without changing the already-tested configuration, RFID, GPIO, USB or protocol command semantics.

## Changes

### `Firmware/Protocol/identium_protocol.c`
- JSON line parser now ignores leading whitespace.
- If a line begins with garbage before a valid `{`, the parser re-synchronises at the opening brace instead of discarding the entire line.
- Existing bounded 2048-byte receive line buffer and oversized-line discard behavior remain unchanged.

### `Firmware/Services/ethernet/ethernet_service.c`
- Socket 0 receive processing now drains up to four 512-byte chunks per foreground pass.
- The drain is explicitly bounded so a busy TCP stream cannot starve RFID, GPIO, configuration-port or watchdog processing.
- Existing capped exponential reconnect backoff is preserved.

### `Firmware/Devices/w5500/w5500.c`
- Socket 0 TX no longer silently truncates an application message when TX free space is insufficient.
- It now returns 0 and requires the caller to retry the complete message.
- This prevents truncated JSON objects from being placed on the protocol stream.

## Not changed

- Configuration behavior from Step 6.
- Native USB CDC.
- Legacy VCP.
- RFID command/application behavior.
- GPIO mapping.
- Configuration port 8080.
- RF region and production F411 configuration.

## Verification target

Run the existing Step 6 hardware configuration test again after building/flashing to prove no regression. Then run the Step 7 TCP hardening tests for:

1. Fragmented JSON across multiple TCP writes.
2. Two newline-delimited JSON commands in one TCP write.
3. Garbage bytes followed by a valid JSON command in the same line/write.
4. An oversized garbage line followed by a valid command on the next line.
5. Reconnect after the server closes the TCP connection.

The protocol specification identifies fragmented frames, two frames in one write, and garbage followed by a valid frame as explicit conformance tests 21-23.
