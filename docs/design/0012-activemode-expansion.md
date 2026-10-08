# 0012 — ActiveMode Expansion

## Purpose

This change expands the working F429 Identium configuration service from the small design 0010 subset to the safe ActiveMode fields that can be implemented without changing the reader's network identity or unverified RF behavior.

The implementation remains based on the supplied company protocol specification. Configuration commands are still partial updates: only keys supplied by the server are changed and the candidate is validated before anything is committed.

## Supported `set_runtime_conf` fields

### `glob_params`

- `hb_cylce` — 5 to 7200 seconds. The company spelling is intentionally preserved.
- `s_buf_size` — 1500 to 8192 bytes.
- `thwdt_net_timeout` — minutes; 0 disables the network recovery timer.
- `thwdt_inv_timeout` — minutes; 0 disables the inventory recovery timer.
- `start_inv_atbeg` — 0 or 1.
- `remote_cmd_reply` — 0 or 1.

### `upload`

- `client_ack` — only 0 is accepted in the current JSON-over-TCP implementation. The documented positive acknowledgement mechanism is part of the binary TCP framing and is not silently emulated here.
- `crc_enable` — only 0 is accepted. The binary TCP CRC polynomial is not documented and the current transport is JSON-over-TCP.
- `recv_timeout` — stored and consumed by the active configuration; no additional range was invented because the supplied specification does not give one.
- `clr_r_buf_time` — stored and consumed by stale-tag disposal.
- `hw_inf` — only 1 (Ethernet) is accepted in this F429 build. Other interfaces are not implemented by this prototype.
- `data_aggr.mode` — 0 immediate / 1 time-batched.
- `data_aggr.timeval` — 50 to 86,400,000 ms.

### Other ActiveMode fields

- `events` — codes 1, 2, 3 and 6 are supported.
- `cus_param` — up to 64 ASCII characters.

Event codes 4 (empty-data event) and 5 (`tag_coming`) are deliberately rejected because their reader-side semantics were not established from the supplied firmware evidence. No invented timing or tag-presence heuristic is used.

The readback also exposes the reference fields `bin_msg_fmt_ver: 0` and `tagcom_raw_mode: 0`; their effect is explicitly documented as unknown.

## Runtime behavior added

### Event subscriptions

The active protocol now checks the configured `events` array before emitting:

- `tag_read` (1)
- `heart_beat` (2)
- `gpi_changed` (3)
- `sync_time_req` (6)

`sync_time_req` is emitted once per boot, at the first successful outbound TCP connection when event 6 is subscribed. This preserves the protocol's boot-time time-synchronisation requirement without generating a request on every reconnect.

### Data buffering

The bounded raw-tag queue from design 0011 remains in place. This change makes the queue consumers use the configured ActiveMode values for:

- `s_buf_size`
- `data_aggr.mode`
- `data_aggr.timeval`
- `clr_r_buf_time`
- `thwdt_net_timeout`
- `thwdt_inv_timeout`

### Persistence

The configuration record is upgraded to V2. V1 records (design 0010) are still readable and are migrated when the next permanent configuration save occurs.

The same two Flash sectors and alternating-record strategy remain in use. The record contains a magic, version, data size, monotonic sequence, payload and CRC32. A corrupted newest slot falls back to the older valid slot.

## Deliberate non-support

The following are not accepted as writable runtime configuration in this step:

- `glob_params.name`
- `upload.tcp.*`, `upload.http.*`, `upload.mqtt.*`
- non-Ethernet `upload.hw_inf` values
- positive `client_ack`
- positive `crc_enable`
- `tag_json_format`
- `gpo_act`
- `gpi_trigger`
- additional ActiveMode/StaticModel fields

These are not silently ignored. The command is rejected with the configuration unsupported error where appropriate.

Network addressing, RF region, RF power, tag filtering, serial configuration, GPO action tables and other hardware-affecting settings are intentionally left for later steps because they need their own application-layer implementation and validation.

## Verification package

`Tools/identium_active_mode_test.py` exercises:

1. Extended ActiveMode persistence.
2. Nested partial update preservation.
3. Atomic rejection of mixed valid/invalid configuration.
4. Duplicate JSON-key rejection.
5. Range checking for `s_buf_size` and aggregation `timeval`.
6. Rejection of unsupported `client_ack` / CRC / event codes.
7. Temporary-static pause enforcement regression.
8. Event-6 `sync_time_req` on the post-reboot connection.
9. Final restoration of the normal `[1,2,3]` event profile.

## Expected hardware sequence

1. Build and flash the firmware on the STM32F429.
2. Run the existing configuration-write regression test first.
3. Run `Tools/identium_active_mode_test.py --reader-ip 192.168.150.50`.
4. Confirm the final ActiveMode readback returns the normal profile.
5. Do not change the reader IP, gateway, RF region or TCP target while this step is under test.

## Remaining protocol work

This change does not claim the entire Identium feature set is complete. Remaining areas include `update_fw_by_http`, the unsupported event/configuration groups listed above, and final legacy-ASCII retirement. ReaderSense packet-level interoperability is being investigated separately from this configuration expansion using a packet capture so that no transport assumptions are invented.
