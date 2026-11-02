# How to build and send a firmware update image (.tlfw)

A `.tlfw` is the application firmware plus a 256-byte signed header. A reader
installs one only if it is signed with the Identium key, so this is the only
way to update a fielded reader without opening it up and using SWD. The format
and what the bootloader does with it are in
[0020 — Bootloader and Signed Firmware Update](../design/0020-bootloader-and-signed-update.md).

All paths below are relative to `STM32_Firmware/`.

## Before the first time

- The private key must exist at `keys/fw_signing_key.pem`. It is **not** in the
  repository (`keys/` is gitignored), so it lives only on the machines you have
  copied it to. Without it you cannot sign anything.
- `python -m pip install cryptography` (for signing) and `pyserial` (for sending).
- Every reader needs `make flash` over SWD **once**, to get the bootloader into
  sector 0. Readers running firmware older than that have the application at
  `0x08000000` and cannot be updated over the link. `FW_STATUS` answers
  `BOOT=NONE` on such a reader, and TAGlink says so on its Firmware page.

## The normal way: one command

Open **Git Bash** — not PowerShell and not cmd. The Makefile uses `find`, and
PowerShell resolves that to Windows' `find.exe`, which silently builds only the
startup file.

```sh
cd /c/Users/Identium/Downloads/RFID_Controller_F429_IDENTIUM_PROTOCOL_STEP8/STM32_Firmware
make BOARD=f429 CONFIG=release VERSION=v0.11.0 image
```

The last line confirms it:

```
build/release/rfid_controller-v0.11.0.tlfw: f429 v0.11.0, 92252 bytes, signed
```

| | |
|---|---|
| F429 output | `build/release/rfid_controller-<version>.tlfw` |
| F411 | `make BOARD=f411 CONFIG=release VERSION=v0.11.0 image` → `build/f411-release/` |
| Version from git | leave `VERSION=` out and it uses `git describe`, e.g. `v0.10.1-2-gc3c7d58` |

Always use `CONFIG=release`. A debug build is `-O0`: 159 KB instead of 92 KB on
the F429, and about 109 KB of the F411's 112 KB application area.

The version string is only a label. Nothing checks it, and there is no rollback
protection — a reader will install an older image just as happily as a newer
one — so make it mean something.

## Checking an image before you send it

```sh
python Tools/fw_sign.py info build/release/rfid_controller-v0.11.0.tlfw \
    --public-c Bootloader/boot_public_key.c
```

```
magic        ok
format       1, header 256 bytes
board        f429
load address 0x08004000
version      v0.11.0
size         92252
state        PENDING
sha-256      ok
signature    ok
```

`signature ok` means the image was signed with the key whose public half is
compiled into the readers' bootloaders. If it says anything else the reader
will refuse the image, and it is better to find that out here.

## Signing a build you made some other way

`make image` is objcopy plus a signing call. If you already have an `.elf` —
from STM32CubeIDE, say — do those two steps yourself:

```sh
arm-none-eabi-objcopy -O binary Debug/RFID_Controller_F429.elf Debug/RFID_Controller_F429.bin

python Tools/fw_sign.py sign --key keys/fw_signing_key.pem --board f429 \
    --version "v0.11.0" Debug/RFID_Controller_F429.bin Debug/RFID_Controller_F429.tlfw
```

Use `--board f411` for the F411. The signer refuses a binary whose reset vector
is outside the application area, which catches an image linked for
`0x08000000` instead of `0x08004000`.

### From STM32CubeIDE

CubeIDE builds the application with the same linker scripts, so its output is a
valid image, but it produces only an `.elf` and knows nothing about signing.
To have it produce a signed image on every build:

1. Project → Properties → C/C++ Build → Settings → **MCU/MPU Post build
   outputs** → tick **Convert to binary file (-O binary)**.
2. Same dialog, **Build Steps** tab → Post-build steps → Command:

   ```
   python ../Tools/fw_sign.py sign --key ../keys/fw_signing_key.pem --board f429 --version "${BuildArtifactFileBaseName}" "${BuildArtifactFileBaseName}.bin" "${BuildArtifactFileBaseName}.tlfw"
   ```

   Post-build commands run from the build output folder (`Debug/`), hence the
   `../`. Set this per configuration and change `--board` for the F411 one.

For anything you actually ship, prefer `make image`: CubeIDE's configurations
are debug builds, and the Makefile is what the release process uses.

## Sending it to a reader

### TAGlink Desktop

Firmware page in the left rail → **Choose file…** → **Send update**. It checks
the file before sending, shows progress, waits for the reader to restart and
then reports what the bootloader decided. The reader keeps running its old
firmware throughout the upload and restarts once at the end.

### Command line

```sh
python Tools/fw_update.py --port COM8 build/release/rfid_controller-v0.11.0.tlfw
python Tools/fw_update.py --tcp 192.168.1.50 build/release/rfid_controller-v0.11.0.tlfw
python Tools/fw_update.py --port COM8 --status
```

A 90 KB image takes about 5 s over USB CDC and roughly 20 s over a
115200-baud serial link, where the hex encoding doubles the bytes on the wire.

## If it does not install

The reader answers `FW_STATUS` with `LAST=` after the restart:

| `LAST=` | What happened |
|---|---|
| `INSTALLED` | Done; the reader is running the new firmware. |
| `BAD_SIGNATURE` | Not signed with the Identium key, or altered after signing. |
| `BAD_HASH` | The image was damaged in transfer. Send it again. |
| `BAD_HEADER` | Wrong board, wrong load address, or not a `.tlfw`. |

In every failing case the application is left untouched and the reader keeps
working on its previous firmware.

Errors during the upload itself come back as `ERROR,<command>,<reason>`:
`NO_BOOTLOADER` (needs `make flash` over SWD once), `NO_STAGING` (an F411 with
no SPI flash), `INVENTORY` (stop the inventory first), `HASH`, `SEQUENCE`,
`FLASH`.

One link-level trap, not specific to updates: a text command claims the
reader's host parser until that link sends `RELEASE`, with no idle timeout. If
a program closes its port without sending `RELEASE`, the next session on that
link gets no replies at all and looks like dead firmware. TAGlink's Disconnect
button sends it; `fw_update.py` sends it when it finishes.

## Keep the key safe

`keys/fw_signing_key.pem` is never committed. **Back it up somewhere safe.**

- Lose it and field updates stop being possible: every reader would then need
  SWD access to get a bootloader carrying a new key, and with read protection
  set that also means a mass erase.
- Leak it and anyone can sign firmware your readers will accept.
