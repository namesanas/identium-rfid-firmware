# Switching the COM10 ESP32 between firmwares

Double-click `switch_firmware.cmd` and choose:

1. **wifi_coprocessor** for the STM32 RFID controller's WiFi link.
2. **mubr_handheld v0.7.1** to restore the handheld firmware and settings saved
   from this ESP32 before the coprocessor was installed.

The menu checks the image's size, SHA256 and application name, then uses the
installed esptool v5 to write and verify it. No rebuild or ESP-IDF activation is
needed. COM10 and 460800 baud are the defaults.

## Wiring

Before either choice, disconnect the **STM32 PA15 -> ESP32 EN** jumper, leave
the ESP32 USB cable connected, and close any serial monitor using COM10.
The STM32 otherwise resets the ESP32 when it cannot handshake, interrupting
flashing and preventing the handheld firmware from starting normally.

After installing **wifi_coprocessor**, reconnect PA15 -> EN and reset the STM32
so it starts the coprocessor link. When running **mubr_handheld**, leave that
jumper disconnected.

If connection fails, hold ESP32 **BOOT**, tap **EN**, release **BOOT**, then retry.

## Images and settings

`firmware_images/manifest.json` identifies the two 4 MB images. Each contains the
bootloader, partition table, application and data regions, so switching also
restores the image's saved settings. Later settings changes are overwritten
by the next switch. The coprocessor image starts with blank ESP32 data regions;
its operating settings are supplied by the STM32.

Keep a copy of the entire `firmware_images` folder somewhere outside this
project. It contains the original handheld firmware backup and may include WiFi
credentials, so its contents are ignored by Git. These snapshots are for the
classic ESP32 on COM10; they are not ESP32-C3 images.

## Command line

Run from `ESP32_Firmware`:

```powershell
python Tools/switch_firmware.py --firmware wifi
python Tools/switch_firmware.py --firmware handheld
```

Use `--port COM11` if Windows assigns the ESP32 another port, or `--baud 115200`
for a slower connection. Add `--dry-run` to check an image and display the flash
command without accessing the ESP32. If esptool is installed elsewhere, pass
`--python C:\path\to\its\python.exe`.
