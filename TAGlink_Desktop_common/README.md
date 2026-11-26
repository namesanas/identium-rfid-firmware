# TAGlink

Desktop application for Identium RFID readers. Every launch opens a device
chooser: **IF9D** opens the existing wall-reader application; **MUBR** opens a
separate handheld workspace. The chooser and both workspaces share the existing
Brown/Blue/Grey and light/dark themes. Use **Appearance** or **Change device**
in the top-right corner of either workspace.

IF9D drives the reader through the firmware's text host commands over USB,
RS232 / RS485, Ethernet or WiFi (TCP 5000), and runs VSWR scans through the module
bridge (TCP 8080 or the same serial link). Its existing features are retained.

## IF9D workspaces

| Workspace | Purpose |
|---|---|
| Inventory | Start / stop continuous inventory, single and sync polls, live tag table and analytics |
| Tags | Read, write, EPC write, lock and kill |
| Settings | RF settings, work mode (passive / active), GPIO, network and HTTP upload, WiFi (join a network or run the reader's own access point, upload interface) |
| Diagnostics | VSWR (return loss) scan, module capabilities, reader health (reset cause, last fault, loop timing, stack use) |
| Log | Activity, connection details and reader commands. Clear log clears the on-screen history. |
| Tools | Read-rate tuning, stock checks, batch encoding, and reader tag memory |

Under **Tools → Tag memory**, select **Refresh memory**. TAGlink shows up to
the latest **64,000** saved records and numbers them from **1**, oldest first.
Use **Find record**, **First**, **Previous**, **Next** or **Last**. Table and CSV
use the same record numbers for the current saved range. When newer records
replace older ones, refreshing the view starts the numbering at the oldest
record still shown. **Clear memory** erases the full reader log after
confirmation with scanning stopped.

**Wired IP.** Choose **Ethernet** in the main window and enter the reader's IP
address beside it, or use **Find readers** under **Settings → Wired network**.
To change the address stored on the reader, connect by Ethernet or USB/serial,
stop scanning, and choose
**Change reader IP**. TAGlink reads the current IP, subnet mask and router
address before editing. It verifies the saved settings and updates its
connection address. For a new IP on another subnet, use USB or adjust the PC
network to reach the reader again.

**Control mode.** In **App control**, the reader waits for commands. In
**Automatic**, it can scan and send tags on its own. **Switch to app control**
stops automatic scanning and returns control to TAGlink. **Save upload
settings** stores settings for Automatic mode.

**Wi-Fi.** Choose **Wi-Fi** in the main window and enter the reader's Wi-Fi IP
address beside it, or choose **Find readers**. A reader already in
use can appear as **In use**. Use USB, serial or Ethernet for antenna checks.

## Running from source

For the ESP32 handheld prototype, use USB serial **COM10 at 115200 baud**.
The serial transport keeps boot/reset lines inactive before opening its CH9102
bridge (VID 1A86 / PID 55D4). Other serial devices retain their existing defaults.
This fixes the case where the COM port opens but the ESP32 remains silent.
Firmware 0.7.1 is the current MUBR development build. Select MUBR for that reader;
the startup identity check rejects a mismatched reader before sending settings
or inventory commands. IF9D retains its own connection history; MUBR stores its
transport, host and USB port under separate settings keys. Neither profile
connects automatically on launch.

Requires Python 3.12 with PySide6 (including QtBluetooth) and pyserial.

```
pip install -r requirements.txt
python main.py
```

## MUBR workspace and Bluetooth LE

- Inventory: continuous START/STOP, bounded single poll, EPC/RSSI/read counts,
  filtering and CSV export. Per-tag frequency 0 displays as **Unavailable**.
  The UI batches updates and bounds its table to 5,000 unique EPCs.
- Settings: supported RF power/session/target/Q/region, HOLD/TOGGLE/TIMED scan
  behavior and delivery destination, reader name, Wi-Fi, Bluetooth enable and
  outbound IPv4 TCP server settings. The physical GPIO is read-only.
- Diagnostics: firmware identity, battery availability and reader diagnostics.
  There are no MUBR tag-memory, lock/kill, VSWR, GPIO-output, multi-antenna,
  fixed-reader upload/work-mode or legacy firmware-upload tools.
- Capabilities select available settings and RF limits. Current firmware
  advertises 5–26 dBm in 0.5 dB steps, single poll up to 3,000 ms and the
  restricted India region 03 plan. The app does not substitute IF9D region codes.

Connect over **Wi-Fi** (reader IP, port 5000), **USB**, or **Bluetooth**.
For Bluetooth, turn on the PC's Bluetooth adapter and reader, press **Find
readers**, select the reader and press **Connect**. Accept the Windows pairing
prompt. QtBluetooth performs LE service/characteristic discovery, pairs through
the OS, enables notifications, and serializes writes with responses. It uses
MUBR's Nordic UART service `6e400001-b5a3-f393-e0a9-e50e24dcca9e` with RX `…0002…`
and TX `…0003…`; it does not use Bluetooth Classic/SPP.

The November 6 BLE fix is installed at the usual `dist/device-profiles/TAGlink.exe`
and also available separately at `dist/device-profiles-ble-fix/TAGlink.exe`.
The old EXE/manifest are preserved in `dist/device-profiles/previous-20261105/`.
It corrects the PySide6 UUID conversion during service/characteristic discovery,
shows connection steps and records lifecycle diagnostics in `logs/mubr-ble.log`
beside that EXE. Close the old app before trying it. If Windows Add device does
not list the reader, set Settings > Bluetooth & devices > Devices > Bluetooth
devices discovery to Advanced and retry with phone BLE clients disconnected.
The full evidence and pending physical check are in
[`../docs/DESKTOP_BLE_TROUBLESHOOTING.md`](../docs/DESKTOP_BLE_TROUBLESHOOTING.md).

Commands are bounded ASCII/CRLF lines. Notification fragments are assembled
independently of MTU; writes are split at the negotiated MTU payload (20 bytes
when MTU is unavailable). Late callbacks from an old BLE session are ignored.
Only one command is pending; STOP can cancel a pending asynchronous START or
single poll. Graceful disconnect/close sends STOP followed by RELEASE; timeouts
close an ambiguous command stream. Reconnect is explicit in the MUBR workspace.
Saved Wi-Fi passwords are kept by the reader; a blank password field preserves
the existing value. Passwords are omitted from the app's traffic log.

App-level BLE uses the firmware's encrypted GATT contract. Windows pairing and
real tag delivery from this updated desktop app still require a hardware check;
mock/session tests and screenshots are not a BLE bench pass.

## Building the Windows executable

```
pip install pyinstaller
pyinstaller main.spec
```

The single-file executable is written to `dist/TAGlink.exe`.
For this change, preserve the older executable and build a separate package:

```
python tools/build_device_profiles.py
```

To retain the old package while building the BLE repair:
`python tools/build_device_profiles.py --variant device-profiles-ble-fix`.

The new executable is `dist/device-profiles/TAGlink.exe`, with a JSON manifest
containing its SHA256 and source hashes. Build output is under `build/`.

## App checks

```
python -m unittest discover -s tests -v
python tools/check_tag_memory.py
python tools/check_device_profiles.py
python tools/smoke_screenshot.py screenshots/device-profiles/if9d-dark
python tools/smoke_screenshot.py screenshots/device-profiles/if9d-light --light
```

These checks use mock transports and synthetic UI data, without opening the
reader's COM port or scanning Bluetooth. Readable chooser/MUBR screenshots are
under `screenshots/device-profiles/`. The project-level loopback integration
check is `../tests/check_taglink_desktop_profile.py`; it exercises the actual
serial/TCP worker with fragmented/coalesced replies from a simulated TCP peer.

## Files

| File | Role |
|---|---|
| `main.py` | Application logic and the host command queue |
| `device_chooser.py` | Themed launch chooser and navigation between device windows |
| `handheld_window.py`, `handheld_session.py`, `reader_profiles.py` | Separate MUBR UI, verified session and handheld protocol contract |
| `ble_transport.py` | QtBluetooth LE discovery, pairing, GATT subscription and bounded fragmented I/O |
| `ui_builder.py`, `theme.py` | Layout and light / dark themes |
| `serial_manager.py`, `protocol.py` | Serial / TCP transport and reply parsing |
| `vswr_transport.py` | ModuleAPI-compatible VSWR request over the module bridge |
| `tools/smoke_screenshot.py` | Off-screen check: feeds sample replies and captures every workspace |
| `tools/screenshot.py` | Off-screen screenshots of each workspace |
