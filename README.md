# Identium RFID Controller

Firmware and host applications for the Identium UHF RFID reader.

| Folder | Contents |
|---|---|
| [`STM32_Firmware/`](STM32_Firmware) | Reader firmware for the STM32 (F429 Discovery and the F411 custom board): sources, HAL drivers, linker scripts, `Makefile`, STM32CubeIDE project, reader test scripts (`Tools/`) and the host-side protocol check (`hostcheck/`) |
| [`ESP32_Firmware/`](ESP32_Firmware) | ESP-IDF project for the WiFi coprocessor (ESP32 / ESP32-C3) |
| [`TAGlink_Desktop/`](TAGlink_Desktop) | TAGlink, the Windows desktop application (Python, PySide6) |
| [`TAGlink_Mobile/`](TAGlink_Mobile) | TAGlink Mobile, the Android application (Flutter) |
| [`docs/`](docs) | Design records, notes, the reader protocol specification and the board schematic (`docs/hardware/`) |

## Developer guides

- [STM32 firmware](docs/notes/dev-guide-stm32-firmware.md): source tree, start-up, protocols, settings, diagnostics, building, flashing, testing
- [ESP32 coprocessor](docs/notes/dev-guide-esp32-coprocessor.md): building and flashing with ESP-IDF for the DevKit and the ESP32-C3, troubleshooting
- [TAGlink apps](docs/notes/dev-guide-taglink-apps.md): desktop and mobile features, connections, building the exe and APK, releasing

## Building

**STM32 firmware** — one source tree for both boards; the board is chosen when building. Needs `arm-none-eabi-gcc` (STM32CubeCLT) and a Unix shell (Git Bash on Windows):

| Board | Command | Output |
|---|---|---|
| F429 Discovery | `make` / `make CONFIG=release` | `build/debug/rfid_controller.elf` / `build/release/rfid_controller.elf` |
| F411 custom board | `make BOARD=f411` / `make BOARD=f411 CONFIG=release` | `build/f411-debug/rfid_controller_f411.elf` / `build/f411-release/rfid_controller_f411.elf` |

Add `flash` to program over SWD (for example `make BOARD=f411 CONFIG=release flash`). It first reads the chip on the ST-LINK and refuses to flash an image built for the other board. `make check-chip` runs only that check.

In STM32CubeIDE, pick the board with the build configuration (hammer drop-down or Project → Build Configurations → Set Active): **Debug** and **Release** build the F429 (`RFID_Controller_F429.elf`); **F411** builds the custom board (`RFID_Controller_F429_f411.elf`, output folder `F411/`). The F411 port is described in [`docs/notes/f411-custom-board-port.md`](docs/notes/f411-custom-board-port.md).

**ESP32 coprocessor** — one ESP-IDF project for the DevKit (`esp32`) and the custom board (`esp32c3`), each built in its own folder:

```
cd ESP32_Firmware
idf.py -B build_esp32   -DIDF_TARGET=esp32   -DSDKCONFIG=build_esp32/sdkconfig   build   # DevKit
idf.py -B build_esp32c3 -DIDF_TARGET=esp32c3 -DSDKCONFIG=build_esp32c3/sdkconfig build   # custom board
```

**TAGlink desktop** — `python main.py` to run, `pyinstaller main.spec` to build `dist/TAGlink.exe` (see [`TAGlink_Desktop/README.md`](TAGlink_Desktop/README.md)).

**TAGlink Mobile** — `flutter build apk --release` (see [`TAGlink_Mobile/README.md`](TAGlink_Mobile/README.md)).

Releases with ready-built firmware images and applications are on the [Releases](https://github.com/namesanas/identium-rfid-firmware/releases) page.
