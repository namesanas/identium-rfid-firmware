# 0020 — Bootloader and Signed Firmware Update

## Scope

Readers can be updated in the field over any host link (USB CDC, host serial port, TCP 5000, WiFi) without SWD. The new firmware only runs if it is signed with the Identium signing key. A small bootloader in flash sector 0 checks the signature and installs the image. Together with read protection (`make protect`, [0017](0017-fault-handling-and-runtime-diagnostics.md)), this means a fielded reader only ever runs firmware we built and signed.

Not yet tested on hardware. So far it has been checked only by the host tests (below) and by builds of all four board/config combinations.

## Flash layout

The application no longer starts at `0x08000000`. Everything is defined in `Firmware/Common/firmware_image.h`.

| | F429 (2 MB) | F411 (512 KB + 4 MB SPI) |
|---|---|---|
| Bootloader | sector 0, 16 KB | sector 0, 16 KB |
| Application | sectors 1-11 from `0x08004000`, linker limit 624 KB | sectors 1-4 from `0x08004000`, 112 KB |
| Update staging | sectors 12-20 (`0x08100000`, 640 KB) | GD25Q32 `0x200000`-`0x3FFFFF` (2 MB) |
| Settings / configuration | sectors 21 / 22-23 (unchanged) | sectors 5 / 6-7 (unchanged) |

- The F411 has no spare internal flash, so its staging area is the upper half of the SPI flash. The tag log keeps the lower 2 MB, which holds about 65,000 reads instead of 130,000 ([0019](0019-tag-backup-log.md)). An F411 without the SPI flash cannot be updated in the field (`ERROR,FW_BEGIN,NO_STAGING`).
- The F411 application area shrinks from 128 KB to 112 KB. A release build is 92.6 KB (81%); a debug build is 108.9 KB (95%).
- `SystemInit()` sets `VTOR` to `0x08004000` (`Src/system_stm32f4xx.c`), and the bootloader sets it again before the jump.

## Image format (`.tlfw`)

A 256-byte header followed by the application `.bin`. All fields are little-endian.

| Offset | Field |
|---|---|
| `0x00` | magic `TLFW`, header version 1, header size 256 |
| `0x08` | board: MCU device ID (`0x419` F429, `0x431` F411) |
| `0x0C` | load address `0x08004000` |
| `0x10` | binary size |
| `0x20` | version text (32 bytes, NUL-terminated) |
| `0x40` | SHA-256 of the binary |
| `0x80` | ECDSA P-256 signature (r ‖ s) over SHA-256 of bytes `0x00`-`0x7F` |
| `0xC0` | state word: written by the bootloader, not signed |

State values: `FFFFFFFF` PENDING, `00000000` INSTALLED, `0000BAD1` BAD_HEADER, `0000BAD2` BAD_HASH, `0000BAD3` BAD_SIGNATURE. Each can be programmed over the erased value without an erase.

## Update sequence

1. The host sends the image with `FW_BEGIN` / `FW_DATA` / `FW_END` (below). The application writes the binary to staging, erasing each sector just before it is first used. The header is kept in RAM.
2. `FW_END` checks the header (format, board, load address, size) and the SHA-256 of the staged binary. It does not check the signature: that is the bootloader's job, and putting micro-ecc in the application as well would not fit the F411 debug build.
3. `FW_APPLY` writes the header to staging (state PENDING), reads it back, replies, and resets after 500 ms. Because the header is written last, an upload that is interrupted at any point leaves nothing for the bootloader to act on.
4. The bootloader (`Bootloader/boot_main.c`) runs on every reset:
   - Staging holds a PENDING image: it checks the header, then the SHA-256, then the signature against the public key compiled into it (`Bootloader/boot_public_key.c`).
   - Valid: it erases the application sectors the image needs and copies the binary, leaving the first 256-byte block (the vector table) for last. Once the SHA-256 of the copy matches, it writes that block and marks the image INSTALLED. If power is lost before that mark, the copy repeats on the next reset, since the image is still intact in staging.
   - Until the vector table is written, the application area cannot start. An interrupted or failed install therefore never leaves a half-copied application that could run, even if staging cannot be read on the next reset (for example, an F411 whose SPI flash does not answer).
   - An install that fails is retried up to three times. After that, the bootloader does not start the application and waits. If the watchdog is running, it resets the board and the install is tried again. Otherwise the board waits for a power cycle.
   - Invalid: it records the reason in the state word. The application is left untouched.
   - Then it checks the application's vector table (stack pointer in RAM, reset handler inside the application area) and jumps to it. With no valid application it waits; if the watchdog is running, it resets the board and the bootloader tries again.
5. After the restart, `FW_STATUS` reports the bootloader's verdict.

The bootloader:
- Runs on the 16 MHz HSI as left by reset, enables no interrupts and uses no HAL.
- Reloads the watchdog during long erases, because a software reset does not stop it.
- Leaves `RCC->CSR` alone, so `GET_DIAG` still reports the real reset cause.
- Uses only the top 8 KB of RAM, so the application's `.noinit` fault record survives.
- Never writes sector 0 or the settings sectors. The bootloader itself can only be changed over SWD.

## Host commands

All replies end in `\r\n`. Inventory is stopped by `FW_BEGIN`.

| Command | Reply |
|---|---|
| `FW_STATUS` | `FW,STATE=IDLE\|RECEIVING\|READY,RECEIVED=<n>,BOOT=1\|NONE,LAST=NONE\|PENDING\|INSTALLED\|BAD_HEADER\|BAD_HASH\|BAD_SIGNATURE,VERSION=<v>` |
| `FW_BEGIN,<bytes>` | `OK,FW_BEGIN,CHUNK=128` |
| `FW_DATA,<offset>,<hex>` | `OK,FW_DATA,<bytes received>` |
| `FW_END` | `OK,FW_END,<version>` |
| `FW_APPLY` | `OK,FW_APPLY`, then a reset |
| `FW_ABORT` | `OK,FW_ABORT` |

Errors are `ERROR,<command>,<reason>`:
- `FW_BEGIN`: `NO_BOOTLOADER`, `NO_STAGING`, `SIZE`, `INVENTORY`, `FORMAT`.
- `FW_DATA`: `OFFSET`, `SIZE`, `SEQUENCE`, `FLASH` or `FORMAT`, followed by the offset the reader expects next. A chunk that has already been received (a retry after a lost reply) is answered `OK`.
- `FW_END`: `HEADER`, `HASH` or `SEQUENCE`.
- `FW_APPLY`: `SEQUENCE` or `FLASH`.

`LAST` describes the most recent image in staging. Once an upload starts, it reads `NONE` until `FW_APPLY`.

## Tools and build

- `make` now also builds the bootloader: `build/boot-<board>/bootloader.elf`, about 6 KB.
- `make flash` programs the bootloader and the application. Every board needs this once, because older firmware has the application in sector 0. `make flash-app` programs only the application.
- CubeIDE still builds the application (same linker scripts), but it only flashes the application. Run `make flash` (or `make bootloader` and program `bootloader.elf`) once per board first.
- `make image` creates the signed `build/<...>/<target>-<git describe>.tlfw`.
- `Tools/fw_sign.py`:
  - `genkey`: creates `keys/fw_signing_key.pem` and `Bootloader/boot_public_key.c`.
  - `sign`: signs a `.bin`.
  - `info <image> [--public-c Bootloader/boot_public_key.c]`: prints an image header and checks its signature.
  - Needs `python -m pip install cryptography`.
- `Tools/fw_update.py --port COM3 | --tcp <ip> <image.tlfw>`: runs the whole sequence and waits for the verdict. `--status` only prints `FW_STATUS`. Over a 115200-baud serial link a 90 KB image takes roughly 20 s.

## Signing key

The private key `STM32_Firmware/keys/fw_signing_key.pem` is created on the development PC. `.gitignore` excludes it (`STM32_Firmware/keys/`, `*.pem`), and it is **never committed**. **Keep a backup somewhere safe.** If it is lost, field updates are no longer possible: every reader then needs SWD to get a bootloader with a new key, and with read protection set that also means a mass erase. If it leaks, anyone can sign firmware our readers will accept.

The public half in `Bootloader/boot_public_key.c` is committed.

## Not covered

- No rollback protection: any correctly signed image installs, including an older one.
- No automatic fallback: if a signed image installs but then misbehaves, the reader keeps running it until a fixed image is sent. Verified signatures rule out corrupted images, but not buggy ones.
- The bootloader checks the signature only when installing, not on every boot. With read protection level 1, flash cannot be rewritten over SWD without a mass erase, so the installed image can only come through this path.
- Not yet in TAGlink Desktop / Mobile; `Tools/fw_update.py` is the host side for now.

## Host tests (`make hostcheck`)

- `hostcheck/make_test_images.py`: generates a throwaway key and signs one test image per board with `Tools/fw_sign.py`.
- `hostcheck/boot_image_host_test.c`, built once per board, runs the bootloader's `BootImage_Check()` on those images:
  - Checks SHA-256 against known test vectors.
  - Accepts the image as signed.
  - Rejects with the right reason: a changed byte in the binary, the version text, the hash or the signature; a wrong board, load address or size; a truncated image.
  - Ignores images that are already handled and erased staging.
- `hostcheck/firmware_update_host_test.c` runs the F411 receiver on a NOR-rule model of the GD25Q32:
  - Sequencing errors, offsets, lost-reply retries and odd chunk sizes.
  - Header written last: an interrupted upload leaves nothing for the bootloader.
  - A second upload over an installed image, a corrupted upload (`HASH`), an image for the other board.
  - The staged result passes the bootloader's check.
  - The tag-log half of the flash is never touched.
- `hostcheck/tag_log_host_test.c` checks the other direction: clearing the tag log (`TAGLOG_CLEAR`) erases only its own 2 MB in 64 KB blocks and never erases the whole chip, so a staged image survives.
