# 0013 — Vendor Tool Compatibility (Reader Manager / ReaderSense)

## Purpose

Make the F429 usable from the vendor PC tools — Reader Manager 2.2 and ReaderSense 4.0.0 (client edition) — without changing the Identium JSON-over-TCP upload protocol from designs 0001–0012.

Both tools use `ModuleAPI.dll`. On TCP 8080 they talk to an "ARM7 mainboard": Silion module frames (`FF …`) are passed through to the RFID module, a few ASCII commands are answered by the mainboard, and `EE` configuration requests use separate one-shot connections. The wire formats below were taken from the decompiled `ModuleAPI.dll` (both versions), `ReaderManager.exe` and ReaderSense's `ModuleBridge.exe`.

## Port 8080 — first byte selects the protocol per connection

| First byte | Handler | Connection |
|---|---|---|
| `EE` | Identium configuration commands (`configuration_port.c`) | one request, client closes |
| anything else | module bridge (`module_bridge.c`) | persistent session |

### Module bridge (`Services/configuration/module_bridge.c`)

- `FF` module frames are forwarded unchanged to the E310 UART; module output is relayed back unchanged.
- While a tool session is open the bridge owns the module: local (JSON) inventory is stopped and resumed 5 s after the tool disconnects. JSON commands that need the module (`resume_inventory`, …) fail during that time.
- Answered locally:

| Request | Reply |
|---|---|
| `IOGET` | `IOGET` + GPI bitmap (never 0x80, which ModuleAPI4 treats as "reader busy") |
| `IOSET 01 pin state` | `IOSETOK` (GPO 1–4; GPO 5/buzzer acknowledged, no hardware) |
| `GIO` (during fast inventory) | `FF 04 AA 00 00 'G' 'I' 'O' bitmap CRC` inside the tag stream |
| `SIO 01 pin state` (during fast inventory) | `FF 03 AA 00 00 'S' 'I' 'O' CRC` inside the tag stream |
| `VER` | 4 bytes `PRODUCT_MAINBOARD_VERSION_0..3` |
| `IPGET` | `IPGET` ip mask gw mac port(BE16) |
| `IPSET` ip mask gw mac port | no reply (tool does not read one); saved and applied after 1.5 s |
| 32 × `FF` (module reset) | discarded |

- Local replies are inserted only at module frame boundaries, so they never split a tag frame during a fast (async) inventory.

### Stream reliability (fast inventory)

Reader Manager always uses fast (async) inventory for E310 modules: the module streams tag frames continuously. Fixes:

- module UART receive queue 256 B → 4 KB (`sillion_application_port.c`)
- relay drains the queue in up to 4 × 1 KB chunks per loop
- `W5500_Socket1_Send()` waits for the previous SEND to complete before issuing another (W5500 requirement)

## EE configuration commands (`vendor_config.c`, `configuration_port.c`)

| Cmd | Meaning | F429 behaviour |
|---|---|---|
| 0x22 | get work state | from persistent settings (1 passive, 3 active) |
| 0x24 / 0x1E | get Active / Static JSON | **no NUL padding any more** — ReaderSense passes the content to `JSON.parse` |
| 0x25 | set ActiveModeJson | supported fields applied through the ActiveMode runtime parser (design 0012); `glob_params.name` and `upload.tcp.ser_ip/ser_port` stored in reader settings; sets work state active |
| 0x1F | set StaticModelJson | `ethernet.ip/nm/gw/dns`, `rf.region`, `rf.tx_powers[0].rp/wp`, `protocol.gen2.session`; `reboot:1` restarts |
| 0x20 | clear | Identium configuration reset to defaults, work state passive |
| 0x23 | switch | toggles passive ↔ active |

The tools send whole documents (read → edit → send back). Fields the F429 cannot act on are ignored instead of rejecting the request. The reply is sent first and the change is applied immediately afterwards, because Reader Manager waits only 1000 ms while a configuration save erases a 128 KB flash sector.

`upload.http` selects HTTP upload (see below); `upload.tcp` selects JSON-over-TCP.

Ignored fields (no hardware or not implemented): mqtt / wiegand / hidkb upload, `gpo_act`, `gpi_trigger`, `tag_json_format`, event codes 4/5, non-zero `client_ack`/`crc_enable`, wlan / bluetooth / 4G / uart sections, DHCP (`ip = 0.0.0.0` keeps the current static address), `tag_data`, `tagops_param`.

## Work state

- **Active (3)**: JSON upload client connects to the configured server; inventory starts at boot if `start_inv_atbeg`.
- **Passive (1)**: no upload connection, no autonomous inventory; the reader waits for the tools.

## HTTP upload (`Services/ethernet/http_upload.c`)

Active mode can upload over HTTP instead of TCP, as described in the protocol specification Part D (e.g. to a ReaderSense web endpoint `http://213.210.21.60:4612/rfid/<token>`).

- **How to select it:** in Reader Manager's Active-mode settings, choose HTTP, enter the URL and press Set. The tools send `upload.http = {url, timeout}` with `tcp` set to `null`. Choosing TCP again switches back. The URL, timeout and upload type are stored in reader settings (record version 2; version-1 records are read once and migrated).
- **Allowed URLs:** only `http://` with a numeric IPv4 host and an optional port (default 80) and path. `https://` and host names are rejected with an error status (no TLS, no DNS on the F429).
- **Requests:** every event (tag_read, heart_beat, gpi_changed, command_reply, sync_time_req, reader_exception) becomes one `POST <path>` with an exact `Content-Length` and `Connection: keep-alive`. `reader_name` is sent as `name/reader-ip`, as Part B.1 requires for HTTP.
- **One request at a time:** events produced while a request is in flight wait in a 12 KB queue. A full queue makes tag batches retry later, exactly as with TCP.
- **Responses:**
  - A non-empty 2xx body is one command and is executed by the same command parser as TCP JSON lines.
  - Its `command_reply` is sent as the next POST, from its own buffer, so the Part D.6 corrupted-reply defect does not occur.
  - Non-2xx bodies (e.g. 404 `{"error":"unknown endpoint"}`) are not executed.
  - Content-Length, chunked and close-delimited bodies are supported.
- **Failures:** with no response within `upload.http.timeout` (or `upload.recv_timeout`, default 10 s, limited to 2–120 s), the connection is closed and the request is sent again on a new connection. After 3 attempts the event is dropped.
- **Connection:** the W5500 uses socket 0, the same socket as TCP upload; only one upload transport is active at a time. A new source port (50000–50999) is used per connection. Socket 0 sends now wait for the previous SEND to finish, as on socket 1.
- **Network:** the reader's gateway (default 192.168.150.1) must route to the server when the server is not on the local network.

## Device search — UDP 15000 (`discovery_service.c`, W5500 socket 2)

Request `EE 06 00 00 50 01` + target MAC (all zero = any). 47-byte reply unicast to the sender: MAC, DHCP flag, IP, mask, gateway, DNS, port 8080, board type 1 (Hc32f46x), mainboard version, work state.

## Persistent reader settings (`reader_settings.c`)

New record in flash sector 21 (append-only log, last valid record wins; erased only when full): IP, netmask, gateway, DNS, MAC, upload server IP/port, reader name, work state, RF region/powers.

- The application flash region in `STM32F429ZITX_FLASH.ld` is reduced from 1792 K to 1664 K so sector 21 is never overwritten by code (image is ≈106 KB).
- **MAC changed**: the old fixed `11:22:33:44:55:66` had the multicast bit set. The default is now `02:1D:xx:xx:xx:xx` derived from the MCU unique ID. Clear the PC's ARP cache (`arp -d *`) once after flashing if the reader is not reachable.
- The reader name, server address and work state used by the Identium JSON protocol now come from these settings (defaults = previous product constants).

## COM route — native USB CDC (`host_transport.c`)

The tools' "COMx" address uses the same protocol as TCP 8080. On the USB virtual COM port a router looks at the start of each line:

| First bytes | Goes to |
|---|---|
| `FF` | module bridge (USB session) |
| `IOGET` `IOSET` `IPGET` `IPSET` `GIO` `SIO` `VER` | module bridge (USB session) |
| `EE 00 len cmd flags …` (+ CRC16-Modbus when flags bit 1) | configuration commands, reply on USB |
| anything else | legacy ASCII parser (unchanged) |

No legacy command starts with these tokens. While a USB session is active every USB byte goes to the bridge; it ends after 30 s with no traffic in either direction (a COM port gives no close event) and the next frame starts a new one. Only one tool session (TCP or USB) at a time. Baud rate is irrelevant on CDC.

The tools' separate "USB" address type is USB HID (VID 0x2E88 / PID 0x4605 via hidapi.dll) and is not supported — the F429 enumerates as CDC; use the COM address.

## Not supported (and why)

- Firmware update from Reader Manager / ReaderSense: it uses the HC32F46x mainboard bootloader protocol (`INITUPFV`), which does not apply to this STM32 board. Module (E310) firmware update through the bridge is not attempted.
- PSAM: not supported by ModuleAPI for ARM7 boards.
- `CONFIGRX` / `CONFIGWX`, `SM6E`: no tool uses them.

## Test

`Tools/reader_manager_connect_test.py --host 192.168.150.50 [--write-tests] [--async-gpio]`
replays the tools' connect sequence and checks IOGET/IOSET/IPGET, EE reads (valid JSON, no NULs), optional work-state toggles, GIO/SIO frames during a fast inventory and UDP discovery.
