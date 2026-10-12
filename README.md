# Identium RFID Controller

Firmware and host applications for the Identium UHF RFID reader.

| Folder | Contents |
|---|---|
| [`STM32_Firmware/`](STM32_Firmware) | Reader firmware for the STM32 (F429 Discovery and the F411 custom board): sources, HAL drivers, linker scripts, `Makefile`, STM32CubeIDE project, reader test scripts (`Tools/`) and the host-side protocol check (`hostcheck/`) |
| [`ESP32_Firmware/`](ESP32_Firmware) | ESP-IDF project for the WiFi coprocessor (ESP32 / ESP32-C3) |
| [`TAGlink_Desktop/`](TAGlink_Desktop) | TAGlink, the Windows desktop application (Python, PySide6) |
| [`TAGlink_Mobile/`](TAGlink_Mobile) | TAGlink Mobile, the Android application (Flutter) |
| [`docs/`](docs) | Design records, notes, the reader protocol specification and the board schematic (`docs/hardware/`) |

## Building

**STM32 firmware** — needs `arm-none-eabi-gcc` (STM32CubeCLT) and a Unix shell (Git Bash on Windows):

```
cd STM32_Firmware
make                  # debug build   -> build/debug/rfid_controller.elf
make CONFIG=release   # release build -> build/release/rfid_controller.elf
make BOARD=f411       # F411 custom board -> build/f411-debug/ (see docs/notes/f411-custom-board-port.md)
```

**ESP32 coprocessor** — ESP-IDF v6.1:

```
cd ESP32_Firmware
idf.py -B build_esp32 -DIDF_TARGET=esp32 -DSDKCONFIG=build_esp32/sdkconfig build
```

**TAGlink desktop** — `python main.py` to run, `pyinstaller main.spec` to build `dist/TAGlink.exe` (see [`TAGlink_Desktop/README.md`](TAGlink_Desktop/README.md)).

**TAGlink Mobile** — `flutter build apk --release` (see [`TAGlink_Mobile/README.md`](TAGlink_Mobile/README.md)).

Releases with ready-built firmware images and applications are on the [Releases](https://github.com/namesanas/identium-rfid-firmware/releases) page.
