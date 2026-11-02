# Open items and how to pick the work back up

Living note. Last reviewed **2026-11-02**, after v0.11.1, by which point work was
paused for a few weeks. Update it in place rather than adding dated copies.

## Where the project stands

`main` is at `f5e6bd9` and pushed. The two newest releases:

| Release | Content |
|---|---|
| v0.11.0 | Bootloader + signed field update, TAGlink Desktop Firmware page, small-screen layout fix, degraded start-up + read protection, `fw_update.py` speed fix |
| v0.11.1 | New TAGlink icon in both apps; no firmware change |

Both boards build clean at `CONFIG=release`, and the Desktop smoke tool renders
all 7 pages at 3 window sizes.

The working tree deliberately carries one uncommitted blank line in
`STM32_Firmware/Src/main.c`. Leave it; it is not a change worth a commit.

## Before you walk away: back up two private keys

Neither is in the repository, and both exist on this PC only.

| File | Lose it and |
|---|---|
| `STM32_Firmware/keys/fw_signing_key.pem` | no reader can ever be updated over the link again — every board needs SWD, and with read protection set that means a mass erase first |
| `C:\Users\Identium\.android\debug.keystore` | a TAGlink Mobile APK built on another machine will not install over the one already on your phones; every device needs an uninstall first |

Copy both somewhere off this machine. It is a one-minute job that cannot be done
after the fact.

## Verified on hardware

- F429: signed `.tlfw` installs (`LAST=INSTALLED`), `GET_DIAG` clean,
  `serial_link_test.py` full PASS.
- F429: an image with one signature bit flipped is rejected
  (`LAST=BAD_SIGNATURE`) and the reader keeps running its old firmware.
- F429: the same update through TAGlink Desktop's Firmware page, good image and
  bad image.

## Not yet tested on hardware

1. **The F411 update path.** Staging lives in the upper 2 MB of the GD25Q32 SPI
   flash rather than internal flash, so it is a different code path from the
   F429's. Needs `make flash` once over SWD to place the bootloader, then a
   `make BOARD=f411 CONFIG=release ... image` sent with `fw_update.py`.
2. **Degraded start-up** (`fcae054`): unplug the RFID module, confirm
   `GET_DIAG` reports `DEGRADED=RFID`, plug it back and confirm recovery within
   about 10 s.
3. **Read protection**: `make protect` / `make unprotect`, on a spare board —
   `make protect` makes the board unreadable, and unprotecting mass-erases it.
4. **F411 tag backup log** (SPI flash tag log, pushed 2026-10-13).
5. **REBOOT over WiFi** through the ESP32 coprocessor.
6. The released `rfid_controller_f429-v0.11.0.tlfw` was never installed on the
   bench reader — the test above used a locally built `boottest1` image.

## Remaining audit fixes, in the order they are worth doing

From the external 11-finding audit of 2026-10-27. Findings #7 (bootloader
fall-through) and #9 (`TAGLOG_CLEAR` erasing the staging area) are fixed in
`c3c7d58`. These nine are open, and none of them need hardware to write —
only to confirm.

| # | Problem | Where to look |
|---|---|---|
| 5 | Configuration saves do not alternate between the A and B slots, so a power cut mid-write can lose the stored settings | `Firmware/Application/configuration/firmware_configuration.c` |
| 6 | Reader-settings storage is never erased when it rolls over | same file, plus `Firmware/Services/configuration/configuration_port.c` |
| 2 | `sprintf` into a fixed buffer when answering `GetRegions` / `GetFrequency` can overflow | `Firmware/Application/Silion/sillion_application.c`, `Firmware/Application/Host/host_interface.c` |
| 4 | A partially applied GPIO write leaves the F411's output 4 in the wrong state | `Firmware/BSP/industrial_io.c`, `Firmware/BSP/f411_custom/board_config.h` |
| 11 | The epoch is held in 32 bits and wraps | `Firmware/Services/Time/time_service.c`, `Firmware/Protocol/identium_command.c` |
| 10 | The configuration socket has no deadline, so a stalled client holds it open forever | `Firmware/Services/configuration/configuration_port.c` |
| 8 | Vendor configuration replies OK before the change has actually been executed | `Firmware/Services/configuration/module_bridge.c` |
| 1 | Blocking waits on the RFID module delay a relay pulse expiring, by up to 10 s | `Firmware/BSP/industrial_io.c` and the module wait loops |
| 3 | The network interface has no authentication at all | protocol-wide; needs a design decision first, not just a patch |

#3 is the only one that is a design question rather than a defect. The other
eight are ordinary bug fixes, and #5, #6 and #2 are the ones most likely to bite
a fielded reader.

## Smaller leftovers

- `TagLink (2).ico` sits untracked in the repository root. The icon that matters
  is already committed at `TAGlink_Desktop/icon.ico`, so this copy can simply be
  deleted or moved out of the project.
- TAGlink Desktop does not remember its window size or position between runs
  (`saveGeometry` / `restoreGeometry` are not wired up).
- Firmware update is Desktop-only. TAGlink Mobile has no Firmware page.
- A `make_image.bat` double-click helper for building a `.tlfw` without opening
  Git Bash was offered and never written.
- `MBVER` (mainboard version) in the `VERSION` reply, and showing it in TAGlink.
- Remote support phase 2 — a live session through a relay server — still needs
  decisions about hosting and a domain before any code.

## Commands worth having at hand

Run the firmware ones from **Git Bash**, not PowerShell: the Makefile uses
`find`, and PowerShell resolves that to Windows' `find.exe`, which silently
builds almost nothing.

```sh
cd STM32_Firmware
make BOARD=f429 CONFIG=release                       # plain build (also refreshes the .hex)
make BOARD=f429 CONFIG=release VERSION=v0.12.0 image # signed .tlfw
make BOARD=f429 CONFIG=release flash                 # SWD, needed once per board
python Tools/serial_link_test.py --port COM8          # host-link regression
python Tools/fw_update.py --port COM8 --status
```

```sh
cd TAGlink_Desktop
python main.py                                        # run from source
QT_QPA_PLATFORM=offscreen python tools/smoke_screenshot.py   # renders every page
```

Remember that a text command claims the reader's host parser until that link
sends `RELEASE`, with no idle timeout. Close a port without sending it and the
next session on that link gets no replies at all, which looks exactly like dead
firmware. On the F429 Discovery, COM3 is the ST-LINK virtual COM standing in for
the RS232/RS485 host port and COM8 is the firmware's native USB CDC — both are
host links sharing one parser, so a TAGlink left running on COM3 is enough to
make COM8 answer nothing.

## Related documents

- [0020 — Bootloader and Signed Firmware Update](../design/0020-bootloader-and-signed-update.md)
- [How to build and send a firmware update image](how-to-build-a-firmware-update-image.md)
- [STM32 firmware dev guide](dev-guide-stm32-firmware.md)
- [TAGlink apps dev guide](dev-guide-taglink-apps.md)
