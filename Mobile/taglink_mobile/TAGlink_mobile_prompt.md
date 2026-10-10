# Task: build "TAGlink Mobile", a phone app that controls a UHF RFID reader over WiFi

Build a **Flutter** app (Android first, iOS from the same code). Send back the complete project source as a zip, plus build steps and a debug APK.

The reader already works. You are building only the app. You don't have the hardware, so also build the simulator described in section 6 and test against it.

## 1. How the app talks to the reader

- **Transport:** plain TCP to the reader's IP, **port 5000**. No TLS, no HTTP, no login.
- **Encoding:** ASCII text lines.
  - Send every command terminated with `\r\n`.
  - Every reply line ends with `\r\n`.
- **One client at a time.**
  - The reader accepts a single TCP client on port 5000.
  - A second connection may be refused or reset. Show a clear message ("Reader is busy with another device").
- **Command lock.**
  - The reader also has USB and serial ports. It serves **one interface at a time**: whichever port sends a command first.
  - Always send `RELEASE` before closing the connection, including when the app goes to the background or the user disconnects.
  - If the app is connected but gets no reply to `GET_STATUS` within 3 s, another interface holds the lock. Tell the user to send `RELEASE` from that interface or reset the reader. Then keep retrying `GET_STATUS` every 3 s.
- **No request IDs.**
  - Match replies by their prefix (table below).
  - Keep **one command outstanding at a time**: a FIFO queue, next command only after the reply or a timeout.
  - Typical reply time is about 30 ms; occasionally up to 1.5 s.
  - Use a 3 s timeout, except `SINGLE_POLL,<ms>`, which takes `<ms>` + 3 s.
- **Unsolicited lines:**
  - `TAG,...` lines can arrive at any time during inventory, interleaved with replies. Route them to the tag list, never to the pending command.
  - Ignore lines starting with `DEBUG,`.
- **Unknown or bad commands** get the reply `ERROR,INVALID_COMMAND`.

### Finding the reader

There is no discovery service over WiFi. Offer:

1. **Manual entry:** IP address, port defaulting to 5000. Remember the last 5 readers.
2. **"Scan network":**
   - Probe every address in the phone's /24 subnet on port 5000, about 64 in parallel with a 0.8 s connect timeout.
   - For each address that answers, send `GET_VERSION`. Keep only those that reply with a line starting `VERSION,`. Then send `RELEASE` and close.
   - List the readers found.
3. **Reader's own access point:** if the reader is in AP mode, the phone joins the WiFi network **`RFID-Reader`** and the reader is at **192.168.4.1:5000**. Offer this address as a shortcut.

## 2. Commands and replies

`<x>` marks a value. Hex values are upper-case without `0x`.

| Purpose | Send | Reply (one line unless noted) |
|---|---|---|
| Status | `GET_STATUS` | `STATUS,IDLE` or `STATUS,INVENTORY` |
| Firmware info | `GET_VERSION` | `VERSION,BTVER=<hex8>,HWVER=<hex8>,FWDATE=<hex8>,FWVER=<hex8>,PROTO=<hex8>` |
| Serial number | `GET_SERIAL` | `SERIAL,YEAR=<n>,SN=<text>` |
| Module temperature | `GET_TEMPERATURE` | `TEMP,<°C>` |
| Start continuous inventory | `START` | `OK,START`, then a stream of `TAG,...` lines |
| Stop inventory | `STOP` | `OK,STOP` (safe to send when idle) |
| One timed read | `SINGLE_POLL,<ms>` (e.g. 1000) | `TAG,...` lines, then `OK,SINGLE_POLL` |
| Read power | `GET_POWER` | `POWER,ANT=<n>,READ=<cdBm>,WRITE=<cdBm>`. Centi-dBm: 2000 = 20.00 dBm |
| Set power | `SET_POWER,<cdBm>` | `OK,SET_POWER`. UI range 5.00–30.00 dBm, step 0.5 |
| Region | `GET_REGION` | `REGION,<hex2>` |
| Antenna | `GET_ANTENNA` | `ANTENNA,TX=<n>,RX=<n>` |
| Session | `GET_SESSION` / `SET_SESSION,<0-3>` | `SESSION,<n>` / `OK,SET_SESSION` |
| Read tag memory | `READ_TAG,<epc>,<bank>,<wordAddr>,<wordCount>` | `READ_DATA,BANK=<n>,ADDR=<n>,WORDS=<n>,DATA=<hex>` |
| Write tag memory | `WRITE_TAG,<epc>,<bank>,<wordAddr>,<accessPwdHex8>,<dataHex>` | `OK,WRITE_TAG` |
| Change EPC | `WRITE_EPC,<oldEpc>,<newEpc>` | `OK,WRITE_EPC` |
| Inputs | `GET_GPI` | `GPI,GPI1=<0/1>,GPI2=<0/1>,GPI3=<0/1>,GPI4=<0/1>` |
| Output | `SET_GPO,<1-4>,<0/1>,<durationMs>` (0 = stay) | `OK,SET_GPO` |
| Capabilities | `GET_CAPABILITIES` | `CAPABILITIES,GPI=<n>,GPO=<n>,...` |
| WiFi status | `GET_WIFI` | `WIFI,LINK=ONLINE,STATE=CONNECTED,IP=<ip>,RSSI=<dBm>,HOST=<0/1>,UPLOAD=<0/1>,ENABLE=<0/1>,MODE=STA\|AP,SSID=<text>,IF=ETH\|WIFI\|AUTO,ERRORS=<n>,REJECTED=<hex>/<n>` |
| Change WiFi | `SET_WIFI,<0\|1>,<STA\|AP>,<ssid>,<password>` | `OK,SET_WIFI`. See below |
| Health | `GET_DIAG` | `DIAG,RESET=...,UPTIME_MS=...,...` (show as raw key/value list) |
| Release lock | `RELEASE` | `OK,RELEASE` |

**Rules for these commands:**
- **Failures:** any command can fail with `ERROR,<COMMAND>`. Show it to the user, never as a crash.
- **Reply format:** every reply is `KEY,` followed by comma-separated values or `name=value` pairs. Write one generic parser that splits on `,` and then on `=`.
- **Banks:** 0 = Reserved, 1 = EPC, 2 = TID, 3 = User.

**Tag line format:**
```
TAG,EPC=E2004703CA4068230FC80111,RSSI=-39,ANT=1,FREQ=927250,TIME=1057
```
- `EPC`: hex, variable length.
- `RSSI`: dBm.
- `ANT`: antenna.
- `FREQ`: kHz.
- `TIME`: reader timestamp in ms; treat it as opaque.

The current rate is about 10 lines/s, but design for **200 lines/s** without UI lag: batch UI updates, e.g. every 200 ms.

**`SET_WIFI` rules:**
- **SSID:** may not contain a comma. The password is everything after the 4th comma.
- **Password:** empty means an open network; otherwise 8–64 characters. `****` keeps the stored password.
- **The connection drops:** after `OK,SET_WIFI` the reader leaves the network the phone is on, or changes its address. Warn the user before sending, then return to the "find reader" screen.

## 3. Screens (MVP)

1. **Connect:**
   - saved readers, manual IP, "Scan network", the AP shortcut;
   - connection state;
   - the busy and lock messages from section 1.
2. **Inventory (main screen):**
   - big Start / Stop button, which shows `STATUS` on connect;
   - live list of unique tags: EPC, read count, last RSSI, antenna, last seen;
   - sort by count, RSSI or EPC;
   - filter by EPC substring;
   - totals: unique tags, total reads, reads per second;
   - Clear button;
   - "Single read" button (`SINGLE_POLL,1000`).
3. **Tag detail:** tap a tag to open it.
   - Read the EPC, TID and User banks.
   - Write the User bank, with access password, confirm dialog and hex validation.
   - Change the EPC.
4. **Settings:**
   - read power, with a slider and Apply;
   - session;
   - region, read-only;
   - GPIO: show inputs, toggle outputs.
5. **Reader info:**
   - version, serial, temperature;
   - WiFi status;
   - `GET_DIAG` as a key/value list;
   - pull to refresh.
6. **WiFi setup:**
   - shows `GET_WIFI`;
   - form for `SET_WIFI`, with the warning before sending.

Also include:
- **Export:** export the current tag list as CSV (EPC, count, RSSI, antenna, first seen, last seen) through the share sheet.
- **Theme:** light and dark.

## 4. Behaviour requirements

- **Never block the UI thread.** The socket, parser and command queue live in a separate service class, Riverpod or Bloc, your choice.
- **Line framing:**
  - Handle TCP chunks that split or join lines.
  - Accept `\n` or `\r\n`.
- **Reconnect:** on socket loss, show "Disconnected" and retry with back-off (1, 2, 5, 10 s), up to a user-visible limit. After reconnecting, send `GET_STATUS` to resync the Start/Stop state.
- **Background or close:** when the app goes to the background or closes:
  - send `STOP` if this app started inventory;
  - then send `RELEASE`;
  - then close the socket.
- **Protocol log screen:** a debug page with the last 500 sent and received lines, timestamped. It's essential for field support.
- **No other traffic:** no analytics, no network calls other than the reader.

## 5. Code layout

- `lib/protocol/`:
  - line framer;
  - reply parser into typed models (`Tag`, `ReaderStatus`, `Version`, `Power`, `WifiStatus`, ...);
  - command builder with argument validation.
- `lib/transport/`: TCP client, command queue with timeouts, and a stream of TAG lines.
- `lib/features/<screen>/`: UI.
- **Unit tests:** cover the framer and parser (split lines, interleaved TAG lines during a command, every reply format above, malformed lines) and the command queue (timeouts, ordering).

## 6. Simulator (required, for testing without hardware)

Write `tools/reader_simulator.py`, Python 3 with the standard library only.

- **Port:** listens on port 5000.
- **Commands:** implements every command above with realistic replies, and answers `ERROR,INVALID_COMMAND` to anything else.
- **Inventory:** after `START`, emits about 50 random `TAG,...` lines per second from a pool of 20 EPCs until `STOP`.
- **Single client:** accepts one client at a time.
- **Test modes** (command-line flags):
  - `--busy`: never replies, to test the lock message;
  - `--drop-after N`: closes the socket after N commands, to test reconnect;
  - `--latency MS`: adds reply delay.

Run the app against it in the Android emulator: the host PC is `10.0.2.2` from the emulator.

## 7. Deliverables

1. Flutter project zip, which must build with `flutter build apk --debug` with no extra setup.
2. `README.md`:
   - build and run steps;
   - how to run the simulator;
   - how to connect to a real reader.
3. The debug APK.
4. A short list of anything not implemented or untested.

Don't claim something works unless you ran it, against the simulator at least.
