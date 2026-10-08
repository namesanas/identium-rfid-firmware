# 0001 — Identium Protocol Transport

## Scope

This step introduces the first real company-protocol path while preserving the known-good native USB CDC + legacy ASCII GUI path for rollback.

The Ethernet host path is now an outbound TCP client using the company's JSON-over-TCP variant. The reader connects to the configured server and exchanges newline-delimited JSON on the same connection.

## Temporary development settings

- Reader name: `RFID-F429-DEV`
- Reader IP: `192.168.150.50`
- Protocol TCP server: `192.168.150.100:9000`
- Local TCP source port: `40001`
- Heartbeat: 10 s

These are development defaults, not final persistent product configuration.

## Implemented

### Reader -> server

- `heart_beat`
- `tag_read` (one tag per JSON event for this first step)

### Server -> reader

- `pause_inventory`
- `resume_inventory`
- `reboot`

Unsupported company commands currently return `not supported operation` so they are visible to the server instead of being silently ignored.

## Still intentionally legacy / next steps

- Native USB CDC remains the existing ASCII command path during migration.
- TCP binary protocol is not implemented yet.
- HTTP JSON is not implemented yet.
- MQTT is not implemented yet.
- Configuration port 8080 is not implemented yet.
- `sync_time`, `set_gpo`, `get_gpi`, configuration commands and OTA command are reserved for later migration stages.
- GPIO implementation remains blocked until the protocol migration reaches the configuration/output stages.

## Important implementation details

- W5500 now supports TCP active-open using `CONNECT` and destination IP/port registers.
- W5500 TCP keep-alive is configured to 30 s. WIZnet documents the keep-alive timer in 5 s units.
- Ethernet reconnection uses capped exponential backoff.
- Protocol RX accepts multiple newline-delimited JSON messages in one W5500 receive operation.
- Oversized protocol lines are discarded instead of being parsed as a truncated command.

## Test

Run the included development server on a PC at `192.168.150.100`:

```text
python Tools/identium_tcp_json_test_server.py
```

Connect the PC and reader to the same `192.168.150.x` network. The server should report an inbound reader connection, then receive a heartbeat immediately and every 10 seconds, followed by `tag_read` events while inventory is running.

Use the server prompt to send `pause`, `resume`, or `reboot`.
