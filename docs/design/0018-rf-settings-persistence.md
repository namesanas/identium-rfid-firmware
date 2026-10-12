# 0018 — RF Settings Persistence, Gen2 and Buzzer Commands

## Scope

The RF settings changed with the host text commands now survive a reset. The three commands that used to answer `ERROR` unconditionally (`SET_BUZZER`, `SET_GEN2`, `SET_ANTENNAS`) now do real work. `GET_CAPABILITIES` reports what the board actually has, and the host-side protocol test builds again.

## What is saved where

| Command | Stored in | Restored at boot |
|---|---|---|
| `SET_POWER`, `SET_REGION` | Reader settings `rfValid` / `region` / `readPower` / `writePower`: the fields the vendor tools' static configuration already writes | Copied into `readerConfig` before the module is configured, as before |
| `SET_ANTENNA`, `SET_ANTENNAS` | Reader settings `rfAntennaValid` / `txAntenna` / `rxAntenna` (new, record version 4) | Copied into `readerConfig` before the module is configured |
| `SET_SESSION` | Configuration service record (`session`), the same field the Identium configuration commands use | Applied after configuration, as before |
| `SET_GEN2` | Reader settings `gen2Valid` / `gen2Target` / `gen2Q` (new, record version 4) | Applied after the session; a module error here is not fatal, and the module keeps its own defaults |

A command answers `OK,<CMD>` only when the module accepted the value **and** it was saved. A save is skipped when the stored value is already the same, so repeating a command doesn't wear the flash. The replies keep the exact `OK,<CMD>` / `ERROR,<CMD>` form: the desktop app matches the whole tail of the line.

## Reader settings record version 4

`ReaderSettings_t` gains six bytes at the end. The previous layout is kept as `ReaderSettings_V3_t` and read by `ReaderSettings_LoadV3()`, which copies it over the defaults (new fields not valid) and makes the next save erase the sector, as the v1 and v2 loaders do. On the F429 bench a v3 record migrated with the network and WiFi settings intact.

## Commands

- **`SET_GEN2,<A|B|A/B>,<q>[,<profile>,<fast>,<hop>]`**
  - The target is sent to the module (0x9B, Gen2 parameter 0x01) as A = `01 00`, B = `01 01`, A/B flipping = `00 00`.
  - Q is dynamic (`-1`, option `00`) or static 0..15 (option `01`, value).
  - The desktop's RF profile, fast mode and hopping fields have no module setting behind them yet, so only 0 is accepted. Anything else answers `ERROR`, rather than being dropped silently.
- **`GET_GEN2`** reads target and Q back from the module: `GEN2,TARGET=<A|B|AB>,Q=<DYNAMIC|n>`. On the bench, B/4, A/B/dynamic and A/dynamic all round-tripped. The module's power-up value read A/dynamic.
- **`SET_ANTENNAS,<port>[,<port>...]`**: the reader inventories on one port, so the list must name one port (repeats allowed). The command then works like `SET_ANTENNA,p,p`. Lists with different ports answer `ERROR`, because multi-port switching is not implemented.
- **`SET_BUZZER,<ms>[,<tone>]`**
  - It sounds the board buzzer for 1..10000 ms. The buzzer is driven through `Industrial_IO_SetBuzzer()` and switched off by `GPIO_Service_Task()`, so no new main-loop task (and no new `GET_DIAG` field) was needed.
  - The F411 buzzer has a fixed tone, so `<tone>` is ignored. Boards without a buzzer (F429 Discovery) answer `ERROR`.
- **`GET_CAPABILITIES`** now reports:
  - `GPO=` as the number of outputs that exist (`Industrial_IO_GetOutputCount()`): 3 on the F411, 4 on the F429;
  - `BUZZER=0|1`;
  - `SET_GEN2=1`, which makes the desktop app send its Gen2 settings.
  The desktop's "Sound buzzer" button reports "This reader has no buzzer" when `BUZZER=0`.

## Host check

`hostcheck/identium_protocol_host_test.c` stubs now match `identium_protocol.c` (`ActiveUpload_Transport_*`). A stand-in device header (`hostcheck/stubs/stm32f429xx.h`) replaces CMSIS, whose inline ARM assembly can't build for a PC. `make hostcheck` builds and runs it with `HOSTCC` (default `gcc`), and CI runs it on every push.

## Verification

All of this was checked on the F429 Discovery bench with the module attached:

- debug and release builds pass with `-Werror` for both boards;
- every command above, including the invalid inputs, behaves as described;
- the values survived a hardware reset (`GET_POWER`, `GET_SESSION`, `GET_GEN2`);
- `GET_DIAG` showed no faults;
- the original values were restored afterwards.

Not yet verified: the buzzer, which exists only on the F411 board, and that board has not been run yet.
