# WiFi coprocessor architecture

## Scope and ownership

The STM32 remains the reader's protocol and application controller. It owns host command parsing, inventory/tag reports, reader settings, and HTTP request formatting. The ESP32 coprocessor owns WiFi station/access-point operation, IP setup and TCP sockets only. There is no MQTT and neither side allocates memory dynamically in the STM32 firmware.

The STM32's cooperative super-loop calls `WiFi_Service_Task()` alongside `Ethernet_Service_Task()`. Each call advances a bounded state machine; it must not wait for I2C, DHCP, connection establishment, or socket data. When the ESP is unavailable, WiFi remains offline and the existing USB, serial, W5500 Ethernet and host command services continue operating. The link is retried in the background.

WiFi host bytes are inserted as `HOST_TRANSPORT_WIFI`, so the existing host router parses the same commands as the Ethernet TCP server. Command replies and tag reports use that source transport. Socket 0 is the host TCP server (the same product host TCP port as Ethernet); socket 1 is the active uploader. The existing configuration/web port and Ethernet host server are not replaced.

## I2C electrical and board wiring

The bus uses 7-bit address `0x42`, STM32 master and ESP32 slave. Both sides use open-drain I2C at 400 kHz and require common ground and 3.3 V-compatible logic. The slave is polled; there is no interrupt wire.

### F429 Discovery bench rig with classic ESP32 DevKit V1

| F429 Discovery | ESP32 DevKit V1 | Connection |
|---|---|---|
| PA8, I2C3 SCL, AF4 | GPIO22 | SCL |
| PC9, I2C3 SDA, AF4 | GPIO21 | SDA |
| PA15 | EN / CHIP_EN | Reset control |
| GND | GND | Common ground |
| 3.3 V logic pull-up rail | 3.3 V | 4.7 kΩ pull-up on each of SCL and SDA |

Do not connect either I2C signal to 5 V. PA15 doubles as JTDI when full JTAG is enabled; use SWD debugging or free that pin from full-JTAG use. The ESP32's EN pin is active-low reset and should have its normal board-level pull-up. The ESP board must be powered from a supply that can handle WiFi current bursts; do not power it from a weak GPIO or assume the F429 3.3 V rail is adequate.

### F411 custom PCB

The requested pins PB6/PB7/PB9 collide with the existing host UART and INPUT2 assignments in `Firmware/BSP/f411_custom/board_config.h`. Those existing assignments are deliberately not changed. The board has the candidate I2C1/PB6/PB7 and EN/PB9 mapping documented in the BSP, but `BOARD_ESP_LINK_ENABLED` is set to `0U`; WiFi coprocessor hardware is therefore disabled on this target until a conflict-free production pin map is agreed and the PCB is revised. Do not enable it by changing the macro alone while the collision remains.

## Wire protocol

The one shared definition is `Firmware/Devices/esp32/esp32_link_protocol.h`, included directly by both firmware projects. Multi-byte frame fields are little-endian except the TCP port bytes in `SOCK_OPEN`, which are explicitly big-endian.

### Frame format

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | Magic `0x52` (`R`) |
| 1 | 1 | Magic `0x57` (`W`) |
| 2 | 1 | Protocol version (`1`) |
| 3 | 1 | Sequence number, echoed in response |
| 4 | 1 | Command |
| 5 | 1 | Socket id (`0` host, `1` uploader) |
| 6 | 2 | Payload length, little-endian |
| 8 | 1 | Flags |
| 9 | N | Payload |
| 9+N | 2 | CRC-16/CCITT-FALSE, low byte first |

CRC parameters are polynomial `0x1021`, initial value `0xFFFF`, no final XOR. Maximum request payload is 220 bytes. Responses are always 64 bytes on the I2C read; a response frame's declared payload is at most 53 bytes and the unused tail is zero-padded. Response flags include `RESPONSE`; `NOT_READY` asks the master to retry, and `ERROR` indicates an application/command error. A response begins with an application status byte (`OK`, `FAILED`, `INVALID`, `BUSY`, or `NOT_CONNECTED`).

### Transaction timing

The STM32 starts a non-blocking I2C write and waits for its completion. It then waits at least 2 ms before reading the 64-byte response, because the ESP32 slave cannot construct new response bytes after the master read has begun. A `NOT_READY` response is retried with a bounded count. The WiFi service polls status on a nominal 5 ms cadence, subject to completing the preceding non-blocking transaction. No ISR attempts to poll the ESP32 or run application logic.

### Commands

| Command | Purpose |
|---|---|
| `HELLO` | Protocol version, ESP chip family and MAC |
| `GET_STATUS` | WiFi state, IPv4 address, RSSI, socket-connected bitmap, socket RX/event bitmap and MAC |
| `WIFI_CONFIG` | Re-send enable, STA/AP mode, credentials, DHCP/static IPv4 values and AP options |
| `WIFI_CONNECT` / `WIFI_DISCONNECT` | Start connection / disconnect |
| `SCAN_START` / `SCAN_RESULTS` | Start an asynchronous scan and read indexed scan results |
| `SOCK_OPEN` | Open the host listener or upload TCP client |
| `SOCK_CLOSE` | Close a socket |
| `SOCK_SEND` | Queue data and return the accepted byte count |
| `SOCK_RECV` | Read available bytes from a socket ring buffer |
| `RESET` | Restart the ESP32 after the response has been staged |

The ESP32 keeps `GET_STATUS` data pre-staged/cached so the master poll is inexpensive. Socket RX/TX queues are bounded ring buffers. Only one host TCP client is accepted at a time. The ESP receives configuration after every successful handshake and does not intentionally persist WiFi credentials in NVS.

## Recovery and concurrent Ethernet

After successive I2C errors or timeouts, the STM32 generates a STOP, clocks SCL up to nine times to release a stuck bus, reinitializes its I2C peripheral, pulses ESP EN low, waits for boot, and starts the handshake again. After handshake it re-sends current settings and reopens the applicable sockets. An absent/dead ESP must not stop the main loop.

Ethernet and WiFi host transports can run concurrently. The `upload.interface` setting chooses `eth`, `wifi`, or `auto`. In `auto`, the STM32 selects Ethernet while the W5500 physical link is up and WiFi otherwise. Active TCP and HTTP uploads share a narrow upload transport interface; HTTP formatting remains in the STM32 HTTP upload service.

## Settings and credentials

The reader settings record is version 3. Version 2 settings are loaded using the old record layout and migrated in RAM with default WiFi fields; the next save erases the old-size append-only sector and writes a v3 record. Existing v1 migration is retained. WiFi settings include enable, `sta`/`ap`, SSID/password, DHCP or static IP/netmask/gateway/DNS, AP SSID/password/channel and upload interface. The existing settings JSON read path outputs password fields as `"****"`. Sending those masked values back preserves the stored password; a new password must be sent explicitly to change it.

## Build and bench verification

The ESP-IDF project is `Esp32/wifi_coprocessor`. Build it with an ESP-IDF v5 environment once for each target, `esp32` and `esp32c3`. The classic ESP32 uses SCL/SDA GPIO22/GPIO21; the ESP32-C3 uses GPIO6/GPIO10. Keep C3 strapping pins IO2, IO8 and IO9 unused by this link.

Suggested bench procedure:

1. Build and flash the ESP-IDF project for classic ESP32, connect the listed wires and power both boards from appropriate 3.3 V-compatible supplies.
2. Build and flash the STM32F429 Discovery firmware with `TARGET_BOARD=BOARD_F429_DISCOVERY`; verify SWD programming/debug is available with PA15 used for ESP reset.
3. Boot both boards and verify USB CDC, serial and W5500 Ethernet host commands still work before enabling WiFi.
4. Configure WiFi settings through the existing settings command path. Read the settings back and confirm both password fields show `****`; send the readback unchanged and confirm the credentials remain usable.
5. Test STA connection and AP mode separately. Confirm the status reports the state/IP and that one TCP client can connect to the WiFi host port and issue an existing reader command.
6. Keep Ethernet plugged in and verify Ethernet and WiFi host connections can both be active. Test upload `eth`, `wifi`, and `auto` with both TCP and HTTP upload modes; unplug Ethernet to verify `auto` selects WiFi and reconnect Ethernet to verify it prefers Ethernet.
7. Stop/reset the ESP or disconnect I2C and verify the reader loop and USB/serial/Ethernet continue. Restore the ESP and verify background recovery, handshake, configuration and sockets return.
8. Repeat the ESP-IDF build for `esp32c3` and wire GPIO6/GPIO10/EN according to the C3 module's board design. Do not connect the classic DevKit's pins by assumption to a C3 module.

Full hardware validation is required before release. The pin conflict on the F411 production board remains unresolved. The repository's previously referenced design notes `0013`–`0015` were not present in the supplied archive, so this document records the implementation as delivered rather than trying to reconstruct absent design decisions.
