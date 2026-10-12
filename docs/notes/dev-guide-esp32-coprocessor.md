# ESP32 WiFi coprocessor: developer guide

This guide covers the ESP-IDF project in `ESP32_Firmware/`: building and flashing it for both boards, and what you need to know to change it. For the full design rationale, read `docs/design/0016-wifi-coprocessor.md`.

Items marked **(unverified)** were not confirmed from the code or docs and should be checked on hardware.

## 1. What the coprocessor does

The ESP32 gives the STM32 a WiFi network and TCP sockets. Nothing else.

| The ESP does | The ESP does not |
|---|---|
| Run WiFi in station (STA) or access-point (AP) mode | Parse reader or host commands |
| DHCP or static IPv4 (values sent by the STM) | Store settings. WiFi uses `WIFI_STORAGE_RAM`, and credentials are never written to NVS |
| Two TCP sockets: a host server (socket 0) and an upload client (socket 1) | Format HTTP, MQTT or tag reports (HTTP formatting stays in the STM) |
| Buffer socket data in fixed 2048-byte RX and TX rings per socket | Start WiFi on its own. After boot it sits in `DISABLED` until the STM configures it |
| Answer the STM over I2C as a polled slave at address `0x42` | Raise an interrupt. There is no IRQ line; the STM polls |

The STM32 owns the reader settings. After every successful handshake it re-sends the WiFi config, connects, and reopens the sockets. Reflashing or resetting the ESP therefore loses nothing.

## 2. Source file map

| File | Role |
|---|---|
| `ESP32_Firmware/CMakeLists.txt` | Project file. Project name `wifi_coprocessor` |
| `ESP32_Firmware/sdkconfig.defaults` | Shared defaults for both targets: log level INFO, size optimisation, `FREERTOS_HZ=1000`, WiFi RX/TX buffer counts |
| `ESP32_Firmware/.gitignore` | Ignores `build/`, `build_*/`, `sdkconfig`, `*.log`. Build folders and their sdkconfig files are not in git |
| `main/CMakeLists.txt` | Component registration. Adds `STM32_Firmware/Firmware/Devices/esp32` to the include path so the protocol header is shared |
| `main/main.c` | `app_main`: NVS init, `WifiManager_Init`, `SocketService_Init`, then starts the `i2c_link` task (pinned to core 1 on the classic ESP32) |
| `main/i2c_link.c/.h` | I2C slave driver setup, pin and chip ID per target, frame parser, CRC check, command dispatch, reply replay |
| `main/wifi_manager.c/.h` | STA/AP start, reconnect, static IP, scan, status |
| `main/socket_service.c/.h` | Non-blocking lwIP sockets, ring buffers, connected/RX/event bitmaps |
| `STM32_Firmware/Firmware/Devices/esp32/esp32_link_protocol.h` | **Shared** wire protocol: command IDs, offsets, result codes, CRC. Used as-is by both projects |
| `STM32_Firmware/Firmware/Devices/esp32/esp32_link.c/.h` | STM side: I2C master, retries, bus recovery, EN reset |
| `STM32_Firmware/Firmware/Services/wifi/wifi_service.c/.h` | STM side: state machine that configures the ESP, polls status and moves socket data |
| `STM32_Firmware/Firmware/BSP/*/board_config.h` | STM pins for the link (`BOARD_ESP_*`) |

## 3. I2C link protocol summary

The STM32 is the I2C master and the ESP is the slave at 7-bit address `0x42`. The bus runs at 400 kHz (`I2C_SCL_SPEED_FM4K` on the STM, `maximum_speed = 400000` on the ESP). The STM uses own address `0x7F` so it never answers as a slave.

### Frame

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Magic `0x52 0x57` ("RW") |
| 2 | 1 | Protocol version, currently `1` |
| 3 | 1 | Sequence number (echoed back) |
| 4 | 1 | Command |
| 5 | 1 | Socket ID (0 = host, 1 = upload) |
| 6 | 2 | Payload length, little-endian |
| 8 | 1 | Flags: `0x01` RESPONSE, `0x02` NOT_READY, `0x04` ERROR |
| 9 | N | Payload (request max 220 bytes) |
| 9+N | 2 | CRC-16/CCITT-FALSE (poly `0x1021`, init `0xFFFF`), low byte first |

Every reply is read as a fixed **64-byte** block, so a reply carries at most 53 payload bytes. Byte 0 of a reply payload is a result code: `0` OK, `1` FAILED, `2` INVALID, `3` BUSY, `4` NOT_CONNECTED.

### Transaction

1. The STM writes the request frame.
2. It waits at least 2 ms, then reads 64 bytes. The ESP must stage the reply in its TX buffer before the read starts; the classic ESP32 cannot stretch the clock.
3. If the read returns an old reply or garbage, the STM re-reads (4 fast reads, then every 10 ms, up to a 1.5 s window). If there is still nothing, it resends the request with the same sequence number, up to 2 times.
4. The ESP remembers the last sequence/command/socket and its reply. A repeated request gets the stored reply and is **not** executed again, so `SOCK_SEND` and `SOCK_RECV` never run twice.
5. After 3 failed transactions the STM recovers the bus (STOP, up to 9 SCL pulses), pulses EN low for 15 ms, waits 800 ms for boot, and handshakes again.

### What the STM sends

| Command | ID | When the STM sends it |
|---|---|---|
| `HELLO` | `0x01` | After each EN reset. Reply: version, chip ID (1 = ESP32, 2 = ESP32-C3), MAC, socket count |
| `GET_STATUS` | `0x02` | Every 5 ms (nominal). Reply: WiFi state, IPv4, RSSI, connected bitmap, RX-pending bitmap, events, MAC |
| `WIFI_CONFIG` | `0x10` | After handshake and whenever settings change. Fixed 216-byte payload (see the offsets in the header) |
| `WIFI_CONNECT` / `WIFI_DISCONNECT` | `0x11` / `0x12` | After config, or when WiFi is disabled |
| `SCAN_START` / `SCAN_RESULTS` | `0x13` / `0x14` | Implemented on the ESP (up to 16 results). The STM WiFi service does not send them today |
| `SOCK_OPEN` | `0x20` | Socket 0 as a server, socket 1 as a client to the upload target. The port is **big-endian** |
| `SOCK_CLOSE` | `0x21` | Upload target changed, or WiFi went down |
| `SOCK_SEND` | `0x22` | Up to 200 bytes per request. Reply gives the accepted byte count |
| `SOCK_RECV` | `0x23` | Asks for up to 48 bytes when the RX-pending bit is set |
| `RESET` | `0x30` | Reply first, then the ESP calls `esp_restart()` 200 ms later |

WiFi state values: `0` DISABLED, `1` IDLE, `2` CONNECTING, `3` CONNECTED, `4` AP_STARTED, `5` ERROR.

## 4. WiFi modes, credentials and ports

### How credentials reach the ESP

1. You set them on the STM with a host command (or the configuration port JSON).
2. The STM stores them in its settings flash.
3. The STM sends them to the ESP in `WIFI_CONFIG` after every handshake, then sends `WIFI_CONNECT`.

The ESP keeps them in RAM only. A bare ESP with no STM attached will never join a network.

### Host commands (STM side, `Application/Host/host_interface.c`)

| Command | Effect |
|---|---|
| `GET_WIFI` | `WIFI,LINK=ONLINE\|OFFLINE,STATE=...,IP=...,RSSI=...,HOST=0\|1,UPLOAD=0\|1,ENABLE=0\|1,MODE=STA\|AP,SSID=...,IF=ETH\|WIFI\|AUTO,ERRORS=n,REJECTED=<cmd hex>/<result>` |
| `SET_WIFI,<0\|1>,<STA\|AP>,<ssid>,<password>` | Saves and applies at once. SSID cannot contain a comma. Password is the rest of the line: empty = open network, otherwise 8 to 64 characters. `****` keeps the stored password. In AP mode the SSID/password are the reader's own AP |
| `SET_WIFI,0,STA,,` | Disables WiFi and clears the station credentials |
| `SET_UPLOAD_IF,ETH\|WIFI\|AUTO` | Which interface active-mode uploads use. `AUTO` = Ethernet while the W5500 link is up, WiFi otherwise |

Examples:

```
SET_WIFI,1,STA,MyOfficeWiFi,MySecretPass
SET_WIFI,1,AP,RFID-Reader,readerpass123
GET_WIFI
```

`ERRORS` counts failed I2C transactions since the STM started. If it keeps rising, check the wiring and pull-ups.

### Mode details

| | STA | AP |
|---|---|---|
| Behaviour | All-channel scan, connects to the strongest AP, PMF capable (not required), accepts any auth mode | `max_connection = 1`, WPA2-PSK if a password is set, open if not |
| IP | DHCP by default; static IP/netmask/gateway/DNS if `dhcp = 0` in settings | ESP-IDF default AP netif with its DHCP server. The IP is normally 192.168.4.1 **(unverified, ESP-IDF default, not set in this code)**. `GET_WIFI` shows the real IP |
| Reconnect | 1 s after each disconnect, forever | n/a |

Static IP fields and the AP channel are set through the configuration port JSON (`wifi.dhcp`, `wifi.ap_channel`, etc. in `vendor_config.c`), not through `SET_WIFI`.

### Defaults (from `reader_settings.c`)

| Setting | Default |
|---|---|
| WiFi enabled | No (`0`) |
| Mode | STA |
| DHCP | On |
| Static IP / mask / gateway / DNS | 192.168.150.51 / 255.255.255.0 / 192.168.150.1 / 8.8.8.8 |
| AP SSID | `RFID-Reader` |
| AP password | Empty, so the AP is **open** unless you set one |
| AP channel | 1 (valid 1 to 13) |
| Upload interface | AUTO |

### TCP ports

| Socket | Type | Port |
|---|---|---|
| 0 (host) | Server, listens on all interfaces | `PRODUCT_ETHERNET_TCP_PORT` = **5000** (same as the Ethernet host port). One client at a time |
| 1 (upload) | Client | The upload server IP/port from reader settings (or the HTTP upload target) |

Sockets only open while the WiFi state is CONNECTED or AP_STARTED. If WiFi drops, the ESP closes them and reopens them when the network returns.

## 5. Install and activate ESP-IDF

ESP-IDF **v6.1** is installed by the Espressif installer (EIM):

| Item | Path |
|---|---|
| ESP-IDF | `C:\esp\v6.1\esp-idf` (the build logs and the activation profile both use this path) |
| Tools and Python venv | `C:\Espressif\tools`, `C:\Espressif\tools\python\v6.1\venv` |
| PowerShell activation | `C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1` |

Activate in every new PowerShell window:

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
idf.py --version
```

The profile sets `IDF_PATH`, puts the toolchains on `PATH`, and defines `idf.py`, `esptool` and `espefuse` as PowerShell functions/aliases. Note the leading `. ` (dot-source); without it nothing stays set.

To install on a new PC, use the Espressif Installation Manager (EIM) and pick v6.1 with the `esp32` and `esp32c3` targets.

## 6. Build

Each target has its own build folder and its own sdkconfig. Never build both targets into the same folder. In PowerShell the `-D` arguments **must be quoted**, or PowerShell splits them.

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
cd C:\Users\Identium\Downloads\RFID_Controller_F429_IDENTIUM_PROTOCOL_STEP8\ESP32_Firmware

# Classic ESP32 (DevKit V1 bench rig)
idf.py -B build_esp32 "-DIDF_TARGET=esp32" "-DSDKCONFIG=build_esp32/sdkconfig" build

# ESP32-C3 (custom board, ESP32-C3-WROOM-02U)
idf.py -B build_esp32c3 "-DIDF_TARGET=esp32c3" "-DSDKCONFIG=build_esp32c3/sdkconfig" build
```

Outputs, in each build folder:

| File | Flash offset (esp32) | Flash offset (esp32c3) |
|---|---|---|
| `bootloader/bootloader.bin` | `0x1000` | `0x0` |
| `partition_table/partition-table.bin` | `0x8000` | `0x8000` |
| `wifi_coprocessor.bin` | `0x10000` | `0x10000` |
| `flash_args` | lists the above plus flash mode/freq/size | same |

Both targets use DIO, 2 MB flash, single-app partition table. Flash frequency is 40 MHz on the ESP32 and 80 MHz on the C3 (from `flash_args`). The ESP32 app is about 0xB99A0 bytes (27% of the 1 MB app partition free).

### Merged single image

One file at offset `0x0` is easiest for production and for anyone without ESP-IDF. Run inside the build folder:

```powershell
cd build_esp32c3
python -m esptool --chip esp32c3 merge-bin -o wifi_coprocessor_esp32c3_merged.bin "@flash_args"

cd ..\build_esp32
python -m esptool --chip esp32 merge-bin -o wifi_coprocessor_esp32_merged.bin "@flash_args"
```

A merged C3 image already exists at `ESP32_Firmware/build_esp32c3/wifi_coprocessor_esp32c3_merged.bin` (dated 2026-10-12). Build folders are git-ignored, so this file is not in the repo. Rebuild and re-merge after any source change.

Clean a target completely:

```powershell
idf.py -B build_esp32c3 "-DIDF_TARGET=esp32c3" "-DSDKCONFIG=build_esp32c3/sdkconfig" fullclean
```

## 7. Flash

### Wiring reference

| | Bench rig | Custom board |
|---|---|---|
| STM32 | F429 Discovery | F411 custom board |
| ESP | ESP32 DevKit V1 (classic ESP32), CH9102 USB-serial on **COM10** | ESP32-C3-WROOM-02U (U19) |
| STM I2C | I2C3: PA8 = SCL, PC9 = SDA (AF4) | I2C1: PB6 = SCL, PB7 = SDA (AF4) |
| ESP I2C pins | GPIO22 = SCL, GPIO21 = SDA | IO6 = SCL, IO10 = SDA |
| ESP EN driven by | PA15, open-drain | PB9, open-drain |
| Pull-ups | 4.7 kΩ to 3.3 V on SCL and SDA (plus ESP internal pull-ups) | per schematic |
| Programming port | DevKit USB | Header CN11 "WIFI_ESP" + 3.3 V USB-serial adapter |

### Bench rig: ESP32 DevKit V1 on COM10

Gotchas first:

| Gotcha | What to do |
|---|---|
| The DevKit auto-reset does not enter download mode | Hold **BOOT**, tap **EN**, release **BOOT**, then start the flash |
| The STM resets the ESP mid-flash through PA15 → EN | **Unplug the PA15–EN wire** while flashing. Halting the STM over SWD is not enough |
| `idf.py monitor` holds COM10 | Close the monitor (Ctrl+]) before flashing or using any other tool on COM10 |

Flash with idf.py (bootloader, partition table and app):

```powershell
idf.py -B build_esp32 "-DIDF_TARGET=esp32" "-DSDKCONFIG=build_esp32/sdkconfig" -p COM10 flash
```

Or flash the build output with esptool from inside `build_esp32` (the command printed by the build):

```powershell
cd build_esp32
python -m esptool --chip esp32 -p COM10 -b 460800 --before default-reset --after hard-reset write-flash "@flash_args"
```

Or a merged image:

```powershell
python -m esptool --chip esp32 -p COM10 -b 460800 write-flash 0x0 wifi_coprocessor_esp32_merged.bin
```

Afterwards, reconnect PA15–EN and reset the STM so it pulses EN and handshakes.

### Custom board: ESP32-C3 through CN11

CN11 "WIFI_ESP" pinout:

| CN11 pin | Signal |
|---:|---|
| 6 | 3V3 |
| 5 | RX |
| 4 | TX |
| 3 | GND |
| 2 | EN |
| 1 | NC |

Whether the RX/TX labels are from the ESP's side or the adapter's side is **(unverified)**. If esptool cannot connect, swap TX and RX. Use a 3.3 V adapter only. CN11 has no BOOT/DTR/RTS lines, so the reset into download mode is manual.

Procedure:

1. Connect the adapter: GND to pin 3, and TX/RX to pins 4/5. Do not power the board from the adapter's 3V3 unless you know it can supply WiFi current.
2. Fit jumper **J2** (IO9 to GND = download mode).
3. **Hold KEY1** (STM reset) for the whole flash. Otherwise PB9 can reset the ESP partway through. Releasing KEY1 also resets the ESP through PB9, so the C3 restarts with IO9 low and enters download mode.
4. Flash:

   ```powershell
   cd C:\Users\Identium\Downloads\RFID_Controller_F429_IDENTIUM_PROTOCOL_STEP8\ESP32_Firmware\build_esp32c3
   python -m esptool --chip esp32c3 -p COMx write-flash 0x0 wifi_coprocessor_esp32c3_merged.bin
   ```

5. Remove J2, release KEY1. The STM boots, pulses EN, and the C3 starts the new firmware.

Exactly how KEY1 and EN interact (whether you need to tap EN on CN11 pin 2 to enter download mode) is **(unverified)**. If esptool reports "Failed to connect", with J2 fitted briefly pull CN11 pin 2 (EN) to GND and release it, then retry.

### Other esptool commands

Replace the port and chip as needed. These need the chip in download mode (see above).

```powershell
python -m esptool -p COM10 chip-id                     # chip type, revision, MAC
python -m esptool -p COM10 flash-id                    # flash chip and size
python -m esptool --chip esp32 -p COM10 erase-flash    # wipe everything (also NVS)
python -m esptool --chip esp32c3 -p COMx write-flash 0x0 wifi_coprocessor_esp32c3_merged.bin
python -m esptool --chip esp32 -p COM10 read-flash 0x0 0x200000 backup_esp32.bin   # 2 MB backup
```

`erase-flash` is safe: the firmware stores nothing that matters in NVS and re-initialises it on first boot.

## 8. Logs and monitoring

The firmware logs on UART0 at **115200** baud, level INFO.

| Board | Where to read logs |
|---|---|
| DevKit | COM10 (the DevKit USB) |
| Custom board | CN11 TX/RX through the USB-serial adapter. The C3 sdkconfig also enables a secondary console on USB-Serial-JTAG, but whether the module's USB pins are wired on this board is **(unverified)** |

```powershell
idf.py -B build_esp32 "-DIDF_TARGET=esp32" "-DSDKCONFIG=build_esp32/sdkconfig" -p COM10 monitor
idf.py -B build_esp32c3 "-DIDF_TARGET=esp32c3" "-DSDKCONFIG=build_esp32c3/sdkconfig" -p COMx monitor
```

Exit with **Ctrl+]**. The monitor decodes crash backtraces using the ELF in the build folder, so use the build folder that matches the flashed firmware. Any plain terminal (PuTTY at 115200 8N1) also works, without the decoding.

Opening the monitor may reset the ESP through DTR/RTS. The STM sees the link drop and recovers by itself (it reconfigures the ESP when it reports DISABLED).

Messages to look for:

| Log line | Meaning |
|---|---|
| `i2c_link: I2C slave address 0x42, SCL=22 SDA=21` (C3: `SCL=6 SDA=10`) | Link task started. Pin numbers confirm the target build |
| `i2c_link: request CRC mismatch` | Corrupted request frame. Occasional = noise; frequent = wiring, pull-ups, or bus speed |
| `wifi_coprocessor: I2C link init failed` / `WiFi init failed` / `NVS init failed` | Fatal start-up error. The link will never answer |
| `wifi_manager: Initial connect: ...` / `DHCP client start: ...` | STA start problem |
| `socket_service: server bind/listen failed: errno=N` | Host server on port 5000 could not open. Retried every 1 s |
| `Brownout detector was triggered` | Supply dropped (brownout detection is on in both sdkconfigs) |

On the STM side, use `GET_WIFI` (`LINK`, `STATE`, `ERRORS`) over USB or serial.

## 9. Troubleshooting

| Symptom | Likely causes | Checks and fixes |
|---|---|---|
| **No I2C link** (`GET_WIFI` shows `LINK=OFFLINE`, `ERRORS` rising) | Wrong target flashed (C3 image on a classic ESP32 or the reverse); SDA/SCL swapped; no pull-ups; no common ground; ESP not powered; EN held low | Check the boot log pin line. Check 4.7 kΩ pull-ups to 3.3 V. Measure EN high. Confirm the ESP log prints at all. On the F429, PA15 must not be used as JTDI (use SWD, not full JTAG) |
| Link works but `ERRORS` slowly climbs, with occasional EN resets | Missed FIFO deadlines (the classic ESP32 cannot clock-stretch), long wires, weak pull-ups | Keep wires short. Keep the link task on core 1 (`main.c`). Look for `request CRC mismatch` |
| **WiFi won't connect** (`STATE=CONNECTING` forever) | Wrong SSID/password; 5 GHz-only network (ESP32 and C3 are 2.4 GHz only); `ENABLE=0` | Re-send `SET_WIFI,1,STA,<ssid>,<password>`. Check `GET_WIFI` shows `ENABLE=1,MODE=STA`. Watch the ESP log |
| `STATE=CONNECTED` but no TCP on port 5000 | Another client already connected (only one allowed); wrong IP; client network isolation on the AP | `GET_WIFI` shows the IP and `HOST=1` when a client is connected. Close the other client |
| AP mode not visible | AP password 1 to 7 characters (rejected by both sides); channel out of range | Use no password or 8 to 64 characters |
| `REJECTED=<cmd>/<result>` in `GET_WIFI` | ESP refused CONFIG/CONNECT/DISCONNECT | The STM retries after 1 s. Result 2 (INVALID) means bad config values |
| **Flash fails** ("Failed to connect", "No serial data received") | Not in download mode; COM port held by monitor or another tool; wrong port; TX/RX swapped on CN11 | DevKit: hold BOOT, tap EN, release BOOT. C3: J2 fitted, KEY1 held. Close `idf.py monitor`. Check the port in Device Manager |
| Flash starts then fails partway (timeout, "chip stopped responding") | **The STM resets the ESP** through EN when the link stops answering | DevKit: unplug PA15–EN. Custom board: hold KEY1. Halting the STM over SWD is not enough |
| ESP reboots in a loop, `Brownout detector was triggered` | Supply cannot handle WiFi current bursts (several hundred mA peaks) | Power the DevKit from its USB or a proper 3.3 V supply, not a GPIO or a weak rail. Add bulk capacitance near the module |
| ESP reset, stays `STATE=DISABLED` | Normal after a restart: the ESP has no stored config | The STM re-sends config automatically when it sees DISABLED while WiFi is enabled |

## 10. Cheat sheet

```powershell
# Activate ESP-IDF
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
cd C:\Users\Identium\Downloads\RFID_Controller_F429_IDENTIUM_PROTOCOL_STEP8\ESP32_Firmware

# Build
idf.py -B build_esp32   "-DIDF_TARGET=esp32"   "-DSDKCONFIG=build_esp32/sdkconfig"   build
idf.py -B build_esp32c3 "-DIDF_TARGET=esp32c3" "-DSDKCONFIG=build_esp32c3/sdkconfig" build

# Flash DevKit (BOOT held + EN tapped, PA15-EN wire unplugged)
idf.py -B build_esp32 "-DIDF_TARGET=esp32" "-DSDKCONFIG=build_esp32/sdkconfig" -p COM10 flash

# Monitor DevKit (Ctrl+] to exit)
idf.py -B build_esp32 "-DIDF_TARGET=esp32" "-DSDKCONFIG=build_esp32/sdkconfig" -p COM10 monitor

# Merge C3 image, then flash it (J2 fitted, KEY1 held)
cd build_esp32c3
python -m esptool --chip esp32c3 merge-bin -o wifi_coprocessor_esp32c3_merged.bin "@flash_args"
python -m esptool --chip esp32c3 -p COMx write-flash 0x0 wifi_coprocessor_esp32c3_merged.bin

# Chip info / erase
python -m esptool -p COM10 chip-id
python -m esptool -p COM10 flash-id
python -m esptool --chip esp32 -p COM10 erase-flash

# Config editor for one target
idf.py -B build_esp32c3 "-DIDF_TARGET=esp32c3" "-DSDKCONFIG=build_esp32c3/sdkconfig" menuconfig
```

Reader host commands:

```
GET_WIFI
SET_WIFI,1,STA,<ssid>,<password>
SET_WIFI,1,AP,<ap-ssid>,<ap-password>
SET_WIFI,0,STA,,
SET_UPLOAD_IF,AUTO
```

## 11. How to change things

### Change the I2C pins

ESP side, in `main/i2c_link.c`:

```c
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define I2C_LINK_SCL_PIN GPIO_NUM_6
#define I2C_LINK_SDA_PIN GPIO_NUM_10
...
#elif defined(CONFIG_IDF_TARGET_ESP32)
#define I2C_LINK_SCL_PIN GPIO_NUM_22
#define I2C_LINK_SDA_PIN GPIO_NUM_21
```

STM side: `BOARD_ESP_SCL_*`, `BOARD_ESP_SDA_*`, `BOARD_ESP_EN_*`, `BOARD_ESP_I2C` and the IRQ names in `STM32_Firmware/Firmware/BSP/<board>/board_config.h`.

Pins to avoid:

| Chip | Avoid |
|---|---|
| ESP32-C3 | IO2, IO8, IO9 (strapping pins; IO9 is the download-mode pin used by J2), IO18/IO19 (USB), IO20/IO21 (UART0 console) |
| ESP32 | GPIO0, 2, 5, 12, 15 (strapping), GPIO6 to 11 (flash), GPIO1/3 (console) |

Rebuild only the target whose pins changed.

### Add a link command

1. Add the command ID (and any payload offsets) to `STM32_Firmware/Firmware/Devices/esp32/esp32_link_protocol.h`. Both projects include this one file.
2. ESP: add a `case` to the `switch` in `I2cLink_HandleRequest()` in `main/i2c_link.c`. Put the result code in `reply[0]` and keep the reply payload at 53 bytes or less (`ESP32_LINK_RESPONSE_MAX_PAYLOAD`). Requests are limited to 220 bytes.
3. The handler runs in the link task and must return quickly. The master reads the reply about 2 ms later and waits at most 1.5 s. Start long work and report progress through `GET_STATUS` or a follow-up command, as scanning does.
4. Remember that a retried request with the same sequence number is answered from the replay cache. Your handler runs once per sequence number.
5. STM: submit it from `Services/wifi/wifi_service.c` with `WiFi_Submit(...)` and handle the result where the other commands are handled.
6. If you change an existing frame layout, bump `ESP32_LINK_PROTOCOL_VERSION`. The ESP drops frames with a different version, so both firmwares must then be flashed together.
7. Rebuild the ESP for **both** targets and rebuild the STM firmware.

### Change sdkconfig per target

Each build folder has its own `sdkconfig`. Edit it with menuconfig:

```powershell
idf.py -B build_esp32 "-DIDF_TARGET=esp32" "-DSDKCONFIG=build_esp32/sdkconfig" menuconfig
idf.py -B build_esp32c3 "-DIDF_TARGET=esp32c3" "-DSDKCONFIG=build_esp32c3/sdkconfig" menuconfig
```

The build folders are git-ignored, so menuconfig changes are local only. To keep a change in git:

| Scope | Put it in |
|---|---|
| Both targets | `ESP32_Firmware/sdkconfig.defaults` |
| One target only | `ESP32_Firmware/sdkconfig.defaults.esp32` or `sdkconfig.defaults.esp32c3` (standard ESP-IDF per-target defaults; these files do not exist yet) |

Defaults only apply when an sdkconfig is generated. After editing a defaults file, delete that target's `build_<target>/sdkconfig` (or run `fullclean` and delete it) and rebuild.

Settings already chosen: log level INFO, optimise for size, FreeRTOS tick 1000 Hz, WiFi static RX buffers 8, dynamic RX 16, dynamic TX 16, brownout detection on, console UART0 115200.

## 12. Known issues and open work

| Item | Status |
|---|---|
| Legacy I2C slave driver (`driver/i2c.h`) | Deprecated in ESP-IDF v6.1 but still works. Moving to `driver/i2c_slave.h` is open work; the new driver can flush its TX FIFO (`i2c_slave_reset_tx_fifo`), which would simplify reply resynchronisation |
| Scan commands | Implemented on the ESP, not used by the STM |
| F411 board status in the design doc | `docs/design/0016-wifi-coprocessor.md` says `BOARD_ESP_LINK_ENABLED` is `0U` on the F411 because of a pin conflict. The current `BSP/f411_custom/board_config.h` sets it to `1U` on I2C1 PB6/PB7 with EN on PB9, matching `docs/notes/f411-custom-board-port.md`. The design doc paragraph is out of date |
| Bench result | F429 Discovery + DevKit: reader verified on WiFi at 192.168.1.17:5000, 1000/1000 commands, 0 I2C errors, no ESP resets (median round trip 31 ms, p95 80 ms) |
| Custom board WiFi | Bring-up on the C3 hardware is listed in the F411 port notes; results are not recorded in the repo **(unverified)** |
