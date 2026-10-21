# TAGlink host applications: developer guide

This guide covers the two programs that talk to the reader firmware:

| App | Folder | Platform | Language / UI | Transports |
|---|---|---|---|---|
| TAGlink (desktop) | `TAGlink_Desktop/` | Windows | Python 3.12, PySide6 6.11, pyserial 3.5 | USB CDC / RS232 / RS485 (COM port), Ethernet TCP 5000, WiFi TCP 5000, module bridge (TCP 8080 or the COM link) |
| TAGlink Mobile | `TAGlink_Mobile/` | Android (iOS scaffold only) | Dart / Flutter 3.47 | WiFi TCP 5000 only |

Both apps speak the firmware's ASCII host protocol, which is implemented in `STM32_Firmware/Firmware/Application/Host/host_interface.c`. Everything below was checked against the source on 2026-10-12 unless it is marked **unverified**.

---

## 1. The host protocol in five minutes

Both apps depend on these rules. They come from `host_interface.c` and `Services/host/host_transport.c`.

| Rule | Detail |
|---|---|
| Framing | A command ends with CR. LF is ignored, so `CMD\r` and `CMD\r\n` both work. Both apps send `\r\n`. Empty lines are ignored. |
| Replies | One line ending in `\r\n`. Commands without data reply `OK,<CMD>` or `ERROR,<CMD>`. Queries reply with a keyword: `GET_VERSION` gives `VERSION,...`, `GET_TEMPERATURE` gives `TEMP,...`. |
| Unknown command | `ERROR,INVALID_COMMAND` |
| Unsolicited lines | `TAG,EPC=<hex>,RSSI=<dBm>,ANT=<n>,FREQ=<kHz>,TIME=<ms>` can arrive at any time during inventory. `DEBUG,` lines may also appear (the mobile app drops them). |
| No request IDs | Replies are matched by prefix, so both apps keep one command in flight at a time. |
| Command lock | The reader serves one host link at a time (USB, RS232/485, Ethernet or WiFi). The first link that sends a printable byte owns the parser. Bytes from the other links are dropped silently, so a second client sees **timeouts, not errors**. |
| `RELEASE` | Stops any running inventory, replies `OK,RELEASE` on the owning link, flushes the RX queue and frees the lock. |
| Automatic release | A dropped Ethernet TCP client (`host_tcp_server.c`) or WiFi client (`wifi_service.c`) frees the lock by itself. **USB and serial links never free it automatically**, so a USB or serial session must send `RELEASE`. |
| One TCP client | Port 5000 accepts a single client. A second client can be refused or reset. |
| Vendor tokens | A line that starts with `FF`, `EE 00`, `IOGET`, `IOSET`, `IPGET`, `IPSET`, `GIO`, `SIO` or `VER` on USB/serial opens a binary module-bridge or config session (used by Reader Manager and by the desktop VSWR scan). Do not start your own text commands with these tokens. |
| Boot time | The firmware needs about 2.7 s after reset before it answers. |

### Commands the firmware implements

| Command | Reply | Used by |
|---|---|---|
| `GET_STATUS` | `STATUS,INVENTORY` or `STATUS,IDLE` | Desktop, Mobile |
| `START` / `STOP` | `OK,START` / `OK,STOP` (`ERROR,...` on failure) | Desktop, Mobile |
| `SINGLE_POLL[,ms]` (1..65535, default 1000) | `TAG,...` lines, then `OK,SINGLE_POLL` | Desktop (3000 ms), Mobile (1000 ms) |
| `SYNC_POLL[,ms]` | `TAG,...` lines, then `OK,SYNC_POLL` | Desktop |
| `GET_VERSION` | `VERSION,BTVER=..,HWVER=..,FWDATE=..,FWVER=..,PROTO=..` | Desktop, Mobile, both network scans |
| `GET_SERIAL` | `SERIAL,YEAR=..,SN=..` | Desktop, Mobile |
| `GET_TEMPERATURE` | `TEMP,<C>` | Desktop, Mobile |
| `GET_REGION` / `SET_REGION,<hex byte>` | `REGION,<hex>` / `OK,SET_REGION` | Desktop (Mobile only reads it) |
| `GET_ANTENNA` / `SET_ANTENNA,<tx>,<rx>` | `ANTENNA,TX=..,RX=..` / `OK,SET_ANTENNA` | Desktop (Mobile only reads it) |
| `GET_POWER` / `SET_POWER,<centi-dBm>` | `POWER,ANT=..,READ=..,WRITE=..` / `OK,SET_POWER` | Desktop, Mobile |
| `GET_SESSION` / `SET_SESSION,<0..3>` | `SESSION,<n>` / `OK,SET_SESSION` | Desktop, Mobile |
| `GET_PROTOCOL`, `GET_FREQUENCY`, `GET_REGIONS` | `PROTOCOL,...`, `FREQ,<kHz>,<kHz>,...`, `REGIONS,...` | Desktop |
| `READ_TAG,<epc>,<bank>,<word>,<count>` | `READ_DATA,BANK=..,ADDR=..,WORDS=..,DATA=<hex>`, then `OK,READ_TAG` | Desktop, Mobile |
| `WRITE_TAG,<epc>,<bank>,<word>,<accesspw>,<hex>` | `OK,WRITE_TAG` | Desktop, Mobile (User bank only) |
| `WRITE_EPC,<old>,<new>` | `OK,WRITE_EPC` | Desktop, Mobile |
| `LOCK_TAG,<epc>,<accesspw>,<mask>,<action>` | `OK,LOCK_TAG` | Desktop |
| `KILL_TAG,<epc>,<killpw>` | `OK,KILL_TAG` | Desktop |
| `GET_CAPABILITIES` | `CAPABILITIES,GPI=..,GPO=..,ANTENNAS=1,HTTP=1,VSWR=1,SET_GPO=1,SET_HTTP=1` | Desktop, Mobile |
| `GET_GPI` | `GPI,GPI1=..,GPI2=..,GPI3=..,GPI4=..` | Desktop, Mobile |
| `SET_GPO,<n>,<0/1>,<seconds>` | `OK,SET_GPO` | Desktop, Mobile |
| `GET_NETWORK` | `NETWORK,IP=..,MASK=..,GATEWAY=..,DNS=..,PORT=..,MAC=..` | Desktop |
| `GET_WORK_MODE` / `SET_WORK_MODE,PASSIVE\|ACTIVE` | `WORK_MODE,...` / `OK,SET_WORK_MODE` | Desktop |
| `SET_HTTP,<url>,<timeout s>,<startAtBoot>` | `OK,SET_HTTP` | Desktop |
| `GET_WIFI` | `WIFI,LINK=..,STATE=..,IP=..,RSSI=..,HOST=..,UPLOAD=..,ENABLE=..,MODE=..,SSID=..,IF=..,ERRORS=..,REJECTED=..` | Desktop, Mobile |
| `SET_WIFI,<0/1>,<STA\|AP>,<ssid>,<password>` (`****` keeps the stored password) | `OK,SET_WIFI` | Desktop, Mobile |
| `SET_UPLOAD_IF,AUTO\|ETH\|WIFI` | `OK,SET_UPLOAD_IF` | Desktop |
| `GET_DIAG` / `CLEAR_DIAG` | `DIAG,RESET=..,UPTIME_MS=..,...` / `OK,CLEAR_DIAG` | Desktop, Mobile (GET only) |
| `REBOOT` | `OK,REBOOT`, then the reader resets 500 ms later | Desktop, Mobile |
| `RELEASE` | `OK,RELEASE` | Desktop, Mobile, both network scans |
| `SET_BUZZER,<ms>,<tone>` | `OK,SET_BUZZER`; `ERROR,SET_BUZZER` on boards without a buzzer (`BUZZER=0`) | Desktop UI only |
| `SET_GEN2,<A\|B\|A/B>,<q>,<profile>,<fast>,<hop>` | `OK,SET_GEN2` / `ERROR,SET_GEN2` (profile = RF mode value or 0, fast 0/1, hop must be 0) | Desktop (RF & Gen2 card, Read-rate tuner), sent when `GET_CAPABILITIES` has `SET_GEN2=1` |
| `GET_GEN2` | `GEN2,TARGET=<A\|B\|AB>,Q=<DYNAMIC\|n>,PROFILE=<n>` | Desktop (Read-rate tuner) |
| `SET_ANTENNAS,<ports>` | `OK,SET_ANTENNAS` for a single port / `ERROR,SET_ANTENNAS` | Not sent by either app |

Reader behaviour seen from the mobile app (recorded during hardware tests, not re-checked today):

- `TAG` lines produced by `SINGLE_POLL` carry `RSSI=0`, `ANT=0` and `FREQ=0`. The mobile app shows `— dBm` when RSSI is 0.
- `GET_SERIAL` returns `YEAR=33554947` (0x02000103). That is a raw module value, not a calendar year.
- During `START` inventory the reader reports each tag once per round of about 1 s. Reads/s is therefore roughly the number of tags in the field (about 7/s with 8 tags).

---

## 2. TAGlink Desktop

### 2.1 Purpose

TAGlink is the engineering and production tool for the reader. You can run inventory, read, write, lock and kill tags, change RF, network, WiFi, HTTP and GPIO settings, switch work mode, run VSWR scans through the module bridge, read health data (`GET_DIAG`), and watch every byte on the wire.

### 2.2 Window layout

The **top bar** (`ui_builder.build_ui`) is always visible:

| Control | What it does |
|---|---|
| Port combo | Lists the COM ports from `serial.tools.list_ports` (refreshed every 2.5 s), then `Ethernet` and `WiFi`. The tooltip shows the port description or the target IP. |
| Baud combo | 115200 / 57600 / 38400 / 19200 / 9600. Only matters for a real UART (RS232/RS485 or the ST-LINK VCP). For the native USB CDC port it is probably ignored (**unverified**). |
| Connect / Disconnect | Opens the link or runs the `RELEASE` handoff (see 2.3). |
| `● Connected` label | Connection state. The tooltip shows `COM8 @ 115200` or `Ethernet <ip>:5000`. |
| ▶ Start | `START` (continuous inventory). Clears the table first when "Clear inventory on Start" is ticked. Clears the queue first. |
| ■ Stop | Clears the queue and the pending command, then sends `STOP`. |
| Single | `SINGLE_POLL,3000` |
| Sync | `SYNC_POLL` |
| Metric chips | Inventory (LIVE/IDLE), Reads, Unique, Rate, Temp, Mode (Passive/Active) |
| Return to passive | Visible only in active mode. Replaces the queue with `STOP`, `SET_WORK_MODE,PASSIVE`, `GET_STATUS` and the info batch. |
| Theme / sun-moon | Colour theme (Blue, Brown, Grey; Brown is the default) and dark/light mode. Both are saved in QSettings. |

The **navigation rail** has six workspaces. The Log button shows a red dot when an `ERROR` line or a transport error arrives while you are on another page.

#### Inventory

| Control | Command / behaviour |
|---|---|
| Filter box + Exact | Client-side EPC filter, 300 ms debounce. Substring match, or full match with Exact. |
| Clear | Clears the table, counters and analytics (local only) |
| Export CSV | Saves `Time, EPC, RSSI (dBm), Antenna, Frequency (kHz), Reads` |
| Refresh | Queues the info batch (`INFO` list in `main.py`). Ignored while inventory runs or a command is pending. |
| Tag table | One row per EPC (`epc_row_map`). Updated from each `TAG` line by `handle_tag()`. |
| Analytics | Total, unique, rate, elapsed time, reads per antenna and reads per frequency |

#### Tags

The "Selected tag" card has a **Use selected row** button. It copies the selected EPC into every target field. The four tabs:

| Tab | Fields | Sends | Validation |
|---|---|---|---|
| Read | bank, start word, word count | `READ_TAG,<epc>,<bank>,<addr>,<words>` | EPC must be hex, even length, at most 124 digits. The `READ_DATA` reply fills "Last data". |
| Write: Tag data | target EPC, bank, address, data, access password | `WRITE_TAG,<epc>,<bank>,<addr>,<pw8>,<hex>` | Password is 8 hex digits. Data is even-length hex, at most 64 bytes. Asks for confirmation. |
| Write: EPC edit | new EPC | `WRITE_EPC,<old>,<new>` | 2..62 bytes of hex. After `OK,WRITE_EPC` the new EPC goes into all fields and the inventory is cleared. |
| Lock | EPC, access password, mask (4 hex), action (4 hex) | `LOCK_TAG,...` | Asks for confirmation |
| Kill | EPC, kill password | `KILL_TAG,<epc>,<pw8>` | Shows an "IRREVERSIBLE" warning dialog |

All tag buttons are disabled while inventory runs.

#### Settings (scrolling grid of cards)

| Card | Controls and commands |
|---|---|
| Reader | Work mode Passive/Active sends `SET_WORK_MODE,<mode>`. The UI changes only after `OK`, then sends `GET_STATUS`. Region (hex) with Set region sends `SET_REGION,XX`. TX/RX with Set antenna sends `SET_ANTENNA,tx,rx`. Power (0–33 dBm, 0.25 steps) with Set power sends `SET_POWER,<dBm×100>`. Session with Set session sends `SET_SESSION,n`. |
| RF & Gen2 | Discover capabilities queues `GET_VERSION, GET_CAPABILITIES, GET_ANTENNA, GET_GPI`. Apply RF settings checks the antenna list, then queues `SET_ANTENNA`, `SET_POWER`, `SET_REGION`, `SET_SESSION`. It sends `SET_GEN2,<target>,<q>,<profile>,<fast>,0` only when the reader reports `SET_GEN2=1`. RF profile lists Reader Manager's RF modes (the combo item data is the gen2tagEncoding value; Module default = 0). Ticking Fast mode sets Session 0 and Target A/B. The Frequency hopping checkbox is hidden (not implemented in the firmware). |
| Network & HTTP | Reader IP, mask, gateway, DNS and config port are filled by `GET_NETWORK` or by Discover readers. They are display and target fields only: no `SET_NETWORK` is sent. Apply HTTP checks for an `http://` URL with no commas, then sends `SET_HTTP,<url>,<timeout>,<startAtBoot>`. Discover readers runs UDP discovery (port 15000, ReaderManager format) **and** a TCP scan of every local /24 on port 5000. Clicking a row sets the IP and selects Ethernet (UDP hit) or WiFi (TCP-only hit). |
| WiFi | Status line from `GET_WIFI`. Reader WiFi IP is the address used when the port combo is set to WiFi. WiFi enabled, mode (STA/AP), SSID, password and open network feed Apply WiFi, which sends `SET_WIFI,...`. An empty password becomes `****` (keep stored); "Open network" sends an empty password. It warns you when you are connected over WiFi and re-reads `GET_WIFI` after 6 s. Upload over with Apply upload interface sends `SET_UPLOAD_IF,AUTO\|ETH\|WIFI`. Refresh sends `GET_WIFI`. A status refresh never overwrites a form you are editing (`wifi_form_dirty`). |
| Peripherals | GPI 1–4 chips come from `GET_GPI`. GPO 1–4 each have state, duration and Set, which sends `SET_GPO,n,state,seconds`. Sound buzzer sends `SET_BUZZER,...` and the current firmware always answers `ERROR,SET_BUZZER`. Refresh GPI sends `GET_GPI`. |
| Interface | One row per EPC (always on), Auto-scroll, Clear inventory on Start, Auto reconnect |

#### Diagnostics

| Card | Behaviour |
|---|---|
| VSWR sweep | Antenna, start, stop and step in kHz. Start scan builds one ModuleAPI `ReturnLossTest` frame (`vswr_transport.build_vswr_frame`, at most 78 points). Over **Ethernet** it opens TCP to `<Reader IP>:<Config port>` (8080 by default). Over **USB/serial** it switches `SerialManager` to binary mode and sends the frame on the same COM link (the firmware routes a line that starts with `FF` to the module bridge). It is **not available over WiFi**. The sweep must stay inside the region's `GET_FREQUENCY` table, otherwise the module answers 0x0105. The results go to a chart and table with PASS when VSWR ≤ 1.5. Export CSV saves them. |
| Reader information | Ten buttons send `GET_VERSION`, `GET_SERIAL`, `GET_TEMPERATURE`, `GET_REGION`, `GET_ANTENNA`, `GET_POWER`, `GET_PROTOCOL`, `GET_SESSION`, `GET_FREQUENCY` and `GET_REGIONS`. Module and Firmware fields come from `VERSION` (HWVER/FWVER). |
| Reader health | Refresh sends `GET_DIAG`. Clear loop timing sends `CLEAR_DIAG`, then `GET_DIAG`. Restart reader (after a confirmation) sends `REBOOT`; on `OK,REBOOT` TAGlink closes the link without `RELEASE` and retries the same port every 2.5 s for up to 45 s, whatever the auto-reconnect setting, then refreshes `GET_DIAG` (`RESET=SOFTWARE`). The table labels the known keys (RESET, UPTIME_MS, FAULTS, LAST_FAULT, PC/LR/CFSR/HFSR/MMFAR/BFAR, LOOP_MAX_US, per-task `*_US`, STACK_USED/SIZE, BOR). |

#### Log

| Tab | Content |
|---|---|
| Event Log | `> CMD` sent, `< line` received, state changes and errors. Capped at 1000 lines. |
| Raw Serial | Every received chunk as it arrived, including binary bridge bytes |
| Command Console | Type any command and press Enter or Send. It goes through the same queue. Shows every received line. |
| About | Short description |

#### Tools (`tools_page.py`)

`ToolsController` drives the reader through the same queue: it sends one command, waits for its `OK`/`ERROR` (8 s watchdog), then sends the next. `Main.handle_tag` passes every tag to `tools.on_tag`, and `Main.on_line` passes every other reply to `tools.on_reply` (deferred with `QTimer.singleShot(0)` so Main has updated the inventory state first).

| Tab | What it does |
|---|---|
| Read-rate tuner | `GET_GEN2` to save the current target/Q/RF mode, then for each preset (Standard S0 + A; Fast S0 + A/B; Fast + RF mode 13; Fast + RF mode 11, Q=2): `SET_SESSION`, `SET_GEN2`, `START`, count reads/unique tags/RSSI for the test time, `STOP`. Best = most unique tags, then most reads/s. The old settings are restored at the end; Apply best mode sends the best preset again (saved by the firmware) and updates the Settings controls. Needs `SET_GEN2=1`. |
| Stock check | Import a CSV/TXT list (an `EPC` column, else the first column; invalid lines skipped) or use the current inventory. Start check starts inventory (`START`) and marks each EPC Found / Missing / Unexpected live. Stopping inventory anywhere ends the check. Export writes Missing, Unexpected, Found. |
| Batch encode | EPC = hex prefix + number (decimal digits or hex), zero-padded to the EPC length (multiple of 4 hex digits). Each tag: 1.5 s scan (`START`/`STOP`), refuse if no new tag or more than one, `WRITE_EPC,<old>,<new>`, optional read-back scan, optional `LOCK_TAG` with the password/mask/action from Tags › Lock. The number advances only after success. Encode continuously keeps scanning and ignores tags already encoded in this run. |

### 2.3 Connections, connect flow and timeouts

| Link | Port combo entry | How it opens (`serial_manager.py`) |
|---|---|---|
| USB CDC (native USB, COM8 on the F429 Discovery here) | `COMx` | `Serial(port, baud, 8N1, timeout=0.05, write_timeout=1)` |
| RS232 / RS485 host port (ST-LINK VCP, COM3 on the Discovery) | `COMx` | same as above |
| Ethernet | `Ethernet` | `socket.create_connection((host, 5000), timeout=5)` with TCP_NODELAY. Host = Settings > Network & HTTP > Reader IP, falling back to `ETHERNET_HOST = 192.168.150.50`. |
| WiFi (ESP32 coprocessor) | `WiFi` | Same TCP code. Host = Settings > WiFi > Reader WiFi IP (it refuses to connect if that field is empty). |

All I/O runs on one worker thread in `SerialManager` (a command queue plus `_rx()` polling). Results come back to the GUI thread through Qt signals: `line_signal`, `raw_signal`, `state_signal`, `error_signal` and `tx_signal`.

**Connect flow**

1. `toggle()` calls `io.connect(port, baud, tcp_host())`. On TCP the worker drains stale bytes (at most 50 reads).
2. `on_state(True)` enables the controls. After 500 ms `request_info_staged()` queues `GET_STATUS` followed by the `INFO` batch: `GET_VERSION, GET_SERIAL, GET_CAPABILITIES, GET_TEMPERATURE, GET_REGION, GET_ANTENNA, GET_POWER, GET_PROTOCOL, GET_SESSION, GET_FREQUENCY, GET_GPI, GET_NETWORK, GET_WORK_MODE, GET_WIFI`.
3. If `STATUS,INVENTORY` comes back (reader in active mode, or inventory left running), the settings queries are removed from the queue and run later, when inventory stops (`info_deferred`).
4. Every 15 s `status_poll()` sends `GET_STATUS` when the queue is idle.

**Disconnect flow (host lock handoff)**

1. Disconnect clears the queue and the pending command, sends `RELEASE` and sets `release_pending`.
2. While `release_pending` is set, every line except `OK,RELEASE` is only logged.
3. `OK,RELEASE` closes the link. If it does not arrive within 2 s, `force_disconnect()` closes the link anyway.
4. If the link drops by itself and "Auto reconnect" is ticked, it reconnects after 1.2 s. A manual disconnect never reconnects.

**Command queue (`main.py`)**

- `enqueue(cmd)` appends to `self.queue`. `request(cmd)` does the same but refuses while inventory runs.
- `pump()` sends one command after a 100 ms gap and stores it in `self.pending`.
- `response_matches_pending()` decides whether a line completes the pending command:
  - `ERROR,<CMD>` or `ERROR,INVALID_COMMAND` completes it;
  - `GET_*` commands complete on their reply keyword (a fixed map, e.g. `GET_FREQUENCY` completes on `FREQ`);
  - `VSWR_SCAN` completes on `OK` or `VSWR_DONE`;
  - every other command completes on `OK,<CMD>`;
  - `TAG` and `READ_DATA` lines never complete a command.
- `check_timeout()` runs every 100 ms:

| Command | Timeout |
|---|---|
| default | 5.0 s |
| `SINGLE_POLL,<ms>` | max(5, ms/1000 + 1) s, i.e. 5 s for the 3000 ms the GUI sends |
| `SYNC_POLL[,ms]` | 3 × (5 + ms/1000) + 0.5 s, i.e. 18.5 s by default (the firmware can retry three times) |
| `RELEASE` on disconnect | 2 s, then forced close |
| VSWR over serial | 35 s deadline |
| VSWR over TCP 8080 | 5 s connect, then 70 idle × 0.5 s receive |
| TCP scan probe | 0.8 s connect, 1.5 s for `VERSION,` |

**Discovery details.** `SerialManager.discover()` broadcasts `EE 06 00 00 50 01 00…` to UDP 15000 and parses the 47-byte or longer replies (IP, MAC, port, board, firmware, mode). Only Ethernet readers answer it. `scan_tcp()` probes every host on each local /24 (64 threads). It sends `GET_VERSION`; a reader that answers is listed as `TCP` and then gets `RELEASE`. A reader whose port is open but silent is listed as **Busy** (another link holds the lock).

### 2.4 File map

| File | Lines | Responsibility | Change policy |
|---|---|---|---|
| `tools_page.py` | ~660 | Tools workspace (read-rate tuner, stock check, batch encode) and its command sequencer | Normal work |
| `main.py` | ~1350 | `Main(QMainWindow)`: state, command queue, reply dispatch (`on_line`), every button handler, QSettings save/restore, app start-up (last line, no `__main__` guard) | Normal work happens here |
| `ui_builder.py` | ~640 | `build_ui(window)` creates every widget **as an attribute on the window** (`window.tx`, `window.wifi_ssid`, …) and wires the handlers. Also `refresh_theme` and `set_vswr_x_range`. | Layout only |
| `theme.py` | ~265 | `COLORS` palettes (blue/brown/grey × dark/light), the style-sheet template, and `stylesheet(dark, name)`. Qt style sheets cannot draw CSS-border triangles, so `_draw_images()` paints the arrow and tick PNGs at run time into `%TEMP%\taglink_theme\`. Call it only after `QApplication` exists. | Style only |
| `serial_manager.py` | ~380 | Worker thread for COM and TCP I/O, line splitting, binary mode, UDP discovery, TCP scan | Verified on hardware: change carefully and retest on USB, serial and TCP |
| `protocol.py` | 39 | `parse_line()` returns `('TAG', Tag)`, `('OK'/'ERROR', name)`, `(KEYWORD, {'KEY': v, '_csv': [...]})`, or `('UNKNOWN', line)` | Verified on hardware: change carefully |
| `vswr_transport.py` | ~265 | ModuleAPI frame builder, CRC, reply parser, `VSWRBridge` thread | Verified on hardware: change carefully |
| `main.spec` | 38 | PyInstaller one-file build, windowed, `icon.ico` bundled, UPX on | |
| `icon.ico` | | Paperclip icon (also used for the mobile launcher icon) | |
| `tools/smoke_screenshot.py` | ~120 | Off-screen run that feeds sample reply lines, asserts state, and saves PNGs of each workspace | |
| `tools/screenshot.py` | ~50 | Off-screen PNGs of each workspace with no data | |

The settings live in `QSettings("SilionRFID", "E310Reader")`. On Windows that is the registry key `HKEY_CURRENT_USER\Software\SilionRFID\E310Reader` (Qt's default native format). It stores the theme, last port, baud, region/tx/rx/power/session, read defaults, checkbox states, filter, HTTP fields, inventory antennas, `net_ip` and `wifi_host`.

### 2.5 Run from source

```powershell
cd C:\Users\Identium\Downloads\RFID_Controller_F429_IDENTIUM_PROTOCOL_STEP8\TAGlink_Desktop
python --version                 # 3.12.10 on this PC
pip install PySide6 pyserial     # PySide6 6.11.2, pyserial 3.5 installed here
python main.py
```

PySide6 includes QtCharts, which the VSWR chart uses. No other packages are needed.

### 2.6 Build the exe

```powershell
cd C:\Users\Identium\Downloads\RFID_Controller_F429_IDENTIUM_PROTOCOL_STEP8\TAGlink_Desktop
pip install pyinstaller          # 6.22.3 installed here
Get-Process TAGlink -ErrorAction SilentlyContinue | Stop-Process   # a running exe locks dist\TAGlink.exe
pyinstaller --noconfirm main.spec
dir dist\TAGlink.exe             # about 53 MB
```

- Output: `dist/TAGlink.exe` (one file, no console). `build/` holds intermediate files.
- `build/`, `dist/` and `screenshots/` are gitignored. **Never commit the exe.** It ships only as a GitHub release asset.
- `dist/main.exe` (dated 2026-10-09) is a leftover from an older spec name and can be deleted.
- `upx=True` in the spec only has an effect if UPX is on PATH. Whether UPX is installed here is **unverified**.
- The exe finds `icon.ico` through `resource_path()` (`sys._MEIPASS`). Any new data file must be added to `datas=[...]` in `main.spec` and loaded with `resource_path()`.

### 2.7 Add a feature (desktop pattern)

**A new query command `GET_FOO` that replies `FOO,A=1,B=2`:**

1. `protocol.py`: add `'FOO'` to the keyword set in `parse_line`. Otherwise the line parses as `UNKNOWN`.
2. `main.py`, `response_matches_pending()`: add `'GET_FOO':'FOO'` to the `expected_kind` map. Without it the command only completes on `OK,GET_FOO`, which never comes, so the queue stalls for the 5 s timeout.
3. `main.py`, `on_line()`: add a branch:
   ```python
   elif kind=='FOO':
       self.apply_foo(data)          # data = {'A':'1','B':'2','_csv':[]}
   ```
4. `ui_builder.py`: add the widgets and a button on the right card:
   ```python
   window.foo_value = QLineEdit(); window.foo_value.setReadOnly(True)
   form.addRow("Foo", _field(window.foo_value))
   form.addRow("", _button("Read foo", lambda: window.request("GET_FOO")))
   ```
   Use `request()` for queries that must not run during inventory and `enqueue()` for anything else.
5. Add any new input widget to the tuple in `set_controls()` so it is disabled while disconnected.
6. To show it after connect, append it to `INFO` at the top of `main.py`.
7. `tools/smoke_screenshot.py`: add a sample line such as `'FOO,A=1,B=2'` to the feed list and an `assert` on the widget.

**A new action `SET_FOO,<n>`** replies `OK,SET_FOO`. That matches automatically through the generic `OK,<CMD>` rule. Add an `elif data=='SET_FOO':` under `kind=='OK'` only if you need a status message or a follow-up query.

**A new workspace:** in `ui_builder.build_ui`, add a tuple to `nav_items` and a glyph to `RAIL_ICONS`, build a `QWidget` page, and call `window.stack.addWidget(page)` **in the same order**. Then update the code that hard-codes page indexes and counts: `select_feature`/`set_log_badge` use index 4 for Log, and `smoke_screenshot.py` asserts `stack.count() == 5`.

### 2.8 Test the desktop app

```powershell
cd C:\Users\Identium\Downloads\RFID_Controller_F429_IDENTIUM_PROTOCOL_STEP8\TAGlink_Desktop
python tools\smoke_screenshot.py screenshots\after          # dark theme, asserts and PNGs
python tools\smoke_screenshot.py screenshots\after-light --light
python tools\screenshot.py screenshots\empty                  # empty pages only
```

- Both tools set `QT_QPA_PLATFORM=offscreen`. They exec `main.py` without its last line, replace `QSettings` with a throwaway INI file in `%TEMP%`, and stop the port and status timers. **Keep the QSettings override** in any new tool: without it the test theme and fields are saved into your real settings.
- `smoke_screenshot.py` opens no transport. It feeds lines to `on_line()` and checks the table, EPC propagation, VSWR rows, the work-mode round-trip, WiFi parsing, `SET_WIFI` and `SET_UPLOAD_IF` strings, the 16 diag rows and the log badge. It prints `5 populated pages x 3 sizes -> <dir>` on success. An `AssertionError` is a regression.
- PNGs are written at 1920×1080, 1500×920 and 1366×768. Look at them after any layout change.
- Hardware check after transport changes: connect over COM8 (USB CDC), COM3 (UART) and WiFi; run Start/Stop, Single, a tag read, a VSWR scan over USB, then Disconnect. The log must show `> RELEASE` and `< OK,RELEASE`.

---

## 3. TAGlink Mobile

### 3.1 Purpose

TAGlink Mobile is a field app for Android phones. Over WiFi it runs inventory, inspects and edits a tag, sets power, session and GPO, configures the reader's WiFi, shows identity and health, and keeps a protocol log. It does **not** do VSWR, lock/kill, region or antenna changes, work mode, HTTP or network settings. Use the desktop for those.

### 3.2 Screens

The app shows `ConnectScreen` while `client.connected` is false and `ReaderShell` (bottom navigation) while it is true (`app.dart`). The pages are created the first time you open them and are then kept alive in an `IndexedStack`.

#### Connect screen (`features/connect/connect_screen.dart`)

| Control | Behaviour |
|---|---|
| Reader IP + Port | Defaults to `192.168.4.1` : `5000`. Connect calls `ReaderClient.connect()`. On success the address is stored at the top of "Recent" (SharedPreferences key `saved_readers`, at most 5). |
| Hotspot | Connects to `192.168.4.1:5000` (the reader's own AP) |
| Scan | Finds the phone's IPv4 (prefers `wlan`/`wifi` interfaces and private ranges) and probes every `.1–.254` on that /24 with 64 parallel workers. Each probe: TCP connect (0.8 s), `GET_VERSION`, wait 2 s for `VERSION,`, then always `RELEASE` and close. Hits are listed under "Found on this network". |
| Recent / Found list | Tap to connect |
| Sun/moon | Theme toggle (SharedPreferences key `dark_theme`) |
| Status text | Shows connection problems (busy, lock held, timed out, retrying) |

#### Shell (`app.dart`)

The app bar shows the page title and `host:port`. It has a theme toggle and a Disconnect button that asks for confirmation and then runs `STOP` (only if this phone started inventory), `RELEASE` and close. A red banner shows any connection message other than "Connected to…". Its **Retry** button sends `GET_STATUS`.

#### Inventory (`features/inventory/inventory_screen.dart`)

| Control | Command / behaviour |
|---|---|
| Status line | From `readerStatus`: "Reading tags" (INVENTORY), "Ready" (IDLE), "Not synced" (UNKNOWN) |
| Start inventory / Stop | `START` or `STOP`, chosen from `readerStatus` |
| Single | `SINGLE_POLL,1000` (timeout 4 s). Disabled while running. |
| Metric cards | Unique tags, total reads, reads/s (averaged over 3 s, because the reader reports once per ~1 s round) |
| Filter by EPC | Substring filter, local |
| Sort | Most reads, strongest signal, or EPC |
| Export CSV | Shares `taglink_tags.csv` (`EPC,Count,RSSI_dBm,Antenna,First_seen,Last_seen`) through the Android share sheet (`share_plus`) |
| Clear | Clears the local list |
| Tag row | Shows EPC, RSSI badge (green ≥ −50, accent ≥ −65, red below, `—` for 0), antenna, last seen and count. Tap opens Tag detail. |

Tags reach the screen as batches every 200 ms (`ReaderClient.tagBatches`). A TAG line never completes a pending command.

#### Tag detail (`features/tags/tag_detail_screen.dart`)

| Control | Command |
|---|---|
| On open, and "Read memory" | `READ_TAG,<epc>,1,0,8`, then banks 2 and 3, one after another. The `DATA=` field of the `READ_DATA` line is shown. |
| Per-bank refresh | `READ_TAG` for that bank, 8 words from address 0 |
| Change EPC | Dialog, then confirmation, then `WRITE_EPC,<old>,<new>`. The EPC must be hex with a length that is a multiple of 4. |
| Write User memory | Dialog (password, word address, data), then confirmation, then `WRITE_TAG,<epc>,3,<addr>,<pw>,<hex>`. Bank 3 is fixed. Data must be a multiple of 4 hex characters. The bank is read again afterwards. |
| Find this tag | Opens Find tag for this EPC |

#### Find tag (`features/tags/find_tag_screen.dart`)

Opened from Tag detail ("Find this tag") or by long-pressing a row in Inventory. Sends `START` if the reader is not already inventorying and `STOP` on close only if it started it. Only reads of the chosen EPC count: RSSI is smoothed (0.65 old / 0.35 new) and mapped from -80 dBm (empty) to -30 dBm (full gauge); the tag counts as lost 1.5 s after its last read. Labels: Far, Nearby, Close (> 0.55), Very close (> 0.8). A 50 ms ticker plays `SystemSound.click` and haptic feedback, from about 1 per second (weak) to about 11 per second (very close); the click follows the phone's touch-sound setting. Test: `test/find_tag_test.dart` (fake reader on a loopback socket).

#### Settings (`features/settings/settings_screen.dart`)

| Control | Command |
|---|---|
| Refresh (pull or button) | `GET_POWER`, `GET_SESSION`, `GET_REGION`, `GET_ANTENNA`, `GET_GPI`, `GET_CAPABILITIES`, one after another. `GPO=<n>` from the capabilities trims the number of output switches. |
| Read power slider + Apply power | 5.0–30.0 dBm in 0.5 dB steps, sends `SET_POWER,<centi-dBm>`, then refreshes |
| Session S0–S3 | `SET_SESSION,n` |
| Region / Antennas | Read-only |
| Digital inputs | GPI 1–4 HIGH/LOW from `GPI,GPIn=` |
| Digital outputs | Switch sends `SET_GPO,n,0\|1,0` (duration 0 = hold). The switch moves back if the command fails. |

#### Info (`features/info/info_screen.dart`)

Refresh sends `GET_VERSION`, `GET_SERIAL`, `GET_TEMPERATURE` and `GET_DIAG`. The screen shows module temperature and FWVER cards, a Module section (labelled VERSION fields, FWDATE formatted), Identity (SN, YEAR) and Health (the DIAG fields, with UPTIME_MS formatted). Restart reader (after a confirmation) calls `ReaderClient.restartReader()`: `REBOOT`, close without `RELEASE` on `OK,REBOOT`, then reconnect to the same address every 3 s from 4 s until 60 s (the ESP32 is reset too and must rejoin WiFi). `ReaderClient.restarting` is true meanwhile; connecting or disconnecting cancels the wait.

#### Wi-Fi (`features/wifi/wifi_screen.dart`)

| Control | Command |
|---|---|
| Refresh | `GET_WIFI`. Fills the summary card, the details, and the enabled/mode/SSID form. The password is never read back. |
| Wi-Fi enabled, Join network / Hotspot, SSID, Password | Input to Apply |
| Apply | `CommandBuilder.setWifi` checks the input (ASCII only, no commas in the SSID, password empty / `****` / 8–64 characters). After confirmation it sends `SET_WIFI,...`, expects exactly `OK,SET_WIFI`, then **disconnects** (RELEASE) and returns to Connect, because the reader may move. |

#### Logs (`features/logs/logs_screen.dart`)

The last 500 TX/RX lines (`DEBUG,` lines are not logged). All/Sent filter, copy all, long-press to copy one line, clear. The list redraws at most every 200 ms.

### 3.3 Connection, lock handling and timeouts (`transport/reader_client.dart`)

TCP to port 5000 is the only transport. There is no USB or serial support on the phone.

**Connect:** `Socket.connect(host, port, timeout: 3 s)` with `tcpNoDelay`. If the app is already connected it first runs the full disconnect (STOP/RELEASE/close). After connecting, `_probeStatus()` sends `GET_STATUS`.

| Situation | What the client does |
|---|---|
| Connect refused or reset | Message: "Reader is busy with another device". This happens when another TCP client already holds port 5000. |
| Connect timeout | "Connection timed out. Check the reader IP and Wi-Fi." |
| TCP is up but `GET_STATUS` times out | Another link (USB or serial) holds the command lock. Message: "…Send RELEASE from USB/serial, or reset the reader…". `GET_STATUS` is retried every 3 s until a `STATUS` line arrives. |
| Socket lost unexpectedly | Fails every queued command and reconnects after 1 s, 2 s, 5 s and 10 s, then gives up ("Automatic reconnect limit reached"). After a reconnect it sends `GET_STATUS` again. |
| App goes to the background (`paused`/`detached`) | Same as a user disconnect: `STOP` if this app started inventory, then `RELEASE`, then close. **Switching apps drops the session by design.** |

**Queue:** a FIFO of `_PendingCommand`, one command on the wire at a time.

| Command | Timeout |
|---|---|
| default | 3 s |
| `SINGLE_POLL,<ms>` | ms + 3 s |
| Scan probe | 0.8 s connect, 2 s reply |

`_matches(command, line)` decides which reply completes the active command. `ERROR,<CMD>` and `ERROR,INVALID_COMMAND` always match. Otherwise a fixed table maps the command to a reply prefix:

```text
GET_STATUS->STATUS,  GET_VERSION->VERSION,  GET_SERIAL->SERIAL,  GET_TEMPERATURE->TEMP,
START->OK,START  STOP->OK,STOP  SINGLE_POLL->OK,SINGLE_POLL  GET_POWER->POWER,
SET_POWER->OK,SET_POWER  GET_REGION->REGION,  GET_ANTENNA->ANTENNA,  GET_SESSION->SESSION,
SET_SESSION->OK,SET_SESSION  READ_TAG->READ_DATA,  WRITE_TAG->OK,WRITE_TAG
WRITE_EPC->OK,WRITE_EPC  GET_GPI->GPI,  SET_GPO->OK,SET_GPO  GET_CAPABILITIES->CAPABILITIES,
GET_WIFI->WIFI,  SET_WIFI->OK,SET_WIFI  GET_DIAG->DIAG,  RELEASE->OK,RELEASE  REBOOT->OK,REBOOT
```

**A command that is not in this table can only finish with an ERROR or a timeout.** This is the first thing to update when you add a command (see 3.7). `READ_TAG` completes on `READ_DATA`. The `OK,READ_TAG` that follows is ignored, because it matches no other command's prefix.

`STATUS` lines update `readerStatus` whenever they arrive, solicited or not. `OK,START` and `OK,STOP` also update it.

### 3.4 File map

| Path | Responsibility |
|---|---|
| `pubspec.yaml` | Name `taglink_mobile`, **`version: 1.0.0+1`**, SDK `>=3.4.0 <4.0.0`. Dependencies: `shared_preferences`, `share_plus`, `cross_file`. Dev dependency: `flutter_lints`. |
| `lib/main.dart` | `runApp(TaglinkApp())` |
| `lib/app.dart` | `TaglinkApp` (theme, lifecycle disconnect, Connect vs Shell switch) and `ReaderShell` (titles, pages, bottom navigation, banner) |
| `lib/theme.dart` | The desktop's **brown** palettes (dark/light) mapped to Material 3, plus the `TaglinkColors` extension (`success`, `log`, …) |
| `lib/protocol/command_builder.dart` | Validated command strings. The UI should build commands only through this class. |
| `lib/protocol/line_framer.dart` | Splits the TCP stream on `\n`, strips `\r`, caps an unterminated line at 64 KiB |
| `lib/protocol/parser.dart` | `parseLine` gives key/values/fields. Typed helpers: `parseTag`, `parseStatus`, `parseVersion`, `parseSerial`, `parsePower`, `parseWifi`, `parseKeyValueReply`. |
| `lib/protocol/models.dart` | `ReaderTag`, `TagRecord`, `WifiStatus`, `ProtocolLogEntry`, … |
| `lib/transport/reader_client.dart` | Socket, FIFO, reply matching, timeouts, lock retry, reconnect, tag batching, 500-line log |
| `lib/features/*/` | One folder per screen: connect, inventory, tags, settings, info, wifi, logs |
| `lib/widgets/ui_helpers.dart` | `showMessage`, `humanError`, `SectionCard`, `KeyValueList`, `PageToolbar`, label maps (`versionLabels`, `diagLabels`, `wifiLabels`, …) |
| `test/` | Flutter tests (see 3.8) |
| `tools/reader_simulator.py`, `tools/test_reader_simulator.py` | Standard-library TCP reader simulator on port 5000 and its unittest suite |
| `scripts/build_debug_apk.sh` | pub get, test, debug build, copies the APK to `dist/TAGlink-Mobile-debug.apk` |
| `android/app/build.gradle` | `com.taglink.mobile`, Java/Kotlin 17, `ndkVersion "28.2.13676358"`, release signed with the **debug key** |
| `android/settings.gradle` | AGP 8.13.0, Kotlin 2.3.20 |
| `android/gradle/wrapper/gradle-wrapper.properties` | Gradle 8.14.3 |
| `android/app/src/main/AndroidManifest.xml` | Label "TAGlink Mobile", INTERNET, ACCESS_NETWORK_STATE and ACCESS_WIFI_STATE permissions, `usesCleartextTraffic=true` |
| `android/app/src/main/res/mipmap-*` | Launcher icon (the paperclip from `TAGlink_Desktop/icon.ico`; adaptive icon in `mipmap-anydpi-v26`) |
| `ios/` | Scaffold only, never built |
| `TAGlink_mobile_prompt.md` | The original specification of the app (protocol rules, screens, simulator) |
| `README.md` | Written before the first build. Its "Not built in this workspace" status section is out of date: the APK does build on this PC. |

### 3.5 Toolchain on this PC

| Item | Location / version (checked 2026-10-12) |
|---|---|
| Flutter | `C:\Users\Identium\dev\flutter`, 3.47.7 stable, Dart 3.13.5 |
| JDK for Gradle | `C:\Users\Identium\dev\jdk17\jdk-17.0.20.1+1` (Temurin 17.0.20.1). The system Java is 25 and **Gradle cannot run on it**. |
| Android SDK | `%LOCALAPPDATA%\Android\Sdk`: platforms android-34/35/36, build-tools 35.0.0/36.0.0, platform-tools (adb 37.0.1), NDK 27.0.12077973 and **28.2.13676358** (pinned), cmdline-tools, cmake |
| Gradle / AGP / Kotlin | 8.14.3 / 8.13.0 / 2.3.20. Flutter 3.47 needs at least Gradle 8.14, AGP 8.11.1 and Kotlin 2.2.20. |
| `flutter config` | `--android-sdk` and `--jdk-dir` are already set |

Set up a **PowerShell** session before any flutter or adb command:

```powershell
$env:JAVA_HOME    = "C:\Users\Identium\dev\jdk17\jdk-17.0.20.1+1"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
$env:Path = "C:\Users\Identium\dev\flutter\bin;$env:ANDROID_HOME\platform-tools;$env:JAVA_HOME\bin;$env:Path"
flutter --version
flutter doctor -v
```

Or in **Git Bash**:

```bash
export JAVA_HOME="/c/Users/Identium/dev/jdk17/jdk-17.0.20.1+1"
export ANDROID_HOME="$LOCALAPPDATA/Android/Sdk"
export PATH="/c/Users/Identium/dev/flutter/bin:$ANDROID_HOME/platform-tools:$JAVA_HOME/bin:$PATH"
```

Accept the SDK licences from **Git Bash**, because the PowerShell pipe does not feed `yes` into the batch file:

```bash
yes | "$LOCALAPPDATA/Android/Sdk/cmdline-tools/latest/bin/sdkmanager.bat" --licenses
```

(The `latest` folder name under cmdline-tools is **unverified**. Use the folder that exists.)

### 3.6 Run, build and install

```powershell
cd C:\Users\Identium\Downloads\RFID_Controller_F429_IDENTIUM_PROTOCOL_STEP8\TAGlink_Mobile
flutter pub get
flutter analyze
flutter test
flutter build apk --release
dir build\app\outputs\flutter-apk\app-release.apk          # about 52 MB
adb devices
adb install -r build\app\outputs\flutter-apk\app-release.apk
```

| Build | Command | Size | Use |
|---|---|---|---|
| Release | `flutter build apk --release` | ~50 MB | Real use and releases. Signed with the debug key, so it installs directly. |
| Debug | `flutter build apk --debug` | ~152 MB (159 MB on disk today) | Only when you need debug tooling. The UI is noticeably janky. |
| Run with hot reload | `flutter run` (phone connected) | | Development. Press `r` to hot reload, `R` to restart. |

- Output folder: `build/app/outputs/flutter-apk/` (gitignored via `TAGlink_Mobile/.gitignore`).
- The first build on a clean machine took about 16 min (downloads over a slow link). Later builds take 25–60 s.
- After big changes run `flutter clean` first. Debug APK packaging is incremental and keeps dead space.
- **Never add `keepDebugSymbols`** to the Gradle config. It produced a 1.35 GB APK (unstripped `libflutter.so`).
- The release APK is signed with the debug keystore of this PC (`%USERPROFILE%\.android\debug.keystore`). An APK built on another PC has a different signature, so `adb install -r` fails with `INSTALL_FAILED_UPDATE_INCOMPATIBLE`. Uninstall first in that case. A Play Store build would need its own upload key.

**Test phone:** Poco X3 Pro, Android 13. On the phone, Developer options must have **USB debugging** and **USB debugging (Security settings)** turned on. The second option is needed for `adb shell input` taps and for installs without a prompt on MIUI. Accept the RSA prompt the first time you connect.

**Simulator (no hardware):**

```powershell
python tools\reader_simulator.py                 # listens on 0.0.0.0:5000
python tools\reader_simulator.py --busy          # accepts commands, never replies (lock case)
python tools\reader_simulator.py --drop-after 3  # drops the client after 3 commands
python tools\reader_simulator.py --latency 500   # delays each reply by 500 ms
```

In the app, enter `10.0.2.2:5000` from the Android emulator, or the PC's LAN IP from a real phone. Allow inbound TCP 5000 in Windows Firewall. The desktop app can also connect to the simulator (WiFi option, host `127.0.0.1`). The simulator does not implement every desktop query, so expect some `ERROR,INVALID_COMMAND` (**unverified**).

### 3.7 Add a feature (mobile pattern)

**A new command, for example `SET_REGION,<hex>`:**

1. `lib/protocol/command_builder.dart`: add a validating builder.
   ```dart
   static String setRegion(int code) {
     if (code < 0 || code > 0xFF) throw ArgumentError.value(code, 'code', 'Region must be 00..FF');
     return 'SET_REGION,${code.toRadixString(16).padLeft(2, '0').toUpperCase()}';
   }
   ```
   A command without arguments goes into the `allowed` set of `simple()`.
2. `lib/transport/reader_client.dart`, `_matches()`: add `'SET_REGION' => 'OK,SET_REGION',`. **If you skip this, every success times out after 3 s** and only errors complete.
3. If the timeout must be longer than 3 s, extend the timeout logic in `send()` (see how `SINGLE_POLL` is handled).
4. If the reply has a new shape, add a model to `models.dart` and a `parseX()` to `parser.dart`. For display only, `parseKeyValueReply()` is enough.
5. Call it from a screen with the usual pattern:
   ```dart
   try {
     final reply = await widget.client.send(CommandBuilder.setRegion(code));
     if (reply.startsWith('ERROR,')) throw StateError(reply);
     if (mounted) showMessage(context, 'Region set.');
   } catch (error) {
     if (mounted) showMessage(context, humanError(error));
   }
   ```
6. Tests: add the expected string to `test/protocol/command_builder_test.dart` and any parser case to `parser_test.dart`. Teach `tools/reader_simulator.py` (`handle()`) to answer the command, and add a case to `tools/test_reader_simulator.py`.

**A new screen:**

1. Create `lib/features/foo/foo_screen.dart` with a `StatefulWidget` that takes `ReaderClient client`. Copy the Info screen: `RefreshIndicator` + `ListView` + `PageToolbar` + `SectionCard`s, with `_refresh()` called from `initState`.
2. `lib/app.dart`, `ReaderShell`:
   - add `null` to the `_pages` list (one slot per page);
   - add a case to `_createPage()`;
   - add the title to `_titles`;
   - add a `NavigationDestination` (Material 3 recommends 3–5; the shell already has 5, so a sixth needs a different navigation or a push route).
   For a detail page, push a `MaterialPageRoute` the way Inventory opens `TagDetailScreen`.
3. Use `TaglinkColors.of(context)` and `Theme.of(context).colorScheme` for colours. Never hard-code hex values.
4. Run `flutter analyze` and `flutter test`, then build the release APK and try it on the phone.

### 3.8 Test the mobile app

```powershell
cd C:\Users\Identium\Downloads\RFID_Controller_F429_IDENTIUM_PROTOCOL_STEP8\TAGlink_Mobile
flutter analyze                                    # expected: "No issues found!"
flutter test                                       # all tests pass
flutter test test\transport\reader_client_test.dart   # one file
python -m unittest tools\test_reader_simulator.py -v   # simulator self-tests
```

| Test file | Cases |
|---|---|
| `test/app_test.dart` | App starts in light and in dark theme (one loop, 2 cases) |
| `test/protocol/command_builder_test.dart` | Valid commands built; bad input rejected (2) |
| `test/protocol/line_framer_test.dart` | Split chunks, CRLF/LF mix, bounded garbage (3) |
| `test/protocol/parser_test.dart` | TAG fields, all reply families, malformed input (3) |
| `test/transport/reader_client_test.dart` | FIFO with interleaved TAG lines on a local `ServerSocket`; timeout before the next command is sent (2) |

The source declares **12 test cases**. The earlier recorded result was "10 tests pass". That run probably predates the light/dark loop in `app_test.dart`. Neither `flutter analyze` nor `flutter test` was run for this guide (**unverified today**).

---

## 4. Releasing a new version

### 4.1 Version numbers

| What | Where | Current value |
|---|---|---|
| Project release | Git tag `vX.Y.Z` and GitHub release | latest tag `v0.6.0` |
| TAGlink Desktop | **Nowhere in the code.** The window title is "RFID Reader" and the About tab has no version. The release tag is the only version. | n/a |
| TAGlink Mobile | `pubspec.yaml`, `version: <versionName>+<versionCode>`. It flows into `flutter.versionName` and `flutter.versionCode` in `android/app/build.gradle`. | `1.0.0+1` (not bumped for v0.6.0) |
| Firmware | `VERSION` reply (FWVER) and the firmware sources | see the firmware docs |

For a new mobile build, raise **both** parts, for example `version: 0.7.0+2`. Android refuses to install a lower `versionCode` over a higher one without `adb install -r -d`. If you want the desktop to show a version, add a `VERSION = "0.7.0"` constant to `main.py`, use it in `setWindowTitle` and in the About text, and bump it with each tag. That constant does not exist today.

### 4.2 Release steps

1. Make sure the work is committed on a feature branch, pushed, and fast-forwarded into `main` (the project's usual flow).
2. Build both apps from the commit you will tag:
   ```powershell
   cd C:\Users\Identium\Downloads\RFID_Controller_F429_IDENTIUM_PROTOCOL_STEP8\TAGlink_Desktop
   python tools\smoke_screenshot.py $env:TEMP\taglink_smoke
   pyinstaller --noconfirm main.spec
   cd ..\TAGlink_Mobile
   flutter analyze; flutter test; flutter build apk --release
   ```
3. Collect the assets with clear names in one folder outside the repo:
   ```bash
   mkdir -p ~/release-v0.7.0 && cd ~/release-v0.7.0
   cp /c/Users/Identium/Downloads/RFID_Controller_F429_IDENTIUM_PROTOCOL_STEP8/TAGlink_Desktop/dist/TAGlink.exe TAGlink-v0.7.0.exe
   cp /c/Users/Identium/Downloads/RFID_Controller_F429_IDENTIUM_PROTOCOL_STEP8/TAGlink_Mobile/build/app/outputs/flutter-apk/app-release.apk TAGlink-Mobile-v0.7.0.apk
   # plus the firmware .elf/.hex/.bin and ESP32 bins as usual
   sha256sum * > SHA256SUMS
   ```
   The asset names used for v0.6.0 are **unverified**. Check the v0.6.0 release page and keep the same scheme.
4. Create an annotated tag and push it:
   ```bash
   git tag -a v0.7.0 -m "v0.7.0" && git push origin v0.7.0
   ```
5. Create the release and upload the assets. There is **no `gh` CLI** on this PC. The REST API is used with the token from `git credential fill` (the token is never printed). A template is below. The exact helper used for v0.5.0/v0.6.0 is not in the repo, so this one is **unverified**. Run it from Git Bash (the PowerShell credential pipe does not work):
   ```python
   # save as %TEMP%\gh_release.py, run: python gh_release.py v0.7.0 notes.md <files...>
   import json, pathlib, subprocess, sys, urllib.parse, urllib.request
   REPO = "namesanas/identium-rfid-firmware"
   tag, notes, files = sys.argv[1], sys.argv[2], sys.argv[3:]
   cred = subprocess.run(["git", "credential", "fill"], input="protocol=https\nhost=github.com\n\n",
                         capture_output=True, text=True, check=True).stdout
   token = dict(l.split("=", 1) for l in cred.splitlines() if "=" in l)["password"]
   def post(url, data, ctype):
       req = urllib.request.Request(url, data=data, method="POST", headers={
           "Authorization": f"Bearer {token}", "Accept": "application/vnd.github+json", "Content-Type": ctype})
       with urllib.request.urlopen(req) as r:
           return json.load(r)
   body = json.dumps({"tag_name": tag, "name": tag, "body": pathlib.Path(notes).read_text(encoding="utf-8")}).encode()
   rel = post(f"https://api.github.com/repos/{REPO}/releases", body, "application/json")
   for f in map(pathlib.Path, files):
       post(f"https://uploads.github.com/repos/{REPO}/releases/{rel['id']}/assets?name={urllib.parse.quote(f.name)}",
            f.read_bytes(), "application/octet-stream")
       print("uploaded", f.name)
   print(rel["html_url"])
   ```
6. Open the release page, check that every asset is there, and compare one checksum after downloading it.

---

## 5. Troubleshooting

| Symptom | App | Likely cause | Fix |
|---|---|---|---|
| `Open failed: could not open port 'COMx': PermissionError` | Desktop | Another program has the COM port (Reader Manager, a serial terminal, a second TAGlink, CubeIDE console) | Close the other program. Check that it is gone: `Get-Process TAGlink, ReaderManager -ErrorAction SilentlyContinue`. |
| COM port missing from the combo | Desktop | Board not powered or enumerated, or a cable without data lines | Check `[System.IO.Ports.SerialPort]::GetPortNames()`. On the F429 Discovery, COM3 is the ST-LINK VCP (host UART) and COM8 is the native USB CDC. |
| Connected but every command times out (5 s) | Desktop | Another link holds the command lock: the phone over WiFi, another PC over TCP, or a USB session that never sent RELEASE | Disconnect the other client properly (it sends RELEASE), send `RELEASE` from the owning link, or reset the reader. A dropped TCP or WiFi client frees the lock by itself; USB and serial do not. |
| "Connected, but another interface holds the command lock…" | Mobile | Same as above, usually TAGlink Desktop on USB | Disconnect in TAGlink Desktop (it sends RELEASE) or send `RELEASE` on that COM port. The app retries every 3 s. |
| "Reader is busy with another device (connection refused/reset)" | Mobile | Port 5000 already has a TCP client | Close the other TCP client (desktop on WiFi/Ethernet, another phone, a stuck `nc`/python session) |
| `Ethernet connect failed: timed out` | Desktop | Wrong IP. The fallback is `192.168.150.50`, the firmware's factory address, used when Settings > Network & HTTP > Reader IP is empty. The office LAN reader was at 192.168.1.50. | Run **Discover readers**, or type the IP. This PC has no wired Ethernet to the reader, so use WiFi here. |
| WiFi option does nothing / "Enter the reader WiFi IP" | Desktop | Settings > WiFi > Reader WiFi IP is empty | Discover readers, or type the IP (it was 192.168.1.17 on the office WiFi) |
| No reader found by Scan | Mobile | Phone not on the reader's network, VPN, guest or isolated WiFi, or a multi-interface phone | Join the same SSID or the reader AP (then use Hotspot = 192.168.4.1), or type the IP |
| Connects, no tags | Both | Inventory not started, antenna not connected, power too low, wrong region, tags out of range, or the reader is in **active** mode and uploads elsewhere | Check `GET_STATUS`, the Mode chip (use Return to passive on the desktop), power and antenna. Try a Single poll. |
| Settings show ERROR after connect | Desktop | The reader is inventorying (active mode), so queries fail | Stop, or Return to passive. The info batch reruns automatically. |
| "Command not supported by this reader firmware" | Desktop | `ERROR,INVALID_COMMAND` from older firmware, or `SET_GEN2` with a non-zero RF profile, fast mode or hopping | Update the firmware; leave those three at their defaults |
| VSWR "Not available over WiFi" | Desktop | The module bridge is not offered over WiFi | Use USB, serial or Ethernet |
| VSWR status 0x0105 / "Outside reader region" | Desktop | The sweep is outside the region's hop table | Use the range that `GET_FREQUENCY` filled in, or change the region |
| VSWR status 0xAA4A | Desktop | No antenna on that port | Connect an antenna or pick another port |
| USB link swallows commands for 30 s after Reader Manager | Desktop | A `VER`/`IOGET`/`FF` line opened a module-bridge session | Wait 30 s of idle, or reset the reader |
| `pyinstaller` fails with `PermissionError` on `dist\TAGlink.exe` | Desktop | The old exe is still running | `Get-Process TAGlink \| Stop-Process`, then rebuild |
| Test settings appear in the real app | Desktop | A tool created `Main()` without the QSettings override | Use the throwaway INI pattern from `tools/smoke_screenshot.py`. Remove the stray values with `reg delete HKCU\Software\SilionRFID\E310Reader /f` (this resets all TAGlink settings). |
| Gradle: `Unsupported class file major version 69` or similar | Mobile | Gradle is running on Java 25 | Set `JAVA_HOME` to the JDK 17 path (see 3.5). Check `flutter doctor -v`. |
| `Minimum supported Gradle version is 8.14` / AGP / Kotlin warnings | Mobile | Old wrapper or plugins | Keep Gradle 8.14.3, AGP 8.13.0, Kotlin 2.3.20 (or newer) |
| `NDK not configured` / NDK download stalls | Mobile | NDK missing | NDK 28.2.13676358 is installed and pinned. Do not change `ndkVersion`. |
| `Android license status unknown` | Mobile | Licences not accepted | `yes \| sdkmanager.bat --licenses` from Git Bash |
| First build takes a very long time | Mobile | Gradle, Maven and engine downloads over a slow link | Wait (about 16 min the first time). Later builds take 25–60 s. |
| APK of 150 MB+ or 1 GB+ | Mobile | Debug build, stale incremental packaging, or `keepDebugSymbols` | Build `--release`, run `flutter clean`, never use `keepDebugSymbols` |
| `adb devices` is empty or shows `unauthorized` | Mobile | USB debugging off, cable charge-only, RSA prompt not accepted, MIUI security option off | Turn on USB debugging and USB debugging (Security settings), set USB mode to File transfer, accept the prompt, then `adb kill-server; adb start-server` |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | Mobile | Installed APK signed with another key | `adb uninstall com.taglink.mobile`, then install again |
| `INSTALL_FAILED_VERSION_DOWNGRADE` | Mobile | Lower `versionCode` | `adb install -r -d <apk>`, or bump `pubspec.yaml` |
| Session drops when switching apps | Mobile | By design: the lifecycle `paused` state runs STOP/RELEASE/close | Reconnect from Recent |
| Inventory list feels slow in a debug build | Mobile | Debug build | Use the release APK |

---

## 6. Cheat sheet

### Python / desktop

```powershell
cd C:\Users\Identium\Downloads\RFID_Controller_F429_IDENTIUM_PROTOCOL_STEP8\TAGlink_Desktop
python main.py
python -c "import PySide6, serial; print(PySide6.__version__, serial.VERSION)"
python -m PyInstaller --version
pyinstaller --noconfirm main.spec
python tools\smoke_screenshot.py screenshots\after [--light]
python -m serial.tools.list_ports -v                 # list COM ports with descriptions
reg query HKCU\Software\SilionRFID\E310Reader        # view saved settings
```

### Flutter

```powershell
flutter --version
flutter doctor -v
flutter devices
flutter pub get
flutter analyze
flutter test
flutter run                                  # debug on the connected phone, hot reload
flutter run --release                        # release mode on the phone
flutter build apk --release
flutter build apk --debug
flutter clean
flutter pub outdated
```

### adb

```powershell
adb devices -l
adb install -r build\app\outputs\flutter-apk\app-release.apk
adb install -r -d build\app\outputs\flutter-apk\app-release.apk   # allow downgrade
adb uninstall com.taglink.mobile
adb shell am start -n com.taglink.mobile/.MainActivity
adb shell am force-stop com.taglink.mobile
adb logcat -c
adb logcat --pid=$(adb shell pidof com.taglink.mobile)          # Git Bash: this app only
adb logcat -s flutter                                            # Dart print()/debugPrint output
adb logcat *:E                                                   # errors only
adb shell ip -4 addr show wlan0                                  # phone IP / subnet
adb shell dumpsys package com.taglink.mobile | findstr version   # installed versionName/Code
adb kill-server; adb start-server
```

In PowerShell, run the `--pid` form as `adb logcat --pid=$(adb shell pidof com.taglink.mobile)` after the app has started. If `pidof` returns nothing, the app is not running.

### Manual reader tests (raw TCP / serial)

The reader serves one link at a time. Always end a manual session with `RELEASE`, especially on USB or serial, where nothing releases the lock for you.

TCP (Ethernet or WiFi), version then release:

```powershell
python -c "import socket;s=socket.create_connection(('192.168.1.17',5000),3);s.settimeout(3);s.sendall(b'GET_VERSION\r\n');print(s.recv(512).decode().strip());s.sendall(b'RELEASE\r\n');print(s.recv(512).decode().strip());s.close()"
```

TCP, status plus a single poll (the TAG lines arrive before `OK,SINGLE_POLL`):

```powershell
python -c "import socket,time;s=socket.create_connection(('192.168.1.17',5000),3);s.settimeout(5);s.sendall(b'GET_STATUS\r\nSINGLE_POLL,1000\r\n');time.sleep(2);print(s.recv(8192).decode());s.sendall(b'RELEASE\r\n');print(s.recv(512).decode());s.close()"
```

Serial / USB CDC (COM8 = USB CDC, COM3 = ST-LINK VCP host UART on the Discovery):

```powershell
python -c "import serial;p=serial.Serial('COM8',115200,timeout=2);p.write(b'GET_VERSION\r\n');print(p.readline().decode().strip());p.write(b'RELEASE\r\n');print(p.readline().decode().strip());p.close()"
```

Release a lock held by a forgotten USB or serial session:

```powershell
python -c "import serial;p=serial.Serial('COM8',115200,timeout=2);p.write(b'RELEASE\r\n');print(p.readline());p.close()"
```

Interactive text session (the pyserial miniterm; Ctrl+] exits). Type commands and press Enter (`--eol CRLF` sends `\r\n`):

```powershell
python -m serial.tools.miniterm COM8 115200 --eol CRLF
```

Check whether port 5000 is reachable without taking the lock (sends nothing):

```powershell
Test-NetConnection 192.168.1.17 -Port 5000
```

---

## 7. What this guide did not verify

- `flutter analyze` / `flutter test` results and the exact test count at run time. The source declares 12 cases; the earlier recorded result was 10/10.
- Builds were not run for this guide. The sizes quoted come from the files present in `dist/` and `build/` on 2026-10-12 (TAGlink.exe 53.5 MB, app-release.apk 52.4 MB, app-debug.apk 159 MB).
- Whether the baud rate matters on the USB CDC port, whether UPX is installed, the cmdline-tools folder name, the v0.6.0 asset names, and the release helper script (the one used before is not in the repo).
- The desktop app against the mobile simulator.
- The reader IP addresses (192.168.1.50 LAN, 192.168.1.17 WiFi) are from earlier sessions and can change with DHCP.
