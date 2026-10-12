# TAGlink Mobile

Flutter Android-first app for operating a TAGlink-compatible UHF RFID reader over its plain TCP text protocol. The app uses the same Dart UI and protocol client for Android and iOS; no reader hardware or cloud service is required for simulator testing.

## Current project status

- Implemented: manual connection, last-five reader history, AP shortcut, /24 network scan, TCP line framing, serialized command queue/timeouts, unsolicited tag batching, reconnect backoff, lock messaging/status retries, inventory view and filters/sorts, tag bank read/write and EPC change forms, reader settings, GPIO, Wi-Fi configuration, reader info/diagnostics, CSV sharing, theme toggle, and the 500-line protocol log.
- Included: standard-library Python TCP reader simulator and integration tests.
- Built and tested: `flutter analyze` is clean, `flutter test` passes, and debug and release APKs build (Flutter 3.47, JDK 17, Android SDK 36). The app was run on an Android 13 phone against the real reader over WiFi. Use the release APK (`flutter build apk --release`) for everyday use; see `docs/notes/dev-guide-taglink-apps.md` for the toolchain, build and release steps.
- The iOS project scaffolding is included, but it has not been built with Xcode/CocoaPods.

## Requirements

- Flutter SDK (stable channel; Dart 3.4 or newer)
- Android SDK/platform tools and a JDK 17 installation for Android builds
- Python 3.10+ for the simulator (standard library only)
- For iOS builds: macOS, Xcode, CocoaPods

## Build the Android debug APK

From the project root:

```sh
flutter --version
flutter pub get
flutter analyze
flutter test
flutter build apk --debug
```

APK output:

```text
build/app/outputs/flutter-apk/app-debug.apk
```

Install on a connected Android phone with USB debugging enabled:

```sh
adb install -r build/app/outputs/flutter-apk/app-debug.apk
```

On the phone, allow TAGlink Mobile to access the local network if Android prompts. No account, API key, or cloud endpoint is required. The app's network traffic is only to reader IPs / TCP port 5000; the external build tools naturally use their package repositories during dependency/build resolution.

## Run the simulator

In terminal 1, from the project root:

```sh
python3 tools/reader_simulator.py
```

The simulator listens on `0.0.0.0:5000`, produces random tag reports from 20 EPCs at approximately 50 reports/s, and accepts one active client session. Useful fault modes:

```sh
python3 tools/reader_simulator.py --busy
python3 tools/reader_simulator.py --drop-after 3
python3 tools/reader_simulator.py --latency 500
```

`--busy` accepts commands without replying, `--drop-after N` closes a session after its Nth command/reply, and `--latency MS` delays command replies by the specified milliseconds. Stop the simulator with Ctrl+C.

### Android emulator

In the app's Connect screen, enter:

```text
IP:   10.0.2.2
Port: 5000
```

`10.0.2.2` maps from the Android emulator to the host PC. If the host firewall blocks inbound TCP 5000, allow that port for the simulator process.

### Physical Android phone

Connect the phone and host PC to the same Wi-Fi/LAN. Enter the host PC's LAN IPv4 address (for example, `192.168.1.20`) and port `5000`. Do **not** enter `127.0.0.1` on the phone; that points to the phone itself. Ensure the PC firewall permits inbound TCP port 5000.

### Simulator integration tests

```sh
python3 -m unittest tools/test_reader_simulator.py -v
```

These tests cover basic command replies, unsolicited inventory tags, comma-containing Wi-Fi passwords, busy mode, drop-after behaviour, and latency. Flutter tests are under `test/` and cover the line framer, typed reply parser, command validation, interleaved tags, FIFO ordering and timeout progression; run them with `flutter test` once Flutter is installed.

## Connect to a real reader

1. Put the phone on the reader's Wi-Fi LAN, or join the reader's access point `RFID-Reader`.
2. In TAGlink Mobile, enter the reader's IP and port `5000`, or tap the `192.168.4.1` access-point shortcut when using the reader AP.
3. Tap **Connect**. The app requests `GET_STATUS` to synchronize the inventory state.
4. If the TCP connection is refused/reset, check whether another phone/client is connected. The reader supports a single TCP client.
5. If TCP connects but `GET_STATUS` times out, the reader's USB/serial interface may hold the command lock. Send `RELEASE` from that interface or reset the reader. The app keeps retrying status every 3 seconds.
6. Disconnect normally. The app sends `STOP` if this app started inventory, then `RELEASE`, then closes the socket. It applies the same cleanup when it enters the background.

The reader protocol is plain ASCII TCP without TLS or login, as specified. Use it on a trusted local network.

## Source map

```text
lib/
  app.dart
  main.dart
  protocol/       line framer, parser/models, validated command builder
  transport/      TCP socket, FIFO queue, timeout/reconnect/log handling
  features/       Connect, Inventory, Tag detail, Settings, Info, Wi-Fi, Protocol log
  widgets/        shared UI helpers
tools/
  reader_simulator.py
  test_reader_simulator.py
test/
  protocol/       framer, parser, command builder tests
  transport/      socket queue and interleaved TAG tests
```

## Notes / limitations to verify on hardware

- Network scan probes the phone's detected IPv4 `/24`; on unusual multi-interface/VPN networks, manual IP entry may be more reliable.
- `GET_WIFI` does not return the saved password, so the app intentionally never pre-fills it. Use `****` to retain the reader's current password.
- The simulator returns synthetic tag-bank data and accepts memory writes for software flow testing only; it does not emulate RF/tag physics or validate production reader firmware.
- There is no TLS or protocol authentication in the reader contract. Avoid exposing port 5000 to untrusted networks.
- Not yet verified: the Android emulator UI and the iOS build.
