# Identium Protocol Hardening — Step 7

## Scope

Step 7 hardens the working STM32F429 JSON-over-TCP implementation without changing the Step 6 command/configuration semantics.

The implementation follows the supplied protocol specification's hardening requirements: persistent TCP framing, bounded buffering, complete-message transmission, stale tag disposal, capped reconnect backoff, and optional network/inventory recovery timers. The current F429 ActiveMode profile keeps the recovery timers disabled and data aggregation in immediate mode, matching the existing configuration readback.

## Firmware changes

### `Firmware/Protocol/identium_protocol.c`
- Keeps the 2048-byte receive accumulator and discards an oversized line until its terminating LF.
- Resynchronizes at the first `{` after leading whitespace or garbage, allowing a valid JSON object to follow malformed prefix bytes.
- Prevents heartbeat-counter jumps when a heartbeat cannot be transmitted.
- Adds a bounded queue of raw RFID tag records (256 entries) so temporary TCP outages no longer drop every tag immediately.
- Discards queued tags older than `clr_r_buf_time` (8 s in the current F429 profile).
- Serializes queued tags only at transmission time and limits every `tag_read` message to `s_buf_size` (1580 bytes).
- Supports the documented `data_aggr` modes: mode 0 emits individual tag events; mode 1 forms a time-based batch.
- Flushes only a bounded number of mode-0 tag events per foreground pass so the RFID and GPIO tasks keep running.
- Adds the documented network and inventory inactivity recovery hooks. Both are disabled in the current profile because their configured values are 0.

### `Firmware/Services/ethernet/ethernet_service.c/.h`
- Adds `Ethernet_Service_SendComplete()`, which checks the W5500 TX free-space register before writing an application message.
- A JSON application message is therefore never intentionally truncated because TX space is temporarily unavailable. The caller retries the complete message later.
- Existing reconnect backoff behavior remains unchanged.

### `Firmware/Config/product_config.h`
Adds named constants for the current ActiveMode upload/recovery profile:
- `s_buf_size = 1580`
- `client_ack = 0`
- `recv_timeout = 10 s`
- `clr_r_buf_time = 8 s`
- `data_aggr.mode = 0`
- `data_aggr.timeval = 1000 ms`
- `thwdt_net_timeout = 0`
- `thwdt_inv_timeout = 0`
- `remote_cmd_reply = 1`

These fields are not yet writable by `set_runtime_conf`; they simply prevent the hardening logic from inventing a different profile than the current readback.

## Deliberately unchanged

- Step 6 configuration service and Flash persistence.
- Native USB CDC.
- Legacy VCP.
- RFID module command sequence.
- GPIO mappings and command semantics.
- Configuration port 8080.
- RF region.
- Future F411 configuration.

## Verification

1. Build and flash this package on the STM32F429.
2. Re-run `Tools/identium_config_write_test.py` and require the exact Step 6 PASS result.
3. Run `Tools/identium_tcp_hardening_test.py` for fragmented frames, multiple frames in one write, garbage resynchronization, oversized-line recovery, whitespace handling and reconnect.
4. With tags present, observe `tag_read` JSON remains valid across a short server outage and that stale queued reads are not replayed after the 8-second stale-data window.
5. Keep the current recovery timers at 0 during bench testing; recovery timers are a later configurable feature and are not enabled by this step.

## Conformance basis

The supplied protocol specification explicitly calls out fragmented frames, two frames in one write, and garbage followed by a valid frame as TCP conformance tests. It also requires bounded tag buffering, stale-data disposal, capped reconnect backoff and optional watchdog recovery.
