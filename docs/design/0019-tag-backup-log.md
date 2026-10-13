# 0019 — Tag Backup Log in External SPI Flash (F411 board)

## Scope

The F411 custom board has a 4 MB GigaDevice GD25Q32E SPI NOR flash. Every tag read the reader reports is also stored there, so the latest ~130,000 reads survive host outages and resets. A host reads them back with the text commands below. The F429 Discovery has no such chip: the feature compiles to no-ops there (`BOARD_TAG_LOG_ENABLED 0`), and the commands report `ABSENT`.

Not yet tested on hardware: so far only the host-side simulation (below) has checked it.

## Hardware

| Signal | F411 pin | Note |
|---|---|---|
| SCK / MISO / MOSI | PA5 / PA6 / PA7 | SPI1, shared with the W5500 (mode 0, APB2/8) |
| Flash CS | PA1 | Driven high in `Board_F411_W5500_GPIO_Init()` before the first W5500 transfer |
| W5500 CS | PA4 | unchanged |

Sharing the bus needs no lock: the W5500 is polled and every SPI access, W5500 or flash, runs from the main loop. Program and erase run inside the chip after their command is sent, with CS released, so W5500 traffic carries on during a 45 ms sector erase.

## Files

- `Firmware/Devices/spi_flash/gd25q.c/.h`: driver. Init wakes the chip from deep power-down, checks JEDEC ID `C8 40 16` and clears any block protection bits. Program/erase calls only start the operation; `GD25Q_IsBusy()` polls WIP.
- `Firmware/Services/storage/tag_log.c/.h`: the ring log.
- `SILION_Application_PublishTag()` in `sillion_application.c`: the three tag paths (async inventory, single poll, sync poll) now report through one helper that feeds the Identium protocol, the ASCII tag line and `TagLog_Append()`. Tags passed straight through the vendor-tool module bridge are not logged: the firmware never decodes them.
- `TagLog_Task()` runs as loop task `TLOG` (shows as `TLOG_US` in `GET_DIAG`).

## Record format

32 bytes, little endian. 8 records per page, 128 per 4 KB sector, 131,072 slots.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | sequence number |
| 4 | 6 | time in ms: Unix time if the host has set the clock, else uptime |
| 10 | 1 | antenna |
| 11 | 1 | RSSI (signed) |
| 12 | 1 | read count |
| 13 | 1 | bits 0–5 EPC length in bytes; bit 6 EPC truncated; bit 7 Unix time |
| 14 | 16 | EPC (zero padded) |
| 30 | 2 | CRC-16/CCITT over bytes 0–29 |

EPCs up to 128 bits are stored whole. Longer ones keep their first 16 bytes and set the truncated flag.

## Ring rules

- A record with sequence `s` always lives in slot `s % 131072`. That number divides 2³², so the mapping keeps working when the 32-bit counter wraps. A read checks the stored sequence, so it can never return a record from an older lap.
- The sector after the write head is always erased. On entering a new sector, `TagLog_Task()` first starts erasing the next one, so the log keeps at least 1022 full sectors = **130,816 records** (up to 130,944).
- Appends go into a 64-record RAM queue (2 KB) and never block. The task programs up to one page per call. While an erase is running, reads wait in the queue. If the queue is full, the read is counted in `DROPPED`.

## Boot (mount)

1. Read the first intact record of each of the 1024 sectors. The sector whose first sequence is newest (serial-number comparison) holds the write head. Scan it for its newest record; the next sequence follows it.
2. A slot torn by a power cut (fails CRC, not erased) can't be programmed again. Skip forward to the next erased slot of that sector.
3. Make sure the sector after the head is erased, which repairs an erase cut short by a power cut. If the head sits at a sector start, check that sector too.
4. The oldest record is the first intact record found walking forward from the head.

A chip with no valid records (new or holding other data) mounts empty at sequence 0 and erases sectors just ahead of the writes. Mount reads about 64 KB, so it takes well under a second. A chip that doesn't answer leaves the log `ABSENT`, and the reader boots normally.

## Host commands (text protocol, any host link)

| Command | Reply |
|---|---|
| `TAGLOG_STATUS` | `TAGLOG,STATUS,<READY\|CLEARING\|ABSENT>,OLDEST=<seq>,NEXT=<seq>,COUNT=<n>,CAPACITY=<n>,PENDING=<n>,DROPPED=<n>` |
| `TAGLOG_READ,<seq>,<count>` | Up to 20 lines `TAGLOG,<seq>,<sec>.<ms>,<E\|U>,<ant>,<rssi>,<reads>,<EPC hex>[,TRUNC]`, then `OK,TAGLOG_READ,NEXT=<seq>`. `E` = Unix time, `U` = uptime of the boot that stored it. Sequences that were overwritten or never written are skipped. Bad arguments: `ERROR,TAGLOG_READ` |
| `TAGLOG_CLEAR` | `OK,TAGLOG_CLEAR`, then a chip erase runs in the background (state `CLEARING`, several seconds). Sequence numbers carry on. `ERROR,TAGLOG_CLEAR` if absent |

To fetch everything: `TAGLOG_STATUS`, then `TAGLOG_READ,<OLDEST>,20` repeatedly, using each reply's `NEXT`, until `NEXT` reaches the status `NEXT`. A host that keeps a cursor should treat a status `NEXT` below its cursor as a cleared log. After `TAGLOG_CLEAR` and a reset with no new tags, numbering restarts at 0.

## Testing

`make hostcheck` builds `hostcheck/tag_log_host_test.c` with `tag_log.c` against a RAM model of the flash. The model enforces NOR rules: no programming over unerased bytes, no crossing a page, and no reads while busy. The tests cover:

- an empty chip
- a reboot
- 300,000 appends with the log wrapped, checking every retained record
- a reboot after the wrap
- a record torn mid-program, both mid-sector and as a sector's first record
- an erase cut short
- a chip full of random data
- a clear
- queue overflow
- a missing chip

On the F411 the log adds ~2.3 KB of RAM.
