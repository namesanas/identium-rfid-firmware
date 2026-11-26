# IDM-AUD-004 — Firmware Rev 3 Re-Audit

**INB1 controller board firmware, revision 3 (271026 delivery, received 27 October 2026)**

| | |
|---|---|
| **Date** | 2 November 2026 |
| **Scope** | Firmware only: STM32_Firmware.zip (bootloader, application, tools, host tests, build outputs). The Gerbers, pick-and-place and design justification in the same drop are byte-identical to the 231026 package, so the hardware conclusions of the 231026 board check stand. |
| **Baselines** | IDM-AUD-002 (6 Oct 2026, firmware rev 2: 130 findings carried forward) |
| **Decision requested** | Load on the 10 Lot 1 prototype boards? Release for pilot units that leave Identium? |
| **Classification** | Confidential — internal use only |

## Decision

# 🟠 LOT 1 BENCH USE ONLY. NOT FOR PILOT

**GO for loading on the 10 internal prototype boards for bench bring-up only. NO-GO for pilot units that leave Identium. Rev 3 is the first build for the real STM32F411 board, with the right overall design. Three problems block any pilot: (1) the production signing key is exposed and the network update path has no authentication; (2) the offline buffer neither reaches 100k tags nor replays to any Identium host protocol; (3) the bootloader cannot be changed once shipped, and it has no rollback and a 112 KB application ceiling. Conditions for lot 1: (a) Identium generates its own signing key and rebuilds the bootloader and image from a clean checkout. (b) Program by SWD, and never use the images in F411/ or Debug/ (F411/RFID_Controller_F429_f411.map:6978 links at 0x08000000 and overwrites the bootloader). (c) Leave RDP at level 0 so SWD recovery stays possible. (d) Keep the boards on an isolated bench network. (e) Until the region code is corrected (FW-02), run RF tests conducted or at reduced power.**

## Executive summary

Revision 3 is the first firmware that actually builds for Identium's own controller board. It adds most of the missing building blocks: a memory chip for storing tag reads, a secure-update mechanism, support for the Reader Manager tool, a web (HTTP) upload mode, and much better handling of faults at start-up. It is fit to load on the 10 internal prototype boards for bench testing, but it must not go on pilot units that leave Identium. The most serious problem is that the design house shipped the secret key that approves firmware updates inside the delivery zip, and the update command needs no password, so anyone on a customer's network could install their own software on every reader. The feature this board exists for still does not work: reads that cannot be sent within about 8 seconds are thrown away, the backup copy in the memory chip is never re-sent automatically, and the chip holds about 65,000 reads instead of the 100,000 tags required. Updates also have no safety net: one bad release would leave readers restarting endlessly and need a site visit to each, and the small update program on each unit cannot be replaced after shipping, which also caps how large future firmware can be. Modbus, Wiegand, RS-485, Wi-Fi (the Wi-Fi chip's own software was not delivered) and Bluetooth are still missing or unusable. Of 130 earlier findings, 6 are fixed, 64 partly fixed, 53 not fixed and 7 got worse; this audit confirmed 108 new candidate findings (merged in this report into the distinct issues below), and before any pilot Identium must hold its own update key and the design team must fix offline buffering and the update safety net before the update program is locked into shipped units.

### What the lead auditor measured directly

- Signing key: keys/fw_signing_key.pem in the zip is an unencrypted ECDSA P-256 private key. Its public half, derived with openssl, is identical to g_bootPublicKey in Bootloader/boot_public_key.c. Anyone who has the zip can therefore sign firmware that every bootloader built from this tree will accept. The key is referred to here by path only.
- Host tests: all five suites were built from source in a separate directory with Apple clang, and all pass. They are identium_protocol, tag_log (F411), boot_image (F429 and F411, signed with the test key) and firmware_update. The tag_log run gives "after 300000 tags: oldest=234624 next=300000 kept=65376", which is the capacity measured in BUF-02. The protocol run gives "http slow server: 2000 read, 0 uploaded, dropped 1744, expired 256", which is the loss in BUF-01 and PRO-02.
- Tag log arithmetic: TAG_LOG_FLASH_BYTES = GD25Q_SIZE_BYTES / 2 = 2 MB, with 32-byte records and 128 per 4 KB sector. Capacity = (512 - 2) × 128 = 65,280 records, plus the partly filled head sector, which matches the measured 65,376. The upper 2 MB is FW_STAGING_SIZE (0x00200000) for an application capped at 112 KB.
- Flash second source: gd25q.c:178-184 rejects any part unless the manufacturer ID is 0xC8 (GigaDevice) and the capacity byte is 0x16 (32 Mbit). Winbond W25Q32JV (0xEF) and W25Q128JV are rejected, so the Option B flash alternates in RFP IDM-RFP-001 need a firmware change before they can be accepted.
- ESP32-C3: no ESP32-side firmware or ESP-IDF project is in the delivery. Firmware/Devices/esp32/esp32_link.h itself says the link is optional on the F411 build because the documented GPIOs "collide with the production UART/input assignments".
- Build: F411 images are now produced, which fixes FW-10, the rev 2 headline. The app is about 92.6 KB in a 112 KB slot and RAM use is about 116.6 KB of 128 KB. The shipped update image is rfid_controller_f411-v0.10.1-1-gfcae054-dirty.tlfw, built from a dirty git tree.
- Method: 13 re-check agents went through all 130 rev 2 findings, and a skeptic challenged every FIXED claim. 9 review dimensions plus a completeness critic raised 108 new candidates. Each was checked by three verifiers with different lenses: 108 were confirmed by at least 2 of 3, 0 refuted and 0 left open. Duplicates across dimensions are merged here into 79 findings.

## Findings at a glance

| Category | Count | Meaning |
|---|---:|---|
| **BLOCKS PILOT** | 17 | Must be fixed before this firmware goes on any unit that leaves Identium |
| CRITICAL | 4 | Compromises every unit, or defeats the core goal (offline buffering, host protocols, field update) |
| HIGH | 11 | Blocks the pilot, or a required function is missing or broken |
| MEDIUM | 41 | Real defect with a workaround |
| LOW | 21 | Minor or quality |
| INFO | 2 | Observation |

| Rev 2 finding status in rev 3 | Count | Meaning |
|---|---:|---|
| FIXED | 6 | Defect gone; every FIXED claim survived an independent skeptic |
| PARTIAL | 64 | Improved, but something real remains |
| NOT FIXED | 53 | Unchanged |
| REGRESSED | 7 | Worse than in rev 2 |

## Scorecard

| Area | Rev 2 | Now | Rationale |
|---|:--:|:--:|---|
| Firmware | 4/10 | **5/10** | Rev 3 is the first drop that is product firmware for the right chip. It builds for the STM32F411CEU6 with an 8 MHz HSE/PLL clock (Src/main.c:168-189) and links behind a bootloader at 0x08004000 (build/f411-release/rfid_controller_f411.map:1447). It adds 58 new files (work/rev2_rev3_filelist.txt): an SPI-flash tag log, a signed-update bootloader, a Reader Manager bridge, HTTP upload, UDP discovery and system monitors. Six earlier findings are fixed (FW-03, FW-04, FW-10, FW-27, FW-56, R2-42). It does not score higher for several reasons. Three earlier findings are still critical (FW-01, FW-17, FW-37) and twelve still high. Seven regressed: for example FW-02, where the default region is still code 0x08 (product_config.h:45, silion_protocol.h:87). The blocking main loop is unchanged (FW-34). The audit confirmed 108 new findings, 7 critical and 16 high. Headroom is thin: about 92.6 KB of a 112 KB slot and about 116.6 KB of 128 KB RAM are used (LEAD_FACTS.md). |
| Requirements coverage | 3/10 | **4/10** | On the real board, several required functions moved from absent to partial: offline flash store, signed field update, 3 relays and 4 GPIs, native USB, RS-232, saved settings, discovery and an HTTP mode. None of the requirements that define the product is met. The tag store holds at most 65,280 records (tag_log.c:40-48; lead measured 65,376) against the >=100k goal (FW-001 §5 p.7 targets 500k), and it is never drained to a host. Field update has no trial boot or rollback (boot_main.c:141-153), which FW-001 §8.1 steps 4-6 require. Modbus, the TCP V1.0 binary protocol, Wiegand, runtime RS-485 (product_config.h:39-43), Wi-Fi (no ESP32 firmware delivered), BLE and branding are all missing. No source file mentions Wiegand, and board_config.h:20 says PC14/PC15 are not connected, while the netlist puts WGD0/WGD1 on them (u7_pinmap.txt:5-6). On the FW-001 §13 review checklist (pp.13-14), items 1, 3, 5, 6, 7, 14 and 16 fail outright. |
| Host-protocol compatibility | 2/10 | **3/10** | Reader Manager compatibility is genuinely new. TCP 8080 carries 0xEE vendor configuration and a 0xFF module bridge (configuration_port.h:7, module_bridge.h:7-9), and UDP 15000 discovery exists (discovery_service.c:10). The Identium host protocols still cannot do their job. Queued tags are thrown away after the 8 s default (identium_protocol.c:415-445, configuration_storage.c:342), and the flash copy can only be read with the legacy ASCII command TAGLOG_READ (host_interface.c:229-300). HTTP accepts only http:// with a literal IPv4 address (http_upload.c:108). It frees a batch whatever status the server returns and gives up after 3 attempts (http_upload.c:588-595, 779-784). Discovery replies only to the sender and reports an HC32F46x board (discovery_service.c:53, 100). Modbus RTU/TCP and the TCP V1.0 binary protocol are absent. Any client on 8080 that does not open with 0xEE, a Modbus poller for example, takes over the RFID module (configuration_port.c:293). FW-09 and FW-16 remain high. |
| Robustness & reliability | 3/10 | **4/10** | Start-up is far more forgiving. The watchdog is armed before any peripheral, and parts that fail to start leave the reader running degraded instead of halted (firmware_configuration.c:290-305). The fault record and stack guard are set up first (Src/main.c:110-114). FW-06 drops to low and FW-56 is fixed. New failure modes offset much of this. The module-bridge idle timeout covers serial links only (module_bridge.c:673-680, 786-791), so an abandoned Reader Manager TCP session stops all reading until reboot. A module that hangs after boot is never detected, and its reset line is deliberately left undriven (board_config.h:36). TAGLOG_CLEAR erases the whole flash chip, including the update staging area (tag_log.c:534). A correctly signed image that fails at runtime makes the unit reboot over and over, with no fallback (boot_main.c:141-153). Settings sit in a single sector that is erased before the new copy is written, so a power cut at that moment loses them (ROB-02/03). The blocking main loop (FW-34) and module-driver defects FW-12 and FW-13 are unchanged. |
| Security | 3/10 | **2/10** | The design is right. A separate bootloader checks an ECDSA P-256 signature and re-hashes the installed image with SHA-256 (boot_main.c:85-89, 99), and a read-protection target exists (Makefile:197-199). As delivered, though, exposure is worse than in rev 2. The production private key ships in the zip (keys/fw_signing_key.pem). Its public half equals Bootloader/boot_public_key.c:11-16 (confirmed by the lead), and it is the Makefile's default signing key (Makefile:164). The update commands work over any host link with no credential (host_interface.c:751-762, 822-834). Rev 3 also re-opens the TCP 5000 command server (product_config.h:13, host_tcp_server.c:20-38), so any host on the LAN can install its own code on every unit. The same port again exposes tag kill/lock and region/power changes (FW-24 regressed to high). Vendor config stores any region code from 0 to 255 (vendor_config.c:1176-1183), and an open Wi-Fi AP is allowed (host_interface.c:101-103). There is no anti-rollback, no write protection and no mandatory RDP. The score is 2, not 1, only because the verification design is sound and replacing the key is mechanical. |
| Build & release hygiene | 2/10 | **4/10** | There are clear gains. A Makefile builds the F429 and F411 applications, the bootloader and a signed image (Makefile:109-170). Four host tests build and pass (LEAD_FACTS.md). Map files and release outputs are included, and the image name carries a git describe string. The release itself cannot be trusted. The signing private key is in the delivery and is the default key (Makefile:164). The pilot image was built from a dirty working tree (build/f411-release/rfid_controller_f411-v0.10.1-1-gfcae054-dirty.tlfw). Neither the git history nor the design note the code cites (host_interface.c:751, docs/design/0020) was delivered. About 276 MB of build output ships (build 152 MB, Debug 77 MB, F411 47 MB). It includes a 12-Oct F411 image linked at 0x08000000 that would overwrite the bootloader (F411/RFID_Controller_F429_f411.map:6978). The binary always reports version 1.0.0.8 (product_config.h:52-56). There is no production programming package and no ESP32 image. The tests pass even though their own output shows the 65,376-record shortfall. |

## Verdicts the lead auditor corrected

- SEC-01 impact narrowed. The finding said shipped units "trust this key permanently until re-flashed over SWD". Sector 0 has no write protection and the application can erase and program internal flash, so a signed application could rewrite the bootloader in the field. That cuts both ways: it is an attack path for anyone holding the leaked key, and a risky recovery route for Identium. For boards still in-house, the fix stays SWD re-flash with a new Identium-held key.
- BUF-02 target restated. FW-001 §5's 500k-record figure assumed a 64 MB QSPI part. The fitted 4 MB GD25Q32 holds at most 131,072 records of 32 bytes, so 500k is a hardware and spec mismatch, not a firmware bug. The firmware is judged against the 100k core goal, which it can meet on the existing chip once staging shrinks and the power-of-two slot mapping is replaced.
- BUF-09 impact narrowed. A non-GigaDevice flash does not silently disable firmware update: FW_BEGIN returns an error. Only the tag log fails silently (BUF-10). Units built with the specified GD25Q32ESIG are not affected. Lead-auditor addition: this is why the Winbond Option B alternates in RFP IDM-RFP-001 cannot be accepted without a firmware change.
- BUF-01 loss window tightened. Reads are lost after about 8 s of outage (clear_buf_time default), not only after 256 queued reads, because stale reads are purged every loop pass whether or not the link is up.
- Counts. The synthesis counts 108 raw new findings, 7 of them critical. Several defects were raised by more than one review dimension (for example the leaked key under both SEC and BLD, and TAGLOG_CLEAR four times). This report merges those duplicates, so the totals here are lower.

## Blocking Findings: These Stop a Pilot Release

A finding blocks the pilot when it must be fixed before this firmware runs on any unit that leaves Identium. Loading it on the internal Lot 1 boards for bring-up is still fine, provided the leaked key is never used to sign a pilot image.

- [`SEC-01`](#sec-01) **Production firmware-signing private key ships inside the delivery zip; shipped bootloaders trust it and shipped images are signed with it**
- [`BUF-01`](#buf-01) **The offline buffer is not store-and-forward: the flash tag log is never drained to any host protocol, and the live path drops reads after 8 s or 256 reads of backhaul loss**
- [`BUF-02`](#buf-02) **The flash tag log keeps only 65,280 to 65,408 records (65,376 measured), below the 100k requirement: half the 4 MB chip is reserved to stage a 112 KB image**
- [`RFM-01`](#rfm-01) **No duplicate suppression or roll-up: every raw read goes into the flash log, so a tag in the field fills the 65k log in minutes**
- [`SEC-02`](#sec-02) **Rev 3 adds unauthenticated network write paths: firmware update, tag-log erase, tag KILL/LOCK, RF and Wi-Fi settings, including a re-opened legacy ASCII server on TCP 5000**
- [`SEC-03`](#sec-03) **No trial boot, confirmation or rollback: a validly signed image that fails at runtime boot-loops the unit and cannot be updated again remotely**
- [`PRO-02`](#pro-02) **HTTP active mode treats 4xx/5xx/3xx replies as delivered and silently drops a batch after 3 failed attempts**
- [`RFM-02`](#rfm-02) **The region from vendor static configuration is saved without validation, acknowledged before it is applied, and sent to the module at every boot, bypassing the SET_REGION whitelist**
- [`RFM-03`](#rfm-03) **Any module-bridge session stops local reading and buffering, and a TCP bridge session is never timed out; a stray 0xFF byte on serial/USB, or any non-0xEE first byte on TCP 8080, opens one**
- [`RFM-04`](#rfm-04) **An RFID module reset or hang at runtime is never detected or recovered, and the module reset line (CN1.11 on PB5) is never driven**
- [`NET-01`](#net-01) **The ESP32-C3 coprocessor firmware is not in the delivery, so Wi-Fi cannot work; BLE is not designed at all**
- [`REQ-02`](#req-02) **The immutable bootloader freezes the F411 application at 112 KB (83% used) while 384 KB of MCU flash sits unused, and there is no way to update the bootloader**
- [`REQ-04`](#req-04) **Modbus RTU/TCP, one of the three required Identium host protocols, is absent, and a Modbus-TCP master on port 8080 stops inventory**
- [`REQ-05`](#req-05) **HTTP active mode does not follow the reader's published active-mode format or FW-001: http:// only, literal IPv4 hosts only, non-standard fields, no charset**
- [`BLD-03`](#bld-03) **Release is not reproducible or traceable: built from a dirty working tree, commit and history not delivered, referenced design docs missing, toolchain undocumented**
- [`BLD-04`](#bld-04) **No production programming package or procedure: flashing rebuilds from source and defaults to the debug build, the bootloader sector is not write-protected, and ESP32-C3 firmware, serial-number and end-of-line steps are missing**
- [`BLD-05`](#bld-05) **Host tests check the implementation rather than the requirements, and the code that can brick a unit (bootloader install, staging reads, settings migration) has no test or on-target evidence**

## Must be fixed, in order

1. Treat keys/fw_signing_key.pem as compromised. Identium should generate a new production key on an Identium-controlled machine, keep it out of every source tree and zip, and rebuild the bootloader with the new public key (Bootloader/boot_public_key.c:11). The design house deletes its copies, and nothing signed with the leaked key goes on any unit that leaves Identium (SEC-01/BLD-01).
2. Require authentication for every write or control command on network, Wi-Fi and serial links: firmware update, TAGLOG_CLEAR, tag kill/lock, region/power, Wi-Fi, IP/MAC and the module bridge (host_interface.c:751-834). Alternatively, close TCP 5000 by default (product_config.h:13). Forbid an open Wi-Fi AP (SEC-02, PRO-03, SEC-11).
3. Make offline buffering work end to end. After reconnect, drain the flash log automatically into the configured Identium protocol, oldest first, and advance only on server acknowledgement. Stop the 8 s discard (identium_protocol.c:415-445, configuration_storage.c:342) from losing reads, and make HTTP retry non-2xx replies instead of dropping the batch (http_upload.c:588-595, 779-784) (BUF-01, PRO-01, PRO-02, NET-02, REQ-01).
4. Meet capacity in tags, not raw reads. Add per-tag roll-up with first-seen, last-seen and read count in Unix ms (FW-001 §5.1), and lift the 65,280-record ceiling (tag_log.c:40-48). That means a larger flash part, with driver and bootloader support for non-GigaDevice IDs (gd25q.c:179-180), and/or reclaiming the 2 MB staging half (BUF-02, BUF-03, RFM-01, BUF-09).
5. Fix the bootloader before it is frozen into shipped units. It needs trial boot, application check-in and automatic rollback (FW-001 §8.1 steps 4-6), a monotonic anti-rollback version, a key ID or rotation path, and verification of the application before jumping to it (boot_main.c:118-147). Re-partition flash so the application is not capped at 112 KB (firmware_image.h:91-95) (SEC-03/04/05/07, BLD-02, REQ-02/03).
6. Correct RF region compliance. The default code 0x08 is not the documented India code (FW-02; product_config.h:45, silion_protocol.h:87). Vendor static config stores any region from 0 to 255 and applies it at every boot (vendor_config.c:1176-1183, 1255-1263). Apply one whitelist on every path and get written sign-off from the compliance owner (RFM-02, REQ-07).
7. Make the reader recover without a site visit. Time out TCP module-bridge sessions (today only serial links time out: module_bridge.c:673-680). Detect a module that dies at runtime and re-initialise it, using the PB5 reset line (board_config.h:36). Make degraded-mode retries non-blocking, and add keep-alive or an idle timeout on port 5000 (ROB-01, RFM-03, RFM-04, RFM-06/ROB-05, ROB-07).
8. Deliver the missing required host protocols. Implement Modbus RTU and TCP (FW-001 §3.3, enabled by default) and the Identium TCP V1.0 binary protocol. Make HTTP mode follow the documented data format, including hostnames/HTTPS, the standard tag keys, reader_exception and sync_time_req. Stop non-vendor clients on 8080 from taking over the module (configuration_port.c:293) (REQ-04, REQ-05, PRO-04, PRO-09).
9. Deliver the ESP32-C3 firmware source, a build and a production flashing procedure. Either add BLE to the link protocol (esp32_link_protocol.h) or formally drop it, and provide a way to update the coprocessor in the field (NET-01, HWI-07, GAP-02).
10. Ship a reproducible, traceable release: a clean tagged commit with history, a version embedded in the binary and reported on every channel (not the fixed 1.0.0.8, product_config.h:52-56), and the bootloader/OTA design notes. Remove stale build outputs. Provide a production programming package (release image, write protection plus RDP, serial/MAC/test traceability) and on-target evidence, including power-cut tests, that the install path works (BLD-03/04/05/06/08).
11. Close the remaining hardware-interface gaps. Write Wiegand output firmware with a corrected pin map (board_config.h:20 vs u7_pinmap.txt:5-6) and review its 5 V and ESD interface. Make RS-485 selectable at run time (product_config.h:39-43). Store settings power-fail-safe in small sectors, and stop TAGLOG_CLEAR from erasing the staging half (tag_log.c:534) (HWI-01/02/03, ROB-02/03/04).

## What the team did well

- The firmware builds and links for the real STM32F411CEU6 board: F411 startup file, linker script and BSP, an 8 MHz HSE/PLL clock (Src/main.c:168-189), and an application at 0x08004000 behind the bootloader (build/f411-release/rfid_controller_f411.map:1447). Rev-2 blockers FW-10 and FW-27 are closed.
- Once Identium holds its own key, the secure-update design is a sound base. A separate 16 KB bootloader checks an ECDSA P-256 signature and SHA-256 hash in a signed header that carries the board ID and reject states, then re-hashes the image after copying it to internal flash (boot_main.c:48-116; firmware_image.h:36-51, 90-99).
- Offline storage now physically exists. The SPI-flash tag log uses 32-byte CRC-16 records, sequence numbers that survive counter wrap, and a ring that always keeps one sector erased ahead (tag_log.c:16-48). A host test drives it through 300,000 tags.
- Reader Manager tooling is supported for the first time: 0xEE vendor configuration and a 0xFF module bridge on TCP 8080 with session-takeover rules (module_bridge.h:7-9; module_bridge.c:52-64), plus UDP 15000 discovery (discovery_service.c:10).
- Start-up fault handling is much better. The watchdog starts before any peripheral, and failed parts degrade instead of halting the reader (firmware_configuration.c:290-305). A persistent fault record and stack guard are initialised first (Src/main.c:110-114), and brown-out and loop monitors are added (firmware_configuration.c:287; Src/main.c:130).
- Each unit now gets its own MAC address, derived from its MCU unique ID (reader_settings.c:210-238), in place of rev 2's shared multicast MAC. Network, server, Wi-Fi and RF settings are saved across reboots.
- Host-side tests exist and pass: protocol, tag log, image verification and update (LEAD_FACTS.md). The HTTP slow-server test is honest enough that it exposes the tag-drop defect.
- The HTTP client avoids command echo loops. Only a 2xx body that names a command_type is passed to the command parser (http_upload.c:597-623).

## Questions for the design team

1. Who generated keys/fw_signing_key.pem, on which machine, and where else do copies exist (CI, laptops, backups, e-mail)? Please confirm in writing when they are destroyed.
2. When will the ESP32-C3 firmware source, build and flashing procedure be delivered? Is BLE in scope for this board, or formally dropped?
3. Which Silion region code is approved for India 865-867 MHz on the module actually fitted, and who owns that compliance sign-off?
4. Will the bootloader be redesigned (trial boot, check-in, rollback, anti-rollback version, key ID, larger application slot) before any unit is programmed for the pilot? It cannot be changed after shipping.
5. What is the plan for per-tag roll-up and an automatic drain with acknowledgement? Is the capacity target to be met on the 4 MB GD25Q32, or will the board move to a 16 MB part (which needs JEDEC-ID detection)?
6. Can we have the git repository with full history and the tag v0.10.1, so that releases can be reproduced?
7. What is the delivery plan for Modbus RTU/TCP and the Identium TCP V1.0 binary protocol?

## Requirements coverage

| Requirement | Status | Note |
|---|---|---|
| Firmware runs on the STM32F411CEU6 product board | **MET** | Builds and links at 0x08004000 behind the bootloader (f411-release map:1447), with HSE/PLL clock (Src/main.c:168-189). Not yet proven on hardware, and no on-target evidence was delivered (BLD-05). |
| Offline tag buffer >= 100k EPC-128 tags (FW-001 §5 targets 500k) | **NOT MET** | Ceiling is 65,280 records (tag_log.c:40-48); the lead measured 65,376 kept after 300k tags. The log counts raw reads with no de-duplication (sillion_application.c:24-31). |
| Buffer drains automatically to the host when the backhaul recovers | **NOT MET** | The log can only be read by hand with the legacy ASCII command TAGLOG_READ (host_interface.c:229-300). The upload queue discards tags after 8 s (identium_protocol.c:415-445; configuration_storage.c:342). |
| First-seen/last-seen timestamps in Unix ms (FW-001 §5.1) | **NOT MET** | Each record has a single 48-bit time that is uptime unless the host has set the clock (tag_log.c:19-29, 57-58). Time sync is off by default (REQ-08). |
| Identium TCP Protocol V1.0 (binary 0xFF/0xEE active mode) | **NOT MET** | Not implemented. FW-09, FW-16 and R2-01 remain high. |
| HTTP active mode per the documented data format (ActModeDataFormat) | **PARTIAL** | Exists, but accepts only http:// with a literal IPv4 host (http_upload.c:108), uses non-standard tag keys, and drops batches on non-2xx replies or after 3 attempts (http_upload.c:588-595, 779-784). |
| Modbus RTU + Modbus TCP (FW-001 §3.3, default-on) | **NOT MET** | Absent. A Modbus master on port 8080 takes over the module and stops inventory (configuration_port.c:293). |
| Reader Manager / vendor tool compatibility and UDP discovery | **PARTIAL** | 0xEE config and 0xFF module bridge on 8080 (module_bridge.h:7-9). Discovery replies unicast only and reports an HC32F46x board (discovery_service.c:53, 100). |
| Signed field firmware update | **PARTIAL** | ECDSA P-256 + SHA-256 verification in the bootloader (boot_main.c:85-99). But the signing key is leaked (keys/fw_signing_key.pem) and the update commands need no credential (host_interface.c:751-834). |
| Trial boot, confirmation and rollback (FW-001 §8.1, §13 item 6) | **NOT MET** | The bootloader installs and jumps with no trial flag, check-in or fallback (boot_main.c:92-153). There is no anti-rollback version. |
| Application headroom for remaining features | **NOT MET** | The 112 KB slot is fixed by the immutable bootloader (firmware_image.h:95; STM32F411CEUX_FLASH.ld:51), and about 92.6 KB is already used (LEAD_FACTS.md). |
| Device protection (readout protection, authenticated control) | **NOT MET** | RDP only through a manual 'make protect' (Makefile:197-199). No write protection on the bootloader sector, and all control channels are unauthenticated. |
| RF region compliant for India | **NOT MET** | Default region is 0x08 (product_config.h:45; silion_protocol.h:87), not the documented India code (FW-02 regressed). Vendor config accepts any region from 0 to 255 (vendor_config.c:1176-1183). |
| Ethernet (W5500) with per-unit identity | **PARTIAL** | Per-unit MAC derived from the MCU unique ID (reader_settings.c:210-238); IP is saved. Ethernet DHCP was not found: 'dhcp' appears only in the Wi-Fi settings (reader_settings.h:66). The W5500 is never health-checked (ROB-06/NET-08). |
| Wi-Fi via ESP32-C3 | **NOT MET** | Only the STM32 side of the I2C link exists (esp32_link_protocol.h:9-10). The coprocessor firmware was not delivered, so Wi-Fi cannot work on any unit. |
| BLE 5.2 (design spec F-04) | **NOT MET** | No BLE function in the firmware or in the STM32-ESP32 link protocol. |
| RS-232 / RS-485 host serial | **PARTIAL** | RS-232 works. RS-485 can only be chosen at compile time and every build defaults to RS-232 (product_config.h:39-43). |
| USB device interface | **PARTIAL** | Native USB CDC now runs on the F411. It still uses the ST VID/PID and strings, and the SDK expects HID (FW-39). |
| Relays (GPO) and GPI | **PARTIAL** | 3 relays and 4 GPIs are mapped. set_gpo accepts a GPO 4 that does not exist, and the numbering departs from FW-001 §6 (HWI-06). |
| Wiegand output (FW-001 §6) | **NOT MET** | No firmware for it. board_config.h:20 says PC14/PC15 are not connected, but the netlist puts WGD0/WGD1 on them (u7_pinmap.txt:5-6). |
| Status LED, buzzer, IP reset | **PARTIAL** | A buzzer command exists (host_interface.c:2343). The LED is switched on once and never driven again (HWI-09), and there is no IP reset (REQ-09). |
| Persistent configuration that survives power loss | **PARTIAL** | Settings persist across reboots. They live in one sector that is erased before rewrite, so a power cut in that window reverts the unit to factory defaults, and each save stalls the MCU for 1-2 s (ROB-02/03). |
| RFID module health detection and recovery | **NOT MET** | Retries happen at boot only. A module failure at runtime is never detected, and the PB5 reset line is deliberately left undriven (board_config.h:36). |
| Impinj E710 module as alternative | **NOT MET** | No module abstraction layer. The Silion API leaks into 12 files (RFM-08). |
| Identium branding and identity (FW-001 §7) | **NOT MET** | Reader name is RFID-F429-DEV (product_config.h:22), board type is reported as HC32F46x (discovery_service.c:13), and the version is fixed at 1.0.0.8 (product_config.h:52-56). |
| Watchdog and fault capture | **PARTIAL** | The application watchdog starts before the peripherals (firmware_configuration.c:290-305), and a fault record is kept (Src/main.c:113). The bootloader only kicks the watchdog and never starts it (boot_watchdog.h:15-17; ROB-10). |
| TLS/HTTPS and DNS (FW-001 §8.1, §13 item 14) | **NOT MET** | Neither exists anywhere, and the ESP link protocol cannot request them (NET-09). |
| Reproducible, traceable release | **NOT MET** | Built from a dirty tree (…-dirty.tlfw), with no git history delivered and the private key inside the zip. The stale F411/ image overwrites the bootloader (F411/RFID_Controller_F429_f411.map:6978). |

## Findings in detail

79 merged new findings, most severe first. "Also raised as" lists duplicates from other review dimensions merged into the card, plus related rev 2 references. "Verifier corrections" records where a verifier changed a line reference, a number or the impact; the card text should be read together with it. File references are relative to STM32_Firmware/ in the 271026 zip. The full machine-readable record, with every vote, is 271026/_fw/work/fw_results.json.

### CRITICAL (4)

<a id="sec-01"></a>

#### SEC-01 — Production firmware-signing private key ships inside the delivery zip; shipped bootloaders trust it and shipped images are signed with it

`CRITICAL` · 🛑 **BLOCKS PILOT**

**Location.** STM32_Firmware/keys/fw_signing_key.pem; Bootloader/boot_public_key.c:11; Tools/fw_sign.py:98-111,194; Makefile:164-170

**Evidence.** `unzip -l STM32_Firmware.zip` lists STM32_Firmware/keys/fw_signing_key.pem (241 B, 27-10-2026 15:53, same minute as boot_public_key.c). Its first line is an unencrypted PKCS#8 header ('BEGIN PRIVATE KEY', not 'ENCRYPTED'). The public half the lead derived (work/derived_pub.hex) equals g_bootPublicKey at Bootloader/boot_public_key.c:11. I checked further with my own read-only Python script (not a tree script): build/boot-f411/bootloader.elf and build/boot-f429/bootloader.elf both contain those 64 public-key bytes and not the hostcheck test key. Both shipped images, build/f411-release/rfid_controller_f411-v0.10.1-1-gfcae054-dirty.tlfw and build/release/rfid_controller-v0.10.1-1-gfcae054-dirty.tlfw, verify as ECDSA P-256 against boot_public_key.c with a correct SHA-256. build/hostcheck/test_f411.tlfw correctly fails. Root cause: fw_sign.py genkey writes the key with serialization.NoEncryption() (fw_sign.py:103-108) to a default path inside the source tree (fw_sign.py:194 '--key', default='keys/fw_signing_key.pem'; Makefile:164 'SIGN_KEY ?= keys/fw_signing_key.pem'). The zip has no .gitignore and no check stops a .pem from being packaged. The tool's own warning, 'never commit it' (fw_sign.py:110), was not followed. The images were built from a dirty tree (Makefile:165 'git describe --tags --always --dirty').

**Impact.** Anyone who has the zip (the design house, anyone it was forwarded to, any machine it sat on) can sign firmware that every bootloader built from this tree will install. Combined with the unauthenticated network update path (SEC-02), any host on the LAN gets arbitrary code execution on every unit, which defeats the purpose of the signed-update chain. Units programmed so far with the shipped bootloader trust this key permanently, until they are re-flashed over SWD (SEC-07).

**Fix.** Treat the key as compromised; it must never sign a pilot image. (1) Identium, not the design house, generates a new P-256 key on an Identium-controlled offline machine or token (HSM/YubiHSM, or at least an encrypted PKCS#8 with BestAvailableEncryption), stored outside any source tree. (2) Rebuild the bootloader with the new public key and re-flash sector 0 over SWD on every board programmed so far, before RDP is set. (3) Delete keys/fw_signing_key.pem and both *.tlfw images from every copy of the delivery, and get written confirmation from the design house that they have destroyed their copies. (4) Change fw_sign.py: no default --key, refuse output paths inside the repository, encrypt the key on genkey, and refuse to sign '-dirty' or untagged versions. (5) Add .gitignore plus a pre-commit/CI grep for '-----BEGIN .*PRIVATE KEY' and '*.pem'.

**Also raised as.** BLD-01 (critical). Earlier audit refs: FW-37

**Verifier corrections.** Two verifiers corrected the impact. Sector 0 has no write protection, and the application already contains internal-flash erase and program code (reader_settings.c:535-555). So anyone holding the leaked key could sign an application that rewrites the bootloader in the field. That works both for an attacker and as a risky recovery route, since a power cut during it bricks the unit. For units still at Identium, re-flashing over SWD remains the right fix. The zip also contains build/hostcheck/test_key.pem, a second private key (the test key, low risk), and it should go too. Both shipped .tlfw images verify against the leaked key, and the F411 one is byte-identical to the shipped release build.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="buf-01"></a>

#### BUF-01 — The offline buffer is not store-and-forward: the flash tag log is never drained to any host protocol, and the live path drops reads after 8 s or 256 reads of backhaul loss

`CRITICAL` · 🛑 **BLOCKS PILOT**

**Location.** Firmware/Application/Silion/sillion_application.c:25-32; Firmware/Protocol/identium_protocol.c:63-64, 416-446, 472-480; Firmware/Application/Host/host_interface.c:193-307, 1291-1320

**Evidence.** PublishTag sends every read down three independent paths: Identium_Protocol_ReportTag, HOST_SendTag and TagLog_Append (sillion_application.c:27-32). The network path keeps only a RAM ring (IDENTIUM_TAG_QUEUE_SIZE 256U / 8192 bytes, identium_protocol.c:63-64). When that ring is full it pops the oldest read and counts it in identiumTagDropped (:472-480), and it also expires reads older than clear_buf_time (:416-446). http_upload.c has its own 12 KB RAM queue (:24, :57) and no flash access. A grep of the whole tree finds TagLog_Read called in exactly one place, the legacy ASCII handler HOST_SendTagLogRecords (host_interface.c:261), which is reached through TAGLOG_READ,<seq>,<count> (:1296). Nothing under Protocol/ or Services/ethernet/ or Services/upload/ references TagLog_*. The lead's host test shows the result: "http slow server: 2000 read, 0 uploaded, dropped 1744, expired 256". The device keeps no record of what it has delivered: per tag_log.h:11-13 the host has to "remember where it stopped". Reads are 20 records per command (HOST_TAGLOG_READ_MAX, host_interface.c:195), which means about 3,300 round trips for a full log, in a format (:232) that is not part of TCP V1.0, Modbus or HTTP active mode. Spec IDM-INB1-FW-001 §5 says the buffer "drains automatically when it returns"; §5.4 gives the drain state machine and acceptance check 3 requires "drains FIFO correctly on backhaul recovery".

**Impact.** After any outage longer than about 256 reads (under 3 s at 100 reads/s), the Identium server or PLC permanently loses those reads, even though they are sitting in flash. Getting them back needs a custom client speaking an undocumented legacy ASCII command over USB, serial, Wi-Fi or TCP port 5000. Reads that were delivered live are also in the log with nothing marking them, so a host that pulls the log gets duplicates. This defeats the core offline-buffering goal.

**Fix.** Add a drain task. Persist a delivered/acknowledged sequence pointer in flash, for example as a small journal of pointer records in a dedicated sector pair. When the uplink is up and the live RAM queue is empty, feed log records in sequence order into the active-mode batch builder (HTTP and TCP) and serve them to Modbus. Advance the pointer only on server acknowledgement. Send sync_time_req before draining (spec §3). Either log only reads that were not acknowledged live, or skip records below the pointer, so nothing is sent twice. Count records overwritten before delivery and report them in status and heartbeat.

**Also raised as.** PRO-01 (critical), REQ-01 (critical). Earlier audit refs: FW-01, R2-51

**Verifier corrections.** The loss window is smaller than first stated. clear_buf_time defaults to 8 s (product_config.h:29), and stale reads are purged every loop pass whether or not the link is up (identium_protocol.c:1185). So any outage longer than about 8 s or 256 reads loses data, even at low read rates. client_ack defaults to 0, so TCP active mode has no server acknowledgement to advance a delivered pointer on. The drain must either enable client_ack or use HTTP 2xx as the acknowledgement. The manual TAGLOG_READ pager is unreliable too (see BUF-07). REQ-01 also counts the capacity shortfall, which is covered in BUF-02.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 2 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="buf-02"></a>

#### BUF-02 — The flash tag log keeps only 65,280 to 65,408 records (65,376 measured), below the 100k requirement: half the 4 MB chip is reserved to stage a 112 KB image

`CRITICAL` · 🛑 **BLOCKS PILOT**

**Location.** Firmware/Services/storage/tag_log.c:30-48, 102-105; Firmware/Common/firmware_image.h:95-99; Firmware/Devices/spi_flash/gd25q.h:25-28

**Evidence.** TAG_LOG_FLASH_BYTES is GD25Q_SIZE_BYTES/2, i.e. 2 MB (tag_log.c:40). Records are 32 bytes (:42). The macro gives TAG_LOG_CAPACITY = (512-2)*128 = 65,280 (:47-48). One sector is always kept erased ahead of the head sector, and the head sector holds 0-127 records, so retained = 65,280 + (nextSeq mod 128). For the lead's run: 300,000 mod 128 = 96, so 65,280 + 96 = 65,376 (oldest = 2,343*128 - 65,280 = 234,624), and the 2,343 erases equal floor(300,000/128). The host test expects capacity 65280 (tag_log_host_test.c:259) and accepts 65,280-65,408 (:313-314). My own Python transcription of the mount and task logic reproduces oldest=234624, kept=65376 exactly. The slot is computed as seq & (TAG_LOG_SLOT_COUNT-1) (:102-105), and the comment at :30-32 requires a power of two dividing 2^32. On the 4 MB part that allows only 2 MB (65,536 slots) or 4 MB, and 4 MB leaves nowhere for staging. Staging takes the upper 2 MB (firmware_image.h:98-99) for an image capped at 112 KB (FW_APP_MAX_SIZE, :95), about 18 times more space than it needs. Configuration is not on the SPI flash: it is in MCU sectors 5-7 (STM32F411CEUX_FLASH.ld:48-51). The spec targets 500,000 records (FW-001 §5; design spec §1 item 2).

**Impact.** The fitted GD25Q32 holds about 65% of the minimum requirement. With a 16 MB W25Q128 and the same half/half split, the log would get 8 MB = 262,144 slots, giving 261,888 records, which meets 100k (not 500k). That needs the firmware changes in BUF-09 first, because the driver rejects any non-GD25Q32 ID. The 4 MB part can reach 100k only if staging shrinks and the slot mapping changes: 784 sectors (3.06 MB) give (784-2)*128 = 100,096 records and leave 960 KB for staging. Changing this after pilot units ship means migrating the on-flash format.

**Fix.** Shrink staging to about 256 KB-1 MB. Replace the power-of-two mask with slot = seq % N, keeping wrap safety by carrying an epoch or 64-bit sequence in a per-sector header (or by bounding the sequence space to a multiple of N). That gives 100k or more on the GD25Q32. Better still, fit a 128 Mbit part (U6 is a 208-mil SOIC-8, footprint SOIC-8_L5.3-W5.3, which also takes the W25Q128JVSIQ) and size the log from the JEDEC ID. Add a host test that asserts capacity >= 100,000 at EPC-128.

**Also raised as.** . Earlier audit refs: FW-01

**Verifier corrections.** The upper bound is 65,408 records, not 65,407. The 500k figure in FW-001 §5 assumed a 64 MB QSPI part. The fitted 4 MB chip holds at most 131,072 records of 32 bytes, so 500k is a hardware and spec mismatch. The target this firmware can meet is 100k or more, and it can meet it on the GD25Q32 once staging shrinks and the slot mapping changes. Example: 960 KB of staging leaves 784 sectors, or 100,096 records. 256 KB of staging leaves 958 usable sectors, about 122,600 records.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="rfm-01"></a>

#### RFM-01 — No duplicate suppression or roll-up: every raw read goes into the flash log, so a tag in the field fills the 65k log in minutes

`CRITICAL` · 🛑 **BLOCKS PILOT**

**Location.** Firmware/Application/Silion/sillion_application.c:27-32, 1481, 1996, 2243; Firmware/Application/configuration/firmware_configuration.c:103-104; Firmware/Protocols/silion/silion_protocol.c:2689-2692; Firmware/Services/storage/tag_log.c:20-30, 473-499

**Evidence.** Rev 3 adds SILION_Application_PublishTag (sillion_application.c:27-32). For every tag it calls Identium_Protocol_ReportTag, HOST_SendTag and then `TagLog_Append(tag);`. All three read paths call it: async at :1481, single poll at :1996 and sync poll at :2243. TagLog_Append (tag_log.c:473-499) only encodes the read into the next 32-byte slot. It does no lookup, so it cannot see that an EPC was already logged. The record has one timestamp and a u8 readCount (tag_log.c:20-30). The default Gen2 session is S0 (firmware_configuration.c:103-104; product_config.h:50). AA48 is started with Search Flags 0x0000 (silion_protocol.c:2689-2692). The module does not de-duplicate in this mode: protocol document §5.6.1 says that "once the tag is read, it will be actively uploaded". Identium's own spec IDM-INB1-FW-001 requires a de-duplicator in front of the buffer: p.5 (TagInfo read_count is per "dedup window"), p.7 §4.3 (the driver pushes into `tag_dedup`) and p.8 §5.1 (each record carries firstseen/lastseen/read_count). None of this exists in the source tree; a grep for dedup finds nothing.

**Impact.** The buffer's capacity is counted in reads, not tags. Take the lead auditor's measured retention of 65,376 records. With a static tag population read at 200 reads/s (an illustrative rate below the 115200-baud ceiling), that is about 5.5 minutes of history. During a network outage, the same few tags in the field overwrite older unique reads. At a portal where each passing tag is read tens of times, the log holds roughly 65,376 divided by reads-per-tag distinct EPCs. The >=100k EPC-128 offline-buffering goal cannot be met by this design, even with more flash. This multiplies the capacity shortfall the lead measured.

**Fix.** Insert a de-duplication and roll-up stage between PublishTag and TagLog_Append, as FW-001 §4.3/§5.1 specify. Use an EPC hash table with a configurable window that keeps firstseen, lastseen, max RSSI, antenna and a u16 read count. Write a record when a tag is first seen and when its window expires, not on every read. Size the RAM table for the expected population and flush it on brown-out. For buffering use cases, default to Session 1/2 with Target A, or make the session part of the provisioning profile. Re-run tag_log_host_test with repeated EPCs to show that the record count tracks unique tags.

**Also raised as.** BUF-03 (high). Earlier audit refs: FW-01

**Verifier corrections.** (1) product_config.h:50 (PRODUCT_RFID_SESSION 0U) is never referenced (grep finds no user). The S0 default comes from firmware_configuration.c:103-104 and the persisted default configuration_storage.c:353 (`data->session = 0U;`). The session can be changed (Configuration_Service_SaveSession, configuration_service.c:462-490; validated <=3 at :449), so S0 is a default, not hard-wired. (2) TagLog_Append (tag_log.c:473-499) encodes into a 64-entry RAM queue (TAG_LOG_QUEUE_SIZE, :52) that TagLog_Task later writes to flash, not straight into a slot; it also drops reads when that queue is full (:482-485). (3) The sync poll path (:2243, 0x22/0x29) reads the module's own tag buffer, which the module already de-duplicates by EPC within one inventory round (protocol doc §7.7 0x9A, Key Value table, p.144). The 'no de-dup' point is fully true for the async AA48 path, which is the one StartInventory uses (sillion_application.c:2285). (4) 'Cannot be met even with more flash' is better put as: tag capacity scales with flash divided by reads per tag, so no realistic flash size reaches 100k tags at a portal. All other line references check out.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

### HIGH (11)

<a id="sec-02"></a>

#### SEC-02 — Rev 3 adds unauthenticated network write paths: firmware update, tag-log erase, tag KILL/LOCK, RF and Wi-Fi settings, including a re-opened legacy ASCII server on TCP 5000

`HIGH` · 🛑 **BLOCKS PILOT**

**Location.** Firmware/Services/ethernet/host_tcp_server.c:24,97; Firmware/Services/wifi/wifi_service.c:305,780-785; Firmware/Application/Host/host_interface.c:764-866,1276,1306,1327-1331,1714,2157,2443,2495; Firmware/Services/configuration/configuration_port.c:184-206,290-300; vendor_config.c:767-769,1118-1160,1176-1182,1316-1334; module_bridge.c:312-339,349-391

**Evidence.** TCP 5000 is back: host_tcp_server.c:24 'W5500_Socket3_Listen(PRODUCT_ETHERNET_TCP_PORT)' (5000, product_config.h:13). Every received byte goes to HOST_Transport_PushByte(HOST_TRANSPORT_ETHERNET, ...) (:97). It is started unconditionally (firmware_configuration.c:514) and serviced every loop (host_interface.c:872). The ESP32 opens the same port as a Wi-Fi server (wifi_service.c:784-785) and pushes into the same parser (:305). That parser accepts FW_BEGIN/FW_DATA/FW_END/FW_APPLY on any transport (host_interface.c:1327-1331 -> :764-866; FW_APPLY reboots into the installer at :834). It also accepts REBOOT (:1276), TAGLOG_CLEAR (:1306, which chip-erases the flash, tag_log.c:532-539), SET_REGION (:1714), SET_POWER, KILL_TAG (:2157), LOCK/WRITE_TAG, SET_WIFI (:2443) and SET_HTTP (:2495, which redirects uploads). Tools/fw_update.py:13 says updates 'work over ... TCP 5000'. TCP 8080: EE commands 0x1F/0x25/0x20/0x23 go to VendorConfig_Handle (configuration_port.c:184-206). They rewrite IP/netmask/gateway (vendor_config.c:1118-1160), region and power, which are persisted and applied (:1176-1182), Wi-Fi credentials and AP mode (:767-769), or run a factory clear (:1316-1334). A non-EE first byte opens the module bridge (configuration_port.c:290-300). The bridge forwards every 0xFF-framed module frame unfiltered (module_bridge.c:349-391, SILION_Application_BridgeSend at :386). IPSET (:312-339) overwrites IP/mask/gateway/MAC and saves them. Grepping Firmware/, Src/, Inc/ and Bootloader/ for auth|password|token|login|secret|psk finds only tag access/kill passwords and Wi-Fi credential fields: no authentication mechanism exists. In rev 2, Firmware/Services/ethernet/ held only ethernet_service.c, with no FW_BEGIN and no config-port writes, so all of these paths are new in rev 3.

**Impact.** Any host on the reader's LAN, and anyone in Wi-Fi range when AP mode is on (SEC-11), can do all of the following without credentials. (a) Install firmware: with SEC-01 this means attacker code on every reachable unit; after key rotation it still allows a downgrade (SEC-04). (b) Erase the whole offline tag buffer, which defeats the >=100k offline-buffering goal. (c) Kill or lock customer tags. (d) Move the RF region or power outside the Indian band, including through raw module frames that bypass any SET_REGION whitelist. (e) Redirect uploads to their own server (data exfiltration). (f) Change the IP or clone a MAC (denial of service or impersonation). (g) Lock out local maintenance (SEC-08). One malicious or infected PC on a customer network compromises the whole fleet on that segment.

**Fix.** Before pilot: (1) Require authentication on network links (Ethernet 5000/8080 and Wi-Fi) for every state-changing command. A practical scheme is a per-unit random commissioning secret, set at factory and printed on the label, with an HMAC-SHA256 challenge-response per connection (SHA-256 is already in the tree). USB and serial can stay open as physical-access paths. (2) Until that exists, refuse FW_*, TAGLOG_CLEAR, KILL/LOCK/WRITE, SET_REGION/POWER, SET_WIFI/HTTP, IPSET, EE 0x1F/0x20/0x23/0x25 and raw module frames on Ethernet/Wi-Fi unless a maintenance window was opened locally (USB command or KEY press) and has not timed out. (3) Make the legacy port 5000 and the vendor-tool module bridge opt-in settings, off by default on pilot units. (4) Allow-list module-bridge opcodes, and never forward region or module-bootloader opcodes from the network.

**Also raised as.** PRO-03 (high). Earlier audit refs: FW-24, R2-32, R2-03, R2-24, FW-20

**Verifier corrections.** Line fixes: (1) The VendorConfig_Handle dispatch is at configuration_port.c:186-209, not 184-206. (2) vendor_config.c:1176-1182 only parses region, accepting any value 0-255. Persistence is at :1244-1249 (ReaderSettings_Save) and the live apply at :1257-1263 (SetRegion/SetPower). (3) Wi-Fi 'mode':'ap' is parsed at vendor_config.c:778-790. Precision: (4) 'All of these paths are new in rev 3' should read 'their network reachability is new in rev 3'. Rev 2 host_interface.c already had SET_REGION (:909) and KILL_TAG (:1350), but the only HOST_Transport_PushByte caller in rev 2 was USB (usbd_cdc_if.c:274), and the rev 2 config port served reads only. Severity: critical double-counts SEC-01 through impact (a). On its own, once the key is rotated, this is an unauthenticated LAN/Wi-Fi admin surface: high, still blocking. Add to the evidence: every cited command string (TAGLOG_CLEAR, FW_APPLY, KILL_TAG, SET_WIFI, IPSET) is present in the shipped build/f411-release/rfid_controller_f411.bin. Recommendation: the PC tools on 8080 have no authentication support, so any gating must stay compatible with them, for example a locally opened maintenance window.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="sec-03"></a>

#### SEC-03 — No trial boot, confirmation or rollback: a validly signed image that fails at runtime boot-loops the unit and cannot be updated again remotely

`HIGH` · 🛑 **BLOCKS PILOT**

**Location.** Bootloader/boot_main.c:48-116,118-139; Bootloader/boot_image.c:19-25; Firmware/Common/firmware_image.h:9-28

**Evidence.** Boot_Install erases the application sectors and copies the staged binary over them (boot_main.c:56-78). No copy of the running image is kept: staging holds only the new image (firmware_image.h:18-23, 25-28). The image is marked INSTALLED as soon as the copy hashes correctly (boot_main.c:106-107), before the new application has ever run. From then on BootImage_Check returns NONE for every boot (boot_image.c:19-25) and the bootloader just jumps (boot_main.c:118-139). Grepping Bootloader/, Firmware/Common, Services/update, Services/system and Src for trial|confirm|rollback|bootcount|attempt finds nothing. The update receiver exists only in the application (firmware_update.c; host_interface.c:764). Consequences: a new app that faults or hangs at start-up (Error_Handler loops at main.c:271-282, and the IWDG then resets into the same image) loops forever. So does an app whose Ethernet/USB stack is broken: it can never receive the next update. The requirement is explicit: FW-001 §8.1 step 6, 'App checks in within 60 s ... otherwise bootloader auto-rolls back' (p.10); acceptance row 'Bad image rolls back without brick' (p.12); review checklist §13 item 6 (p.13); design spec F-07, 'dual-bank A/B safe rollback' (p.7) and §7.5 steps 7-8 (p.19).

**Impact.** One bad release pushed to pilot units at customer sites needs a site visit per unit. The visit uses KEY2/BOOT0 with the ST system bootloader, or SWD. With RDP level 1 that also means an RDP regression mass erase that wipes the bootloader and settings (Makefile:201-203). Field update, a core goal, has no fail-safe.

**Fix.** Implement trial/confirm with the space that already exists. The 2 MB SPI staging area holds about 17 images of 112 KB. Before erasing the app, have the bootloader save the current application into a 'previous' slot in SPI flash, or have the receiver keep the last good signed image. Mark the new image TRIAL, using bit-clearing state values for a boot-attempt counter. Have the application mark it GOOD only after it has run for about 60 s with the network or a host link up. If GOOD is not reached after N attempts, the bootloader restores the previous slot, after verifying it against its signed header. Add a host test that simulates a non-confirming image.

**Also raised as.** REQ-03 (high). Earlier audit refs: FW-17

**Verifier corrections.** Context: FW-001 p.2 and the design spec p.19 describe a different platform (STM32H7, LAN8720, W25Q512/64 MB QSPI with 16 MB for firmware A/B). The rollback requirement still applies (F-07 p.7; §7.5 steps 7-8 p.19; FW-001 §8.1 step 6 p.10; acceptance p.12; checklist item 6 p.13), but the A/B layout has to be adapted to the 4 MB GD25Q32. 2 MB / 112 KB is about 18 raw slots, so 'about 17' is fine allowing for headers. Additional evidence: the Error_Handler comment (main.c:274-277) says a fault before the watchdog is started, during clock set-up, halts the reader outright with no reset loop, so it needs a site visit even sooner.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="pro-02"></a>

#### PRO-02 — HTTP active mode treats 4xx/5xx/3xx replies as delivered and silently drops a batch after 3 failed attempts

`HIGH` · 🛑 **BLOCKS PILOT**

**Location.** Firmware/Services/ethernet/http_upload.c:588-603, 779-785, 860-863; Firmware/Protocol/identium_protocol.c:760-774; Firmware/Services/system/diagnostics.c:142-146

**Evidence.** In HTTP mode a tag batch leaves the protocol queue as soon as HttpUpload_Submit accepts it (identium_protocol.c:760-774, via ethernet_service.c:283-286). When any complete response arrives, HttpUpload_FinishResponse sets g_state = HTTP_STATE_IDLE and g_attempts = 0 unconditionally (http_upload.c:593-595). The status code only decides whether the body is parsed as a command (:599-603). A 500, 503, 404 or 302 therefore ends the request exactly as a 200 does, and the event is never resent. When the connection drops or the response times out, the same request is retried at most 3 times. After that the code gives up (:779-785): "g_state = HTTP_STATE_IDLE;   /* give up on this event */". No counter records these losses. GET_DIAG reports only the RAM-queue drop and expiry counters (diagnostics.c:142-146). HttpUpload_GetLastStatus (:860-863) has no caller anywhere in the firmware. The host test does not compile http_upload.c (Makefile:213-216 builds only identium_protocol.c).

**Impact.** A routine server-side error (a reverse proxy answering 502/503 during a deploy, a database outage returning 500, a wrong path returning 404) silently discards up to one full POST of tags per request, about 130 tags with 96-bit EPCs. Nothing on the reader shows that it happened.

**Fix.** Treat only 2xx as delivered. On 5xx, 408, 429 or a transport failure, keep the batch and retry with backoff, without limit, falling back to the flash log from PRO-01. Treat 4xx other than 408/429 as a configuration error and raise it, both as a reader_exception and in GET_DIAG. Count failed and abandoned POSTs and expose the last HTTP status. Add an HTTP host test that drives http_upload.c against scripted responses.

**Also raised as.** NET-02 (high). Earlier audit refs: FW-16

**Verifier corrections.** Minor precision. The 3-attempt give-up (http_upload.c:779-785) applies only after an established connection drops or times out. HttpUpload_OnDisconnected is called only when ethernetClientConnected was set (ethernet_service.c:42-50, 166-168). If the server cannot be reached at all, the request waits in the SENDING state and is not discarded by this path; loss then happens upstream through PRO-01. The timeout close that leads to a retry is at http_upload.c:849-857. HTTP_MAX_ATTEMPTS=3 is at http_upload.c:27. Makefile:213-216 is the protocol test target only. Other host tests exist (tag_log, boot_image, firmware_update at :217-225), but none compiles http_upload.c.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="rfm-02"></a>

#### RFM-02 — The region from vendor static configuration is saved without validation, acknowledged before it is applied, and sent to the module at every boot, bypassing the SET_REGION whitelist

`HIGH` · 🛑 **BLOCKS PILOT**

**Location.** Firmware/Services/configuration/vendor_config.c:1170-1183, 1229-1249, 1257-1264, 1393-1457, 1474-1483; Firmware/Services/configuration/reader_settings.c:309-339; Firmware/Application/configuration/firmware_configuration.c:452-457; Firmware/Application/Silion/sillion_application.c:342-351, 1148-1156; Firmware/Application/Host/host_interface.c:338-358, 1733-1737

**Evidence.** VendorConfig_SetStatic (vendor command 0x1F, reachable on TCP 8080 and through the 0xEE prefix on USB/serial) accepts any rf.region from 0 to 255. Line 1179 tests only `(number <= 255U)`. It then sets rfValid=1 and region=number (1236-1240) and saves the record (1246) before touching the module. The live SetRegion/SetPower results are discarded with (void) (1262-1263). If a tool session owns the module, they are not applied at all (comment 1254-1256: "applied at the next boot"). The tool has already received OK, because VendorConfig_Handle replies before execution (1427-1457) and VendorConfig_Task ignores the Execute result (1481). ReaderSettings_IsSane (reader_settings.c:309-339) checks no RF field. At boot, firmware_configuration.c:452-454 copies the stored region into readerConfig. ConfigureReader then sends SILION_SetRegion(readerConfig.region) (sillion_application.c:342-351) with no check. The whitelist exists only in SILION_Application_SetRegion (1148-1156). Separately, the host text SET_REGION path now persists any whitelisted code, including NA 0x01 and China 0x06/0x0A (host_interface.c:1733-1737 -> HOST_PersistRegionPower 338-358). FW-001 p.7 §4.2 says: 0x97 "India region code at boot; lock until factory reset".

**Impact.** One unauthenticated config write, or a misconfigured Reader Manager, can make every later boot transmit in 902-928 MHz (0x01), 920-925 MHz (0x06) or full band 860-960 MHz (0xFF), none of which is legal in India. The write survives reboots, and the tool was told OK. If the stored code is one the module rejects, SET_REGION fails at every boot. Firmware_Configuration_StartReader then fails, the reader stays in degraded RFID mode and retries forever (see RFM-06). Nothing reads until someone overwrites the setting. Whether the vendor tools send module region codes or their own enumeration in rf.region is unverified. The firmware assumes module codes (configuration_service.c:1066-1074 reports readerConfig.region raw).

**Fix.** Make one region validator the single owner of region and call it in all four places: vendor JSON, host SET_REGION, ReaderSettings load and ConfigureReader. For India SKUs, accept only the India-legal code(s) and fall back to the product default if the stored value is invalid. Validate and apply the setting before saving it, and save only after the module returns 0x0000 (read back with 0x67). In VendorConfig, return INVALID before acknowledging when rf.region or rf.tx_powers fail validation. Add range checks for region and power to ReaderSettings_IsSane. If the spec's region lock is kept, the bridge (RFM-05) must also block 0x97 and AA40 region writes.

**Also raised as.** REQ-07 (medium). Earlier audit refs: FW-02, R2-07, R2-04

**Verifier corrections.** (1) ReaderSettings_IsSane (reader_settings.c:309-339) does check some RF fields: rfAntennaValid, gen2Valid, gen2Target and gen2Q. What it does not check is rfValid, region, readPower or writePower. Reword to 'no region or power check'. (2) The live path at vendor_config.c:1262 does go through the whitelisted SILION_Application_SetRegion when the module is IDLE. The whitelist result is discarded and the unvalidated value stays saved. So the bypass happens at boot (ConfigureReader, sillion_application.c:342-351), not on the live apply. (3) Code 0x0A ('CHINA_2', silion_protocol.h:90) does not appear in the protocol doc's region table at all (Appendix 2, p.241). 0xFF full band is listed only for China-certified modules. The Appendix 2 table also gives INDIA = 0x04 for non-China-certified modules, while the firmware's 'India' 0x08 is CE_LOW 865-867 in the China-certified table. The recommended single validator must pick codes per module certification. (4) Accepted power is <=3300 (vendor_config.c:1210, 1218), and that is also stored and re-applied at boot. (5) VendorConfig_Handle spans about 1381-1458; it returns OK at :1457 after a structural JSON check only (:1403-1413). VendorConfig_Task discards the Execute result at :1481. The other line references are correct.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="rfm-03"></a>

#### RFM-03 — Any module-bridge session stops local reading and buffering, and a TCP bridge session is never timed out; a stray 0xFF byte on serial/USB, or any non-0xEE first byte on TCP 8080, opens one

`HIGH` · 🛑 **BLOCKS PILOT**

**Location.** Firmware/Services/configuration/configuration_port.c:256-262, 289-299, 345-372; Firmware/Services/configuration/module_bridge.c:46, 57, 591-651, 782-834; Firmware/Services/host/host_transport.c:184-195, 271-276; Firmware/Application/Silion/sillion_application.c:2436-2448; Firmware/Devices/w5500/w5500.c:659, 705, 1177

**Evidence.** Opening a bridge calls SILION_Application_BridgeAcquire, which stops local inventory and puts the module UART in raw mode (sillion_application.c:2436-2448). From then on no tag reaches PublishTag, so nothing goes to TagLog or the Identium protocol. Entry triggers: (a) on TCP 8080, any first byte other than 0xEE starts a session (configuration_port.c:289-299, comment "anything else = Reader Manager module session"); (b) on USB or RS-232/RS-485, a single 0xFF at line start calls Module_Bridge_BeginLink (host_transport.c:271-276). Every later byte then goes to the bridge while a partial frame is pending (184-195), until LEN+5 bytes have arrived. Exit: serial sessions end after 30 s with no traffic (module_bridge.c:57, 788-791), then a 5 s grace period follows (46, 812). TCP sessions end only on CLOSE_WAIT/CLOSED (configuration_port.c:345-372). Module_Bridge_Poll's idle check applies only to serial links (782-793). The W5500 keep-alive is set only in StartTCPClient (w5500.c:659, 705), not in StartTCPServer1 (1177 onward). Module_Bridge_BeginOn refuses a new session on the same transport (600-605), and socket 1 is the only 8080 socket.

**Impact.** If a PC running Reader Manager drops off the network mid-session (Wi-Fi loss, sleep, cable pull) while the module is idle, the board sends nothing, socket 1 stays ESTABLISHED, and the module stays bridged indefinitely. Local inventory, active-mode upload and the offline buffer stop silently until a reboot, or until someone opens a session on serial/USB to take it over. A port scanner or HTTP probe on 8080 causes a shorter stop: the duration of the connection plus 5 s. On RS-485/RS-232 a single line glitch or cable-plug event that decodes as 0xFF stops reading for about 35 s, and the following legacy host commands are swallowed and forwarded to the module as junk.

**Fix.** Give TCP bridge sessions the same idle timeout as serial sessions (no PC byte for N seconds releases the module), and enable Sn_KPALVTR on socket 1. Open a bridge only on a CRC-valid module frame or a whole vendor token, never on a lone 0xFF; on serial, drop a partial frame after a few character times. Consider making the bridge an explicit, authenticated or physically gated service mode in active-mode deployments. At minimum, report bridge ownership in diagnostics and in the Identium protocol (for example as a reader_exception) so a halted reader is visible.

**Also raised as.** ROB-01 (high). Earlier audit refs: R2-21, R2-03, FW-15, FW-24

**Verifier corrections.** (1) A plain port scan (SYN or connect with no payload) does not open a bridge: with g_rxLength==0 the handler returns (configuration_port.c:285-288). Only a probe that sends data does, such as a banner grab or an HTTP GET. Change 'a port scanner or HTTP probe' to 'any client that sends a first byte other than 0xEE (an HTTP request, a service-detection probe)'. (2) The ~35 s figure for a stray 0xFF is the worst case, when no further bytes arrive (30 s idle at module_bridge.c:57/788 plus 5 s grace at :46/812). If legacy traffic continues, the session ends sooner: once the junk frame of LEN+5 bytes is forwarded, the next printable non-token byte ends it at once (host_transport.c:211-216 -> Module_Bridge_EndLinkForLocalUse, module_bridge.c:709-721). (3) Additional point: the serial idle timer also counts module output, because g_lastActivityMs is refreshed in Module_Bridge_SendToHost (module_bridge.c:107). A serial session where the PC left an inventory running therefore never times out either. (4) BridgeAcquire is sillion_application.c:2422-2450 (stop at :2442, raw mode at :2446). The cited 2436-2448 is acceptable.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="rfm-04"></a>

#### RFM-04 — An RFID module reset or hang at runtime is never detected or recovered, and the module reset line (CN1.11 on PB5) is never driven

`HIGH` · 🛑 **BLOCKS PILOT**

**Location.** Firmware/Application/configuration/firmware_configuration.c:239-268, 542-550; Firmware/Application/Silion/sillion_application.c:2265-2298; Firmware/Protocols/silion/silion_protocol.c:2236-2245, 2689-2692; Firmware/Application/Silion/sillion_application_port.c:70, 96; Firmware/BSP/f411_custom/board_config.h:36-39; Firmware/BSP/f411_custom/board.c:178-188

**Evidence.** SYSTEM_DEGRADED_RFID is set only at boot (firmware_configuration.c:547). Firmware_Configuration_Task retries only while that flag is set (241-244); no runtime path sets it. StartInventory sets INVENTORY after TX-complete alone (2290-2297). Async frames with a non-zero status only increment asyncBadPacketCount (silion_protocol.c:2236-2245). AA48 is sent with "No heartbeat" (2689-2692), although §5.6.1 Search Flags BIT7 gives a 15 s heartbeat. silionAsyncPacketCount is zeroed (port.c:96; app.c:2282) but never incremented, so tag silence cannot be measured. Hardware (net.py): module 5 V on CN1.1/2 comes from F8 with no switch; CN1.5 EN is tied to 3V3 through R67; U8 TPS22919 (ON = PA8) feeds U2.16 (SP3232), not the module; CN1.11 goes to PB5 with an R32 pull-up. The firmware configures PB5 as an input and comments "never drive it low" (board_config.h:36-37; board.c:180-183), although IDM-AUD-003 F-EL-12 notes that pin 11 is RST on SFM2100-style modules. The protocol document (§2.1, points 1-2) says a module reset from unstable power returns it to the BOOT layer, where it answers 0101 and loses all non-permanent configuration. FW-001 p.6 §4.1 requires catching 0x0101 and resynchronising via 0x0C.

**Impact.** If the module browns out or locks up after boot, the reader keeps reporting inventory running but reads nothing. Every host command fails with DEBUG,STATUS=0101, nothing re-runs ConfigureReader, and no fault or event is raised. Recovery takes a full power cycle, because the only possible hardware reset line is unused. IDM-AUD-003 lists module resets from the sagging, PTC-fed 5 V rail as a likely symptom on this board. If the module has been set to power-on-to-APP (as FW-001 §4.1 directs), it comes back in the APP layer with default settings; for a China-certified module that is NA 902-928 MHz (§2.2 table). The next StartInventory (no region re-apply, app.c:2265-2298) would then transmit there.

**Fix.** Add runtime supervision. Enable the AA48 heartbeat (Search Flags BIT7) and treat 2 missed heartbeats, an AA49 or error frame, or any 0x0101 reply as module loss. On loss, set SYSTEM_DEGRADED_RFID, raise an event, and re-run the full ConfigureReader, session, Gen2 and region sequence through the existing retry path before restarting inventory. Confirm what CN1.11 is on the chosen module. If it is RST, pulse PB5 low (open-drain) before each retry and at cold boot. Otherwise record the hardware gap (F-EL-12) as a known pilot limitation.

**Also raised as.** HWI-05 (medium). Earlier audit refs: FW-12, FW-32, FW-20

**Verifier corrections.** (1) An opt-in runtime recovery path exists but is not mentioned: the Identium protocol inventory watchdog (identium_protocol.c:886-926) calls NVIC_SystemReset() after N minutes of inventory with no tag read. The reboot re-runs ConfigureReader, including 0x04 to APP and region. It is off by default (configuration_storage.c:345 inventoryWatchdogMinutes = 0; product_config.h:33). It cannot tell an empty field from a dead module, and it cannot clear a hung module because module power is not switchable. (2) Tag silence is therefore measurable, by identiumInventoryQuietSinceMs (identium_protocol.c:1090). The global silionAsyncPacketCount is indeed never incremented, but pSilionHandle->asyncPacketCount is incremented (silion_protocol.c:2270); it is just never read. (3) board.c:181 itself labels CN1.11 'module RST' ('CN1 pin 11 (module RST) reaches PB5'), which strengthens the point. IDM-AUD-003 F-EL-12 (line 869) adds that pin 11 is NC on the Impinj CM710/510/310 family. (4) 'Every host command fails with DEBUG,STATUS=0101' is overstated. After a module reset, appState stays INVENTORY. A STOP that gets an error reply returns 0 without leaving INVENTORY (sillion_application.c:2355-2380), so most host commands are refused locally (appState != IDLE) and never reach the module. The likely outcome is a reader stuck 'inventorying'. (5) The NA 902-928 MHz scenario is conditional and should be labelled so. It needs a path back to IDLE plus StartInventory without a region re-apply, such as BridgeRelease (2466-2479). It also assumes no AA40 §9.3 power-on region was saved, which is unverified.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="net-01"></a>

#### NET-01 — The ESP32-C3 coprocessor firmware is not in the delivery, so Wi-Fi cannot work; BLE is not designed at all

`HIGH` · 🛑 **BLOCKS PILOT**

**Location.** Firmware/Devices/esp32/esp32_link_protocol.h:6; Firmware/Devices/esp32/esp32_link.c; Firmware/Services/wifi/wifi_service.c; 271026/STM32_Firmware.zip (whole delivery)

**Evidence.** esp32_link_protocol.h:6 says: "This header is shared verbatim by the STM32 firmware and ESP-IDF project." That ESP-IDF project was not delivered. The zip has 2,487 entries. Its only ESP-related files are the STM32 side (esp32_link.c/.h, esp32_link_protocol.h, wifi_service.c/.h) and their build outputs. It has no sdkconfig, CMakeLists.txt, idf_component.yml, app_main or ESP image. The same search over every other zip under PCB/Anas also returns 0 hits. Makefile:105 builds only Src/Drivers/Firmware for the STM32, and Tools/ has no ESP flashing script. The link is a custom I2C protocol: the STM32 is the I2C1 master at 400 kHz (esp32_link.c:244-251) and the ESP is slave 0x42. Each frame is 'R''W' + ver + seq + cmd + socket + len + flags, with CRC-16/CCITT-FALSE and fixed 64-byte replies (esp32_link_protocol.h:7-16, 101-123). No off-the-shelf ESP32-C3 firmware (e.g. ESP-AT) can answer this framing. The STM32 code also depends on ESP behaviour that is described only in its own comments: the ESP replays its saved reply when a sequence number is resent (esp32_link.c:532-533), stages NOT_READY replies (513-519), and comes back unconfigured after a self-restart (wifi_service.c:484-498). BLE: the command set (esp32_link_protocol.h:39-50) covers only Wi-Fi and sockets. A grep for BLE/Bluetooth/GATT/NimBLE in Firmware/, Src/, Tools/ finds nothing. Yet DESIGN JUSTIFICATION.docx item R217 says the ESP32-C3 "will work merely as WIFI/BLE module, so it is interfaced with I2C protocol". U19 (ESP32-C3-WROOM-02U-N4) is in PickAndPlace_PCB1_2026_10_13.xlsx, so it is fitted on every board. The board can program it: per net.py, CN11.2 = EN, CN11.4 = U19.12 TXD, CN11.5 = U19.11 RXD, and J2 shorts U19.8/IO9 to GND for download mode. BOARD_ESP_LINK_ENABLED = 1 (BSP/f411_custom/board_config.h:75), so the STM32 sends HELLO every second and pulses EN after every 3 failed transactions, forever (wifi_service.c:339-344; esp32_link.c:485-490). esp32_link.h:19-20 still says the link is optional/colliding on F411, which is stale.

**Impact.** Wi-Fi (STA, AP, the Wi-Fi host port on 5000, and the Wi-Fi upload/AUTO fallback) cannot work on any unit. BLE (design spec F-04, BLE 5.2) exists in neither the firmware nor the link protocol. SET_WIFI and SET_UPLOAD_IF accept settings that do nothing. Production has no image and no procedure for programming the fitted U19.

**Fix.** Get the ESP-IDF project from the design team: source, sdkconfig, partition table, build steps and version tag. Get a CN11/J2 programming procedure for production. Write a real link specification: framing, HELLO reply layout, reply-replay and NOT_READY semantics, socket auto-reconnect rules, error codes. Integration-test both ends on hardware at 400 kHz, including the C3's I2C slave FIFO behaviour on 231-byte writes. Decide whether BLE is in scope; if it is, extend the protocol. Until ESP firmware exists, disable or hide the Wi-Fi commands on pilot units.

**Also raised as.** HWI-07 (medium), REQ-10 (medium). Earlier audit refs: FW-36, REQ-01

**Verifier corrections.** (1) The zip has 2,482 entries (2,062 files plus 420 directories), not 2,487. The only ESP-related names are the STM32-side esp32_link and wifi files and their build outputs. A search for sdkconfig, CMakeLists, idf_component, app_main and esp-idf found nothing in any other zip under PCB/Anas, including 'BLE reader audit/WIFI_BLE_reader_pcb.zip'. (2) The frame layout ('R''W', ver, seq, cmd, socket, len LE16, flags, CRC LE16) is built in esp32_link.c:182-198. esp32_link_protocol.h:7-16 gives only the magic, sizes and address, and lines 101-123 are only the CRC function. Cite esp32_link.c:182-198 for the framing. (3) 'pulses EN after every 3 failed transactions' understates it. g_failureCount is cleared only at esp32_link.c:268 (init) and :510 (valid reply), and the reset path (135-151, 422-440) does not clear it. After the first 3 failures, every further failed transaction triggers bus recovery and an EN pulse (485-490, 545-548, 326-327). (4) HELLO is sent whatever wifiEnabled is (wifi_service.c:697-701 comes before any wifiEnabled check), so the reset loop runs even with Wi-Fi disabled, which is the default (reader_settings.c:250). (5) SET_UPLOAD_IF,ETH still works. SET_UPLOAD_IF,WIFI does more than nothing: it closes Ethernet socket 0 and stops all active uploads (ethernet_service.c:82-90). Say so. (6) Design spec F-04 (section 3.1) presents Wi-Fi/BLE 5.2 as a 'populated option ... (DNP)'. The finding still stands because U19 is in the pick-and-place file and R217 commits to Wi-Fi/BLE. (7) For blocks: the pilot gate can also be met by disabling or hiding the Wi-Fi commands and the HELLO/EN loop, with documentation, if Wi-Fi is not in the pilot scope.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 2 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="hwi-01"></a>

#### HWI-01 — The Wiegand port added to the board (CN2 on PC14/PC15) has no firmware; the status LED, tag-read beep and IP reset are also missing

`HIGH`

**Location.** Firmware/BSP/f411_custom/board_config.h:18-21 (header comment), whole file (no Wiegand defines); whole own-code tree (Firmware/, Src/, Inc/, Boards/, Bootloader/)

**Evidence.** Netlist (231026/_a4, net.py comp CN2 / R71 / R69): U7.3 PC14 is on net WGDO, going to CN2.3 and to R71 (10k to the 3V3 rail `$1N269`). U7.4 PC15 is on net WGD1, going to CN2.4 and to R69 (10k to 3V3). CN2.2 is GND and CN2.1 is not connected. The silkscreen marks CN2 as "WG" (assembly_reference_top.png). The rev 3 header still says: "No LSE" / "(PC14/PC15 not connected)" (board_config.h:19-20). It also cites docs/hardware/SCH_Schematic1_2026-10-13.pdf and docs/notes/f411-custom-board-port.md (:18, :21), and neither file is in STM32_Firmware.zip (unzip -l shows no docs/). A case-insensitive grep of own code for wiegand, PC14, PC15 and WGD finds only that comment, plus generic GPIOC lines in gpio.c. Identium's firmware spec IDM-INB1-FW-001 §6 (p.9) requires a Wiegand output mode that sends each EPC as a Wiegand-26/34/66 frame (default off). The design spec (p.5, J46; p.11) lists Wiegand D0/D1 as a product interface. Boards/f411/F411_custom.ioc holds only HSE and USB (Mcu.Pin0-5), so no machine-readable pin map exists to catch the omission.

**Impact.** A required host interface that the 13-Oct board now physically provides cannot be used on any unit. The wrong comment shows the BSP was traced from a schematic revision without Wiegand, so the pin map can drift again unnoticed. Any site that integrates through a Wiegand access panel cannot be served.

**Fix.** Add WIEGAND_D0/D1 = PC14/PC15 to board_config.h and fix the header comment. Write a Wiegand-26/34/66 output driver: open-drain, sink-only, OSPEEDR low. Use roughly 50-100 us low pulses at about 1-2 ms bit spacing, timed from a hardware timer, not the superloop. Keep it disabled by default as the spec says. Respect the electrical limits in HWI-02. Ship the schematic the BSP was traced from inside the firmware package, or generate a full-pin .ioc, so the map can be checked.

**Also raised as.** REQ-09 (medium). Earlier audit refs: R2-20, REQ-02, REQ-01

**Verifier corrections.** 1) Replace "The rev 3 header still says" with "The rev 3 header says". The PC14/PC15 comment is new in rev 3. The rev 2 header (_audit2/.../Firmware/BSP/f411_custom/board_config.h:15-21) made no pin-trace claim at all. 2) Delete the inference that "the BSP was traced from a schematic revision without Wiegand". board_config.h:18 says the map was traced from SCH_Schematic1_2026-10-13.pdf, which has the same date as the 13-Oct Gerbers that carry WGDO/WGD1. Write instead: the comment contradicts the netlist of the board it claims to be traced from, and the cited schematic is not in the package, so the cause cannot be checked. 3) Add the acceptance criterion as evidence: FW-001 test table row 6 reads "Wiegand emission verified on scope" (pdftotext line 797). 4) The rest checks out: the netlist (CN2.3=WGDO->R71.2,U7.3; CN2.4=WGD1->R69.2,U7.4; CN2.1 single-pin NET_7; CN2.2 GND), the silkscreen "CN2 WG", the .ioc Mcu.Pin0-5 = PH0/PH1/PA11/PA12 plus two virtual pins, no docs/ in the zip, and FW-001 §6 p.9 / design spec p.5 J46 and p.11.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="req-02"></a>

#### REQ-02 — The immutable bootloader freezes the F411 application at 112 KB (83% used) while 384 KB of MCU flash sits unused, and there is no way to update the bootloader

`HIGH` · 🛑 **BLOCKS PILOT**

**Location.** Boards/f411/STM32F411CEUX_FLASH.ld:47-51; Firmware/Common/firmware_image.h:18-23,53-68,91-95; Firmware/Common/firmware_image.c:16-18; Bootloader/boot_main.c:124-131; Firmware/Services/configuration/reader_settings.c:28-32; Firmware/Services/configuration/configuration_storage.c:15-18

**Evidence.** Linker script: `FLASH (rx) : ORIGIN = 0x8004000, LENGTH = 112K` (STM32F411CEUX_FLASH.ld:51), with the note that "sectors 5-7 hold the settings" (:47-49).

firmware_image.h:18-23 gives sectors 5-7 (384 KB) to reader settings and configuration. reader_settings.c:30-32 uses sector 5 (0x08020000); configuration_storage.c:15-18 uses sectors 6 and 7 (0x08040000 / 0x08060000). The records stored there are a few hundred bytes, yet they take 75% of the 512 KB MCU flash.

The lead measured the shipped f411-release application at about 92.6 KB of the 112 KB slot (83%), and RAM at about 116.6 of 128 KB.

The bootloader rejects any image with loadAddress != FW_APP_BASE or imageSize > FW_APP_MAX_SIZE (firmware_image.c:16-18; firmware_image.h:91, :95). It also refuses to jump outside that window (boot_main.c:124-131). The image header (firmware_image.h:53-68) has no image-type field. No application code erases or writes sector 0 (grep FLASH_SECTOR_0/FW_BOOT_BASE: definitions only). The bootloader and this layout therefore cannot be changed in the field.

These required features are still missing and would have to fit in the remaining ~19 KB:
- Identium TCP V1.0 binary active mode
- Modbus RTU+TCP (REQ-04)
- Wiegand 26/34/66 (REQ-09)
- HTTPS/TLS and DNS (REQ-05)
- log replay (REQ-01)
- rollback (REQ-03)

**Impact.** Once pilot units ship with this bootloader, every later field update is capped at 112 KB. TLS alone would not fit. Delivering the specified feature set later would mean recalling units for SWD reprogramming, which defeats field update.

**Fix.** Fix the layout before any unit leaves Identium:
- Move settings into two 16 KB sectors (1-2) or into the GD25Q.
- Start the application at sector 3 or 4 (about 448-464 KB).
- Have the bootloader take the application base and size from one shared layout definition.
- Add a signed bootloader-update image type (staged copy plus verify), or at least reserve room for one.
- Review the RAM budget (about 11 KB headroom) at the same time.

**Also raised as.** BLD-02 (medium). Earlier audit refs: FW-60, R2-53, R2-26, FW-17

**Verifier corrections.** (a) 'Cannot be changed in the field' is overstated. No code sets write protection on sector 0. brownout.c only writes the BOR bits (it comments that RDP/WRP are left alone). The Makefile 'protect' target sets RDP level 1 only (Makefile:197). Code running from flash can still erase and program flash at RDP 1. A future signed application of 112 KB or less could therefore carry a one-shot bootloader updater. That updater does not exist, there is no image type for it, and a power cut during the sector-0 rewrite bricks the unit. So: 'no designed path; only a brick-risky sector-0 rewrite from the application'. (b) The impact should add that RDP 1 makes SWD recovery a mass erase, which wipes the settings. (c) Sectors 5-7 hold append-only logs (reader_settings.c:21-27), so the space is partly used for wear spreading. It is still wasteful, because the F411's 16 KB sectors 1-3 would serve. 'Records of a few hundred bytes take 75%' should note this.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="req-04"></a>

#### REQ-04 — Modbus RTU/TCP, one of the three required Identium host protocols, is absent, and a Modbus-TCP master on port 8080 stops inventory

`HIGH` · 🛑 **BLOCKS PILOT**

**Location.** Firmware/Services/configuration/configuration_port.c:289-304; Firmware/Services/configuration/module_bridge.c:591-651; Firmware/Application/Silion/sillion_application.c:2422-2451; Firmware/Services/upload/active_upload_transport.c:11-28

**Evidence.** There is no Modbus code in Firmware/ or Src/. A grep finds only a "serial CRC16-Modbus trailer" comment (host_transport.c:248).

What the specs require: RTU at address 0x02 on the serial port, Modbus-TCP on port 8080, and holding register 0xB00 x 125 (Modbus Protocol V1.0 pp.2-3; FW-001 §3.3 p.5; DS §7.4 p.19).

What happens on port 8080 instead:
- The first byte selects the session: `if(g_rxBuffer[0] != 0xEEU)` leads to Module_Bridge_Begin (configuration_port.c:293-295).
- A Modbus-TCP request starts with 00 00 (the document's example is "00 00 00 00 00 06 02 03 0b 00 00 7d"), so it opens a Reader Manager bridge session.
- That session calls SILION_Application_BridgeAcquire (module_bridge.c:618-626), which stops any running inventory (sillion_application.c:2436-2443) and puts the module in raw mode for as long as the client stays connected.

RTU on RS-485 is not possible either, because the serial port is forced to RS-232 at boot (REQ-06). Active mode is not offered on any serial link: the upload transport is Ethernet or Wi-Fi only (active_upload_transport.c:11-28).

**Impact.** Customers who integrate through a PLC over Modbus get no data; FW-001 §3 makes Modbus default-on. Pointing a Modbus master at the documented port silently stops autonomous reading. The binary TCP V1.0 mode is also still absent (prior FW-09/FW-16/R2-01), which leaves Modbus and HTTP as the only routes, and neither conforms.

**Fix.** - Implement a Modbus server: RTU on the host USART with address 0x02 and 3.5-character framing, and TCP on 8080 with MBAP, serving 0xB00 x 125 exactly as the document describes.
- On port 8080, recognise MBAP (protocol id 0x0000 in bytes 2-3) before falling back to the EE/FF dispatcher, or move the config/bridge service to another port as Identium decides.
- Never let an unrecognised client stop inventory.
- Check the flash budget first (REQ-02).
- Add host tests built from the document's RTU and TCP byte examples.

**Also raised as.** . Earlier audit refs: R2-03, R2-51, REQ-04, FW-24

**Verifier corrections.** (a) Inventory is not stopped for good. SILION_Application_BridgeAcquire records bridgeResumeInventory (sillion_application.c:2436-2438), and inventory resumes when the bridge is released (:2475-2477). A persistent Modbus poller still holds it off for as long as it stays connected. Wording: 'stops autonomous reading for as long as a Modbus master stays connected'. (b) Arguably critical: with Modbus absent, TCP active mode being JSON lines rather than V1.0 binary, and HTTP non-conforming (REQ-05), none of the three Identium host protocols conforms. I keep high for this single-protocol finding.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="req-05"></a>

#### REQ-05 — HTTP active mode does not follow the reader's published active-mode format or FW-001: http:// only, literal IPv4 hosts only, non-standard fields, no charset

`HIGH` · 🛑 **BLOCKS PILOT**

**Location.** Firmware/Services/ethernet/http_upload.c:108-172,261-310; Firmware/Services/ethernet/ethernet_service.c:109-123; Firmware/Protocol/identium_protocol.c:199-226,521-579,1155-1161; Firmware/Services/configuration/configuration_service.c:23

**Evidence.** What works: rev 3 adds a real HTTP/1.1 POST client (http_upload.c). It appends "/ip" to reader_name (:286 onward) and feeds the reply body to the command parser (:631-641).

Gaps against reader (pp.1-9) and FW-001 §3.2 (pp.4-5, whose example is "POST https://customer-server/path"):
(a) No HTTPS. Only `"http://"` is accepted, and the code notes it "also rejects https://" (http_upload.c:108, :134). There is no TLS anywhere.
(b) No hostnames. The parser comment reads "Host: dotted IPv4 only." (:151), and ethernet_service.c:111 reads "No usable URL (e.g. a hostname): nothing to connect to". With no DNS, a URL with a hostname never connects, and nothing reports why.
(c) Wrong TagInfo keys. Tags are sent as {"ep","at","rc","ri","fq"} (identium_protocol.c:543, :579). reader (pp.5-6) specifies epc, bank_data, antenna, read_count, protocol, rsi, firstseen_timestamp and lastseen_timestamp. tag_json_format is a fixed constant {1,2,5,8} (configuration_service.c:23).
(d) No reader_exception. Identium_Protocol_ReportReaderException (identium_protocol.c:1155) has no caller anywhere, so the event is never sent. reader says it is uploaded immediately, without subscription.
(e) sync_time_req is off by default and malformed. It needs event code 6, which is not in the default event list (configuration_storage.c:355-358; identium_protocol.c:202-208). Its envelope also omits event_data (:213), where reader p.8 shows `event_data: { }`.

**Impact.** An Identium server, or the SDK's Python HTTP reference script, cannot parse the tag uploads. HTTPS endpoints and hostname URLs, which are the normal case for cloud servers, do not work. Faults never reach the server.

**Fix.** - Emit the reader TagInfo keys. Keep the compact form only as an explicit, documented option, and only if Identium formally adopts the 'Reader-Protocol-Specification' dialect, which was not supplied.
- Add DNS (over W5500 UDP) and TLS, which needs the flash budget from REQ-02. Otherwise get Identium to sign off HTTP-only as a pilot limitation.
- Call ReportReaderException from the module-fault and antenna-fault paths.
- Enable sync_time_req by default in HTTP mode and send it with event_data {}.
- Verify byte-for-byte against the SDK's Python HTTP script.

**Also raised as.** PRO-09 (medium), NET-13 (low). Earlier audit refs: R2-11, R2-01, FW-16, FW-32

**Verifier corrections.** (a) Per the audit rules, replace the reader-model name in the title and evidence with 'the ActModeDataFormat document' (Identium active-mode data format). (b) Item (b), 'nothing reports why', is wrong. The only setter of httpUrl is vendor_config.c:957-963. It validates through HttpUpload_ParseUrl (:490) and returns VENDOR_CONFIG_STATUS_INVALID for https://, hostnames or malformed URLs (:947-950; comment at :455-457). A hostname URL cannot be saved at all; it is rejected at configuration time. (c) For (d), add that Identium_Protocol_ReportReaderException appears in the f411-release map's 'Discarded input sections' (map lines 872-874, inside the block from line 106 to 1332), so it is not even linked.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 2 duplicate(s) were each confirmed by at least 2 of 3.*

### MEDIUM (41)

<a id="sec-04"></a>

#### SEC-04 — No anti-rollback: any older image signed with the key installs, and the header has no monotonic version

`MEDIUM`

**Location.** Firmware/Common/firmware_image.h:53-68; Firmware/Common/firmware_image.c:6-27; Bootloader/boot_image.c:6-51; Firmware/Services/update/firmware_update.c:312-357

**Evidence.** The only version field is free text: 'char version[32]' (firmware_image.h:62). reserved0/reserved1 are always zero (:61, :64; fw_sign.py:133-136). FwImage_HeaderIsValid checks magic, format, board, load address, size and NUL termination only (firmware_image.c:11-24). BootImage_Check never reads the version (boot_image.c:6-51), and neither does FirmwareUpdate_Finish (firmware_update.c:312-357). The running application cannot even state its own image version: it reports a hand-edited 1.0.0.8 (product_config.h:52-56), unrelated to the signed header text 'v0.10.1-1-gfcae054-dirty' (Makefile:165). The zip already contains two such signed -dirty images.

**Impact.** Every image ever signed with a key stays installable for that key's lifetime. Through the unauthenticated update path (SEC-02), an attacker or a careless operator can put any unit back on an old build with known defects, for example one with the open port-5000 surface. Captured update sessions can be replayed. Operators also have no reliable way to tell which build is running.

**Fix.** Add a signed numeric security version (uint32) in reserved0 and a semantic version triple. Have the bootloader refuse images whose security version is below a floor kept in a dedicated flash word, raised when an image is confirmed GOOD (SEC-03). Have FW_END reject a downgrade early with a clear error. Embed the same version in the application (generated version.h) and report it in GET_VERSION and the heartbeat.

**Also raised as.** . Earlier audit refs: FW-44

**Verifier corrections.** 'The running application cannot even state its own image version' is overstated. FW_STATUS (host_interface.c:842-856) reports LAST=<outcome>,VERSION=<text> read from the staging header (firmware_update.c:412-444), which shows the version of the last installed image. It is indirect: it is lost when a new upload starts or TAGLOG_CLEAR chip-erases staging, absent after an SWD flash, and not embedded in the running binary. Reword to 'the application has no build-embedded image version; GET_VERSION/VER reports a hand-edited 1.0.0.8 (product_config.h:52-56) and FW_STATUS only echoes the staging header'.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="sec-05"></a>

#### SEC-05 — Bootloader jumps into the application without verifying it, even right after an install that failed or did not hash-match (TOCTOU on the SPI payload)

`MEDIUM`

**Location.** Bootloader/boot_main.c:60-89,102-108,118-147; Firmware/Common/firmware_image.c:41-52

**Evidence.** The staged payload is read from SPI flash twice: once to hash it (firmware_image.c:41-52, via boot_image.c:30) and again to copy it (boot_main.c:60-78). The post-copy SHA-256 of internal flash (boot_main.c:85-89) only decides whether the state word is written (:106-107). main() then calls Boot_StartApplication unconditionally (:145-147). That function checks only that the stack pointer is in RAM and the reset vector is in the app range (:124-131). A partially programmed or substituted image therefore runs. At a normal boot the installed app is never checked either (boot_image.c:19-25 returns NONE once INSTALLED), although its signed header and hash sit in staging. boot_main.c, boot_staging.c and boot_flash.c are not compiled by any host test (Makefile:213-239 builds only boot_image.c and firmware_update.c).

**Impact.** Security: an attacker with physical access to the SPI bus (a flash emulator, or a clip on U6) can serve the genuine signed image on the verify pass and other code on the copy pass, and that code runs. Without WRP (SEC-06) it can then rewrite sector 0 for good. Reliability: if staging becomes unreadable after an interrupted install has already erased the app, the bootloader marks the image rejected and jumps into a half-written application. The unit is then bricked until on-site recovery.

**Fix.** Jump only to a verified image. After a failed or mismatched install, retry or reset and do not start the app. At every boot, hash the application area against the INSTALLED header in staging (about 0.1 s for 112 KB) and re-install from staging on a mismatch. Copy from a RAM-verified stream: hash each chunk as it is programmed and compare at the end before marking or jumping. Add host or on-target tests for interrupted-install and corrupted-staging cases.

**Also raised as.** . Earlier audit refs: FW-37

**Verifier corrections.** Precision on the reliability scenario: after a failed or mismatched install, the state stays PENDING (boot_main.c:104-107), so the half-written app runs for that boot and the next reset retries the install. The permanent case arises on the retry when the verify pass fails. BootStaging_Read cannot report flash errors (boot_staging.c:138-154 returns 1 for any in-range read), so a busy or garbage read gives a hash mismatch, the image is marked BAD_HASH at boot_main.c:111, and the bootloader jumps into the half-written app from then on. Makefile:213-239 also builds the identium_protocol and tag_log tests; the point that matters stands: boot_main.c, boot_staging.c and boot_flash.c have no host test. For the impact, note that even with WRP (SEC-06) the sector-0 rewrite remains possible from code running in the app.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="sec-06"></a>

#### SEC-06 — Device protection is a manual, optional step: no write protection on the bootloader/key sector, RDP only through 'make protect', no hardware watchdog

`MEDIUM`

**Location.** Makefile:19-21,32,185-187,197-203; Firmware/Services/system/brownout.c:52-59; Bootloader/boot_watchdog.h:4-8; Bootloader/boot_main.c:149-153

**Evidence.** The protect target writes only 'BOR_LEV=$(BOR_LEV) RDP=0xBB' (Makefile:198). No nWRP bits are set anywhere: grepping own code for WRP/OB_WRP/WDG_SW finds only the WRPERR flag mask in boot_flash.c:10. Protection is a separate, optional target (Makefile:19-21) that 'make flash' does not apply (:185-187). The default CONFIG is debug (:32), so 'make flash' programs a debug build that also exposes DEBUG_FAULT over the network (host_interface.c:1334-1344). The firmware deliberately leaves option bytes alone (brownout.c:52-59) and only reports RDP in GET_DIAG (diagnostics.c:41-44,150). The app contains flash erase/program code (reader_settings.c:535-555; configuration_storage.c:273), so any code running in the app can erase sector 0. BOOT0 is on KEY2 (u7_pinmap.txt pin 44) and SWD is on CN7 (pins 34/37). The IWDG runs only if the app started it (boot_watchdog.h:4-8); otherwise a unit with no valid app spins forever (boot_main.c:149-153).

**Impact.** A unit that leaves without 'make protect' has SWD and the USB DFU system bootloader (KEY2) fully open: firmware, Wi-Fi passwords and the bootloader/public key can be read or replaced. Even with RDP 1, any code that ever runs in the application (via SEC-01/SEC-02 today, or a future memory-safety bug) can rewrite the bootloader and its trusted key. That compromise then survives key rotation and cannot be recovered remotely.

**Fix.** Make one factory programming step, and include it in the pilot procedure: write the bootloader and app, set nWRP on sector 0 (bootloader plus key), set RDP level 1 and BOR, and optionally set WDG_SW=0 (hardware IWDG). Verify the result read-back in the factory test. Have the application raise a visible fault in GET_DIAG and the heartbeat if RDP is 0 or sector 0 is unprotected on a release build. Make 'make flash' default to CONFIG=release. Consider leaving CN7 unpopulated on shipped units.

**Also raised as.** . Earlier audit refs: FW-37

**Verifier corrections.** (1) 'The firmware deliberately leaves option bytes alone (brownout.c:52-59)' is inaccurate. brownout.c writes the BOR option bits when RDP is level 0 and no debugger is attached (:47-62), and leaves RDP and WRP alone. (2) Recommendation caveat: on STM32F4, nWRP bits remain software-modifiable at RDP level 1. That is from the ST reference manual, which I did not re-open in this review. Setting nWRP on sector 0 therefore guards against accidental or buggy erase but not against deliberate code running in the app. The impact sentence ('any code that ever runs in the application can rewrite the bootloader') stays true even with WRP set. The real defence is closing SEC-01/SEC-02. (3) Verified: DEBUG_FAULT is absent from build/f411-release/rfid_controller_f411.bin and present in build/f411-debug's binary, so the debug-build exposure applies only to the default CONFIG=debug (Makefile:32, :91-93 adds -DDEBUG).

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="sec-07"></a>

#### SEC-07 — No key rotation or revocation path: one compiled-in key, no key ID, and the bootloader cannot be updated in the field

`MEDIUM`

**Location.** Bootloader/boot_public_key.c:11; Bootloader/boot_image.c:37-44; Firmware/Common/firmware_image.h:53-68; Firmware/Common/firmware_image.c:16

**Evidence.** The bootloader verifies against a single constant key (boot_image.c:37-44 using g_bootPublicKey). The header has no key-ID or image-type field (firmware_image.h:53-68), and loadAddress must equal FW_APP_BASE (firmware_image.c:16), so no signed image can replace sector 0. fw_sign.py:15-18 itself says that without the key 'every reader needs SWD (and, with read protection set, a mass erase) to get a new bootloader'. The spec's image format has an image-type field with 0x02 = bootloader (FW-001 §8.2, p.10).

**Impact.** SEC-01 shows how keys leak. If the replacement key ever leaks after deployment, every fielded unit needs an on-site SWD visit and a mass erase. No remote recovery is possible.

**Fix.** Provision two or three public keys in the bootloader (a primary key plus offline backup keys) with a key index in the header, plus a signed 'revoke key N' record kept in a write-once flash word. Add a signed bootloader-update image type that the bootloader installs with a verify-before-switch scheme, or at least document the SWD rotation procedure and budget for it.

**Also raised as.** . Earlier audit refs: FW-37

**Verifier corrections.** (1) The title's 'the bootloader cannot be updated in the field' and the impact's 'No remote recovery is possible' are overstated. No WRP is set and the app runs privileged with flash-erase code, so an Identium-signed application written for the purpose could rewrite sector 0 with a bootloader that carries a new key. That is risky, since power loss mid-write bricks the unit, and it is not implemented, but it is possible. Even if WRP were added, the app could clear it (see SEC-06). (2) The impact should therefore read: after a key leak, units not yet tampered with can in principle be rotated remotely by a risky self-update. Only units that may already have run attacker code need an on-site SWD visit, with a mass erase if RDP 1 is set. (3) fw_sign.py:15-18 describes losing the key, not rotating it. It is weak support here.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="sec-08"></a>

#### SEC-08 — One idle or half-open client on TCP 5000 can lock the host command and update path until reboot: no keep-alive or idle timeout on the new socket

`MEDIUM`

**Location.** Firmware/Services/host/host_transport.c:521-554; Firmware/Services/ethernet/host_tcp_server.c:29-43,75-124; Firmware/Devices/w5500/w5500.c:705,1806-1829

**Evidence.** The first printable byte from any transport claims the ASCII parser (host_transport.c:521-543). Bytes from other transports are then dropped (:551-554). The TCP server releases ownership only on CLOSE_WAIT/CLOSED/INIT (host_tcp_server.c:103-118 -> :29-43). It has no idle timer, and W5500_Socket3_Listen sets no keep-alive (w5500.c:1806-1829); only socket 0 writes KPALVTR (w5500.c:705).

**Impact.** Once a port-5000 client has sent one byte, every USB CDC and RS-232/485 ASCII command is ignored, including FW_* (the local fallback for updates). This happens if the client stays idle, or if its PC loses power so the connection goes half-open. It lasts until the TCP session closes, which may be never without keep-alive, or until a reboot. The trigger can be malicious, needing only one SYN and one byte, or accidental.

**Fix.** Write Sn_KPALVTR for socket 3 and add an application idle timeout (for example 60 s without a complete command) that closes the socket and releases ownership. Let USB, a physical link, always pre-empt a network owner, or drop global ownership for the FW_* commands.

**Also raised as.** ROB-07 (medium), NET-07 (medium). Earlier audit refs: FW-05, FW-15, R2-21

**Verifier corrections.** Wrong line numbers: the claim on the first printable byte is at host_transport.c:528-550 (not 521-543), and the rejection of non-owner bytes is at :559-565 (not 551-554). Release points are worth citing too: host_tcp_server.c:29-43 (socket closed), the RELEASE command at host_interface.c:995, and wifi_service.c:279/505. No idle-timeout path exists. Precision: on a half-open session the W5500 retransmission timeout would close the socket once the reader transmits on socket 3 (replies, or tags if socket 3 is the tag transport). The 'possibly never' case is the silent idle or half-open session where the reader has nothing to send. The same ownership rule probably applies to the Wi-Fi host socket; that is unverified.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 2 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="sec-11"></a>

#### SEC-11 — Wi-Fi AP can be enabled with no password (allowed and documented), with the same default SSID on every unit

`MEDIUM`

**Location.** Firmware/Services/configuration/reader_settings.c:213,270; Firmware/Application/Host/host_interface.c:101-105,152-156; Firmware/Services/configuration/vendor_config.c:638-670,767-787

**Evidence.** The default AP SSID is 'RFID-Reader' on every unit (reader_settings.c:270), and the AP password defaults to empty (the settings memset at :213). SET_WIFI documents 'empty = open network / open AP' (host_interface.c:101-105) and accepts an empty password (:152-156). Over TCP 8080, 'ap_password' takes any length, with no 8-character minimum (vendor_config.c:638-670, 767-769), and mode 'ap' is accepted (:776-787). In AP mode the ESP32 opens the TCP 5000 command server (wifi_service.c:780-785).

**Impact.** A technician who enables AP mode for commissioning, or an attacker using SEC-02, creates an open access point. Anyone nearby can then reach the full unauthenticated command set, including firmware update. The generic SSID also hides the device's identity.

**Fix.** Reject AP mode without a WPA2 key of at least 8 characters on every path. Generate a per-unit default AP key and print it on the label. Use an Identium-branded per-unit SSID (for example with the MAC suffix). Time the AP out after commissioning.

**Also raised as.** NET-11 (low). Earlier audit refs: FW-36

**Verifier corrections.** (1) Wi-Fi is disabled by default (reader_settings.c:250 wifiEnabled = 0U) and the default mode is STA (:251). The open AP needs an explicit configuration step, which the finding implies but should state. (2) SET_WIFI rejects 1-7 character keys (host_interface.c:154-156) but accepts an empty one. The 8080 vendor path checks only the maximum length (vendor_config.c:652-653), so short keys are accepted there. 'ap' mode is accepted at :786-787. (3) Whether an empty key really produces an open AP depends on the ESP32-C3 firmware, which is not in this tree. The STM32 side forwards a zero-length AP password (wifi_service.c:238-241), and the comment at host_interface.c:103 documents 'open AP' as intended behaviour. Mark the on-air behaviour unverified.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="buf-09"></a>

#### BUF-09 — Driver and bootloader accept only a GigaDevice 32 Mbit part; a Winbond second source (likely under the non-Chinese supply-chain goal) silently disables the tag log and OTA staging

`MEDIUM`

**Location.** Firmware/Devices/spi_flash/gd25q.c:178-184; Firmware/Devices/spi_flash/gd25q.h:21-28; Bootloader/boot_staging.c:135; Firmware/Services/update/firmware_update.c:48-51

**Evidence.** GD25Q_Init returns 0 unless the manufacturer ID is 0xC8 and the capacity ID is 0x16 (gd25q.c:178-183), and the size is a compile-time 4 MB constant (gd25q.h:25). The bootloader requires manufacturer 0xC8 (boot_staging.c:135). Staging availability is simply GD25Q_IsPresent() (firmware_update.c:48-51). U6 is a GD25Q32ESIG in a 208-mil SOIC-8 with WP#/HOLD#/CS# pulled up by 10k to 3V3 (netlist U6, R25, R33, R11). The design spec (§1 item 1) calls for a non-Chinese component supply chain, and GigaDevice is a Chinese vendor.

**Impact.** Fitting a W25Q32 or W25Q128 (pin-compatible, and needed for BUF-02) gives a unit with no offline buffer and no field update. Because of BUF-10, nothing reports it.

**Fix.** Identify the part by JEDEC manufacturer and capacity (support GigaDevice, Winbond and ISSI) and derive size and partition offsets from the capacity ID. Use the same table in the bootloader. Keep 3-byte addressing for parts up to 16 MB.

**Verifier corrections.** The firmware update does not fail silently. With a non-GigaDevice part, FW_BEGIN replies ERROR,FW_BEGIN,<reason>. Only the tag log fails silently (BUF-10). The bootloader checks only the manufacturer byte (boot_staging.c:129-135), while the app checks manufacturer and capacity. So a GigaDevice 128 Mbit part would pass the bootloader and fail the app, which is inconsistent. Units built with the specified GD25Q32ESIG are not affected. The trigger is a BOM substitution. Design spec §3.2 NF-01 requires non-Chinese components where commercially available. Lead-auditor note: the Winbond alternates listed as Option B in RFP IDM-RFP-001 (W25Q32JVSSIQ, W25Q128JVSIQ) will not work with this firmware.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="buf-05"></a>

#### BUF-05 — TAGLOG_CLEAR chip-erases the whole GD25Q32: it wipes the firmware staging half, fails any update in progress and blocks the flash for seconds

`MEDIUM`

**Location.** Firmware/Services/storage/tag_log.c:532-541; Firmware/Devices/spi_flash/gd25q.c:344-364; Firmware/Application/Host/host_interface.c:1306-1320; Firmware/Services/update/firmware_update.c:58-61, 359-379

**Evidence.** TagLog_Task responds to clearRequested with GD25Q_StartChipErase() (tag_log.c:534). That is command 0xC7 (gd25q.c:15, 361), documented as "several seconds" (gd25q.h:65). It covers 0x200000-0x3FFFFF, the staging area (firmware_image.h:98-99). Effects on an update: FirmwareUpdate_Apply writes the PENDING header to staging (firmware_update.c:359-379) and the bootloader installs it at the next reset (boot_main.c:102-107), so a clear between Apply and reboot silently cancels the update. During an upload, StorePage's WaitReady(1000) (firmware_update.c:58-61, 75-81) times out and returns FW_UPDATE_ERR_FLASH. During the erase, appends beyond the 64-entry queue are dropped (tag_log.c:482-486). TAGLOG_CLEAR needs no credential (host_interface.c:1306-1308). It reaches the parser from TCP port 5000 (host_tcp_server.c:97, product_config.h:13), USB (usbd_cdc_if.c:274), Wi-Fi (wifi_service.c:305) and serial (serial_port.c:315). The host test models a whole-chip memset (tag_log_host_test.c:143-150) but never checks that staging survives a clear (TestClear :452-476).

**Impact.** Anyone on the LAN can wipe the offline buffer. An operator clearing the log can cancel or break a staged firmware update. Reads arriving during the multi-second erase are lost.

**Fix.** Clear only the log partition: incremental 64 KB block erases (0xD8) over 0x000000-0x1FFFFF from TagLog_Task, or a logical clear that persists oldestSeq = nextSeq and erases lazily. Refuse the clear while FirmwareUpdate_GetState() is not IDLE/READY-applied. Put destructive commands behind authentication.

**Also raised as.** SEC-09 (low), ROB-04 (medium), REQ-12 (low). Earlier audit refs: FW-24

**Verifier corrections.** (1) The window between Apply and reboot is only 500 ms (host_interface.c:834, ReaderSettings_RequestReboot(500U)), so that case is narrow. The wider exposure is a clear after FW_END and before FW_APPLY. If the erase is still running, Apply's StorePage WaitReady(1000) fails with ERR_FLASH. If it has finished, Apply rewrites and checks only the header page (firmware_update.c:366-377), replies OK,FW_APPLY and reboots, and the bootloader then rejects the wiped image (boot_main.c:109-111). The update fails late after a success reply. (2) Say explicitly that nothing is bricked: the bootloader validates the image before installing (boot_main.c:99-107). (3) The legacy parser has no authentication for any command (no auth logic in host_interface.c or host_transport.c), so TAGLOG_CLEAR is one case of the broader FW-24 issue.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 3 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="buf-08"></a>

#### BUF-08 — Offline timestamps cannot be recovered: before time sync they are uptime of an unidentified boot, and the time-sync bootstrap is off by default and not awaited before buffered tags are sent

`MEDIUM`

**Location.** Firmware/Services/storage/tag_log.c:57-58, 149, 185-188; Firmware/Services/Time/time_service.c:16-18, 31-33, 59-72; Firmware/Application/Host/host_interface.c:229-233

**Evidence.** The record time comes from Time_GetEpochMs() when the record is appended (tag_log.c:149), and the record is flagged epoch only if the value is >= 1e12 (:57-58, 185-188). The epoch base is zeroed at every boot (time_service.c:31-33) and set only by sync_time (identium_command.c:678). The reader asks for it only while connected and only with event 6 enabled (identium_protocol.c:202-207, 1200-1204). host_interface.c:233 says U means "uptime of the boot that stored it", but the 32-byte layout (tag_log.c:19-28) has no boot counter or time anchor. The board has no 32.768 kHz crystal (PC14/PC15 are Wiegand, u7_pinmap.txt pins 3-4) and VBAT is tied to 3V3. Elapsed time is still uint32 (time_service.c:67-71), so logged epoch times jump back 49.7 days without a re-sync. SILION_Tag_t.timestampMs is the module's own counter (silion_protocol.c:2450), so log time is taken when the main loop pops the tag and inherits main-loop latency.

**Impact.** In the typical field case, site power cycles and the backhaul is still down, so every buffered read carries a meaningless uptime that cannot be converted to wall time even after a later sync. Spec FW-001 §2 and §5.1 require firstseen and lastseen in Unix ms.

**Fix.** Persist a boot counter and store it in each record or in a per-sector header. When sync_time arrives, write a time-anchor record (boot id, uptime, epoch) so the drain can rewrite U times to epoch. Use a 64-bit tick. Stamp reads at UART parse time. Fit an LSE crystal and VBAT backup at the next hardware spin.

**Also raised as.** REQ-08 (medium). Earlier audit refs: FW-26, R2-23

**Verifier corrections.** (1) The spec citation should be FW-001 §3.2 (TagInfo, and the RTC TIME BOOTSTRAP note that sync_time_req comes before inventory and before any drain) plus §5.1, not "§2 and §5.1". (2) Addition: the sync request is attempted once per boot and marked done even when event 6 is disabled, because SendSyncTimeRequest returns 1 at identium_protocol.c:202-207 and :1202-1204 sets identiumSyncTimeRequested. If event 6 is enabled later, no request is sent until reboot. (3) Context: the design spec assumed RTC-backed timestamps (§7.3, "RTC-backed (drift <5 ppm with TCXO option)"), which the board cannot provide (u7_pinmap.txt pin 1 VBAT is on the 3V3 net, and pins 3-4 PC14/PC15 are the Wiegand nets WGDO/WGD1).

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="hwi-03"></a>

#### HWI-03 — RS-485 mode can only be selected at compile time; every shipped build runs RS-232, so the RS-485 port is unusable

`MEDIUM`

**Location.** Firmware/Config/product_config.h:39-43; Firmware/Application/configuration/firmware_configuration.c:373-376; Firmware/Services/serial/serial_port.c:200, :203-223; Makefile:61; .cproject:214-215

**Evidence.** product_config.h:42 contains "#define PRODUCT_SERIAL_DEFAULT_MODE     0U" (0 = RS232). firmware_configuration.c:373-376 passes that constant to Serial_Port_Init. Serial_Port_SetMode is the only routine that drives PB14 (mux), PA8 (TPS22919), PB12 (DE) and PB13 (RE#), and its only caller is serial_port.c:200 inside Serial_Port_Init. A grep of Services/configuration, Application/Host and Protocol for 485, rs232 and serial_mode finds no command, setting or persisted field that changes the mode. Neither the Makefile F411 DEFS (:61) nor the CubeIDE F411 configuration (.cproject:214-215) defines PRODUCT_SERIAL_DEFAULT_MODE. The hardware supports RS-485 correctly: U12 S=high selects U5 (SP3485) on 1B2/2B2, DE is on PB12 and RE# on PB13 (netlist U12/U5), and R18 even makes RS-485 the power-on default (board_config.h:50-51). The design spec lists RS-485 as interface J45 (p.5).

**Impact.** Every unit built from this tree leaves the SP3485 path permanently deselected (PB14 low), so no RS-485 host or Modbus-RTU integration can be commissioned in the field. The only workaround is a separate, differently signed firmware build per unit type.

**Fix.** Add a persistent serial-mode setting, with a protocol and config command and a GET to report it, that calls Serial_Port_SetMode at boot and on change. Report the mode in diagnostics. Decide whether the power-on default should follow the hardware default (RS-485 via R18) or stay RS-232, and document it. Fix HWI-04 at the same time, because enabling RS-485 activates that race.

**Also raised as.** REQ-06 (medium). Earlier audit refs: REQ-01

**Verifier corrections.** 1) Overstated: "Serial_Port_SetMode is the only routine that drives PB14, PA8, PB12 and PB13". PB12/PB13 are also driven by Serial_Port_SetDriverEnable from Serial_Port_StartNextChunk (serial_port.c:140-151), although only in RS-485 mode. All four pins are also written low by Serial_Port_ConfigOutputPin (serial_port.c:63) during init. Correct statement: the mode (PB14 mux, PA8 MAX3232 power) is set only through Serial_Port_SetMode, whose sole caller is serial_port.c:200. 2) Severity: product_config.h:39-40 documents the workaround ("Can be overridden from the compiler command line"). A -DPRODUCT_SERIAL_DEFAULT_MODE=1 build gives RS-485, so the rubric points to medium (real defect with a workaround) rather than high. Raise it to high only if the pilot site needs RS-485. 3) Optionally add FW-001 line 303 (Modbus RTU on RS-485 concurrently with TCP) as the requirement being missed.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="pro-07"></a>

#### PRO-07 — 0xEE frames on the RS-232/RS-485/USB COM route: the CRC is never checked or sent, flags are ignored, and CLEAR/SWITCH accept any payload

`MEDIUM`

**Location.** Firmware/Services/host/host_transport.c:234-266; Firmware/Services/configuration/configuration_port.c:60-69, 78-82, 389-416; Firmware/Services/configuration/vendor_config.c:1432-1443; Firmware/Services/configuration/vendor_config.h:12-13

**Evidence.** Rev 3 adds 0xEE configuration requests on the serial and USB links. The frame assembler uses flag bit 1 only to add 2 bytes to the expected length (host_transport.c:245-249). It then passes the frame to Configuration_Port_HandleLinkRequest (:259-265), which checks only header and length (configuration_port.c:397-406). The CRC is never verified. Replies are built with flag byte 0 and no CRC (configuration_port.c:60-69) and sent on the serial link as-is (:78-82). The reply-required flag (bit 0) is ignored: every request is answered. TCP Protocol V1.0 says the CRC exists when data travels over the serial port and defines the two flag bits (p.1-2). The firmware spec says the CRC flag is 'always 1 on UART' (IDM-INB1-FW-001 p.4). The commands reachable this way include CLEAR (0x20, factory-clear to passive mode) and SWITCH (0x23, toggle active/passive). They are documented with fixed content "00 50" and "01" (vendor_config.h:12-13), but the handler accepts them with any payload (vendor_config.c:1432-1443). They also include SET_ACTIVE and SET_STATIC JSON writes.

**Impact.** On a multi-drop or noisy RS-485 bus, a corrupted frame can be executed and clear configuration, flip the work state, or write config. A host that follows the spec and expects a CRC on serial replies cannot validate them. SWITCH is a toggle, so a host retry flips the state back.

**Fix.** When flag bit 1 is set, verify the CRC16 (confirm the algorithm with Identium; the code comment assumes CRC16-Modbus) and drop bad frames. Always require a CRC on the serial link. Append a CRC and set the flag in serial replies. Honour the reply-required bit. Validate the exact CLEAR/SWITCH payloads, and give SWITCH an explicit target state.

**Also raised as.** ROB-08 (medium)

**Verifier corrections.** (a) CLEAR (0x20) and SWITCH (0x23) are vendor-tool commands and are not in TCP Protocol V1.0. The serial CRC rule comes from the TCP V1.0 frame definitions (reader-to-server p.1, flags p.2, server-to-reader p.3) and from FW spec p.4 ('always 1 on UART'). Cite those, not the TCP V1.0 command set. (b) For a corrupted frame to execute, the corruption must keep the 0xEE 0x00 header and a consistent length (host_transport.c:251-257, configuration_port.c:397-406). The likelihood is moderate rather than high. The defect is still real. (c) CLEAR does a full static '{"reset":1}' restore plus passive mode (vendor_config.c:1316-1338), so 'factory-clear' is accurate.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="rfm-06"></a>

#### RFM-06 — With the module absent or dead, each degraded-mode retry blocks the whole main loop for about 5.5 s every 15.5 s, and boot is delayed by about 16.5 s

`MEDIUM`

**Location.** Firmware/Application/configuration/firmware_configuration.c:70-72, 239-268, 542-550; Firmware/Application/Silion/sillion_application.c:72-78, 149-152, 211, 229-234, 259-263; Firmware/Application/Silion/sillion_application_port.c:432-479

**Evidence.** ConfigureReader always spends 200 ms (:211) plus 300 ms (:234), then waits for the GET_VERSION reply with the default 5000 ms Transaction timeout (:149-152, :259-263). SILION_WaitForResponse services only the RFID RX queue and the watchdog (port.c:438-476). The boot call is StartReader(3U) at firmware_configuration.c:542, before the main loop starts. Firmware_Configuration_Task re-arms the retry 10 s after each attempt (267), so the cycle is about 10 s idle plus 5.5 s blocked. The code comment claims "Each retry blocks the main loop for about a second" (70-71). Each failed attempt also prints "DEBUG,RX_TIMEOUT" to the active host transport (sillion_application.c:76-78). vendor_config.c:1343-1346 notes that Reader Manager waits only 1000 ms for a reply.

**Impact.** A reader with a missing, unplugged or dead module is the case GET_DIAG was meant to diagnose (comment at 537-539). Instead it is unresponsive about 35% of the time. Ethernet, Wi-Fi, USB, GPI sampling and the vendor tools stall for 5.5 s at a time, so tool requests and diagnostics time out intermittently, and the host stream gets unsolicited DEBUG lines. The first 16.5 s after power-on are dead for every interface.

**Fix.** Probe for the module with a short timeout (for example GET_VERSION with 200-300 ms, since the doc's replies are immediate) before running the full sequence. Back off the retry interval exponentially, to minutes. Better still, make bring-up a non-blocking state machine stepped from the main loop. Remove DEBUG output from the host stream (or gate it behind a debug flag) and fix the comment.

**Also raised as.** ROB-05 (medium). Earlier audit refs: FW-34, R2-06, FW-22, FW-50

**Verifier corrections.** (1) The 5 s wait applies to an absent module as well as a dead one, because PA10 has an internal pull-up (board.c GPIO_ConfigAlternate ... GPIO_PIN_PU, ~lines 47-60), so a floating RX produces no UART-error early exit. (2) The DEBUG,RX_TIMEOUT lines go to HOST_SendString. During the boot attempts no host transport is normally active yet, so the unsolicited lines matter mainly for the runtime retries. (3) The vendor reply-timeout comment is at vendor_config.c:1341-1346. The other references are exact (firmware_configuration.c:70-72, 267, 542-549; sillion_application.c:151, 211, 234, 76-78; port.c:432-479).

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="rob-02"></a>

#### ROB-02 — All reader settings live in one 128 KB MCU flash sector: every save stalls the whole MCU (RFID, Ethernet, USB) for the erase, and a power cut during it loses IP, MAC, server, Wi-Fi and RF settings

`MEDIUM`

**Location.** Firmware/Services/configuration/configuration_storage.c:9-18, :548-557; Firmware/Services/configuration/reader_settings.c:28-31, :540-560; Firmware/Protocol/identium_command.c:817-830; Firmware/Services/configuration/vendor_config.c:1047-1077, :1343-1345; build/f411-release/rfid_controller_f411.map:397, :1581-1583

**Evidence.** Configuration storage uses F411 sectors 6/7 and reader settings use sector 5 (configuration_storage.c:9-18; reader_settings.c:28-31). These are in the same single 512 KB bank as the code and vector table (Boards/f411/STM32F411CEUX_FLASH.ld:51, FLASH at 0x08004000). Configuration_Storage_Save erases a sector on every save (configuration_storage.c:554). Nothing runs from RAM: the .RamFunc input is discarded (address 0, map:397) and HAL_FLASHEx_Erase sits in flash at 0x08004dfc (map:1581-1583). The design's own comment says the F429 staging was put in bank 2 'so the CPU keeps running from bank 1 while they erase' (firmware_update.c:106-107); the F411 has no second bank. vendor_config.c:1343-1345 gives 1-2 s per erase. set_static_conf saves without checking inventory state (identium_command.c:817-830 -> configuration_service.c:2265). VendorConfig_SetActive saves runtime config and reader settings without pausing inventory (vendor_config.c:1052-1077). The read-stall-during-erase behaviour comes from ST's reference manual (RM0383, not opened in this audit) and is the same behaviour the design relies on in the F429 comment.

**Impact.** For the whole erase no interrupt handler can fetch code. USART1 (RFID module, 115200 baud) overruns and drops bytes, so async tag frames are lost. The 4096-byte RX ring added in rev 3 (sillion_application_port.c:19) cannot help, because the ISR never runs. USART2 and USB OUT bytes are lost too. SysTick ticks collapse into one, so Time_GetTickMs and the epoch clock (time_service.c:65-72) fall 1-2 s behind on every save.

**Fix.** Refuse persistent saves while inventory runs, as ApplyTempStaticConfig already does (configuration_service.c:2312), or pause and resume inventory around a save. Better: keep settings in the GD25Q (4 KB sectors), or append to a pre-erased log so erases are rare and can be scheduled. Alternatively run the erase with VTOR and the critical ISRs in SRAM. Re-sync time after a save.

**Also raised as.** ROB-03 (medium). Earlier audit refs: R2-34, R2-26 (rev 2 on F429: erase in bank 2 blocked only the superloop; on the F411 it also stalls ISRs)

**Verifier corrections.** The title overstates it. Only Configuration_Storage_Save erases a 128 KB sector on every save (configuration_storage.c:554). ReaderSettings_Save is append-only and erases sector 5 only when the log is full (444-byte records, 295 slots) or on the first save after a layout migration (reader_settings.c:540-563). Suggested title: 'every configuration save (and every ~295th reader-settings save) erases a 128 KB sector...'. Impact correction: USB OUT data is probably not lost, because the OTG FS core NAKs bulk OUT until the endpoint is re-armed, so the host retries (not checked on hardware). The USART1 and USART2 byte loss and the SysTick collapse stand. VTOR is in flash: system_stm32f4xx.c:96-112 and :182 use VECT_TAB_OFFSET 0x4000 with VECT_TAB_SRAM commented out. This is worth citing.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="rob-06"></a>

#### ROB-06 — The W5500 is never health-checked: a dead or wedged chip goes undetected and adds 20-40 ms of socket re-open polling to every main-loop pass, starving tag processing

`MEDIUM`

**Location.** Firmware/Devices/w5500/w5500.c:239-288, :423, :1232-1243, :1606-1612; Firmware/Application/configuration/firmware_configuration.c:474-490; Firmware/Services/configuration/configuration_port.c:365-381; Firmware/Services/configuration/discovery_service.c:72-76

**Evidence.** W5500_Init pulses reset and sets initialized=1 without reading VERSIONR (w5500.c:239-288). W5500_ReadVersion (w5500.c:423) has no caller (grep). W5500_Init is called only once, at boot (firmware_configuration.c:477), so the degraded-Ethernet branch (:474-490) cannot catch a dead chip, and there is no later re-init or reset. If the chip reads 0x00, Configuration_Port_Task sees CLOSED on every pass and calls W5500_StartTCPServer1, which polls 20 x 1 ms for INIT (w5500.c:1232-1243). Discovery_Service_Task, also called every pass, calls W5500_Socket2_OpenUdp, which polls 20 x 1 ms (discovery_service.c:72-76; w5500.c:1606-1612). Neither is rate-limited; host_tcp_server.c is limited to once per second.

**Impact.** The main-loop period becomes 20-40 ms or more. SILION_Application_Task publishes only one async tag per pass (sillion_application.c:1468-1482) into a 32-entry queue (silion_protocol.h:127), so above about 25-50 tags/s tags are dropped, and USB/serial command latency rises. This contradicts the comment that USB, serial and Wi-Fi keep working when the W5500 fails (firmware_configuration.c:474-475).

**Fix.** Check VERSIONR (0x04) at init and periodically. On failure mark Ethernet degraded, stop calling the socket tasks, and retry a hardware reset (PB1) with back-off. Rate-limit socket re-open attempts the way host_tcp_server.c does.

**Also raised as.** NET-08 (medium). Earlier audit refs: FW-35

**Verifier corrections.** Clarify the 20-40 ms. If MISO reads 0x00, socket 1 is CLOSED and both re-open paths poll, giving about 40 ms. If MISO reads 0xFF, socket 1 falls into the 'else' branch (configuration_port.c:385-388) and only the discovery re-open costs about 20 ms. The one-tag-per-pass publish is at sillion_application.c:1467-1482. The queue define is at Firmware/Protocols/silion/silion_protocol.h:127 (path missing in the finding). Note also that the W5500_Init failure branch can only fire on NULL arguments.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="hwi-02"></a>

#### HWI-02 — Wiegand lines go straight from the field connector to the backup-domain pins PC14/PC15, with 3V3 pull-ups and no level translation or protection

`MEDIUM`

**Location.** Board netlist CN2.3/CN2.4 -> U7.3/U7.4 (R71/R69 to 3V3); constraint on any driver added for HWI-01

**Evidence.** Netlist: WGDO = CN2.3, R71.2, U7.3. WGD1 = CN2.4, R69.2, U7.4. These nets carry no series resistor, TVS or buffer. Both pull-ups go to 3V3 (`$1N269`), and VBAT (U7.1) is tied to the same rail. LEAD_FACTS.md: PC14/PC15 are backup-domain pins that ST limits to 3 mA sink, 2 MHz and 30 pF, and ST forbids sourcing current from them. The design spec (p.11) puts Wiegand behind a TI TXS0108EPWR, "3.3 V CMOS to 5 V open-drain", and the board omits it. On a Wiegand link the receiving panel normally supplies the pull-ups, and a reader output is open-collector. This board instead carries receiver-style pull-ups to 3.3 V. Whether PC14/PC15 tolerate 5 V (FT) or not (TC) is unverified here, because the ST pin table was not opened in this review.

**Impact.** The spec requires Wiegand output. Driving a panel that pulls D0/D1 up to 5 V (the usual Wiegand practice) means the MCU sinks the panel current through pins limited to 3 mA, and the idle line sits above VDD, which may inject current if the pins are not 5 V tolerant. Long field cables carry ESD and surges straight onto MCU pins. Pulse timing is not the problem: a 2 MHz edge limit is ample for 50-100 us pulses.

**Fix.** Hardware respin: buffer D0/D1 with open-drain transistors or the specified TXS0108E-class translator plus series resistors and TVS, and decide whether the port is an output, an input or both. Until then, any HWI-01 driver must be open-drain, sink-only and low-speed, and must be documented as compatible only with panels that pull up to 3.3 V through 1 kOhm or more. Confirm the PC14/PC15 I/O type from the STM32F411 datasheet pin table.

**Also raised as.** . Earlier audit refs: REQ-02, FW-26

**Verifier corrections.** 1) Keep 5 V tolerance as unverified. An attempt to fetch the ST STM32F411 datasheet pin table timed out. If PC14/PC15 are FT, the "idle line above VDD may inject current" part of the impact drops out, leaving drive-current and ESD/surge exposure. Say so explicitly. 2) The statements "the receiving panel normally supplies the pull-ups" and "a reader output is open-collector" are general industry practice with no citation. Label them as engineering context, not evidence. 3) Name the ST 3 mA / 2 MHz / 30 pF / no-sourcing limits as coming from LEAD_FACTS.md (a paraphrase of ST's backup-domain power-switch note), not from a datasheet page opened in this review.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="hwi-04"></a>

#### HWI-04 — Non-atomic GPIOB ODR writes race between the RS-485 DE/RE switching in the USART2 ISR and main-loop writes to relays, LED and ESP32 EN on the same port

`MEDIUM`

**Location.** Firmware/Drivers/gpio/gpio.c:160-164; Firmware/Services/serial/serial_port.c:67-89, :132-160, :317-324; Firmware/BSP/industrial_io.c:176-180; Firmware/Devices/esp32/esp32_link.c:405, :410, :426

**Evidence.** GPIO_WriteToOutputPin does read-modify-write on ODR: "pGPIOx->ODR |= (1UL << PinNumber);" (gpio.c:162). In RS-485 mode the USART2 TC interrupt path (serial_port.c:317-324, then StartNextChunk :140-143) calls Serial_Port_SetDriverEnable(0), which writes PB12 (DE) and PB13 (RE#) on GPIOB (board_config.h:56-62). The main loop writes the same GPIOB ODR with no critical section: relays PB2/PB10 through Industrial_IO_SetOutput (industrial_io.c:176-180, board_config.h:137-140), ESP32 EN PB9 (esp32_link.c:426), I2C recovery PB6 (esp32_link.c:405/410) and LED PB8 (board.c:219). Serial_Port_Send guards its own call with CriticalSection_Enter (serial_port.c:240), but those other writers do not.

**Impact.** Suppose a relay or EN write reads GPIOB->ODR while DE=1, the TC ISR then clears DE and sets RE# low, and the main loop writes the stale value back. DE stays high and RE# stays high with an empty TX queue, so no further TC arrives to correct it. The reader then drives the multi-drop RS-485 bus indefinitely and cannot receive until the next transmit. The reverse interleaving silently undoes a relay or ESP-EN change. The defect is latent while RS-485 is compile-time-only (HWI-03) and goes live as soon as RS-485 is enabled.

**Fix.** Make GPIO_WriteToOutputPin and GPIO_ToggleOutputPin write BSRR (set = 1<<pin, reset = 1<<(pin+16)). That is atomic on Cortex-M and removes the race for every port without critical sections.

**Also raised as.** . Earlier audit refs: FW-54

**Verifier corrections.** 1) Remove LED PB8 (board.c:219) from the list of racing main-loop writers. It runs once in Board_F411_GPIO_Init, before Serial_Port_Init and before USART2 interrupts can fire. 2) Add further GPIOB writers that do race: W5500 reset PB1 (w5500.c:269, :277) and ESP EN PB9 at esp32_link.c:148 and :240. The relays (gpio_service.c:148/:318 -> industrial_io.c:176) and esp32_link.c:405/:410/:426 run from the superloop (main.c:137, :140). 3) Delete "The reverse interleaving silently undoes a relay or ESP-EN change". The USART2 ISR's read-modify-write cannot be preempted by main-loop code, and no higher-priority ISR that writes GPIOB was found. The only harmful interleaving is a main-loop RMW interrupted by the TC ISR, which leaves DE/RE# stuck high. 4) Severity: the path is unreachable in the delivered F411 build, because g_serialMode never becomes RS485 (see HWI-03), and the ISR touches GPIOB only under the RS-485 gates at serial_port.c:140 and :147. Rate low now; it becomes medium once RS-485 is selectable.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="hwi-06"></a>

#### HWI-06 — set_gpo still accepts GPO 4, which does not exist on this 3-relay board, so a multi-output command half-applies and returns the wrong error; GPO numbering also departs from FW-001 §6

`MEDIUM`

**Location.** Firmware/BSP/industrial_io.h:6; Firmware/Services/GPIO/gpio_service.c:8, :251-265, :316-318; Firmware/Services/GPIO/gpio_service.h:26; Firmware/BSP/industrial_io.c:169-173; Firmware/BSP/f411_custom/board_config.h:143-144

**Evidence.** INDUSTRIAL_IO_COUNT is 4U (industrial_io.h:6), and GPIO_SERVICE_MAX_OUTPUTS equals it (gpio_service.c:8). The validation loop rejects only outputNumber > 4 (:251-254), and its comment still says "four-output controller" (:258). On the F411, BOARD_OUTPUT4_PORT is NULL (board_config.h:143), so GPO 4 passes validation. In the apply loop each item first runs "gpioOutputStates[index] = state;" (:316). Industrial_IO_SetOutput then returns 0 for the NULL port (industrial_io.c:169-173), and set_gpo returns 20003 "invalid set_gpo item" after earlier items have already switched their relays. gpio_service.h:26 promises "one atomic set_gpo operation containing 1..4 outputs". Separately, the firmware maps GPO 1-3 to PB2/PB10/PA0, which matches the silkscreen "GPO 1/2/3 (DRY)" on CN12/CN13/CN14 (netlist U4/U9/U16 and the assembly image). IDM-INB1-FW-001 §6 (p.9) instead defines GPO 1 as a TPL7407L transistor output and GPO 2-4 as relays 1-3. The board has no such transistor output.

**Impact.** A host following the Identium protocols, which expose GPO 1-4, that sends e.g. [GPO1 on, GPO4 on] sees an error while relay 1 has already energised. The reported GPO 4 state is also corrupted. A host written to FW-001 §6 switches a different relay than it intends.

**Fix.** Validate against Industrial_IO_GetOutputCount() or a per-board output count before applying anything, and return "not supported operation" (code 2) for GPO 4 and above on this board. Fix the comment. Get Identium to decide the logical GPO numbering for this hardware (silkscreen or FW-001 §6), update the spec or the map, and expose the output count in capabilities.

**Also raised as.** . Earlier audit refs: R2-18, R2-17

**Verifier corrections.** 1) The validation check is at gpio_service.c:252-255, not :251-254. The comment is at :257-258. 2) Delete "The reported GPO 4 state is also corrupted". gpioOutputStates (gpio_service.c:15) is read only internally (:153, :315), and no host-visible getter reads it. 3) Name the path that exhibits the partial apply. It is the JSON set_gpo command (identium_command.c:706-760), whose parser does no range check on "gpo" (identium_command.c:441). The text SET_GPO command (host_interface.c:2400-2409) is single-item, so GPO 4 there returns ERROR with no partial apply. 4) Add: earlier items with a duration have already armed their pulse timers (gpio_service.c:329-334) when the error returns. 5) In the recommendation, note that GET_CAPABILITIES already reports GPO=Industrial_IO_GetOutputCount() (host_interface.c:2341-2346), so only the JSON/TCP capability exposure is missing.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="buf-04"></a>

#### BUF-04 — Every read is written to flash continuously (online or offline): uniform but heavy wear reaches the rated endurance in about 8-25 months at 100-300 reads/s, with no erase counting, verify or failure reporting

`MEDIUM`

**Location.** Firmware/Application/Silion/sillion_application.c:31; Firmware/Services/storage/tag_log.c:549-576, 596-610

**Evidence.** TagLog_Append runs whether or not the host link is up (sillion_application.c:31). Each 4 KB sector is erased once per 128 records (tag_log.c:549-576), and the lead's run counted 2,343 erases for 300k records. The ring cycles every log sector once per 65,536 records, so erases per sector per day = rate * 86400 / 65536. At 50/s that is 66/day and about 4.2 years to 100k cycles; at 100/s, 132/day and about 2.1 years; at 300/s, 396/day and about 253 days. The 100k P/E figure is the usual GD25Q32E datasheet rating; the datasheet is not in the package, so the figure is unverified here. Page programs are not read back (:596-610), no per-sector erase count is stored, and a failed erase or program only shows up later as unreadable records.

**Impact.** Pilot and production units at busy portals wear out the 2 MB log within the product life. They then lose buffered reads silently, and TAGLOG_STATUS still reports READY.

**Fix.** Dedup first (BUF-03). Log only reads that were not delivered or acknowledged live, or only while the uplink is down. Add a per-sector header with an erase count, verify each program by reading back the CRC, retire failing sectors, and expose wear in status. Use the extra space from BUF-02 to cut cycles per day.

**Verifier corrections.** No line corrections. Two caveats. The endurance figures assume continuous 24/7 read rates. The 100k P/E rating (and the 45/400 ms erase times used elsewhere) comes from general datasheet knowledge or code comments, not from a document in the package, as the finding says. One addition: erase failures cannot be detected at all. The driver checks only WEL before starting an operation (gd25q.c:118-125) and never checks for an erase or program failure afterwards.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="buf-06"></a>

#### BUF-06 — After TAGLOG_CLEAR, logging restarts mid-sector; a reboot then loses the post-clear records in that sector, and before the next sector boundary it resets the sequence counter to 0

`MEDIUM`

**Location.** Firmware/Services/storage/tag_log.c:272-303, 371-374, 523-529

**Evidence.** When the chip erase completes, oldestSeq and headSector are set from the unaligned nextSeq (tag_log.c:527-528). TagLog_SectorFirstSeq stops at the first erased slot and treats the sector as empty (:272-293: "an erased slot before any intact one means the sector holds nothing"). If no sector qualifies, the mount sets nextSeq = 0 (:371-374). That contradicts tag_log.h:82-83 ("Sequence numbers keep counting"). TestClear (tag_log_host_test.c:452-476) never re-mounts after a clear. My Python transcription of the mount logic (which reproduces the lead's 65,376 result) gives: clear at seq 300,000 (slot offset 96), 10 appends, reboot -> oldest=0 next=0 and 0 of 10 records readable; with 100 appends, 32 are lost.

**Impact.** The sequence counter can go backwards, reusing numbers the host has already seen while the host's cursor sits in the future, so a cursor-based client misses records. Up to 127 reads are lost on every clear followed by a reboot.

**Fix.** After a clear, round nextSeq up to the next sector boundary. Alternatively, make SectorFirstSeq skip leading erased slots, or persist the clear point. Add a host test for Clear, then k < 128 appends, then Init, then verify sequence continuity and readability.

**Verifier corrections.** Add the condition: the loss happens only when the clear lands on a sequence number that is not a multiple of 128 (a 127/128 chance). The lost count is 128 minus the slot offset, up to 127. The numbers check out: clear at 300,000 (sector 295, offset 96), 10 appends, reboot gives oldest=0, next=0, 0 of 10 readable; with 100 appends, oldest=300032, next=300100, 68 of 100 readable, so 32 lost. On impact: once the counter resets, a cursor client sitting near 300,010 gets nothing until the counter catches up, because TagLog_Read rejects seq >= nextSeq (tag_log.c:638) and NEXT keeps advancing (host_interface.c:297-302). The loss is effectively larger than the up-to-127 records stated.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="buf-07"></a>

#### BUF-07 — TAGLOG_READ silently skips records whenever the flash is still erasing after 50 ms and advances NEXT past them, so replay during live inventory loses data

`MEDIUM`

**Location.** Firmware/Services/storage/tag_log.c:54-55, 644-651; Firmware/Application/Host/host_interface.c:259-262, 297-302

**Evidence.** TagLog_Read returns 0 if GD25Q_WaitReady(TAG_LOG_READ_WAIT_MS = 50) times out (tag_log.c:55, 645-650). The code's own comments put a sector erase at 45 ms typical (tag_log.h:16) and 400 ms worst case (tag_log.c:50-51), and an erase starts every 128 records. HOST_SendTagLogRecords handles any 0 return with `continue;` (host_interface.c:261-262), the same as for an overwritten record, and reports NEXT past it (:297-302). The host-test flash model's WaitReady always succeeds immediately (tag_log_host_test.c:79-84), so this path is never exercised.

**Impact.** A host draining the log while the reader is still inventorying permanently skips intact records whenever an erase runs past 50 ms. Erase time grows as the flash wears. The host cannot tell a busy record from an overwritten one.

**Fix.** Return a tri-state result (OK / GONE / BUSY). On BUSY, end the batch and return NEXT equal to the busy sequence. Alternatively, wait the full erase timeout or use erase-suspend (0x75) for reads.

**Verifier corrections.** (1) Scope: the skip hits only the records read before the erase finishes. Every later TagLog_Read in the batch waits up to another 50 ms (flashBusy stays set, tag_log.c:645-648), so a 400 ms erase costs at most the first ~8 records of a 20-record batch. (2) A missing and more frequent loss path: the same `continue` plus NEXT = start + count (host_interface.c:259-262, 297-302) also skips sequence numbers not yet written. TagLog_Read returns 0 for seq >= nextSeq (tag_log.c:638), so a host polling TAGLOG_READ,<NEXT>,20 at the head with fewer than 20 new records moves its cursor past records that are written later. The comment at host_interface.c:230-231 ("never written are skipped") shows this is by design, but it loses data for any naive client.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="buf-10"></a>

#### BUF-10 — Tag-log failure is silent: init result ignored, no degraded flag or host event, and a reset during a chip erase disables the log and OTA staging for the whole boot

`MEDIUM`

**Location.** Firmware/Application/configuration/firmware_configuration.c:495-497; Firmware/Devices/spi_flash/gd25q.c:186-190; Firmware/Services/storage/tag_log.c:449-480; Firmware/Services/system/system_fault.h:28-30

**Evidence.** The init call discards its result: `(void)TagLog_Init(&w5500Spi);` (firmware_configuration.c:497). The degraded bits cover only RFID, Ethernet and watchdog (system_fault.h:28-30). GD25Q_Init gives up if WIP is still set after 500 ms (gd25q.c:187-190), but a chip erase started by TAGLOG_CLEAR lasts several seconds (gd25q.h:65). A REBOOT command or watchdog reset during the clear therefore leaves state ABSENT, which makes TagLog_Append a no-op (tag_log.c:477-480) and removes OTA staging (firmware_update.c:48-51). There is no retry until the next boot. Runtime program and erase failures (failed WEL checks in TagLog_Task) only fill the queue and increment DROPPED. The only visibility is TAGLOG_STATUS on the legacy parser (host_interface.c:205-210).

**Impact.** A pilot unit can run for days without buffering reads or accepting updates, and the server is never told.

**Fix.** Add SYSTEM_DEGRADED_STORAGE and report it in the heartbeat and as a reader_exception event. At init, wait up to the chip-erase maximum, or retry the mount from TagLog_Task once the chip goes idle. Count program and erase failures and surface them.

**Verifier corrections.** (1) During a chip erase the JEDEC ID read at gd25q.c:176-183 probably fails before WaitReady(500) is reached; the outcome, state ABSENT, is the same either way. (2) Add the other causes of a silent ABSENT state: a mount failure (tag_log.c:460-463, EnsureSectorErased timeouts) and a dead or missing chip. (3) The degraded-bit evidence is accurate: system_fault.h:28-30 defines only RFID, ETHERNET and WATCHDOG.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="pro-04"></a>

#### PRO-04 — Any non-0xEE client on TCP 8080, or a single 0xFF noise byte on the serial host port, takes over the RFID module and silently stops active-mode inventory

`MEDIUM`

**Location.** Firmware/Services/configuration/configuration_port.c:289-304; Firmware/Services/configuration/module_bridge.c:46, 57, 591-627, 782-833; Firmware/Application/Silion/sillion_application.c:2422-2447; Firmware/Services/host/host_transport.c:269-277

**Evidence.** On 8080, any first byte other than 0xEE goes to Module_Bridge_Begin (configuration_port.c:293-299). Module_Bridge_BeginOn calls SILION_Application_BridgeAcquire (module_bridge.c:618-627), which stops a running inventory (sillion_application.c:2436-2443). Bytes that are neither 0xFF frames nor known tokens are discarded one at a time (module_bridge.c:505-507), and the client gets no reply. The module is handed back only 5 s after the client goes away (module_bridge.c:46, 812-833). On the serial link, a 0xFF at the start of a line begins a bridge session (host_transport.c:269-277). With no traffic, that session lasts until the 30 s idle timeout (module_bridge.c:57, 788-791), and the module is released 5 s later. 0xFF is the byte most likely produced by a glitch on an idle, biased RS-232/RS-485 line, and RX framing errors are not filtered (serial_port.c:306-316). The Identium Modbus Protocol V1.0 puts Modbus-TCP on port 8080 (p.3). A Modbus master that polls continuously keeps g_lastHostByteMs fresh, so inventory stays stopped for as long as it polls, and it never gets an answer. In rev 2, non-0xEE connections were simply closed (R2-03).

**Impact.** Port scanners, browsers, Modbus-TCP pollers (a documented Identium protocol) and RS-485 bus noise each stop tag reading, for about 5 s, about 35 s, or indefinitely. No event tells the server. This conflicts directly with the future Modbus-TCP service on the same port.

**Fix.** Start a bridge session only after a complete module frame with a valid LEN and CRC16 (or a complete ASCII token) has been received. Close other TCP clients without touching the module. Do not stop local inventory until the frame is validated. Report the suspension, for example as a reader_exception. When Modbus-TCP is added, identify MBAP by protocol id 0x0000 and a sane length field, not by the first byte, which is a transaction-id byte and can be 0xFF or 0xEE. Settle how 8080 is shared between the SDK bridge and Modbus.

**Also raised as.** . Earlier audit refs: R2-03, FW-18, R2-51

**Verifier corrections.** (a) The mechanism for the 'indefinite' TCP case is wrong. A TCP bridge session has no idle timeout and simply lasts while the TCP connection is ESTABLISHED (configuration_port.c:256-263). It ends on CLOSE_WAIT or CLOSED (:344-370), and the module is released 5 s later (module_bridge.c:812-815). g_lastHostByteMs only decides whether a different transport may take over the session (module_bridge.c:598-606). Any client that just holds the connection open (browser keep-alive, an idle Modbus master) suspends inventory for as long as it stays connected; it does not need to poll. (b) RX bytes with framing errors are passed on by the UART driver, which forwards every RXNE byte without checking FE/NE (Drivers/uart/uart.c:531-558). serial_port.c:306-316 only forwards them. (c) That 0xFF is the most likely glitch byte (a short low pulse read as a start bit followed by all-ones data) is engineering reasoning, not verified on hardware. Mark it as such. (d) Inventory does resume on release (sillion_application.c:2475-2479), so the 5 s and 35 s figures stand.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="pro-05"></a>

#### PRO-05 — HTTP mode ignores hb_cylce: every TCP reconnect is treated as a new session and sends a heartbeat at once, so heartbeats fire every ~1-2 s against servers that close idle connections

`MEDIUM`

**Location.** Firmware/Protocol/identium_protocol.c:1187-1211; Firmware/Services/ethernet/ethernet_service.c:131-137, 166-189, 196-235; Firmware/Services/ethernet/http_upload.c:626-629, 694

**Evidence.** In HTTP mode the reader keeps a TCP socket open to the HTTP server and redials whenever it closes (ethernet_service.c:166-235). It sends "Connection: keep-alive" (http_upload.c:694) and closes on "Connection: close" (:626-629). Identium_Protocol_Task clears identiumWasConnected whenever the socket is down (identium_protocol.c:1187-1191). On every reconnect it sets identiumLastHeartbeatMs = now - heartbeatPeriodMs, which forces an immediate heartbeat (:1193-1211). Consider a server that closes after each response (HTTP/1.0-style servers such as Python http.server) or whose keep-alive timeout is shorter than hb_cylce. Each heartbeat POST is followed by a close, a 1-2 s redial (ethernet_service.c:166-189, 219-235), and another immediate heartbeat. reader says heartbeats follow the configured cycle (ActModeDataFormat p.3). No HTTP test server exists in Tools/; identium_active_mode_test.py:4-5 covers only TCP port 9000.

**Impact.** Heartbeat traffic runs at about 5-10 times the configured rate with the default 10 s cycle (configuration_storage.c:339), and the heartbeat counter advances accordingly. Server load and connection churn grow with fleet size. Server-side logic that relies on the heartbeat rate is misled.

**Fix.** In HTTP mode, schedule heartbeats on hb_cylce only, independent of socket state. Open the TCP connection only when something is queued, and treat a close after a response as normal. Keep the 'heartbeat on new session' behaviour for the persistent TCP mode only. Add an HTTP/1.0 close-per-response server to the test tools.

**Also raised as.** . Earlier audit refs: R2-33

**Verifier corrections.** Precision. In HTTP mode, 'connected' is the raw socket state (Identium_Protocol_IsConnected returns ActiveUpload_Transport_IsConnected, identium_protocol.c:1150-1153). The redial interval is about 1 s after a reader-side close: the reconnect delay is reset to 1000 ms on establish (ethernet_service.c:133-137) and next attempt = now+1000 (:166-170). After a server-side close (CLOSE_WAIT) it is about 2 s, because the doubled delay is used (:183-188). The heartbeat itself is gated on event code 2 (identium_protocol.c:1042-1045), which is in the default event set, so the effect appears with defaults. That Python http.server closes after each response is general knowledge and was not checked here.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="pro-06"></a>

#### PRO-06 — reader_exception is never sent (dead code, discarded from the F411 image) and lacks the reader timestamp; sync_time_req lacks event_data; module failure is invisible to the server

`MEDIUM`

**Location.** Firmware/Protocol/identium_protocol.c:210-215, 228-259, 1018-1032, 1079-1082, 1155-1161; build/f411-release/rfid_controller_f411.map:872-875; Firmware/Application/configuration/firmware_configuration.c:543-547; Firmware/Services/configuration/configuration_storage.c:355-358

**Evidence.** Identium_Protocol_ReportReaderException is defined at identium_protocol.c:1155-1161, but nothing in the firmware calls it (grep finds only the definition and header). In the shipped F411 release map, its .text and .rodata are listed under "Discarded input sections" (map lines 872-875, inside the 106-1331 discarded block). reader lists reader_exception as uploaded without subscription (ActModeDataFormat p.2) and gives it the members err_code, err_string and timestamp (p.6). The builder at :244 emits only err_code and err_string, and inserts err_string without JSON escaping. The firmware does detect faults, for example System_Fault_Set(ERROR_RFID_INIT) plus SYSTEM_DEGRADED_RFID when the module does not start (firmware_configuration.c:543-547). The heart_beat event carries only a counter (:1026-1031), so heartbeats continue normally while the module is dead. sync_time_req is sent as {"reader_name":..,"event_type":"sync_time_req"} with no event_data (:213). reader's example has event_data: { } (p.8). It is also off by default: default events are {1,2,3} (configuration_storage.c:355-358). tag_read is gated on subscription code 1 (:1079-1082) and silently skipped when that code is absent, although reader says tag_read needs no subscription (p.2). None of this existed in rev 2; reader_exception and sync_time_req are new and incomplete.

**Impact.** A server cannot tell that the reader is up but cannot read tags (module dead, antenna fault, degraded mode). Strict JSON consumers can fail on the missing event_data. A configuration that omits code 1 stops all tag uploads with no error.

**Fix.** Call ReportReaderException from System_Fault_Set and degraded-state transitions, and from module status and antenna-fault codes (FW spec p.7 §4.4). Add a timestamp (epoch ms) and JSON-escape err_string. Emit event_data:{} for sync_time_req. Always upload tag_read regardless of the events list. Consider putting a status field in the heartbeat, matching TCP V1.0 0x04, which carries a status code.

**Also raised as.** . Earlier audit refs: R2-11, FW-32

**Verifier corrections.** (a) reader defines the heartbeat event_data as a plain number (p.7-8), so the firmware's heartbeat format conforms. A status field in the heartbeat comes only from the TCP V1.0 binary 0x04 frame (p.2), so keep that as a 'consider' item and do not imply non-conformance. (b) reader marks sync_time_req as 'subscribe to upload: yes' (p.3), so having it off by default conforms to reader. The conflict is with the FW spec RTC bootstrap (p.5) and with any buffered-record drain. Also note that SendSyncTimeRequest returns 1 when event 6 is disabled (identium_protocol.c:202-208), so the once-per-boot flag is set without a request ever being sent. (c) System_Fault_SetDegraded(SYSTEM_DEGRADED_RFID) is at firmware_configuration.c:547; System_Fault_Set(ERROR_RFID_INIT) is at :544-546.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="pro-10"></a>

#### PRO-10 — UDP discovery replies only by unicast to the requester, so readers on another subnet (including factory-default units) cannot be found; board type reported as HC32F46x

`MEDIUM`

**Location.** Firmware/Services/configuration/discovery_service.c:35, 53, 99-100; Firmware/Services/configuration/reader_settings.c:215-228; Firmware/Devices/w5500/w5500.c:1724-1744

**Evidence.** Discovery is new in rev 3 and answers the RFID Manager search on UDP 15000. The reply is sent with W5500_Socket2_SendTo to the requester's own IP and port (discovery_service.c:99-100). The W5500 routes a reply for an off-subnet IP through its configured gateway, and SendTo gives up after 50 ms without SEND_OK (w5500.c:1724-1744). The factory default is 192.168.150.50 with gateway 192.168.150.1 (reader_settings.c:215-228). The Utility doc tells users the reader defaults to 192.168.1.100, gateway 192.168.1.254, and to put the PC on that segment (Utility V1.0 p.1). A PC set up that way gets no reply, and neither does any PC when the reader was configured for another network, which is exactly the case discovery exists for. The reply reports board type 1 = 'Hc32f46x' (discovery_service.c:35, 53), while the firmware spec assigns 0x10 to INB-1 so back-offices can tell hardware apart (IDM-INB1-FW-001 p.9). Module type and module firmware version are always 0. Unverified: whether legacy readers broadcast this reply; no capture is available.

**Impact.** Out-of-box commissioning that follows Identium's own Utility doc fails twice: the direct connection fails because the default IP differs, and IP search fails because of the unicast reply. The board also identifies itself as a different mainboard.

**Fix.** Send the discovery reply to 255.255.255.255:<source port>, or send both broadcast and unicast. Confirm against a capture of a legacy reader. Align the factory network defaults with the Utility doc (tracked in FW-08/R2-02). Report board type 0x10 unless Identium confirms the tools need 1, and fill in the module type and version.

**Also raised as.** . Earlier audit refs: FW-40, FW-08, R2-02

**Verifier corrections.** (a) discovery_service.c:35 is the wrong line. :35 is blank inside Discovery_BuildReply. The board-type constant DISCOVERY_BOARD_HC32F46X 1U is at :13, the layout comment '35 board type (1 = Hc32f46x)' is at :25, and the assignment reply[35] is at :53. Byte offset 35 appears to have been confused with a line number. (b) w5500.c: the 50 x 1 ms wait loop is at :1734-1743. It is the MCU giving up waiting for SEND_OK, not the W5500 aborting the send. The reply to an off-subnet requester fails because the W5500 sends via its gateway (192.168.150.1, reader_settings.c:225-228; mask /24 at :220-223), and no such gateway answers ARP on a direct PC link. That is standard W5500 behaviour, but I did not open the datasheet, so mark that step unverified. (c) FW spec p.9 §7 assigns 0x10 as the heartbeat-protocol mainboard_type and lists 0x01 as the legacy ARM7 mainboard. It does not mention the discovery reply explicitly. The same §7 calls for a one-shot UDP broadcast for first-time discovery, which supports the broadcast recommendation.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="rfm-05"></a>

#### RFM-05 — Releasing the module bridge does not re-sync module state; the bridge forwards every opcode unfiltered and swallows the PC's first STOP even when the local STOP failed

`MEDIUM`

**Location.** Firmware/Application/Silion/sillion_application.c:2436-2450, 2453-2480; Firmware/Services/configuration/module_bridge.c:365-391, 618-627; Firmware/Services/configuration/configuration_service.c:1066-1074; Firmware/BSP/f411_custom/board.c:235

**Evidence.** Module_Bridge_ProcessHostBytes forwards every 0xFF frame that has the right length to the module (365-391) without checking its opcode. That includes 0x97 region, 0x91 power, 0x9B Gen2, 0x09 start-bootloader, AA31 carrier and the AA40 permanent-save family (protocol document §9.3 power-on region, §9.6 run-to-APP, §9.7 power-on baud). BridgeRelease (2453-2480) only stops and optionally restarts inventory. It does not re-apply readerConfig or check the run phase. StartInventory then succeeds on TX-complete alone. BuildStaticJson keeps reporting readerConfig region, power and antenna (configuration_service.c:1066-1074). The MCU UART is fixed at 115200 (board.c:235; PRODUCT_RFID_BAUDRATE is never used) and there is no baud scan. Separately, BridgeAcquire returns 1 even when StopInventory failed (2440-2450), and BeginOn then sets g_consumeFirstStop=1 (626). The PC's own Stop frame is discarded (372-381), which contradicts the comment at 2440-2441 that "the PC sends its own Stop".

**Impact.** After a Reader Manager session, the module may run on the region, power or antenna the PC chose while the firmware reports, and the host believes, India and 25 dBm, until the next MCU reboot. A tool that leaves the module in the BOOT layer leaves the reader "inventorying" with no tags. A tool that changes the module's power-on baud through AA40 makes RFID permanently unreachable after the next power cycle. If the local STOP was lost, the module keeps streaming through the whole tool session and the PC's STOP never reaches it.

**Fix.** On BridgeRelease, run the full ConfigureReader sequence plus session and Gen2 (which also re-checks run phase) and mark RFID degraded if it fails. Filter bridged opcodes: block AA40 baud and region writes, and 0x97 values outside the SKU's legal set, unless a factory or service flag is set. Set g_consumeFirstStop only when the local StopInventory returned 1. Add a baud-scan fallback at boot (try 115200, then the other §9.7 rates).

**Also raised as.** . Earlier audit refs: FW-20, FW-12, R2-35

**Verifier corrections.** (1) The bridge also forwards frames with no CRC check (module_bridge.c:365-391), not just without an opcode filter. (2) The header comment at module_bridge.c:38-41 ('The module is already stopped at that point') makes the same wrong assumption as sillion_application.c:2440-2441. (3) After release, Module_Bridge_Poll may also auto-start inventory when the work state is ACTIVE and start-at-boot is set (module_bridge.c:816-832), so the 'inventorying with no tags' outcome is reached on that path too. All cited line references are accurate.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="rfm-07"></a>

#### RFM-07 — The 4096-byte RX ring added in rev 3 adds no tag buffering: bytes are parsed at once into a 32-entry queue that loses tags, is popped one tag per loop pass, and has no backpressure

`MEDIUM`

**Location.** Firmware/Application/Silion/sillion_application_port.c:19, 248-265, 288-303; Firmware/Protocols/silion/silion_protocol.h:127; Firmware/Protocols/silion/silion_protocol.c:2258-2266, 2858-2866; Firmware/Application/Silion/sillion_application.c:1468-1483, 2324

**Evidence.** Rev 3 raises SILION_APP_RX_QUEUE_SIZE from 256 to 4096 (port.c:19). SILION_ProcessRxQueue drains the whole ring into the parser on every call (248-265), including inside every module wait loop. Each tag frame is pushed into asyncTagQueue, which has SILION_ASYNC_TAG_QUEUE_SIZE 32 entries (silion_protocol.h:127). When it is full the tag is dropped (`queue->overflow++;`, silion_protocol.c:2864). SILION_Application_Task pops a single tag per main-loop pass (sillion_application.c:1474-1482). Overflow raises only a generic ERROR_BUFFER_OVERFLOW (port.c:288-303), with no count reported. StopInventory still discards the ring before sending AA49 (2324). FW-001 p.7 §4.3 requires backpressure: on overflow, send AA49, drain, then resume with AA48.

**Impact.** Tags are lost whenever the module rate exceeds one tag per loop pass, or when any pass or blocking transaction lasts longer than 32 tag intervals. At about 250 reads/s that is roughly 130 ms. Examples are a 1 s StopInventory wait, a degraded retry (RFM-06), or a slow Wi-Fi or Ethernet pass. The enlarged ring sits in front of the drop point, so it does not help. Lost reads never reach the offline log. The loop monitor's GET_DIAG max-pass figure can be used to measure this on hardware (not done here; unverified).

**Fix.** Drain the async queue completely on each pass, or push straight into the de-dup stage (RFM-01), which is cheap. Alternatively, keep raw frames in the 4 KB ring and parse them only as fast as they are consumed. Implement the spec's backpressure (pause with AA49 or a duty cycle on overflow) and report a dropped-tag counter in diagnostics. Do not clear the ring in StopInventory; parse it.

**Also raised as.** . Earlier audit refs: FW-30

**Verifier corrections.** (1) The 'degraded retry (RFM-06)' example is invalid. Firmware_Configuration_Task retries only when SILION_Application_GetState() is IDLE (firmware_configuration.c:251-255), so no inventory is streaming during a retry. (2) Tags arriving during the 1 s StopInventory wait (sillion_application.c:2356) are moot because the stop is intended. The real loss in StopInventory is that SILION_ClearRxQueue (:2324) discards bytes already received but not yet parsed. (3) Loss needs either a sustained module rate above one tag per main-loop pass, or a pass longer than about 32 tag intervals. The loop-pass time is unmeasured, so the impact remains unverified on hardware, as the finding already says. (4) The 32-entry queue is unchanged from rev 2 (rev 2 silion_protocol.h:127). Rev 3 only enlarged the byte ring (256 -> 4096; rev 2 port.c:19).

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="net-03"></a>

#### NET-03 — TCP active upload stalls permanently when data aggregation is on and s_buf_size is larger than the 2 KB socket buffer

`MEDIUM`

**Location.** Firmware/Protocol/identium_protocol.c:794-829; Firmware/Services/ethernet/ethernet_service.c:291-303; Firmware/Services/upload/active_upload_transport.c:73

**Evidence.** Runtime config accepts s_buf_size from 1500 to 8192 (Services/configuration/configuration_service.c:1290-1296). With aggregation on, the TCP branch builds batches of up to IDENTIUM_TAG_BATCH_MAX_TAGS = 64 tags and up to s_buf_size bytes (identium_protocol.c:66, 794, 806-809). One tag object (format at 540-581) is about 62 bytes for EPC-96, so about 33 or more queued tags make a batch larger than 2,048 bytes. Ethernet_Service_SendComplete refuses any message larger than Sn_TX_FSR (ethernet_service.c:291-296). Socket 0's TX buffer is never resized from the W5500 default of 2 KB: nothing writes Sn_TXBUF_SIZE for S0, and w5500.c:1043-1050 assumes 2 KB. The Wi-Fi upload queue is also 2,048 bytes (wifi_service.c:13; active_upload_transport.c:73). On a refusal the code keeps the batch and retries later (identium_protocol.c:820-826), so every pass rebuilds the same oversize batch.

**Impact.** In TCP active mode with aggregation on and s_buf_size above 2048, tag upload stops for good once about 33 tags have accumulated. The 256-tag queue then overflows and new tags are dropped or expire, while the connection still looks healthy.

**Fix.** Limit each batch to the transport's real capacity: min(s_buf_size, TX buffer size). Alternatively stream one message across several SENDs while keeping it intact. Enlarge the S0 TX buffer, using memory from the unused S4-S7, if 8 KB batches are wanted. Reject s_buf_size values the active transport cannot carry.

**Also raised as.** . Earlier audit refs: R2-09

**Verifier corrections.** (1) A tag object is about 68 bytes, or 69 with the comma, for EPC-96 with a 6-digit frequency and 3-character RSSI. The format string is at identium_protocol.c:540-584, not 540-581. With the envelope (622-627) the batch passes 2,048 bytes at about 29 tags, not 33. (2) It takes two non-default settings: aggregation defaults off (configuration_storage.c:351) and s_buf_size defaults to 1580 (configuration_storage.c:340). With aggregation off, batches are 4 tags (identium_protocol.c:781). (3) Overflow drops the oldest tags and counts them in identiumTagDropped for GET_DIAG (identium_protocol.c:78). It does not drop the new ones, and the counters do show it. (4) 'For good' is slightly too strong. If clear_buf_time expiry keeps fewer than about 29 tags queued, a batch can still go out. At any real read rate it stays stalled.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="net-04"></a>

#### NET-04 — Wi-Fi service stops moving data between about 24.9 and 49.7 days of uptime (32-bit deadline wrap)

`MEDIUM`

**Location.** Firmware/Services/wifi/wifi_service.c:86, 401, 729-739 (also 642-644, 697, 808)

**Evidence.** Time_GetTickMs is a 32-bit millisecond counter (Services/Time/time_service.c:16, 42-45, 76-79). g_nextControlMs is a static that starts at 0 and is written only when the ESP rejects a CONFIG, CONNECT or DISCONNECT (wifi_service.c:86, 401). The gate at 729-739, `((int32_t)(now - g_nextControlMs) < 0)`, polls status and returns whenever the link is configured. With g_nextControlMs = 0 this condition holds for every tick from 2^31 ms (24.86 days) to 2^32 ms (49.71 days). In that window RECV, SOCK_OPEN, SEND and HttpUpload_Task (766-845) never run. The same happens 24.86 days after any rejection. g_nextHelloMs and g_nextUploadOpenMs are set at boot (642-644), updated only on failures, and compared the same way (697, 808). An ESP reset or a first upload-socket open in that window is therefore also held off until the counter wraps.

**Impact.** After about 25 days of continuous operation, Wi-Fi host commands, tag output and uploads stop for about 25 days. No error is raised, and only a reboot recovers. The defect is latent until NET-01 is fixed, but any pilot longer than 25 days will hit it.

**Fix.** Initialise these deadlines from Time_GetTickMs(). Better, use armed flags with elapsed-time checks of the form `(now - start) >= interval` instead of signed comparisons against stale deadlines. Add a scheduler test that crosses tick 0x80000000.

**Verifier corrections.** (1) g_nextHelloMs is not 'updated only on failures'. It is also set to now on any settings change (wifi_service.c:714). (2) 'Only a reboot recovers' is wrong. The service recovers by itself when the tick wraps at 2^32 ms (49.71 days), and the dead window then recurs every 49.71 days: from 2^31+k*2^32 to (k+1)*2^32 ms. A reboot just ends it sooner. (3) The gate at 729-739 also blocks WIFI_CONFIG (741-749), because it applies while g_needsConfiguration is set. Re-configuration after an ESP reset inside the window is held off as well. (4) Mention that WiFi_Service_Init (618-650) never initialises g_nextControlMs.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="net-05"></a>

#### NET-05 — Wi-Fi upload socket is never reopened after the server closes it

`MEDIUM`

**Location.** Firmware/Services/wifi/wifi_service.c:474-513, 799-825

**Evidence.** g_uploadSocketOpened is cleared only by: open failure or reject, DISCONNECT, ESP restart, CLOSE_UPLOAD, link reset, or a settings change (wifi_service.c:347, 410, 471, 496, 534, 686, 712). When a GET_STATUS reply shows the upload-connected bit falling, the handler only calls HttpUpload_OnDisconnected (507-511). g_uploadSocketOpened stays 1, so the re-open at 808-818 never runs, and the close at 802-806 runs only when the target changes. HttpUpload_Task and upload sends run only while the connected bit is set (821-825, 841-845). Recovery therefore depends on the undelivered ESP firmware redialling silently. SOCK_OPEN in esp32_link_protocol.h:87-95 does not specify that behaviour.

**Impact.** If the active-mode TCP server restarts, or an HTTP server closes an idle keep-alive connection, Wi-Fi uploads stop until the settings change or the ESP resets. Meanwhile tags queue up and are then dropped.

**Fix.** When the upload-connected bit falls, mark the socket closed (sending SOCK_CLOSE if needed) and re-open it with backoff, as the Ethernet client does (ethernet_service.c:166-181). Document the ESP-side contract.

**Verifier corrections.** (1) For HTTP over Wi-Fi the finding is right only for a server close while the client is idle or mid-send. A response with 'Connection: close', or an HTTP timeout, calls ActiveUpload_Transport_Close, which goes to WiFi_Service_CloseUploadSocket (http_upload.c:626-629, 856; active_upload_transport.c:56-60). The CLOSE_UPLOAD completion then clears g_uploadSocketOpened (wifi_service.c:533-538), so those paths do re-open. (2) While the connected bit is 0, HttpUpload_Task does not run (821-825), so its timeout close at http_upload.c:849-857 cannot rescue a request either. (3) The behaviour depends on an ESP-side contract that does not yet exist (NET-01), so this is a specification gap as much as an STM32 defect.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="net-06"></a>

#### NET-06 — Ethernet and Wi-Fi host-port output is silently cut mid-line when the 2 KB queue fills

`MEDIUM`

**Location.** Firmware/Services/ethernet/host_tcp_server.c:126-152; Firmware/Services/wifi/wifi_service.c:96-107, 869-878; Firmware/Services/host/host_transport.c:676-683

**Evidence.** Host_Tcp_Server_Send copies until its 2,048-byte ring is full and then drops the rest ("queue full: drop the rest", host_tcp_server.c:136-142). WiFi_Service_HostSend returns a partial count when its 2,048-byte queue is full (wifi_service.c:13, 100-106). HOST_Transport_SendString ignores both return values (host_transport.c:676-683). WiFi_Service_HostGetFreeSpace (wifi_service.c:875) is never called. Tag lines (HOST_SendTag, Application/Host/host_interface.c:640-718) and TAGLOG_READ replies (up to 20 lines per command, host_interface.c:195, 259-294) both use this path. Wi-Fi drains at most one 200-byte SOCK_SEND per I2C transaction, interleaved with a GET_STATUS every 5 ms (wifi_service.c:15, 17, 828-839). At 400 kHz that is roughly 15 KB/s at best, before main-loop latency.

**Impact.** During tag bursts, or with a slow client, the host receives partial lines spliced onto the next line, so the text stream is corrupted without any error. Over Wi-Fi this starts at roughly 150-200 tags/s.

**Fix.** Make each line all-or-nothing: check free space first and count drops. Add backpressure towards the tag producer, and expose a drop counter.

**Also raised as.** . Earlier audit refs: R2-13

**Verifier corrections.** (1) On Ethernet the 2 KB ring sits in front of the W5500 socket 3 TX buffer, which is another 2 KB (w5500.c:1809, 1816; host_tcp_server.c:57). Truncation therefore needs a client that is slow or stalled, not just a burst. (2) The Wi-Fi throughput figures (about 15 KB/s, onset at 150-200 tags/s) are estimates from the I2C timing (9+200+2-byte write, 2 ms reply delay at esp32_link.c:18, 64-byte read, GET_STATUS every 5 ms) and should be labelled as such. With no ESP firmware (NET-01) the Wi-Fi half is latent.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="net-09"></a>

#### NET-09 — No TLS, DNS or MQTT anywhere, and the ESP link protocol cannot even request them

`MEDIUM`

**Location.** Firmware/Services/ethernet/http_upload.h:29-30; Firmware/Services/ethernet/http_upload.c:124-136; Firmware/Services/configuration/vendor_config.c:946-951; Firmware/Devices/esp32/esp32_link_protocol.h:87-95

**Evidence.** HttpUpload_ParseUrl accepts only http:// with a dotted IPv4 host. Its comments say it "also rejects https://" (http_upload.c:134) and that "the reader has no DNS resolver" (http_upload.h:29-30). The vendor-tool path returns INVALID for https:// or a host name (vendor_config.c:948-951). The ESP SOCK_OPEN payload is type + port + IPv4 only (esp32_link_protocol.h:87-95). It has no hostname or TLS option, so the ESP32-C3, which could run TLS, cannot provide HTTPS either. A grep finds no mbedTLS/TLS, MQTT or mDNS code. Identium_INB1_Design_Specification.pdf §3.1 F-08 requires HTTPS and MQTT. The HTTP active-mode envelope in IDM-INB1-FW-001 §3.2 posts to an https:// URL.

**Impact.** The reader can post only to a plain-HTTP server addressed by IP on a trusted LAN. Cloud endpoints (HTTPS, DNS names) are unreachable. Tag data and server commands (set_gpo, reboot) travel in clear and unauthenticated.

**Fix.** Decide where TLS lives. On-MCU TLS is not credible on the F411 with about 11 KB of RAM free. Realistic options are the ESP32-C3 (extend SOCK_OPEN with hostname, TLS flag and CA selection; this also needs Ethernet routing via the ESP or a gateway) or a site gateway. Document the limitation for the pilot.

**Also raised as.** . Earlier audit refs: REQ-01, REQ-07, FW-16

**Verifier corrections.** (1) The FW spec citation is IDM-INB1-FW-001 section 3.2, pages 4-5 (envelope 'POST https://customer-server/path'). The design spec citation is section 3.1, F-08, which lists HTTPS and MQTT 'for backward compatibility'. F-06 also calls for an Identium mDNS service and MQTT topic prefix, which supports the finding further. (2) MQTT appears in the code only in comments (identium_protocol.h:12; vendor_config.c:942). WIFI_CONFIG carries a DNS server field (esp32_link_protocol.h:76), but nothing uses names, because SOCK_OPEN takes only IPv4.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="bld-03"></a>

#### BLD-03 — Release is not reproducible or traceable: built from a dirty working tree, commit and history not delivered, referenced design docs missing, toolchain undocumented

`MEDIUM` · 🛑 **BLOCKS PILOT**

**Location.** build/f411-release/rfid_controller_f411-v0.10.1-1-gfcae054-dirty.tlfw (header offset 0x20); Makefile:29,105,165; Bootloader/boot_main.c:4; Firmware/Services/update/firmware_update.h:6; Tools/fw_sign.py:4; Firmware/BSP/f411_custom/board_config.h:18-21; Firmware/Application/Host/host_interface.c:752

**Evidence.** The shipped image's header version is 'v0.10.1-1-gfcae054-dirty': one commit after tag v0.10.1, with uncommitted changes. The delivered tree has no .git, .gitignore, README or CHANGELOG (find/ls), so commit fcae054 and the dirty delta cannot be identified. DWARF producer and comp_dir in the release ELF show 'GNU C11 14.3.1 20250623 ... -Os' built under C:/Users/Identium/Downloads/RFID_Controller_F429_IDENTIUM_PROTOCOL_STEP8/STM32_Firmware with STM32CubeCLT_1.22.0. That toolchain version appears nowhere in the tree: Makefile:29 says only 'arm-none-eabi-gcc on PATH (STM32CubeCLT)'. From the delivered zip (no .git), Makefile:165 'VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo unknown)' yields 'unknown'. Makefile:105 builds the source list with unsorted 'find', so link order (and binary bytes) depends on the host filesystem. Source comments cite docs/design/0020-bootloader-and-signed-update.md (boot_main.c:4, firmware_update.h:6, fw_sign.py:4, host_interface.c:752), docs/hardware/SCH_Schematic1_2026-10-13.pdf and docs/notes/f411-custom-board-port.md (board_config.h:18-21). No docs/ folder is delivered. Partial consistency evidence: the 83 objects in build/f411-release/*.map match exactly the 83 delivered sources, and every literal string >=10 chars in the release .bin occurs in the delivered source. A byte-for-byte rebuild was not attempted (no arm-none-eabi toolchain here): unverified.

**Impact.** Identium cannot rebuild the firmware it would put on pilot units. It cannot prove which source produced it, and cannot later make a minimal fix against the exact field version. The design rationale for the bootloader/OTA (the riskiest new code) is not in Identium's hands.

**Fix.** Deliver the full git repository, with history and tags, to an Identium-owned remote. Build release images only from a clean, tagged commit, in a pinned toolchain (CubeCLT 1.22.0 / GCC 14.3.1, ideally a container or CI job). Fail the release if 'git describe' contains -dirty. Sort C_SRCS. Publish a manifest (tag, commit, toolchain, SHA-256 of bootloader/app/.tlfw). Deliver docs/ (the 0020 design note, the F411 port notes) with the source.

**Also raised as.** . Earlier audit refs: REL-04, FW-43, R2-50 (new: Makefile/git-describe release flow and a signed dirty build)

**Verifier corrections.** (1) Overstated impact: "Identium cannot rebuild the firmware" should read "cannot reproduce or prove the exact shipped binary". Identium can build from the delivered tree: the 83 objects in f411-release/*.map match the 82 .c files plus the startup file exactly, and the toolchain can be recovered from DWARF (STM32CubeCLT_1.22.0, GCC 14.3.1). (2) Add that Makefile:165 falls back to 'unknown' only when no git repo encloses the tree. If the tree sits inside some other repository, VERSION silently takes that repository's describe string. (3) All doc references verified: boot_main.c:4, firmware_update.h:6, fw_sign.py:4, host_interface.c:752, board_config.h:18 and :21. No docs/ folder exists.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="bld-04"></a>

#### BLD-04 — No production programming package or procedure: flashing rebuilds from source and defaults to the debug build, the bootloader sector is not write-protected, and ESP32-C3 firmware, serial-number and end-of-line steps are missing

`MEDIUM` · 🛑 **BLOCKS PILOT**

**Location.** Makefile:32-33,185-190,197-203; Firmware/Application/Host/host_interface.c:1334-1343; Firmware/Devices/esp32/esp32_link_protocol.h:6; Firmware/Services/configuration/reader_settings.c:230-238; Firmware/Services/system/diagnostics.c:33-44,150

**Evidence.** The only programming flow is make: 'flash: $(BUILD)/$(TARGET).elf $(BOOT_BUILD)/bootloader.elf check-chip' (Makefile:185). It compiles on the programming PC and needs the full source tree, toolchain and Python. There is no prebuilt, checksummed combined bootloader+app image. Defaults are 'CONFIG ?= debug' / 'BOARD ?= f429' (Makefile:32-33), so 'make flash BOARD=f411' programs the -Og DEBUG build. That build contains the DEBUG_FAULT deliberate-crash command (host_interface.c:1334-1343). 'strings' finds DEBUG_FAULT in build/f411-debug/rfid_controller_f411.bin and not in the release .bin. 'make protect' sets only BOR_LEV and RDP=0xBB (Makefile:198). No option sets WRP on sector 0 (grep WRP: none in Makefile/Tools/Firmware), so application code can erase the bootloader. The ESP32-C3 (U19) runs a custom I2C link: 'shared verbatim by the STM32 firmware and ESP-IDF project' (esp32_link_protocol.h:6). That ESP-IDF project is not delivered, and no step programs U19 via CN11 (netlist: CN11.2=EN/U19.2, CN11.4/.5=U19.12/.11). There is no serial-number scheme or unit record. The MAC is a CRC32 of the UID behind the 02:1D prefix (reader_settings.c:230-238), with nothing logged. On the plus side, check-chip refuses a wrong MCU (Makefile:175-183), flashing verifies (-v), and GET_DIAG reports RDP and BOR (diagnostics.c:33-44,150), so an end-of-line check is possible. No such procedure is written down.

**Impact.** Pilot units can easily be programmed with the debug image or a toolchain-dependent rebuild. The bootloader is not protected against accidental or malicious erase. Wi-Fi/BLE cannot work on units because no ESP32 firmware exists to program. No traceability links unit, MAC, firmware and test result.

**Fix.** Release a production package from CI: bootloader.hex, app.hex (or one combined hex), a SHA-256 manifest and a programming script that calls STM32_Programmer_CLI without building. Make flash/protect refuse CONFIG=debug, or default BOARD=f411 CONFIG=release. Add WRP on sector 0 to 'protect' (software can still clear it at RDP1, so document that limit). Obtain and version the ESP-IDF companion firmware and add a CN11 programming step. Define an end-of-line script: GET_DIAG (RDP=1, BOR=3), version, MAC, W5500/GD25Q/ESP32 presence, relays/GPI loopback. Log serial number + MAC + image hashes per unit.

**Also raised as.** . Earlier audit refs: FW-38, FW-37, FW-36, FW-08 (new: rev 3 adds flash/protect targets, but they are incomplete)

**Verifier corrections.** (1) reader_settings.c: the MAC derivation is at lines 232-238 (the 0x02 and 0x1D bytes are at :233-234). (2) Note that GET_SERIAL (host_interface.c, the handler after GET_VERSION) returns the RFID module's serial number, not a board serial. That supports the claim there is no board serial scheme. (3) Soften "compiles on the programming PC": make rebuilds only when its targets are out of date. Even so, it needs the toolchain and the full tree. (4) Otherwise accurate: Makefile:32-33 (CONFIG ?= debug, BOARD ?= f429), :185-187, :197-199, :175-183. CN11.2 is on the net with U19.2 and U7.46, and CN11.4/.5 go to U19.12/.11 (net.py comp CN11).

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="bld-05"></a>

#### BLD-05 — Host tests check the implementation rather than the requirements, and the code that can brick a unit (bootloader install, staging reads, settings migration) has no test or on-target evidence

`MEDIUM` · 🛑 **BLOCKS PILOT**

**Location.** Makefile:27,109,213-239; hostcheck/tag_log_host_test.c:259,313-314; hostcheck/identium_protocol_host_test.c:394-399; hostcheck/make_test_images.py:27-30; Bootloader/boot_main.c:48-116; Bootloader/boot_staging.c; Firmware/Services/configuration/reader_settings.c:354-475

**Evidence.** Makefile:213-239 builds only identium_protocol.c, tag_log.c, boot_image.c, firmware_image.c, sha256.c, uECC.c and firmware_update.c. Those modules hold 2,616 of about 32,776 own C lines (8%). Not compiled by any test: boot_main.c (Boot_Install erase/copy/verify and power-cut retry), boot_staging.c and boot_flash.c (the bootloader's own SPI/flash drivers), reader_settings.c record migration v1->v4 (:354-475), configuration_storage.c, silion_protocol.c (3,956 lines), host_interface.c (2,685 lines), module_bridge.c/discovery_service.c (vendor-tool compatibility), esp32_link.c and http_upload.c. The tests lock in shortfalls: 'CHECK(s.capacity == 65280U);' (tag_log_host_test.c:259) passes while the requirement is >=100k. The slow-server case asserts 'uploaded < 2000U' and only that the losses are counted (identium_protocol_host_test.c:396-399); the lead measured 1,744 dropped and 256 expired out of 2,000. The boot test signs with a throwaway key on the host (make_test_images.py:27-30). There it runs micro-ecc's generic C, not the ARM assembly path the bootloader executes. hostcheck is not part of 'all' (Makefile:109). Makefile:27 mentions CI builds with -Werror, but no CI configuration is delivered. Tools/*.py are live-reader scripts, several written for the F429 (identium_tcp_hardening_test.py:3), and no run logs or results are delivered.

**Impact.** The update path is the only field recovery for RDP-locked units. If it fails on real F411 hardware or after a power cut, the unit stops in the bootloader's 'wait for SWD' loop (boot_main.c:149-153). Nothing delivered shows it was exercised. Requirement failures (tag capacity, tag loss) pass the suite.

**Fix.** Before the pilot, run and record on real F411 boards: an OTA install with power cuts during erase, copy and state-write; a staging-read failure; an RDP1 unit update; and settings migration from each older record version. Turn the requirements into failing tests (tag capacity >=100k EPC-128; zero loss while the server is down, served from the flash log). Add host tests for the bootloader install logic (with simulated flash), reader_settings migration, the Silion parser and the host command parser. Run make hostcheck plus both firmware builds with -Werror in CI.

**Also raised as.** . Earlier audit refs: REL-04, R2-50 (rev 2 delivered no tests; rev 3 adds good but narrow tests)

**Verifier corrections.** (1) identium_protocol_host_test.c: 'assert(uploaded < 2000U)' is at :399 and the loss-sum assert at :400-402. Cite 396-402. (2) The own-line total depends on scope: Firmware + Bootloader without Middlewares and micro-ecc is 33,916 lines (35,512 with Src), against the finding's 32,776. The 2,616 tested lines are exact, so the share is 7-8% either way. (3) The micro-ecc point is confirmed and can be stated precisely. On thumb2, uECC.c:180-182 includes asm_arm.inc, whose 'small' inline-asm add/sub/mult/square run at uECC_OPTIMIZATION_LEVEL=1. A host build takes the generic C path.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="bld-06"></a>

#### BLD-06 — The firmware binary carries no version or identity of its own; three version schemes disagree

`MEDIUM`

**Location.** Firmware/Config/product_config.h:53-56; Firmware/Services/configuration/module_bridge.c:485-493; Firmware/Services/configuration/discovery_service.c:55-58; Firmware/Services/ethernet/http_upload.c:691,698-701; Firmware/Application/Host/host_interface.c:845-856; Firmware/Services/update/firmware_update.c:412-433; Makefile:165-170

**Evidence.** No version string, git hash or build date exists in own code (grep for FW_VERSION/GIT_/__DATE__ finds nothing). 'strings' on the release .bin shows only format strings. Three notions are in use. (1) A hand-edited constant 1.0.0.8 (product_config.h:53-56) is reported by 'VER' (module_bridge.c:485-493), by discovery bytes 38-41 labelled 'bootloader version' (discovery_service.c:55-58) and by the HTTP User-Agent (http_upload.c:691). (2) The git-describe text 'v0.10.1-1-gfcae054-dirty' exists only in the .tlfw header (Makefile:165-170). (3) FW_STATUS 'VERSION=' (host_interface.c:845-856) is read from the staging header in SPI flash (firmware_update.c:412-433), i.e. the last staged image, not the running one. After an SWD re-flash the staging area is not erased, so FW_STATUS keeps reporting the old image. GET_VERSION returns only the RFID module version.

**Impact.** During the pilot Identium cannot query which application a unit is actually running. OTA campaigns and bug reports cannot be matched to builds, and the reported '1.0.0.8' never changes across releases.

**Fix.** Generate a version header at build time (semver from the tag, commit hash, dirty flag, board, build config). Place a small identity block at a fixed offset in the image, readable by the bootloader and fw_sign.py. Report it from VER, discovery, GET_VERSION/GET_DIAG, the Identium JSON heartbeat and the User-Agent. Derive the .tlfw header version from that same block rather than a CLI argument.

**Also raised as.** . Earlier audit refs: FW-44, REL-04 (new: rev 3 introduced a second, inconsistent version path)

**Verifier corrections.** (1) The 'bootloader version' label for bytes 38-41 is at discovery_service.c:28. Lines 55-58 only fill the bytes. (2) Wording: FW_STATUS prints VERSION= next to LAST=<outcome> (host_interface.c:850). It is designed to report the last staged update, but the finding's point stands: nothing reports the running image. (3) The 1.0.0.8 constant is at product_config.h:52-56.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="bld-07"></a>

#### BLD-07 — fw_sign.py takes the board ID from the command line and does not validate the binary against it; a mis-signed F429 image installs on an F411 and leaves it unbootable

`MEDIUM`

**Location.** Tools/fw_sign.py:114-136; Firmware/Common/firmware_image.c:11-21; Bootloader/boot_main.c:102-107,118-131,149-153

**Evidence.** fw_sign.py checks only the size limit and that the reset vector lies in the application area (:120-131). It stamps 'board["id"]' from --board (:133-135) and never checks the initial stack pointer or any marker in the binary. The shipped F429 build/release/rfid_controller.bin (92,228 B; vector SP 0x20030000, entry 0x08016EE9) passes 'sign --board f411'. The bootloader then finds a valid header (boardId 0x431, load address, size, hash, signature: firmware_image.c:11-21), erases sectors 1-4 and installs it (boot_main.c:102-107). Only afterwards does Boot_StartApplication reject the stack value, which exceeds BOOT_RAM_END 0x20020000 (boot_main.c:124-131). The unit then loops in the bootloader (boot_main.c:151-153) with no application, so it has no update path.

**Impact.** A single operator error during release (wrong .bin with the right key) bricks every unit it reaches over OTA. Recovery needs SWD, and on RDP1 units a mass erase that also wipes settings (Makefile:201-203).

**Fix.** Have fw_sign.py derive the board from an identity block inside the binary (see BLD-06) and check the SP against that board's RAM end. Make the bootloader validate the staged image's vector table (stack/entry) before erasing the current application. Sign only through 'make image' / CI.

**Verifier corrections.** Add two points. (1) The application's update receiver does not check the vector table either: firmware_update.c:337 calls only FwImage_HeaderIsValid. (2) The default 'make' produces exactly this F429 image (BOARD ?= f429, Makefile:33), which raises the likelihood. No line-number corrections are needed.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="bld-08"></a>

#### BLD-08 — Delivery reintroduces ~276 MB of build output, including stale pre-bootloader images linked at 0x08000000, Windows executables, .pdb/.pyc files and a test private key

`MEDIUM`

**Location.** F411/RFID_Controller_F429_f411.elf; F411/objects.list; Debug/RFID_Controller_F429.elf; build/f411-debug/rfid_controller.{elf,hex,bin}; RFID_Controller_F411.launch:77; build/hostcheck/*; Tools/__pycache__/*.pyc

**Evidence.** du: build 152 MB, Debug 77 MB and F411 47 MB, out of a 293 MB zip (2,117 of 2,482 entries). Stale images have the vector table at 0x08000000 (llvm-objdump -h and the map 'Memory Configuration' show FLASH 0x08000000): F411/RFID_Controller_F429_f411.elf (12 Oct), Debug/RFID_Controller_F429.elf (22 Oct) and build/f411-debug/rfid_controller.* (12 Oct, beside the current rfid_controller_f411.*). Programming any of them overwrites the bootloader in sector 0. F411/objects.list lacks tag_log, gd25q, firmware_update, firmware_image and sha256 compared with the current build. RFID_Controller_F411.launch:77 still debugs 'F411/RFID_Controller_F429_f411.elf'. The CubeIDE configurations (.cproject, dated 12 Oct) do not build or flash the bootloader, so an IDE flash of a blank chip leaves sector 0 empty and the app at 0x08004000 cannot start from reset. build/hostcheck holds five PE32+ Windows executables (no .exe extension) with MSVC .pdb files, plus test_key.pem (a throwaway test private key). Tools/__pycache__ holds two CPython 3.12 .pyc files.

**Impact.** A technician or engineer can easily pick an image named 'f411' that is two weeks old, lacks the tag log and update service, and erases the bootloader. Untrusted binaries and keys travel with the source and bloat every hand-off.

**Fix.** Ship source only, with a .gitignore for build/, Debug/, F411/, __pycache__/ and *.pem. Release artifacts go in a separate, manifest-listed package (BLD-04). Update or remove the CubeIDE configurations and .launch files so the IDE cannot produce or flash a bootloader-less layout, or document a bootloader-first IDE flow.

**Also raised as.** . Earlier audit refs: REL-04 (rev 2 had removed build outputs; rev 3 reintroduces them)

**Verifier corrections.** (1) Units: build/Debug/F411 hold 284,278,554 bytes (271 MiB; du gives 152M + 77M + 47M = 276M). The zip is 293 MB uncompressed and 88 MB compressed. Say so to avoid confusion. (2) Add that the CubeIDE F411 configuration now links with Boards/f411/STM32F411CEUX_FLASH.ld at 0x08004000 (.cproject:242) and has no Bootloader source entry (.cproject:259-264). That is the concrete evidence for the 'IDE flash leaves sector 0 empty' claim. RFID_Controller_F411.launch:18 also downloads the stale F411 ELF.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="req-11"></a>

#### REQ-11 — None of the FW-001 branding items are implemented, and new code presents a foreign identity (HC32F46x board type, "RFID-F429-DEV", STMicroelectronics USB strings)

`MEDIUM`

**Location.** Firmware/Services/configuration/discovery_service.c:13,53; Firmware/Config/product_config.h:22,52-56; Firmware/Services/configuration/reader_settings.c:601-604; Firmware/USB_DEVICE/App/usbd_desc.c:65-69; Src/main.c:9

**Evidence.** - The discovery reply, new in rev 3, reports board type `DISCOVERY_BOARD_HC32F46X 1U` (discovery_service.c:13, :53), which is the SLD1030's MCU. FW-001 §7 and checklist #8 require mainboard_type 0x10 so back-office systems can tell INB-1 units apart (p.9, p.13).
- The default reader_name, sent in every event, is `"RFID-F429-DEV"` (product_config.h:22; reader_settings.c:601-604).
- USB enumerates as VID 1155 (ST) with the strings "STMicroelectronics" and "STM32 Virtual ComPort" (usbd_desc.c:65-69).
- main.c:9 still carries the STMicroelectronics copyright.
- There is no boot banner, no copyright string in flash, no mDNS / _identium._tcp service and no web UI (grep banner/mdns/"Creating identities": no hits).
- The only Identium string on the wire is the HTTP User-Agent (http_upload.c:691).
- The mainboard version is a hand-set 1.0.0.8 (product_config.h:52-56), unrelated to the shipped image version v0.10.1-1-gfcae054-dirty (from the lead).

**Impact.** DS §1 names Identium firmware and branding as one of the three reasons for building this board. Customers instead see an ST / "F429-DEV" / HC32 device.

**Fix.** - Set Identium USB strings, and a VID/PID as Identium decides.
- Default reader_name to something like identium-inb1-XXXX derived from the MAC.
- Print a boot banner with version, serial number and MAC on USB and RS-232.
- Generate one version.h from git and use it everywhere.
- Report a distinct discovery board type (0x10) if Reader Manager accepts it (verify first). Otherwise document the compatibility trade-off.

**Also raised as.** . Earlier audit refs: FW-44, FW-49, R2-10, PRG-02, FW-40

**Verifier corrections.** (a) The mainboard_type 0x10 requirement is misapplied. FW-001 §7 (p.9) and checklist #8 (p.13) put mainboard_type in the TCP V1.0 binary heartbeat (CMD 0x04; FW-001 p.4 table), which does not exist; the firmware's heartbeat is JSON (identium_protocol.c:1029). The discovery reply's board-type byte is a Reader Manager / ReaderSense field (discovery_service.c:16-18, :25), and HC32F46x is reported for tool compatibility. Restate it as two items: the heartbeat has no mainboard_type at all, and discovery impersonates the SLD1030 board type. (b) main.c:9 is a source-file comment, not visible to customers; drop it from the 'customers see' impact. (c) Default reader_name confirmed: no default name is set in the settings defaults, so ReaderSettings_GetName falls back to PRODUCT_PROTOCOL_READER_NAME (reader_settings.c:603).

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="gap-01"></a>

#### GAP-01 — New rev-3 network/inventory watchdogs reboot the reader during every backhaul outage and continuously in passive mode, discarding queued tags

`MEDIUM`

**Location.** Firmware/Protocol/identium_protocol.c:847-884, 886-927, 1079-1091, 1150-1153, 1170-1184; Firmware/Services/ethernet/ethernet_service.c:93-107; Firmware/Services/configuration/configuration_service.c:1299-1316; Firmware/Services/configuration/vendor_config.c:816-831; hostcheck/identium_protocol_host_test.c:49

**Evidence.** Rev 2 only echoed thwdt_net_timeout as a constant string (rev-2 configuration_service.c:676). Rev 3 acts on it. Identium_Protocol_UpdateNetworkRecovery clears its timer only when `connected != 0U` (identium_protocol.c:853-857). Once networkWatchdogMinutes have passed with no connection, it calls NVIC_SystemReset (identium_protocol.c:880-883). `connected` is ActiveUpload_Transport_IsConnected() (identium_protocol.c:1150-1153, 1170). The check runs every main-loop pass, before the connected test (identium_protocol.c:1179-1184), and nothing in it looks at the work state. In passive work state, Ethernet_Service_Task closes socket 0 and clears the connection on every pass (ethernet_service.c:93-107), so `connected` stays 0 permanently. A server outage, or an HTTP URL with no usable target (ethernet_service.c:109-121), has the same effect. The value can be set from the vendor tool: VendorConfig_SetActive forwards "thwdt_net_timeout"/"thwdt_inv_timeout" (vendor_config.c:819-822) to Configuration_Service_ApplyRuntimeConfig, which accepts any unsigned value (configuration_service.c:1299-1316). It persists in configuration storage, and VendorConfig_SetWorkState(PASSIVE) does not clear it (vendor_config.c:412-450). The inventory watchdog is refreshed only in Identium_Protocol_ReportTag, after `if(Identium_Protocol_EventEnabled(1U) == 0U) return 1U;` (identium_protocol.c:1079-1091). With the tag_read event disabled it therefore reboots while tags are being read (identium_protocol.c:923-926). Neither reset path flushes anything. The tag log has no flush API (tag_log.h:67-84), and its 64-record RAM queue (tag_log.c:53) and the upload queue are simply lost. The host test stubs the watchdog to 0 (identium_protocol_host_test.c:49), and Tools/identium_active_mode_test.py:184-185 only sends 0, so this path has never been exercised. The the reader's ActModeDataFormat document text defines no thwdt semantics (checked with pdftotext), and FW-001 has no such requirement.

**Impact.** Once a vendor-tool user sets a non-zero net timeout, the reader reboots every N minutes for as long as the backhaul is down. That is exactly the period offline buffering exists for. Each reboot loses up to 64 unwritten tag-log records plus the upload queue, stops reading for the RFID re-initialisation (about 16.5 s if the module is slow or absent, per RFM-06), restarts the uptime time base so post-reboot records cannot be ordered against pre-reboot ones, and repeats sync_time_req. A reader switched to passive mode (vendor-tool polling) with the setting left on reboot-loops indefinitely even though nothing is wrong. A reset that lands during a TAGLOG_CLEAR chip erase also disables the tag log and staging for the next boot (BUF-10).

**Fix.** Arm the network watchdog only in active work state with a valid upload target. Never reboot while the tag log holds undrained records (or while offline buffering is the intended behaviour); recover the socket or the W5500 instead of the MCU. Refresh the inventory watchdog on every module tag, whatever the event filter. Add a TagLog flush before every NVIC_SystemReset. Bound the accepted minutes, document them, and add host tests with non-zero values.

**Also raised as.** . Earlier audit refs: BUF-01, BUF-08, BUF-10, PRO-01, RFM-04, RFM-06; R2-33 (heartbeat semantics) is a separate item

**Verifier corrections.** 1) The RAM queue define is at tag_log.c:52 (`#define TAG_LOG_QUEUE_SIZE 64U`), not :53. 2) The watchdog minutes can also be set over the Identium host protocol. set_runtime_conf calls Configuration_Service_ApplyRuntimeConfig (identium_command.c:781-791), so the setting is reachable by any connected host as well as the vendor tool. 3) Both values default to 0 (configuration_storage.c:344-345, product_config.h:32-33), so the fault only appears after someone sets a non-zero value. Say this in the impact. 4) Remove the reader model name. Write "the reader's ActModeDataFormat document" (pdftotext of all five protocol PDFs finds no 'thwdt'). 5) "Loses up to 64 records" is an upper bound. TagLog_Task runs in the same main-loop pass as the reset (main.c:139 vs :149), so the normal loss is a few records, nearer 64 only during a sector erase.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="gap-02"></a>

#### GAP-02 — Coprocessor (ESP32-C3) firmware cannot be updated in the field: no update command in the STM32-ESP link and the MCU cannot reach the download strap

`MEDIUM`

**Location.** Firmware/Devices/esp32/esp32_link_protocol.h:39-50; netlist U19.8/J2/R22, U19.11-12/CN11, U19.2/U7.46 (231026/_a4 net.py)

**Evidence.** The STM32-ESP32 command set is HELLO, GET_STATUS, WIFI_CONFIG/CONNECT/DISCONNECT, SCAN_START/RESULTS, SOCK_OPEN/CLOSE/SEND/RECV and RESET (esp32_link_protocol.h:39-50). There is no OTA, flash-write or version-transfer command. Netlist (net.py): U19.8 (ESP32-C3 IO9, the boot strap) goes only to R22 (10k to 3V3) and jumper J2 (J2.1 to GND). The ESP UART0 pins U19.11/U19.12 go only to header CN11.5/CN11.4. The MCU drives only EN (U7.46 = PB9 to U19.2/CN11.2). The STM32 therefore cannot put the ESP32 into ROM download mode, and the app-side update chain (FW_BEGIN..FW_APPLY, firmware_update.c) covers only the STM32 image. FW-001 §8 and DS-001 F-07/§7.5 require OTA for the product. The ESP32-C3 firmware is not in the delivery (NET-01), so whether it would carry its own Wi-Fi OTA is unverified.

**Impact.** Once ESP32 firmware exists, any fix to it, to the I2C link protocol (ESP32_LINK_PROTOCOL_VERSION 1) or to future BLE support means opening the sealed IP65 enclosure to fit J2 and use CN11. A field update of the STM32 that changes the link protocol would leave the coprocessor on the old version. This undermines the field-update goal for every unit with the Wi-Fi/BLE option populated.

**Fix.** Define a coprocessor update path before the ESP firmware is written. Either add link commands that stream a signed ESP image (the ESP side writes its OTA partition and verifies it), or route IO9 to an MCU GPIO so the STM32 can run the ESP ROM loader over a UART. Include the ESP image and its version in the signed update package and in FW_STATUS.

**Also raised as.** . Earlier audit refs: NET-01, HWI-07, NET-09, BLD-04, SEC-12, FW-36

**Verifier corrections.** Optional extra evidence: the ESP32-C3 USB-Serial-JTAG pins (U19.13/U19.14 = IO18/IO19) are unconnected (NET_66/NET_67), so there is no other wired programming path. J2.1 and R22.1 sit on the GND and 3V3 nets inferred from U19.9/U19.1. No other changes needed.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

### LOW (21)

<a id="sec-10"></a>

#### SEC-10 — Bootloader robustness: it never starts the IWDG, reads staging without waiting for the SPI flash to finish (WIP), and handles a failed INSTALLED write badly

`LOW`

**Location.** Bootloader/boot_staging.c:99-154,156-177; Bootloader/boot_main.c:106-107

**Evidence.** BootStaging_Init reads the JEDEC ID right after release-power-down, with no status poll (boot_staging.c:119-135). BootStaging_Read never checks WIP and always returns 1 (:138-154). The application driver handles this case ('A program or erase cut short by a reset may still be running', gd25q.c:184-188), but the bootloader does not. boot_main.c:107 discards the result of '(void)BootStaging_SetState(FW_IMAGE_STATE_INSTALLED)'. If that write fails, the image stays PENDING, and every later reset re-verifies, erases and reprograms sectors 1-4. Behaviour on hardware is unverified (no target available).

**Impact.** A reset that lands during an SPI program/erase (tag log, SEC-09) can defer or lose an update. A flash that rejects the state write causes a re-flash and several extra seconds of boot on every reset, which eats into the internal-flash erase endurance (about 10k cycles).

**Fix.** Poll status until WIP clears, with a bounded wait, in BootStaging_Init before the JEDEC read and before reading the image. Check the SetState result. On failure, retry, then record the failure in a RAM or backup register for the app to report instead of re-installing on every boot.

**Also raised as.** ROB-10 (low)

**Verifier corrections.** (1) 'BootStaging_Read ... always returns 1' should be 'never reports a flash error': it returns 0 for an out-of-range request (boot_staging.c:142-143). (2) BootStaging_SetState (:156-177) sends WREN and PAGE_PROGRAM without first waiting for WIP and without a read-back, and returns only the WaitReady result. INSTALLED (0x00000000) only clears bits, so any partial program leaves state != PENDING and stops the loop. The re-flash-every-reset case therefore needs the program to have been ignored entirely, for example because the chip was busy. Hardware behaviour is unverified, as the finding already says.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation). The 1 duplicate(s) were each confirmed by at least 2 of 3.*

<a id="sec-13"></a>

#### SEC-13 — Firmware binary and tag data sit in plaintext in external SPI flash; RDP gives no protection of firmware IP

`LOW`

**Location.** Firmware/Common/firmware_image.h:22-28; Bootloader/boot_main.c:107; Firmware/Services/storage/tag_log.c:60-67; Firmware/Services/configuration/reader_settings.c:141-148

**Evidence.** Staging is in the external GD25Q32 (firmware_image.h:22-23). After install only the state word changes (boot_main.c:107), so the full application binary stays readable there. Tag records (EPC, time, antenna, RSSI) are stored in clear (tag_log.c:60-67). Wi-Fi credentials are stored in clear in internal flash (reader_settings.c:141-148).

**Impact.** Anyone with a SOIC-8 clip on U6 can read the complete firmware and the buffered customer tag data whatever the RDP level. Wi-Fi credentials are protected only if RDP 1 was actually applied (SEC-06). This matters only if Identium treats the firmware as confidential IP or tag data as sensitive.

**Fix.** Decide whether firmware confidentiality matters. If it does, encrypt the payload (for example AES-CTR with a per-product key in RDP-protected internal flash) and erase the staging slot once the image is confirmed GOOD. Document that the tag buffer is unencrypted at rest.

**Verifier corrections.** reader_settings.c:141-148 is the legacy ReaderSettings_V3_t migration layout (struct closes at :150). Cite the current ReaderSettings_t instead: wifiPassword at reader_settings.h:69 and wifiApPassword at :75, saved as plain record data at reader_settings.c:531-533. The conclusion (stored in clear) is unchanged.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="hwi-08"></a>

#### HWI-08 — The buzzer on PC13 (backup-domain pin) is driven as a FAST push-pull current source, the Q1 base floats until firmware runs, and the buzzer type is unconfirmed

`LOW`

**Location.** Firmware/BSP/f411_custom/board.c:8-22, :199-210; Firmware/BSP/industrial_io.c:261-275; netlist R66/Q1/BUZZER1

**Evidence.** Board_F411_GPIO_Init configures PC13 through GPIO_ConfigOutput, which sets GPIO_DRV_SPEED_FAST and push-pull (board.c:16-19, :206-210). Netlist: U7.2 PC13 drives R66 (4.7k), then the Q1 base (Q1.1, net `$1N324`). That net has no pull-down, so PC13 sources about 0.55 mA of base current. BUZZER1 (HNB09A05, "3kHz", BOM line 1) is low-side switched from 3V3. LEAD_FACTS.md gives ST's limits for PC13-PC15: 3 mA sink, 2 MHz, 30 pF, and no use as a current source. The firmware drives the buzzer with DC on/off only (industrial_io.c:266-270), and host_interface.c:2564 assumes "The board buzzer has a fixed tone". Whether HNB09A05 is an active (self-oscillating) buzzer, and its rated voltage, is unverified (no datasheet in the package).

**Impact.** Running a backup-domain pin outside ST's stated use is a reliability concern, not a functional failure today. If the part turns out to be a passive transducer, DC drive only produces a click and SET_BUZZER is silently ineffective. A floating base can let the buzzer tick during reset and bootloader time.

**Fix.** Set PC13 OSPEEDR to low speed. On the respin, add a base pull-down (e.g. 47k), or move the buzzer to a pin that can source current, ideally one with a timer channel. Confirm from the HNB09A05 datasheet that it is active and rated for 3.3 V. If it is passive, drive it at 3 kHz from a timer.

**Also raised as.** . Earlier audit refs: R2-20

**Verifier corrections.** 1) The fixed-tone comment is at host_interface.c:2565, not :2564. 2) Add that GPIO_DRV_SPEED_FAST = 2U (gpio.h:62), the "fast" OSPEEDR setting, which exceeds ST's 2 MHz guidance for PC13. 3) The ST limits come from LEAD_FACTS.md, not a datasheet page opened here. 4) Buzzer type (active or passive) and rated voltage remain unverified, as stated. "Can tick during reset" is plausible but untested.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="hwi-09"></a>

#### HWI-09 — The only MCU status LED (PB8) is switched on once at board init and never driven again

`LOW`

**Location.** Firmware/BSP/f411_custom/board.c:212-223; Firmware/BSP/f411_custom/board_config.h:146-149

**Evidence.** board.c:212 says: "Status LED on: shows the firmware got as far as board init." A grep for BOARD_LED_STATUS and LED across own code finds no other writer. Netlist: PB8 drives R10 (330R) and LED3 to GND. LED2 and LED5 are driven by the W5500 (U1.27/U1.25 through R35/R36) and LED4 is the power LED (R68), so LED3 is the only firmware-controlled indicator.

**Impact.** Operators get no indication of RF activity, network or host link, degraded mode or fault. The LED stays lit through a main-loop hang until the watchdog fires, which hides faults during installation and field service.

**Fix.** Add an LED pattern service driven from the existing fault, degraded and inventory state: e.g. a slow blink when healthy, a fast blink when degraded, a flicker on tag reads, and off or solid on fault. Make sure a stalled main loop stops the blink.

**Also raised as.** . Earlier audit refs: R2-20, REQ-03

**Verifier corrections.** Minor: also note that the LED is re-lit at every boot, including after a watchdog reset, so it cannot even show that a reset happened. Context: rev 2 planned four LEDs (power, link, RF, error) as TBD (rev 2 board_config.h:89-97). The 13-Oct board gives the MCU only one, so any pattern service must multiplex states on LED3.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="buf-11"></a>

#### BUF-11 — The 64-record RAM queue does not cover the worst-case erase at the rates its own comment claims

`LOW`

**Location.** Firmware/Services/storage/tag_log.c:50-52, 482-486

**Evidence.** The comment says the queue "covers a worst-case 400 ms sector erase at a few hundred reads per second" (tag_log.c:50-51), but TAG_LOG_QUEUE_SIZE is 64U (:52). At 200 reads/s, 400 ms is 80 records and at 300 reads/s it is 120, so the excess is dropped (:482-486, counted in DROPPED). Any power loss also discards up to 64 queued records. That is within the spec's allowance of loss since the last segment boundary (FW-001 §5.3).

**Impact.** Reads are lost on slow (worn or hot) erases at high read rates, and during every chip erase.

**Fix.** Size the queue to rate * tSE(max), about 128-160 entries (4-5 KB of RAM; check headroom, since RAM is ~116.6 of 128 KB). Better, cut the record rate with dedup (BUF-03).

**Also raised as.** . Earlier audit refs: FW-30

**Verifier corrections.** The 400 ms worst-case erase figure comes from the code comment (tag_log.c:50-51), not from a datasheet in the package. The arithmetic depends on that number: 64 records cover about 160 reads/s for 400 ms. The power-loss note is correctly framed as within FW-001 §5.3, which allows loss of records since the last segment boundary.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="pro-08"></a>

#### PRO-08 — Vendor-tool config (0xEE 0x25) silently drops client_ack=1 and crc_enable=1 and replies OK

`LOW`

**Location.** Firmware/Services/configuration/vendor_config.c:869-870, 1457; Firmware/Services/configuration/vendor_config.h:15-17; Firmware/Services/configuration/configuration_service.c:1471-1488

**Evidence.** The vendor config path deliberately ignores fields it cannot act on instead of rejecting the document (vendor_config.h:15-17). VendorFilter_CopyKeyIfValue passes client_ack and crc_enable to the parser only when they equal 0 (vendor_config.c:869-870), and the request is acknowledged with status 0 (:1457). The JSON set_runtime_conf path, by contrast, flags client_ack=1 as unsupported and rejects it (configuration_service.c:1471-1488). TCP Protocol V1.0 makes server confirmation of tag data a configurable option (p.1).

**Impact.** A commissioning engineer who enables server confirmation in the vendor tool gets an OK. The reader keeps uploading fire-and-forget, so the site believes tag delivery is acknowledged when it is not. This compounds PRO-01.

**Fix.** Return status INVALID (or a specific 'unsupported' status) when the document asks for client_ack=1 or crc_enable=1, matching the JSON path, until the ACK flow is implemented. Then implement the ACK flow.

**Also raised as.** . Earlier audit refs: FW-16, R2-36

**Verifier corrections.** Precision. The status-0 reply at vendor_config.c:1457 comes from the validation step in VendorConfig_Handle. The filter that drops client_ack and crc_enable unless they are 0 runs later, in the deferred VendorConfig_SetActive (:800, :869-870). The OK is therefore sent before the document is even filtered. The silent-ignore policy is deliberate and documented (vendor_config.h:15-17). The real defect is that it covers a delivery-guarantee setting.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="pro-11"></a>

#### PRO-11 — Reader logical name is capped at 32 chars and copied from JSON with escape sequences intact; truncation inside an escape breaks every event

`LOW`

**Location.** Firmware/Services/configuration/reader_settings.h:13; Firmware/Services/configuration/vendor_config.c:260-277, 844-857; Firmware/Protocol/identium_protocol.c:213, 244, 286, 625, 1029

**Evidence.** READER_SETTINGS_NAME_MAX is 32 (reader_settings.h:13). reader allows a logical name of fewer than 255 characters (ActModeDataFormat p.1). VendorJson_GetString copies the raw JSON string bytes, escapes included, and truncates at the buffer size without regard to escape boundaries (vendor_config.c:260-277). The result is stored as the name (:844-857). Every event builder inserts the name with a bare %s (identium_protocol.c:213, 244, 286, 625, 1029). A name whose 32nd character is the backslash of an escape leaves an unterminated JSON string in every message.

**Impact.** Long names are silently shortened. In the edge case above, every upload becomes invalid JSON until the name is changed.

**Fix.** Unescape on input. Reject names longer than the limit instead of truncating them, or raise the limit. JSON-escape the name when emitting events.

**Also raised as.** . Earlier audit refs: R2-02

**Verifier corrections.** Minor additions. The gpi_changed builder also inserts the name with a bare %s (identium_protocol.c:1131 argument; its format string is just above). The cited lines 213/244/286/625/1029 are the format strings, and their GetName() arguments are on the next line. The vendor path (vendor_config.c:844-857) is the only code that writes settings.name besides the default (reader_settings.c:273), so the raw-escape problem affects every name set by the tools.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="rfm-08"></a>

#### RFM-08 — No module abstraction for the planned E710 alternative: the Silion API leaks into 12 files and rev 3 adds E310-specific hard-coding

`LOW`

**Location.** Firmware/Application/Silion/sillion_application.c:576-580, 1242-1252; Firmware/Services/configuration/module_bridge.c:70-74; Firmware/BSP/f411_custom/board.c:235; callers listed in evidence

**Evidence.** A grep finds SILION_* types and calls in 12 files outside the driver directories: identium_command.c, identium_protocol.c/.h, uart_dispatch.c, firmware_configuration.c, host_interface.c, reader_settings.h, module_bridge.c, configuration_service.c, vendor_config.c, tag_log.c/.h. No interface or driver table exists, and grep finds no E710/EX10 code. Rev 3 hard-codes the E310's RF modes (gen2Profiles {103,105,107,112,113}, comment "Gen2 RF modes this module (E310) supports", :1242-1252). The 0x03 reply's hardware-version word, whose byte 3 is the chip type (E710=0x31, E310=0x33; protocol document §4.3 table), is parsed only for printing (:576-580). It is never used to adapt profiles, port count or power limits. The baud rate is fixed at 115200 (board.c:235), but a module's power-on rate can be any of 9600-921600 (§9.7; the document's example reads 921600). The bridge's ModuleAPI emulation is Silion-frame specific (module_bridge.c:70-74). The protocol document covers E310/E510/E710/E910 on the same wire format (§1, line 142-144), and FW-001 p.2 names that same chip family.

**Impact.** If the E710 part is an EX10-family module of the same protocol, it would mostly talk to this driver. However, E710 RF modes are rejected by the E310 whitelist, a multi-port E710 module cannot be driven (single antenna, prior FW-14), and a module shipped at a different baud cannot be reached. If the E710 module uses another vendor's protocol, there is nothing to plug into, and the tag type, configuration paths, bridge and protocol layers all need rework.

**Fix.** Define a small reader-module interface (init/configure, start/stop inventory, tag callback, set region/power/antennas/Gen2, health, version/chip type) with the Silion/EX10 driver as one implementation. Use a neutral tag struct in place of SILION_Tag_t. Read the chip type and port count from 0x03 at boot and choose RF-mode lists and limits from it. Add baud autodetection. Confirm with purchasing which E710 module is planned and obtain its protocol document before the pilot BOM is frozen.

**Also raised as.** . Earlier audit refs: FW-14, REQ-06, FW-61

**Verifier corrections.** (1) Identium's own spec (FW-001 p.2 and p.6 §4) names the module as an Impinj EX10-series part (E310/E510/E710/E910), and the protocol doc applies to all four chips (scope, txt lines 142-145). So the E710 alternative is most likely on this same wire protocol, and the 'another vendor's protocol' branch is speculative. The concrete, evidenced gaps are: the E310-only RF-mode whitelist (sillion_application.c:1242-1252, new in rev 3; absent from the rev 2 file), the chip-type byte parsed only for printing (:576-580, used only in the sprintf at :604-609), the fixed 115200 baud (board.c:235), and single-antenna operation (host_interface.c:483-488). (2) The '12 files' count is correct: host_interface.c, firmware_configuration.c, uart_dispatch.c, identium_command.c, identium_protocol.c/.h, configuration_service.c, module_bridge.c, reader_settings.h, vendor_config.c, tag_log.c/.h. This is a maintainability observation rather than a defect.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="rob-09"></a>

#### ROB-09 — EE frame length arithmetic wraps for declared lengths of 0xFFFA or more, so a 6-byte frame is dispatched with dataLength up to 65535

`LOW`

**Location.** Firmware/Services/host/host_transport.c:245-266; Firmware/Services/configuration/configuration_port.c:402-406

**Evidence.** `needed = (uint16_t)(6U + (((uint16_t)mux->ee[2] << 8U) | mux->ee[3]));` (host_transport.c:245) and `if((uint16_t)(6U + dataLength) > length)` (configuration_port.c:403) both wrap to 0-5. The frame 'EE 00 FF FF cmd 00' is therefore dispatched after 6 bytes with dataLength 0xFFFF. Today every handler re-checks the length (configuration_port.c:108, :134; vendor_config.c:1408-1409), and CLEAR/SWITCH ignore it, so nothing is read out of bounds. The TCP path compares without wrapping (configuration_port.c:325).

**Impact.** This is a latent out-of-bounds read of the 2 KB mux buffer: it becomes real as soon as a future command trusts dataLength.

**Fix.** Do the length arithmetic in 32 bits and reject dataLength > HOST_MUX_EE_MAX - 8 before the frame is used.

**Verifier corrections.** Minor: with flags bit 1 set, declared lengths 0xFFF8-0xFFF9 also wrap in HOST_Mux_Byte (+2 after the wrap point). HandleLinkRequest then rejects them, because (uint16_t)(6 + 0xFFF8) = 0xFFFE is greater than 6. Only 0xFFFA-0xFFFF pass both checks. Today it is purely latent, which the finding already says.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="rob-11"></a>

#### ROB-11 — Host command line buffer is not cleared when a transport is released on disconnect, so a partial line from the dropped client prefixes the next client's first command

`LOW`

**Location.** Firmware/Application/Host/host_interface.c:314-315, :985-986, :2650-2651; Firmware/Services/host/host_transport.c:626-654; Firmware/Services/ethernet/host_tcp_server.c:29-38; Firmware/Services/wifi/wifi_service.c:274-281, :502-506

**Evidence.** hostRxBuffer and hostRxIndex are reset only by the RELEASE command (:985-986) and after a CR (:2650-2651). HOST_Transport_Release flushes the byte queue (host_transport.c:648-649) but not the parser's line. Host_Tcp_Server_Disconnected and the Wi-Fi service call HOST_Transport_Release directly when their client goes away.

**Impact.** After a TCP or Wi-Fi client drops, the next transport's first command is corrupted: it gets ERROR or runs as a different command.

**Fix.** Add a release hook that resets the host line assembler (the same fix R2-31 needs for the JSON parser).

**Also raised as.** . Earlier audit refs: FW-05, R2-31

**Verifier corrections.** Minor: in practice the corrupted first command almost always gives ERROR,INVALID_COMMAND. Running it 'as a different command' needs a contrived prefix, so call that possible but unlikely. HOST_Transport_Release is at host_transport.c:626-654, and the queue flush is at :648-649.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="rob-12"></a>

#### ROB-12 — USB_CDC_Transport_Send and Serial_Port_Send copy with interrupts disabled, and module-bridge relays now pass them up to 992 bytes at a time, longer than the RFID UART can tolerate

`LOW`

**Location.** Firmware/Services/USB/usb_cdc_transport.c:72-94; Firmware/Services/serial/serial_port.c:240-262; Firmware/Services/configuration/module_bridge.c:522-575

**Evidence.** Module_Bridge_RelayModuleBytes sends up to sizeof(g_relay)-32 = 992 bytes per chunk (module_bridge.c:526-536, 569) through HOST_Transport_LinkSend. Both send functions copy the whole chunk with PRIMASK set (usb_cdc_transport.c:72-94; serial_port.c:240-262). In the shipped f411-release ELF the USB copy loop is about 19 instructions with 5 memory accesses per byte (0x08015c68-0x08015ca4). That gives an estimated 200 us or more at 96 MHz for 992 bytes, against about 87 us per character on USART1 at 115200 baud (board.c:235). This is an estimate from the disassembly; it was not measured on hardware.

**Impact.** During USB/serial vendor-tool sessions with the module streaming, USART1 can overrun while a chunk is being copied, corrupting the module frames the tool receives.

**Fix.** Copy with interrupts enabled, using an SPSC ring that publishes the head index in one store, or cap each locked copy at about 64 bytes.

**Also raised as.** . Earlier audit refs: FW-33

**Verifier corrections.** The shipped-ELF loop at 0x08015c68-0x08015ca4 is 19 instructions with 6 memory accesses per byte, not 5: ldrh [r4] twice, ldrh [r12], ldrb, strh, strb. The conclusion is unchanged: about 19-25 cycles per byte x 992 is about 200-250 us at 96 MHz (main.c SystemClock_Config, PLLN 96 / PLLP 2), against 86.8 us per character at 115200 baud (board.c:235). This is still an estimate, not a measurement. The chunk is min(link TX free, 1024) - 32, so 992 is the maximum (module_bridge.c:524-536).

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="rob-13"></a>

#### ROB-13 — ESP32 link: g_transferEvent is written by the ISR but not declared volatile, and is cleared only after the interrupt transfer has started

`LOW`

**Location.** Firmware/Devices/esp32/esp32_link.c:74, :209-216, :451-454, :572-574, :660-671

**Evidence.** `static uint8_t g_transferEvent;` (:74) is written by I2C_ApplicationEventCallback in ISR context (:660-671) and polled by ESP32_Link_Task. ESP32_Link_StartTransmit starts I2C_MasterSendDataIT (:209) and only then sets g_transferEvent = ESP32_LINK_EVENT_NONE (:215). An error event raised before that store is lost. The RX path clears the flag first, which is the correct order (:572-574).

**Impact.** Today each occurrence costs one 80 ms transfer timeout (:451-454). Under LTO or inlining, the missing volatile could make the poll miss events.

**Fix.** Declare g_transferEvent volatile and clear it before starting the transfer.

**Verifier corrections.** Weaken the volatile point. The build uses -Os with no LTO (Makefile:90, :141), and every read of g_transferEvent sits in ESP32_Link_Task, once per call (esp32_link.c:454, :494, :503), so the missing volatile has no effect in this build. The ordering race needs an I2C error event (BERR/ARLO/AF) to fire within the few instructions between I2C_MasterSendDataIT returning and the store at :215. That is practically very rare, and each occurrence costs one 80 ms timeout (ESP32_LINK_TRANSFER_TIMEOUT_MS, esp32_link.c:17). It is a quality fix; low or info.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="net-10"></a>

#### NET-10 — HTTP over Ethernet waits at least 1 s before every new connection after a normal server close

`LOW`

**Location.** Firmware/Services/ethernet/ethernet_service.c:131-137, 166-181, 196-199; Firmware/Services/ethernet/http_upload.c:626-629

**Evidence.** When socket 0 leaves ESTABLISHED, the next connect is scheduled at now + ethernetReconnectDelayMs (ethernet_service.c:166-181). That delay is reset to 1,000 ms on each successful connect (line 136) and doubled on each disconnect. Connects are gated at 196-199. HttpUpload_FinishResponse closes the socket on "Connection: close" (http_upload.c:626-629). The code itself notes that HTTP servers close idle keep-alive connections (ethernet_service.c:201-203). The host test's simulated server never closes the connection, so this path is untested.

**Impact.** Against a server that closes after every response, throughput is capped at about 1 POST/s, i.e. at most about 130 EPC-96 tags/s in 8 KB batches. That is below a busy reader's read rate. It is unverified whether Identium's reference Python HTTP server closes per request.

**Fix.** Reconnect immediately after an orderly close that followed a completed request. Apply backoff only after failed connects.

**Verifier corrections.** (1) The delay is also doubled after each connect attempt (ethernet_service.c:224-235), not only after a disconnect. It is reset to 1,000 ms on ESTABLISHED (131-137). The minimum gap after an orderly close is still 1 s, as stated. (2) Per-POST throughput is about 115-120 EPC-96 tags (about 68 bytes per tag object), not 130.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="net-12"></a>

#### NET-12 — The MAC address is a 32-bit hash of the MCU ID in the locally administered range, stored in settings and overwritable over the network

`LOW`

**Location.** Firmware/Services/configuration/reader_settings.c:230-238; Firmware/Services/configuration/module_bridge.c:312-330

**Evidence.** The default MAC is 02:1D followed by CRC32(UID), so only 32 bits vary (reader_settings.c:230-238). It is saved in the settings record. IPSET on the unauthenticated port 8080 can replace it with any unicast value (module_bridge.c:315-330). The board has no MAC EEPROM. The old fixed 11:22:33:44:55:66 path (W5500_SetNetworkConfig) is no longer linked (map:735-736).

**Impact.** Collision probability for N units on one L2 segment is about N^2/2^33: about 0.1% at 3,000 units and about 1.2% at 10,000. There is no IEEE OUI, and a tool or user can clone a MAC onto another unit. Acceptable for the pilot, not for volume.

**Fix.** Buy an IEEE MA-S/OUI block and write a serialised MAC at production, into the F411 OTP area or the GD25Q security registers. Allow MAC writes only in a factory mode.

**Also raised as.** . Earlier audit refs: FW-08

**Verifier corrections.** (1) IPSET accepts any unicast, non-zero MAC except the tools' 'FC FF' placeholder (module_bridge.c:323-330). (2) The collision figures assume all N units share one L2 segment, a worst case. The arithmetic (N^2/2^33: 0.10% at 3,000, 1.16% at 10,000) is right. (3) net.py find for EEPROM, 24C and AT24 returns nothing, which supports 'no MAC EEPROM'.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="bld-09"></a>

#### BLD-09 — F429/F411 configuration drift: the default board is the dev kit, the product image reports 'RFID-F429-DEV', the IDE F411 config is debug-only, and TARGET_BOARD silently falls back to F429

`LOW`

**Location.** Makefile:33,215; Firmware/Config/product_config.h:12,22; Firmware/Services/configuration/reader_settings.c:273,603; Firmware/Config/firmware_config.h:9-11; .cproject:188-215

**Evidence.** 'BOARD ?= f429' (Makefile:33), although the product is the F411. PRODUCT_PROTOCOL_READER_NAME is "RFID-F429-DEV" (product_config.h:22). It is the default name (reader_settings.c:273, :603) sent in every Identium JSON message (identium_protocol.c:214,245,288,626,1030), and 'strings' finds it in the F411 release .bin. firmware_config.h:9-11 defaults TARGET_BOARD to BOARD_F429_DISCOVERY when it is undefined. 12 files select on STM32F411xE and 15 on TARGET_BOARD, with no #error tying the two. The CubeIDE 'F411' configuration (.cproject:188-215) is -Og with DEBUG defined and has no release variant; its artifact is named RFID_Controller_F429_f411. The protocol host test is built only with -DSTM32F429xx (Makefile:215). Several Tools scripts are written for the F429 (identium_tcp_hardening_test.py:3, identium_tcp_json_test_server.py:68).

**Impact.** Pilot units identify themselves as an F429 development unit. A build that omits -DTARGET_BOARD would quietly mix F429 board code with F411 MCU headers. Defaults steer people toward the wrong target.

**Fix.** Make F411 the default board, rename the default reader name (or derive it from MAC/serial), and add '#if defined(STM32F411xE) != (TARGET_BOARD == BOARD_F411_CUSTOM) #error'. Remove the F429 fallback, and add an F411 release configuration or drop the CubeIDE build in favour of the Makefile.

**Also raised as.** . Earlier audit refs: R2-05, FW-10 (the F411 now builds; drift remains)

**Verifier corrections.** Remove or correct the impact sentence "A build that omits -DTARGET_BOARD would quietly mix F429 board code with F411 MCU headers". It does not fail quietly. I ran a clang -fsyntax-only pass (thumbv7em, Makefile include set, -DSTM32F411xE, no TARGET_BOARD). main.c, identium_protocol.c, diagnostics.c, firmware_configuration.c and usb_cdc_transport.c fail with 'redefinition of enumerator NonMaskableInt_IRQn' from stm32f429xx.h, because the F429 BSP/driver headers and the HAL's stm32f411xe.h are both pulled in. With -DTARGET_BOARD=2 these files compile. The build therefore breaks loudly. The missing #error that would tie the two macros is still a valid hygiene point. Everything else verified: Makefile:33 and :215, product_config.h:22 ('RFID-F429-DEV'), reader_settings.c:273 and :603, the five identium_protocol.c call sites, firmware_config.h:9-11, 12 files on STM32F411xE and 15 on TARGET_BOARD, .cproject:188-215 (-Og, DEBUG), identium_tcp_json_test_server.py:68.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="bld-10"></a>

#### BLD-10 — The new F411 CubeMX project describes only the clock and USB, so it cannot generate or validate the board pin map; the F429 .ioc is stale

`LOW`

**Location.** Boards/f411/F411_custom.ioc:10-24,77; RFID_Controller_F429.ioc:10-17

**Evidence.** F411_custom.ioc lists IPs NVIC/RCC/SYS/USB_DEVICE/USB_OTG_FS only, and pins PH0, PH1, PA11, PA12 plus two virtual pins (Mcu.PinsNb=6, :18-24). It has no SPI1, I2C1, USART1/2 or GPIO, and ProjectManager.MainLocation=Core/Src (:77) does not match this tree. The clock settings are consistent: HSE 8 MHz, PLLM 4, PLLN 96, 96 MHz SYSCLK, 48 MHz USB (:104-117). RFID_Controller_F429.ioc still has 3 IPs and 1 pin (:10-17).

**Impact.** There is no tool-checked alternate-function/pin-conflict view of the real board, and the shared-pin issues are found only by hand. The .ioc cannot regenerate code into this project.

**Fix.** Complete F411_custom.ioc with every pin from the netlist (SPI1 PA5-7, CS PA1/PA4, I2C1 PB6/7, USART1 PA9/10, USART2 PA2/3, mux PB14, DE/RE PB12/13, relays, GPIs, PC13/PC14/PC15 limits) and use it as the pin-map reference, or delete both .ioc files and keep board_config.h as the single source.

**Also raised as.** . Earlier audit refs: PRG-01, R2-28

**Verifier corrections.** Add that neither .ioc file feeds the Makefile build. The impact is tooling and documentation only, and info would also be a defensible rating.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="bld-11"></a>

#### BLD-11 — Memory-layout constants are duplicated with no link-time checks; the bootloader's promise to preserve the fault record is not enforced

`LOW`

**Location.** Boards/f411/STM32F411CEUX_FLASH.ld:51,171-179; STM32F429ZITX_FLASH.ld:55; Firmware/Common/firmware_image.h:91,95; Tools/fw_sign.py:39,43; Src/system_stm32f4xx.c:96,110; hostcheck/make_test_images.py:35; Bootloader/bootloader_f411.ld:2-3,8; Makefile:7-8

**Evidence.** The application base 0x08004000 is written out separately in both app linker scripts, firmware_image.h:91, fw_sign.py:39, system_stm32f4xx.c:110 (VECT_TAB_OFFSET 0x00004000U) and make_test_images.py:35. The 112 KB limit appears in the linker script (:51), firmware_image.h:95 and fw_sign.py:43. bootloader_f411.ld:2-3 claims the app's .noinit fault record survives because the bootloader uses only RAM 0x2001E000+. The app script has no ASSERT keeping .noinit below that address; it currently ends at 0x200185A8, about 23 KB of .bss growth away. Makefile:7-8 says the code area is 128 KB, but it is 112 KB.

**Impact.** A future layout change (needed for BLD-02) can silently desynchronise the bootloader, the signer and the vector table, or let the bootloader overwrite the fault record.

**Fix.** Define the layout once (a header included by the linker scripts through the C preprocessor, or generated) and add ASSERTs: app end <= settings start, .noinit end <= bootloader RAM origin, LENGTH == FW_APP_MAX_SIZE. Have fw_sign.py read the constants from that header.

**Verifier corrections.** The second half of the title, "the bootloader's promise to preserve the fault record is not enforced", is wrong as stated. STM32F411CEUX_FLASH.ld:41-42 and :183-190 reserve _Min_Heap_Size 0x200 + _Min_Stack_Size 0x4000 = 0x4200 after .noinit. So the link fails ('region RAM overflowed') as soon as .noinit ends above 0x20020000 - 0x4200 = 0x2001BE00, which is below the bootloader RAM origin 0x2001E000 (bootloader_f411.ld:8). F429 is the same: 0x2002BE00 < 0x2002E000. The protection is implicit, not an explicit ASSERT, and it would be lost only if the stack reservation fell below 8 KB. Also, '.noinit is about 23 KB of .bss growth away' is wrong: the link would fail after about 14.4 KB of growth (0x2001BE00 - 0x200185A8). Retitle to the duplication and missing-ASSERT issue and drop the fault-record claim, or describe it as an implicit dependency.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="bld-12"></a>

#### BLD-12 — Warning policy not enforced in firmware builds; naked fault handlers contain C statements

`LOW`

**Location.** Makefile:98-99,139-142; Src/stm32f4xx_it.c:62-66,82-150; Firmware/Drivers/spi/spi.c:236

**Evidence.** Firmware and bootloader compile with -Wall only (Makefile:98, :141). -Werror is left to EXTRA_CFLAGS, and no CI is delivered. My clang syntax pass over the 60 own F411 translation units (thumbv7em, the Makefile's include set, -DSTM32F411xE -DTARGET_BOARD=2) gave 1 -Wall warning (spi.c:236, the open rev-1 defect) and 6 -Wextra unused-parameter warnings. That is clean. Clang also rejects the five fault handlers declared naked (stm32f4xx_it.c:62-66) because their bodies add a C 'while (1)' after FAULT_RECORD_ENTER. GCC compiles them, but its manual calls mixing C into naked functions unsupported. The loop is unreachable after the asm 'b Fault_Record_Capture'.

**Impact.** Small: warnings can creep back unnoticed, and the fault-capture path relies on unsupported compiler behaviour.

**Fix.** Add -Wextra -Werror to the Makefile for own code (keep vendor code at -Wall). Remove the C loops from the naked handlers (the asm already branches to a noreturn function) or move them into the asm. Run both board builds in CI.

**Also raised as.** . Earlier audit refs: FW-59, R2-54 (spi.c:236 is tracked there)

**Verifier corrections.** Add that the shipped code is correct. Disassembly of the release ELF shows each naked handler ends 'b.w Fault_Record_Capture' followed by an unreachable 'b .' (NMI_Handler at 0x08004792, HardFault_Handler at 0x080047B6), with no prologue and no stack use. The naked/C mix is a portability and compiler-support issue (info), not a runtime defect. The warning-policy part (-Wall only at Makefile:98 and :141, -Werror left to EXTRA_CFLAGS, no CI delivered) stands.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="bld-13"></a>

#### BLD-13 — No third-party notice file for binary distribution (micro-ecc BSD-2, ST HAL BSD-3, CMSIS Apache-2.0, ST USB SLA0044)

`LOW`

**Location.** Bootloader/micro-ecc/LICENSE.txt; Bootloader/micro-ecc/README_IDENTIUM.txt; Drivers/STM32F4xx_HAL_Driver/LICENSE.txt; Drivers/CMSIS/LICENSE.txt; Firmware/Middlewares/ST/STM32_USB_Device_Library/LICENSE.txt

**Evidence.** micro-ecc's BSD-2 licence is shipped, and README_IDENTIUM.txt pins upstream commit 541b3a78... and says 'unmodified' (not verified against upstream). BSD-2/BSD-3 require the copyright notice to be reproduced in the documentation of a binary distribution, and CMSIS is Apache-2.0. The USB library is under ST SLA0044, which restricts use to ST devices and is satisfied here. No NOTICE / THIRD_PARTY file or product-documentation text covers these. Own-generated files such as Boards/f411/STM32F411CEUX_FLASH.ld:25 carry an ST copyright line.

**Impact.** Pilot units leaving Identium are a binary distribution without the required notices. The fix is easy but it is a legal obligation.

**Fix.** Add THIRD_PARTY_NOTICES (micro-ecc, ST HAL/CMSIS, ST USB library) to the repo and to the product manual or web page. Put Identium headers on own files.

**Also raised as.** . Earlier audit refs: R2-50

**Verifier corrections.** Clarify the USB library point. SLA0044 clause 2 (LICENSE.txt:28-33) exempts binaries embedded in an ST microcontroller from the notice requirement, so the ST USB library needs no notice in this product. The notice obligation comes from micro-ecc (BSD-2), the ST HAL (BSD-3 applies because no Package_license file is delivered, per Drivers/STM32F4xx_HAL_Driver/LICENSE.txt) and CMSIS (Apache-2.0). Including the USB library in a notices file is harmless.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="gap-03"></a>

#### GAP-03 — Factory defaults are development settings: active upload on by default, dialling a hard-coded 192.168.150.100:9000

`LOW`

**Location.** Firmware/Config/product_config.h:15-22; Firmware/Services/configuration/reader_settings.c:215-218, 242-248; Firmware/Services/ethernet/ethernet_service.c:93-94, 127

**Evidence.** product_config.h:15-21 labels the server "development target": 192.168.150.100 port 9000. ReaderSettings_GetDefaults sets the reader IP to 192.168.150.50 (reader_settings.c:215-218), copies that server address (reader_settings.c:242-246) and sets `settings->workState = READER_SETTINGS_WORK_ACTIVE;` (reader_settings.c:248). Ethernet_Service_Task skips connecting only when the work state is not ACTIVE (ethernet_service.c:93-94) and otherwise dials settings->serverIp (ethernet_service.c:127). FW-001 §3 (p.3) says TCP and HTTP active modes are "default-off ... until the customer configures an endpoint". DS-001 p.5 gives the existing product's factory IP as 192.168.1.100.

**Impact.** A unit fresh from the factory, plugged into a customer LAN that happens to use 192.168.150.0/24, opens a TCP session to whatever host is at .100:9000 and streams heartbeats (and tags, once inventory runs) under the reader name RFID-F429-DEV. Every unit also defaults to the same IP, which differs from the address customers' existing documentation and tools expect.

**Fix.** Ship with active upload disabled (passive or no endpoint) until an endpoint is configured. Choose and document an Identium factory IP, plus DHCP or a unique default. Remove the developer target from product_config.h.

**Also raised as.** . Earlier audit refs: FW-08, R2-02, REQ-11

**Verifier corrections.** Nuance: DS-001 Q-07 (p.25) still lists the default IP scheme as an open decision (keep 192.168.1.100 or move to DHCP/mDNS). The p.5 citation is the J48 IP_Rst row, which restores 192.168.1.100. The other defaults behind the claim: uploadMode is 0 = TCP via memset (reader_settings.h:29), and uploadInterface is AUTO, which picks ETH when the link is up (reader_settings.c:271, active_upload_transport.c:19-20). The reader name RFID-F429-DEV is set at reader_settings.c:273.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="gap-04"></a>

#### GAP-04 — An IP/MAC change does not reset the port-5000 socket, so a connected host session is orphaned and keeps the legacy parser locked

`LOW`

**Location.** Firmware/Services/configuration/reader_settings.c:645-656; Firmware/Devices/w5500/w5500.c:394-417; Firmware/Services/ethernet/host_tcp_server.c:81-123; Firmware/Services/configuration/module_bridge.c:338; vendor_config.c:1306

**Evidence.** ReaderSettings_Task applies a pending network change by calling ReaderSettings_ApplyNetwork, W5500_Socket1_Close and Ethernet_Service_Restart (reader_settings.c:650-655), under the comment "Connections made on the old address cannot survive the change" (reader_settings.c:652). W5500_SetNetworkConfigValues writes only GAR/SUBR/SHAR/SIPR (w5500.c:406-411) and resets no socket. Socket 3 (TCP 5000) leaves ESTABLISHED only when the W5500 reports CLOSE_WAIT or CLOSED (host_tcp_server.c:86-118). Those states never arrive for a peer that can no longer reach the old address while the reader has nothing to send. Changes are triggered by IPSET on 8080 (module_bridge.c:338) and by vendor static config (vendor_config.c:1306).

**Impact.** Whenever the vendor tool re-addresses a reader while a host application is connected on port 5000, that socket stays ESTABLISHED forever. Port 5000 is unusable, and if that client had claimed the host parser, USB and RS-232/485 legacy commands stay locked until reboot. This is a deterministic trigger for the half-open lockout.

**Fix.** On a network apply, close and re-listen socket 3 and release the host transport, as is already done for socket 1. Add keep-alive and an idle timeout to socket 3 regardless.

**Also raised as.** . Earlier audit refs: SEC-08, ROB-07, NET-07, FW-15

**Verifier corrections.** Add that nothing else releases the claim. HOST_Transport_ReadByte drops bytes from any transport that is not active (host_transport.c:559-565), and only RELEASE, which the orphaned client can no longer send, or a socket-3 CLOSED/CLOSE_WAIT clears it (host_tcp_server.c:29-39, host_interface.c:995). Socket 3 has no keep-alive: only socket 0 sets KPALVTR (w5500.c:659, 705). The qualifier "while the reader has nothing to send" is essential. If host tags are routed to Ethernet, a send from the new IP would get a RST or time out and close the socket.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

### INFO (2)

<a id="sec-12"></a>

#### SEC-12 — Update chain deviates from FW-001 §8; design note missing; Identium-protocol OTA unsupported; no TLS; app does not pre-check the signature

`INFO`

**Location.** Firmware/Protocol/identium_command.c:887-896; Firmware/Services/ethernet/http_upload.c:134; Firmware/Services/update/firmware_update.h:4-17; Bootloader/boot_main.c:4

**Evidence.** The code repeatedly cites docs/design/0020-bootloader-and-signed-update.md (boot_main.c:4, firmware_update.h:6, fw_sign.py:4), but the zip contains no docs/ directory or .md file (unzip -l). The Identium JSON 'update_fw_by_http' command returns 'not supported operation' (identium_command.c:887-896), so field update exists only as the legacy ASCII FW_* commands (host_interface.c:752-763). No channel uses TLS: http_upload.c:134 rejects https://, whereas FW-001 §3 calls for TLS 1.3 (p.2). The image format ('TLFW', text version, no image type) differs from FW-001 §8.2 ('INB1', image type, semver; p.10). FW_END only checks the SHA-256 and leaves the signature to the bootloader (firmware_update.h:10-11), so an unsigned image costs a full reboot before it is rejected.

**Impact.** The behaviour rests on an undelivered design document. The OTA server flow required by the spec (HTTPS fetch, server-initiated update) does not exist. A wrongly signed image causes avoidable downtime.

**Fix.** Deliver design note 0020 and reconcile it with FW-001 §8, or record agreed deviations. Implement update_fw_by_http, or an authenticated equivalent, on the Identium protocol. Verify the ECDSA signature in FW_END (micro-ecc is already linked in the update host test) so bad images are rejected before reset.

**Also raised as.** . Earlier audit refs: FW-17, R2-51

**Verifier corrections.** (1) The TLS 1.3 requirement is in FW-001 §2 'Code Organisation & Build' (Crypto row, p.2) and in review checklist item 14 (p.13), not §3. (2) FW-001 targets a different platform (STM32H7, LAN8720, W25Q512, FreeRTOS/mbedTLS per p.2), so some deviations follow from the hardware change. Record them as agreed deviations rather than plain non-conformance. (3) uECC is not in the F411 application build (no uECC.o in build/f411-release). Verifying the signature in FW_END would add uECC's code size to an app already at about 83% of its 112 KB slot. That is feasible but should be measured.

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

<a id="gap-05"></a>

#### GAP-05 — The 32-byte MPU stack guard is smaller than many stack frames (up to 840 B), so an overflow can skip it

`INFO`

**Location.** Firmware/Services/system/stack_monitor.c:6, 17-34; Boards/f411/STM32F411CEUX_FLASH.ld:42; build/f411-release/Firmware/Application/Host/host_interface.su:10

**Evidence.** STACK_MONITOR_GUARD_BYTES is 32U (stack_monitor.c:6), and the MPU region is SIZE=4, i.e. 32 bytes no-access (stack_monitor.c:23-27). The linker script describes the 16 KB budget as "enforced by the MPU guard" (STM32F411CEUX_FLASH.ld:42). The shipped F411 release .su files list frames far larger than 32 bytes, e.g. HOST_Interface_Task 840 B (host_interface.su:10), VendorConfig_SetActive 696 B and Configuration_Storage_Save 576 B. A single `sub sp` that moves SP below the guard lets that function's locals write below it without touching the 32-byte window. The practical risk is low: about 12 KB of unused RAM sits below the guard before the heap and .bss.

**Impact.** Overflow detection is best-effort, not enforcement as the linker comment claims. A deep overflow could corrupt memory before or instead of faulting.

**Fix.** Make the guard at least as large as the largest frame (1 KB region), or use -fstack-check or a stack-limit check, and correct the comment.

**Also raised as.** . Earlier audit refs: FW-56

**Verifier corrections.** The free-RAM figure should be about 14 KB, not about 12 KB. In the F411 release map _end = 0x200185A8 (map:4594). With the 0x200 heap reserve the gap up to the guard base (0x20020000 - 0x4000 = 0x2001C000) is 0x3858, i.e. 14,424 B. _sbrk may grow the heap into that space up to the guard (Src/sysmem.c:58-59). VendorConfig_SetActive appears in the .su as VendorConfig_SetActive.constprop.isra (696 B).

*Confirmed by 3 of 3 verifiers (evidence, engineering, mitigation).*

## Status of Every Rev 2 Finding

All 130 findings carried by IDM-AUD-002 were re-checked against the rev 3 tree, which now targets the F411 custom board. That covers 76 from rev 1 and 54 new in rev 2. "rev 2" shows the severity IDM-AUD-002 gave each one; "now" is its severity in rev 3.

### REGRESSED (7)

#### FW-02 — Default RF region is China-1 (920-925 MHz), no India region is defined, and SET_REGION accepts any value

`REGRESSED` · rev 2: MEDIUM → now: HIGH

**Rev 3 evidence.** UNCHANGED: there is still no real India code. Firmware/Config/product_config.h:45 keeps PRODUCT_RFID_DEFAULT_REGION 8U, and Firmware/Protocols/silion/silion_protocol.h:87 still labels 0x08 (the doc's CE_LOW, whose 867.5 MHz channel is above 865-867 MHz) as SILION_REGION_INDIA. No 0x04 constant exists (grep REGION). The ASCII allowlist at sillion_application.c:1148-1155 still admits NORTH_AMERICA 0x01, CHINA_1 0x06 and the bogus 0x0A, and the comment 'SET_REGION,FF' remains at host_interface.c:1707. IMPROVED: the region now persists. SET_REGION saves via HOST_PersistRegionPower (host_interface.c:338-358, called at :1736 only after SetRegion succeeds), and boot restores it (firmware_configuration.c:452-455). REGRESSION (new path, linked in F411 map: VendorConfig_Handle map:1542-1544): VendorConfig_SetStatic accepts JSON rf.region as any value 0..255 (vendor_config.c:1176-1183) and writes it to flash (:1229-1249) before any validation. It then calls SetRegion and ignores the result (:1262), so the reply still reports success. At the next boot readerConfig.region is loaded from flash (firmware_configuration.c:452-455), and ConfigureReader sends it to the module with no allowlist check (sillion_application.c:341-344). The path is reachable over unauthenticated TCP 8080 (configuration_port.c:189-207), USB and serial (host_transport.c:278-284). Rev 2 had only the validated ASCII path. Rev 3 therefore re-opens 'accepts any value', persistently. Unverified (needs hardware): if the module rejects an invalid code with a non-zero status, ConfigureReader fails (status check sillion_application.c:122-135), and the reader would stay in RFID-degraded retry (firmware_configuration.c:228-260) after every boot.

**Remaining work.** 1) Add the real INDIA 0x04 constant, rename 0x08 to CE_LOW, and choose the default per module type after compliance sign-off. 2) Put one region validator in front of every path (ASCII SET_REGION, JSON rf.region in vendor_config.c, and the boot restore in firmware_configuration.c:452-455/ConfigureReader). Use an India-SKU allowlist, cross-checked against the module's 0x71 list, and drop 0x01/0x06/0x0A or put them behind a factory command. 3) In VendorConfig_SetStatic, validate first, apply, and save only on success. Return failure when SetRegion fails rather than ignoring it at :1262. 4) Re-validate the stored region at load, falling back to the default when it is invalid, so a bad record cannot leave RF dead. 5) Fix the SET_REGION,FF comment, and clamp power to the certified limit (rp/wp currently accept up to 3300 at vendor_config.c:1207-1221).

#### FW-05 — Host transport ownership latches to the first sender and only RELEASE frees it: USB/Ethernet lockout

`REGRESSED` · rev 2: LOW → now: MEDIUM

**Rev 3 evidence.** The first-sender latch is unchanged: host_transport.c:528-550 makes the first printable byte's source hostActiveTransport, and :559-565 silently drops bytes from every other source. Rev 2 had only USB-labelled producers. Rev 3 feeds four into the same latch: USB (usbd_cdc_if.c:274), Ethernet from a new ASCII TCP server on port 5000 (host_tcp_server.c:97, socket 3, PRODUCT_ETHERNET_TCP_PORT 5000 product_config.h:13, started at firmware_configuration.c:514), serial RS-232/485 (serial_port.c:315, which pushes every received byte) and Wi-Fi (wifi_service.c:305). So the USB/Ethernet lockout from the title is back, with serial and Wi-Fi added. Auto-release exists only on a TCP-5000 disconnect (host_tcp_server.c:29-39) and on Wi-Fi client loss (wifi_service.c:274-281, :501-506). USB never releases: CDC_SET_CONTROL_LINE_STATE is empty (usbd_cdc_if.c:236-238), CDC_DeInit only resets the TX queue (:171-175), and VBUS sensing is disabled (usbd_conf.c:415). Serial never releases either. So one stray printable byte on an RS-485 bus, a USB host that sent one command and was unplugged, or a silent or half-open client on TCP 5000 (no keep-alive on socket 3, w5500.c:1806-1830) locks the other three links out of the ASCII parser until RELEASE arrives on the owning link (host_interface.c:960-995) or the unit reboots. There is no inactivity timeout. Release does not stop inventory: HOST_Transport_Release (host_transport.c:626-654) only clears the owner and tag transport, and the StopInventory call is only in the RELEASE command (host_interface.c:968). Vendor-tool frames (FF/EE/tokens) bypass the latch (host_transport.c:507-515), so RFID Manager traffic is not affected. JSON/HTTP active mode uses a separate parser.

**Remaining work.** 1) Add an inactivity timeout to hostActiveTransport (a few seconds after the last complete command), or drop the global owner and reply on the link a command came from. 2) Release on USB DTR drop, suspend or disconnect (fill in CDC_SET_CONTROL_LINE_STATE and the suspend callback) and stop or redirect a link-owned inventory on any link loss. 3) Enable keep-alive and an idle timeout on socket 3 (port 5000). 4) For serial/RS-485, claim ownership only on a complete, valid command line, not on any printable byte, or make the serial host port opt-in. 5) Add a host test: USB claims, cable is pulled, then serial/TCP commands must work.

#### FW-15 — Single TCP server socket with no keep-alive: a half-open client blocks all future connections

`REGRESSED` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Socket 0 is still an outbound client with keep-alive (w5500.c:659 keepAlive=6, written to W5500_S0_KPALVTR at :705), so it recovers on its own. However, rev 3 adds a new single-socket TCP listener: socket 3 on port 5000 for the legacy ASCII parser (host_tcp_server.c:20-25, 75-124; W5500_Socket3_Listen w5500.c:1806-1830). It writes no Sn_KPALVTR (the only KPALVTR write in w5500.c is the socket-0 one at :705) and has no idle timeout. Non-terminal states are ignored as 'transient' (host_tcp_server.c:120-122). A silent or half-open client therefore holds ESTABLISHED indefinitely, new connections cannot be accepted, and the client also latches the host parser (FW-05). This is the exact FW-15 defect re-created on a new port. The config port (socket 1, 8080) is unchanged from rev 2: W5500_StartTCPServer1 still writes no keep-alive (w5500.c:1177-1270). The g_waitingForClientClose wait is still unbounded (configuration_port.c:269-272), as are the partial-frame waits (:285-288, :306-309, :337-340), and other states only clear g_started (:396-399). Rev 3 also keeps 8080 occupied by persistent Reader Manager module sessions (configuration_port.c:256-263). A TCP module session has no idle timeout of its own: MODULE_BRIDGE_LINK_IDLE_MS applies only to serial links (module_bridge.c:52-57, 784-792), and it is dropped only when a different transport takes over after 3 s of host silence (module_bridge.c:597-614). Two listeners can now be blocked until reboot, where rev 2 had one.

**Remaining work.** 1) Write Sn_KPALVTR (for example 6 = 30 s) for sockets 1 and 3 when listening. 2) Add application idle timeouts: on 8080, close if no complete request arrives within about 5 s or the client has not closed within about 2 s after the response; on 5000, close after N seconds without a command; on a TCP module session, end after about 30 s of host silence, as for serial links. 3) Time out and force-close non-terminal states (SYNRECV, FIN_WAIT, CLOSING, LAST_ACK, INIT) on sockets 0, 1 and 3. 4) Consider a 'new connection pre-empts stale one' policy or a second listening socket. 5) Add a test that opens and abandons connections on 5000 and 8080, then checks that a new client is served.

#### FW-24 — Unauthenticated, unencrypted TCP port exposes KILL/LOCK/WRITE and RF region/power commands

`REGRESSED` · rev 2: LOW → now: HIGH

**Rev 3 evidence.** Rev 2 had closed the inbound legacy-parser port. The R2 tree has no socket-3 server, and only the config port listens (R2 configuration_port.c:300/313 W5500_StartTCPServer1). Rev 3 adds a new file, Firmware/Services/ethernet/host_tcp_server.c. It listens on PRODUCT_ETHERNET_TCP_PORT (host_tcp_server.c:24), which is 5000U (Firmware/Config/product_config.h:13). It feeds every received byte to the legacy ASCII parser: `HOST_Transport_PushByte(HOST_TRANSPORT_ETHERNET, chunk[i]);` (:97). The server is initialised at firmware_configuration.c:514 and run from HOST_Interface_Task (host_interface.c:872). Both are linked into the shipped F411 release image (build/f411-release map: Host_Tcp_Server_Init 0x0801340c, Host_Tcp_Server_Task 0x0801342c). HOST_Transport_PushByte accepts the ETHERNET and WIFI sources with no authentication (Firmware/Services/host/host_transport.c:393-401). A grep finds no auth, password or token gate on any transport. The commands reachable unauthenticated over the LAN are: KILL_TAG (host_interface.c:2157), LOCK_TAG (:2066), WRITE_TAG (:2237), WRITE_EPC (:1969), SET_REGION (:1714), SET_POWER (:1600), REBOOT (:1276) and TAGLOG_CLEAR (:1306), which wipes the offline buffer. Firmware update is also reachable: FW_BEGIN/FW_DATA/FW_END/FW_APPLY are documented as "Firmware update over any host link" (:752) and dispatched at :1328. Firmware/Services/update/firmware_update.c has no transport check. Port 8080 now relays raw module frames from any client (configuration_port.c:290-298 calls Module_Bridge_Begin) and accepts IPSET, which changes the reader's IP after 1.5 s (module_bridge.c:32, :434-442, :338). The same parser is reachable over the Wi-Fi host socket (wifi_service.c:302-305). Wi-Fi is off by default (reader_settings.c:248), and an empty AP password means an open AP (host_interface.c:101-105). One sub-item improved: the server IP and port are now persisted settings rather than compile-time only (reader_settings.c:242-246). The production signing key ships in the zip (keys/fw_signing_key.pem, confirmed in LEAD_FACTS). Together with that key, port 5000 lets anyone on the LAN install arbitrary signed firmware on any unit. That makes the combination critical until the key is rotated.

**Remaining work.** 1. Remove or disable the port-5000 legacy server by default, or put a per-device commissioned credential or challenge in front of it, the 8080 module bridge and the Wi-Fi host socket. 2. Until then, refuse KILL, LOCK, WRITE, SET_REGION, SET_POWER, TAGLOG_CLEAR, IPSET, REBOOT and FW_* on ETHERNET and WIFI sources (host_transport.c:393, host_interface.c:752-830). 3. Rotate the signing key, and allow firmware update only from an authenticated or local link. 4. Rate-limit configuration, GPO and reboot commands, and log them in the fault record. 5. Never allow an open Wi-Fi AP. 6. Plan TLS or an authenticated tunnel for the management and upload channels.

#### FW-58 — Very long, very complex functions and heavy copy-paste

`REGRESSED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Function sizes were measured with a brace-matching script (lizard is not available here). Firmware/Protocols/silion/silion_protocol.c and .h are byte-identical to rev 2 (diff -q). Function sizes there: SILION_ProcessByte @3375 about 398 lines; ParseTagBuffer @2964 about 379; ParseAsyncTag @2278 about 348. HOST_Interface_Task grew from 1402 lines (rev 2, host_interface.c:175-1576) to 1817 lines (rev 3, host_interface.c:869-2685). It is still a strcmp/strncmp if/else chain with 11 sscanf calls and an atoi at :1051. The SILION_ClearUartFlags(); preamble in sillion_application.c grew from 37 copies to 45. The W5500 driver adds parallel Socket2 (UDP) and Socket3 (TCP server) APIs next to the duplicated Socket0/Socket1 sets: w5500.c grew from 1408 to 2004 lines. Socket3_Receive even issues W5500_S0_RECV (w5500.c:1896). New long functions: Identium_Command_ProcessJson about 340 lines (identium_command.c:110); WiFi_HandleCompletedCommand about 258 lines (wifi_service.c:319); ConfigureReader about 337 lines (sillion_application.c:202). Some new code is factored into helpers, e.g. HOST_SendTagLogRecords (host_interface.c:229).

**Remaining work.** Make host-command dispatch table-driven and use checked strtoul parsers. Split the Silion parser functions and use one helper for the overflow-reset block. Fold the Clear/txComplete preamble into TransactionWithTimeout. Replace the Socket0-3 copies with a socket-indexed W5500 API. Split the new JSON and Wi-Fi handlers. Add complexity and stack-usage limits in CI.

#### FW-57 — Host RX path can drop Ethernet bytes: W5500 drained without checking free space in the 256-byte host queue

`REGRESSED` · rev 2: NONE → now: LOW

**Rev 3 evidence.** The rev-2 caveat (1) has come true. Rev 3 adds a TCP server on port 5000 that feeds the legacy host ring. host_tcp_server.c:94-98 reads up to 128 bytes (HOST_TCP_SERVER_RX_CHUNK, :11) with W5500_Socket3_Receive, which advances RX_RD and issues RECV, so the bytes are consumed (w5500.c:1832-1897). It then pushes each byte with HOST_Transport_PushByte without checking free space. PushByte drops the byte and counts an overflow when the ring is full (host_transport.c:415-423). The Wi-Fi host socket does the same with up to 255 bytes (wifi_service.c:302-306). Exposure is narrow. The ring is now 1024 bytes (host_transport.h:12). HOST_Interface_Task calls Host_Tcp_Server_Task once (host_interface.c:872) and then drains the ring to empty, with no early exit (only gotos to host_command_done at :1067/:1178/:1190). So TCP alone cannot overflow it. Loss needs USB bytes (usbd_cdc_if.c:272-279, ISR, always re-armed) or serial bytes (serial_port.c:315) to fill more than 895 bytes during a blocking command. Overflow is reported as ERROR_HOST_RX_OVERFLOW (host_interface.c:886-897). The JSON Identium path still uses the synchronous Identium_Protocol_PushTcpByte (firmware_configuration.c:516-519) and is unaffected.

**Remaining work.** Bound the Socket3 read and the Wi-Fi forward by the ring's free space (add HOST_Transport_GetRxFree), so unread bytes stay in the W5500/ESP32 buffer and TCP back-pressure applies. Give USB CDC flow control: re-arm OUT only when there is room. Add a host test that floods TCP and USB at the same time.

#### R2-49 — Line-ending churn inflates the rev-2 diff about 3.4×; the tree mixes CRLF and LF

`REGRESSED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** No .gitattributes or .gitignore ships in the drop. The tree still mixes line endings: 102 CRLF and 58 LF own text files (c/h/py/s/ld, excluding ST middleware and micro-ecc). New rev-3 files are split as well, for example Bootloader/*.c and Common/sha256.c are LF while Services/system/*.c and Devices/esp32 are CRLF. Rev 2 to rev 3: 27 of the 45 modified .c/.h/.ld files changed line-ending style (26 LF->CRLF, e.g. Src/main.c, w5500.c, identium_protocol.c, configuration_service.c; 1 CRLF->LF, ethernet_service.h). Over those 45 files a plain diff gives 28,101 changed lines, and one that ignores CR and whitespace gives 6,559. That is about 4.3x inflation, up from 3.4x in rev 2. Indentation is still mixed: host_interface.c has 55 tab-indented lines against 2,185 space-indented (e.g. :321-324), and sillion_application.c has 82 against 1,681. The team does use git (Makefile:165 `git describe --tags --always --dirty`), but the shipped image was built from a dirty tree (build/f411-release/rfid_controller_f411-v0.10.1-1-gfcae054-dirty.tlfw).

**Remaining work.** Commit a .gitattributes (`* text=auto eol=lf`, with CRLF only where a tool needs it). Normalise once in a dedicated commit. Ship diffs or a git bundle against the reviewed tag, and build release images only from a clean, tagged tree.

### NOT FIXED (53)

#### FW-49 — Source hygiene and branding: stray pasted prompt in header, wrong file names and comments

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Firmware/Protocols/silion/silion_protocol.h and silion_protocol.c are byte-identical to rev 2 (diff -q against the R2 tree gives no output). Every rev 2 residual therefore remains. The file banners still say "impinj.h" / "impinj.c" (h:2, c:2). The third-party reference "PySilion / docs/PROTOCOL.md" is still cited (h:22, c:30). SILION_CMD_ASYNC_INVENTORY is still defined twice (h:77 and h:125). SINGLE_INVENTORY and SINGLE_TAG_INVENTORY are still both 0x21 (h:67, h:114). The wrong CRC example "FF 03 21 03 E8 00 03 2A" is still in a comment (c:1123). SILION_REGION_INDIA is still 0x08 (h:87), which is CE_LOW in the module table. The SetRegion whitelist is unchanged (Firmware/Application/Silion/sillion_application.c:1148-1155), so the real India code 0x04 is rejected. PRODUCT_RFID_DEFAULT_REGION is still 8U (product_config.h:45). Silion naming is unchanged: 994 SILION_* tokens across 18 own files, and the folder is still Application/Silion/sillion_*. The stray note "//i dont have these folders..." is still at sillion_application_port.h:7. There is also a new wrong comment in rev 3: f411_custom/board_config.h:19-20 says "No LSE (PC14/PC15 not connected)", but on this board those pins are Wiegand D0/D1 (net WGDO/WGD1).

**Remaining work.** 1. Fix the banners, and replace the PySilion citation with the Identium/Impinj protocol document. 2. Remove the duplicate defines. 3. Correct the CRC example comment to A5 E8. 4. Rebuild the region table from Appendix 2 (INDIA = 0x04, CE_LOW = 0x08), fix the SetRegion whitelist and set the default region to match. 5. Remove the stray comment, and correct the board_config.h PC14/PC15 note. 6. Optionally rename the module layer to vendor-neutral names.

#### FW-34 — The blocking superloop starves the other interfaces for up to 5 s

`NOT FIXED` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** The firmware is still a blocking cooperative superloop (Src/main.c:132-159) with the IWDG fed at :158. Watchdog_Refresh is still called inside every blocking wait: sillion_application_port.c:421 (SILION_DelayMs), :446 (WaitForResponse), :502 (WaitForTxComplete). So a long stall never trips the watchdog. The retry structure is the same as rev 2. SingleInventory runs 3 attempts (sillion_application.c:1959), each with TransactionWithTimeout of 5000+timeoutMs (:1979-1981). SynchronousInventory also runs 3 attempts (:2081, :2114-2116). Write/EPC transactions wait 10000 ms (:1552-1553, :1648-1649). The host timeout is still accepted up to 65535 ms (host_interface.c:1051-1055 atoi, :1163-1171 sscanf). Worst case is still about 212 s. Exposure is wider than in rev 2: the legacy ASCII parser is now reachable over TCP port 5000 (host_tcp_server.c:94-98, product_config.h:13) and over the Wi-Fi host socket (wifi_service.c:302-306), so a network client can trigger the stall. New blocking: a degraded RFID module is retried for about 1 s every 10 s (firmware_configuration.c:70-72, 239-268). Loop_Monitor only records maximum durations for GET_DIAG (loop_monitor.h:6-10, loop_monitor.c:55-83) and never acts on them. The only mitigation: tag-log writes are queued and described as non-blocking (tag_log.h:15-16).

**Remaining work.** Convert module transactions into non-blocking state machines, or move to an RTOS with one task per interface. Remove Watchdog_Refresh from the three wait loops and feed the IWDG once per loop pass after per-task heartbeat checks. Cap the SINGLE_POLL/SYNC_POLL timeouts (now 1-65535 ms) and the 10 s write waits, and drop the 3x inventory retries. Keep Ethernet, Wi-Fi and Identium heartbeats serviced during module operations. Make the degraded-module retry asynchronous.

#### FW-12 — Async inventory state can silently desync: AA48 start reply never checked; module-side stop/error never detected

`NOT FIXED` · rev 2: HIGH → now: HIGH

**Rev 3 evidence.** SILION_Application_StartInventory (sillion_application.c:2265-2299) is identical to rev 2 apart from line endings (diff --strip-trailing-cr). It sends 0xAA48, waits only for SILION_WaitForTxComplete(100U) (:2290), then sets appState = SILION_APP_INVENTORY (:2295) without reading the module's reply. Firmware/Protocols/silion/silion_protocol.c and .h are byte-identical to rev 2. So the AA48 ack, unsolicited AA49 and error-status frames are still not routed or checked. SILION_Application_Task only pops tags (:1468-1484). silionAsyncPacketCount and BadFrameCount are only ever reset (:2282-2283; sillion_application_port.c:96-97) and are never incremented or checked. Callers still trust appState: boot auto-start (firmware_configuration.c:214-222), the Identium resume_inventory reply 'reader is inventorying now' (identium_command.c:625-634), and the inventory recovery logic (identium_protocol.c:899-900). StopInventory is unchanged: if the module has already stopped or replies with an error, it returns 0 and appState stays INVENTORY (sillion_application.c:2312-2400). Every IDLE-only command then stays locked out, e.g. SingleInventory at :1951 and Sync at :2076. Only partial escapes exist: the vendor-tool module-bridge release forces IDLE (sillion_application.c:2453-2474). A new inventory-quiet reboot timer (identium_protocol.c:886-927) is off by default (configuration_storage.c:345) and cannot tell 'no tags in the field' from 'module stopped'.

**Remaining work.** In StartInventory, wait for the 'Moduletech'+AA48 reply, checking command 0xAA and status 0, before entering INVENTORY. Return the module status on failure. In SILION_ProcessByte, route 0xAA control and error frames (AA48 ack, AA49, non-zero status) to an event path. On an unsolicited stop or error, set IDLE, report a reader_exception/0x05 fault and optionally restart. Let StopInventory resync to IDLE when the module is already stopped. Add an async-frame liveness check that does not depend on tags being present.

#### FW-13 — Synchronous inventory reports zero tags and leaves them in the module buffer whenever more than 255 tags are found

`NOT FIXED` · rev 2: HIGH → now: HIGH

**Rev 3 evidence.** Paths are relative to 271026/_fw/src/STM32_Firmware. SILION_Application_SynchronousInventory has moved to Firmware/Application/Silion/sillion_application.c:2053. I diffed it (CRLF stripped) against rev 2 :1775-1990. The only change is that drained tags now go through SILION_Application_PublishTag (:2243). The count logic is identical: `uint8_t totalTags;` at :2056; `if(pSilion->expectedLength < 4U)` at :2147; `totalTags = pSilion->rxBuffer[8];` at :2152-2153; `if(totalTags == 0U) return 1U;` at :2159-2161, which returns before the 0x29 drain. Search Flags BIT4 (rxBuffer[6..7]) is still never parsed. The comment at :2133-2145 still says that a 0x0000 search-flag request guarantees a 1-byte count. Firmware/Protocols/silion/silion_protocol.c and .h are byte-identical to rev 2 (cmp). The function is reachable and linked: the SYNC_POLL handler at host_interface.c:1198 calls it, and build/f411-release/rfid_controller_f411.map:1862 links it. In rev 3 the ASCII parser is fed from USB (usbd_cdc_if.c:274), TCP 5000 (host_tcp_server.c:97), RS-232/485 (serial_port.c:315) and Wi-Fi (wifi_service.c:305), so the defect can be reached from more places than in rev 2.

**Remaining work.** Everything from the rev 2 list is still open. (1) Parse Search Flags at rxBuffer[6..7]. When BIT4 is set, require expectedLength >= 7 and read a 4-byte big-endian Tags Found from rxBuffer[8..11]. (2) Make totalTags uint32_t. (3) Never skip the 0x29 drain because of the 1-byte field. (4) Drain until the count is reached, status 0x0400 or an empty batch, with iteration and time bounds. (5) Correct the comment at :2133-2145. (6) Optionally return the tag count in OK,SYNC_POLL. (7) Test with more than 255 tags, or with a simulated BIT4 response.

#### FW-14 — Protocol API supports only one antenna and one per-antenna power setting, so a 4-port reader cannot be driven

`NOT FIXED` · rev 2: HIGH → now: HIGH

**Rev 3 evidence.** The protocol layer is unchanged. Firmware/Protocols/silion/silion_protocol.c and silion_protocol.h are byte-identical to rev 2 (cmp). SILION_SetInventoryAntenna (:658) still sends one TX/RX pair, and SILION_SetAntennaPower (:711) still sends one record. There is still no N-port enable, no N-record power setter and no dwell API. A grep for 'dwell' finds only the hard-coded "ant_max_dwell_time":0 at configuration_service.c:1070. Rev 3 states the single-port limit openly instead of fixing it. The comment at host_interface.c:483-488 says 'Multi-port switching is not implemented'. SET_ANTENNAS (:2615-2622) is accepted only when every listed port is the same (:514-517 parser). GET_CAPABILITIES reports ANTENNAS=1 (:2343). The config JSON emits a single tx_powers entry and "ants":[txAntenna] (configuration_service.c:1069, :1092). vendor_config.c:1168-1169 applies only the first tx_powers entry. product_config.h:46-47 still defaults to antenna 1. One real improvement: the single antenna is now persisted (host_interface.c:360-375) and restored at boot (firmware_configuration.c:459-464). Identium's spec IDM-INB1-FW-001 calls for 1, 4 or 8 antenna ports and per-antenna 0x91 power at boot (pdftotext -layout lines 81 and 399).

**Remaining work.** (1) Add a multi-port enable: 0x91 option 0x02 with N TX/RX pairs. (2) Add a multi-record power setter: 0x91 option 0x03. (3) Add a dwell setter: 0x95 option 0x02. (4) Replace the single txAntenna/rxAntenna/readPower/writePower with per-port arrays. Persist them in ReaderSettings and apply them in ConfigureReader. (5) Let SET_ANTENNAS accept a real port list and add per-port SET_POWER. (6) Make tx_powers, ants and ant_max_dwell_time real and writable in the config JSON. (7) Check port numbers against the count reported by 0x61. (8) Report the true antenna count in GET_CAPABILITIES.

#### FW-21 — WriteTagData retry sends a second blind write and returns failure without reading the reply

`NOT FIXED` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** SILION_Application_WriteTagData is now at Firmware/Application/Silion/sillion_application.c:1490. With CRLF stripped, its body is identical to rev 2 :1211-1312. The first write is sent at :1528. The loop at :1548 is `for(attempt = 0U; attempt < 1U; attempt++)`, so it runs once. SILION_ClearRxQueue at :1550 runs after the frame is sent. On failure, `if(attempt < 2U)` at :1561 is always true, so it waits 100 ms and sends a second blind write at :1568. `return 0U;` at :1585 then exits without ever reading that write's reply. The success tail at :1588-1590 is unreachable. ReadTagData (:1804) has the same structure: loop bound attempt < 1U at :1838 and the dead `if(attempt < 2U)` at :1882. The function is reachable from the WRITE_TAG handler (host_interface.c:2309-2318), which can now be reached from USB, TCP 5000, serial and Wi-Fi, and it is linked in build/f411-release/rfid_controller_f411.map:1844.

**Remaining work.** (1) Rewrite WriteTagData as a bounded retry loop of N = 2-3 attempts. Each attempt clears the RX queue and frame before sending, sends, calls TransactionWithTimeout, checks the status, and then breaks or retries. (2) Never send a frame whose reply is not consumed. (3) Delete the dead tail. (4) Make the loop bound and the delay condition consistent in both WriteTagData and ReadTagData. (5) Tell module status codes such as 0x0400 apart from timeouts, so a write that may already have landed is not re-applied blindly. (6) Bench test: a failed write followed immediately by another command must not produce CMD_MISMATCH.

#### FW-23 — USART_SendDataIT cannot report busy; SILION_SendFrame always reports success

`NOT FIXED` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Firmware/Drivers/uart/uart.h is byte-identical to rev 2 (cmp). The state values still collide: USART_BUSY_IN_TX 0U and USART_BUSY_IN_RX 0U at :82-83, USART_READY 1U at :84. uart.c differs from rev 2 only by #if defined(USART3) guards. USART_SendDataIT (:277-306) returns USART_BUSY_IN_TX both when it refuses (:280-283) and when it starts a transfer (:305). USART_ReceiveDataIT (:308) still compares against BUSY_IN_RX == 0. silion_protocol.c is byte-identical to rev 2. SILION_SendFrame ignores the USART_SendDataIT result (:261) and returns 1 unconditionally (:267). Callers still clear txComplete before sending: sillion_application.c:227, :252, :275, :304, :340 and sillion_application_port.c:93. Rev 3 mitigates two new paths only. The module bridge checks TxBusyState == USART_READY before sending (sillion_application.c:2482-2493, :2512). serial_port.c tracks its own g_txBusy and casts the driver result to (void) (:152-159).

**Remaining work.** (1) Give the UART states distinct values. (2) Make USART_SendDataIT return OK or BUSY, and fix the matching check in USART_ReceiveDataIT. (3) Propagate a refused send through SILION_SendFrame and make every SILION_* caller handle it. (4) Clear txComplete only after a send is accepted. (5) Do not rebuild the static frame buffers while TX is in flight. (6) Pass the buffer size to SILION_BuildCommandFrame and check it.

#### FW-31 — Weak resynchronisation after garbage or lost bytes

`NOT FIXED` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Firmware/Protocols/silion/silion_protocol.c and silion_protocol.h are byte-identical between rev 2 and rev 3 (cmp against _audit2/fw2/RFID_Controller_F429_second_revision). The same defects therefore sit at the same lines in rev 3. An oversize LEN byte is dropped and the parser returns to WAIT_HEADER without re-checking that byte as 0xFF (silion_protocol.c:3426-3436). The rxIndex overflow branch (:3438-3446) and the CRC-fail branch (:3737-3758) only set frameError and WAIT_HEADER, with no rescan of the bytes already consumed for an embedded 0xFF. Ignoring CR line endings, the rev 3 Firmware/Application/Silion/sillion_application_port.c differs from rev 2 in only four ways. The RX queue grew from 256 to 4096 bytes (:19), which makes overflow-induced byte loss less likely. A raw-mode flag was added for the module bridge (:48-53, :243-247, :353-400). A comment was edited and VCP_SendTag was renamed HOST_SendTag. There is still no inter-byte or stall timeout: SILION_Application_Task (Firmware/Application/Silion/sillion_application.c:1468-1483) only drains the queue and pops tags. The ORE/FE/NE/PE flags are still examined only inside the blocking wait helpers (sillion_application_port.c:453-454, :509-510), not in the async inventory path. No resync or discard counters exist (grep for resync/interbyte/lastRxTick in Firmware and Src finds nothing).

**Remaining work.** All of the rev 2 items are still open, in SILION_ProcessByte (silion_protocol.c:3372-3770). (a) Treat a rejected LEN byte of 0xFF as a new header. (b) After a CRC failure or rxIndex overflow, rescan the buffered bytes for the next 0xFF and re-feed from there. (c) Add an inter-byte or stall timeout of a few character times, checked from SILION_ProcessRxQueue or SILION_Application_Task. (d) Reset the parser on ORE/FE/NE/PE in the async path. (e) Add resync, discard and line-error counters to GET_DIAG.

#### FW-33 — VCP_SendString disables all interrupts while it copies the whole string

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** The function the finding names is gone: Firmware/Services/VCP was removed in rev 3 (271026/_fw/work/rev2_rev3_filelist.txt, 'Only in R2/Firmware/Services: VCP'), and grep finds no VCP_ symbol. The rev 2 card already flagged the same pattern in USB_CDC_Transport_Send, and that function is unchanged: the only diff from rev 2 is the new GetFreeSpace (Firmware/Services/USB/usb_cdc_transport.c:99-102). It still copies the whole message byte by byte between CriticalSection_Enter and Exit (:72-94), which is a global PRIMASK mask (Firmware/Common/critical_section.h:22-31). The new Serial_Port_Send (Firmware/Services/serial/serial_port.c:240-261) repeats the pattern: it copies the full message with a per-byte modulo under PRIMASK and starts TX inside the mask (:256-259). Both are reached for every host reply (host_transport.c:695-711), including GET_DIAG lines of up to 512 bytes (Firmware/Services/system/diagnostics.h:23) and module-bridge relays. The RFID and host USART IRQs are still both priority 5 (firmware_configuration.c:116-129). The OTG_FS IRQ is priority 0 (Firmware/USB_DEVICE/Target/usbd_conf.c:104). No DMA is used. On the 96 MHz F411 (Src/main.c:177-180), a 512-byte masked copy is roughly one 115200-baud character time. That figure is my estimate; nobody has measured it on hardware.

**Remaining work.** Make USB_CDC_Transport_Send and Serial_Port_Send lock-free SPSC rings: copy the bytes with interrupts enabled, publish the head with one store, and keep only the start-TX or in-flight bookkeeping in a short critical section, or use BASEPRI or per-IRQ masking. Give the RFID USART a higher NVIC priority than the host USART. Consider DMA plus IDLE-line detection for the RFID UART. Measure the masked window on the F411 board during continuous inventory with USB or serial as the tag or diag sink.

#### FW-41 — Unbounded sprintf of module-supplied lists in GetFrequency/GetRegions overflows stack buffers

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Paths are relative to 271026/_fw/src/STM32_Firmware. Running diff --strip-trailing-cr on the GetFrequency..'SETS' block of Firmware/Application/Silion/sillion_application.c shows no difference between rev 2 and rev 3. The file was only re-saved with CRLF line endings and changed in other places. Rev 3 code: SILION_Application_GetFrequency is at :944. It declares `char response[420]` at :946 and sets `count = pSilion->expectedLength / 4U;` at :971 with no clamp. It appends with plain sprintf at :988-992. SILION_Application_GetRegions is at :1007. It declares `char response[180]` at :1009 and loops `for(i = 0U; i < pSilion->expectedLength; i++)` at :1037, also with plain sprintf. LEN is still limited only by the frame size: silion_protocol.c:3426 rejects LEN+7 > 255, so LEN can be up to 248. Worst case is about 689 B written into 420 (about 441 B with 6-digit kHz values) and about 754 B written into 180. GetFrequency is linked in the F411 release image (build/f411-release/rfid_controller_f411.map:1809). Both functions are reached through the exact-match commands GET_FREQUENCY and GET_REGIONS (host_interface.c:1545/1550, 1570/1575). In rev 3 the ASCII parser is also fed from TCP port 5000 (host_tcp_server.c:97, product_config.h:13), Wi-Fi (wifi_service.c:305) and RS-232/485 (serial_port.c:315), so more hosts can issue the command. The overflow still needs a module reply that has a valid CRC and a long data field.

**Remaining work.** 1. Replace sprintf with snprintf(&response[pos], sizeof(response)-pos, ...) and check every return value. 2. Clamp count to min(LEN/4, (rxIndex-7)/4, 50 channels). Clamp the GetRegions loop to what 180 B can hold, or size the buffer for LEN=248. 3. Reserve room for the CRLF and NUL, and report truncation instead of sending a partial list. 4. Add a host test that feeds a CRC-valid reply with LEN=248.

#### FW-42 — Latent SPI driver defects

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Firmware/Drivers/spi/spi.c and spi.h are byte-identical to rev 2 (checked with cmp). All the cited defects remain: spi.h:70-72 still defines BUSY_IN_TX=0, BUSY_IN_RX=0, READY=1. spi.c:236 still has `(uint16_t*)pSPIHandle->pTxBuffer++;`. The double Len-- is still at spi.c:234-235. The OVR clear is still skipped while TX is busy (spi.c:280-292). The polled loops are still unbounded (spi.c:97/122). In the shipped F411 release image the linker discards the SPI IT and polled API: map:801 SPI_SendData, :805 SPI_SendDataIT and :823 SPI_IRQHandling are all under 'Discarded input sections'. USART_SendData and USART_ReceiveData are discarded too (map:834/836). So sub-items 1-4 and the UART polled loops (uart.c:192/233/245) are still dead code. The live paths are unchanged: W5500_SPI_Transfer spins without a bound on TXE/RXNE (w5500.c:69, 75) and WaitUntilNotBusy spins on BSY (w5500.c:105). The new GD25Q flash driver shares SPI1 and copies the same unbounded spins (gd25q.c:58, 79, 85); it is linked (map:2032). The IWDG starts before W5500 and flash init (firmware_configuration.c:301, versus :477/:497), so a wedged bus causes a reset loop rather than a permanent hang. GPIO writes are still an ODR read-modify-write (gpio.c:162-163), and in rev 3 this sub-item is a live race. RS-485 DE and RE_N (PB12/PB13, board_config.h:56-62) are now written from the USART2 TC interrupt: uart_dispatch.c:39-45 -> serial_port.c:317-323 -> Serial_Port_StartNextChunk -> SetDriverEnable (:139-141). At the same time, main-loop code does a read-modify-write on the same GPIOB ODR for relays PB2/PB10 (board_config.h:137-140, industrial_io.c:176) and the LED on PB8. If the interrupt lands between main's ODR read and write, main writes back DE=1 and RE_N=1. The RS-485 driver then stays on and the receiver stays off until the next serial TX. The window is a few instructions wide, so the race is rare, and the next TX clears it. The USART BRR carry loss is also unchanged (uart.c:637/653).

**Remaining work.** 1. Make GPIO_WriteToOutputPin write BSRR instead of doing an ODR read-modify-write. This fixes both the old CS item and the new race between the ISR and main on GPIOB. The lead should consider raising that race as a new finding. 2. Add bounded waits that return a timeout status in W5500_SPI_Transfer and WaitUntilNotBusy (w5500.c:69/75/105) and in the copies in gd25q.c (:58/79/85). 3. Delete the unused SPI IT and polled API, or fix it: distinct READY/BUSY constants, set the states in SPI_Init, advance the 16-bit pointer by 2, guard odd lengths, always clear OVR. 4. Fix the BRR rounding carry (see FW-53).

#### FW-45 — RX parser keeps overwriting the frame buffer after a frame is marked ready

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Firmware/Protocols/silion/silion_protocol.c is byte-identical to rev 2 (cmp). In SILION_RX_WAIT_HEADER (:3391-3409), a new 0xFF still restarts the frame (rxBuffer[0], rxIndex=1, frameError=0) without checking or clearing frameReady. frameReady is set at :3719/:3734 and cleared only by SILION_ClearFrame (:3901-3930). sillion_application_port.c changed in 66 lines. SILION_ProcessRxQueue (:236-264) still drains `while(rxHead != rxTail)` into SILION_ProcessByte and does not stop at frameReady. Its only new logic is a rawMode early return for the module bridge (:243-246). SILION_WaitForResponse (:438-475) still checks SILION_IsFrameReady only after the full drain. The RX queue grew from 256 to 4096 bytes (:19), so a single drain can now consume up to 16 times more bytes after a frame is marked ready. The application still checks only the command byte (sillion_application.c:106). A following frame with the same command byte in the same drain therefore still overwrites the status and payload of the frame being returned.

**Remaining work.** 1. Stop draining once SILION_IsFrameReady() is 1 and leave the rest in the ring until SILION_ClearFrame. Alternatively, make ProcessByte hold bytes while frameReady is set, or copy each CRC-validated frame into a separate response slot that the parser never writes. 2. Match each reply to the outstanding request in the protocol layer and route 0xAA async frames to a separate path. 3. Add a test with two back-to-back frames that have the same command byte.

#### FW-46 — SILION_ReadTagData sends from stack buffers with interrupt-driven TX (use after return)

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** silion_protocol.c is byte-identical to rev 2. SILION_ReadTagData (:1394) still builds into a stack `uint8_t frame[16]` (:1401-1402) and passes it to the interrupt-driven SILION_SendFrame. It is still declared at silion_protocol.h:439 and still has no caller (grep). The F411 release link discards it (map:892, 'Discarded input sections'), so the use-after-return is not in the shipped image; the source is unchanged, so it is not fixed. The related live defect is also unchanged. SILION_SendFrame (silion_protocol.c:244-269) ignores the result of USART_SendDataIT and always returns 1. USART_SendDataIT returns USART_BUSY_IN_TX both when TX is busy (uart.c:282) and on success (uart.c:305), so no caller can detect a dropped frame. Rev 3's new module-bridge path does this correctly: it copies into a static bridgeTxBuffer (sillion_application.c:2420, 2518-2521) and checks TxBusyState first (:2512-2515). That pattern was not applied to ReadTagData or to SendFrame.

**Remaining work.** 1. Delete SILION_ReadTagData and its declaration at silion_protocol.h:439, or make frame[] static. 2. Make USART_SendDataIT return different values for 'accepted' and 'busy', and have SILION_SendFrame pass that result on, or queue the frame, instead of always returning 1. 3. Reject frameLength==0 in SILION_SendFrame.

#### FW-47 — The select-length byte overflows for EPCs over 31 bytes; EPC length limits inconsistent

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** silion_protocol.c is byte-identical to rev 2. ReadTagDataByEPC still accepts 1..62 bytes (:1529) and writes `(uint8_t)(epcLengthBytes * 8U)` (:1604-1605). The same 8-bit truncation is still at :1757, :1874, :1975 and :2071. No path sets Option BIT5 or sends a 2-byte select length. The READ_TAG handler is identical to rev 2 (diff --strip-trailing-cr of the READ_TAG..WRITE_EPC block shows no difference). It still uses `%126[^,]` and allows up to 124 hex characters (host_interface.c:1841-1870), then calls SILION_Application_ReadTagData (:1922) -> SILION_ReadTagDataByEPC (sillion_application.c:1847, linked at map:2556). A 32-62-byte EPC therefore still produces a select length of 0 or a wrapped value, which can match any tag. WRITE_EPC, LOCK_TAG, KILL_TAG and WRITE_TAG still cap the EPC at %62[^,], i.e. 31 bytes (host_interface.c:1991, 2087, 2176, 2263). More hosts can now reach this: the ASCII parser also accepts commands from TCP port 5000, Wi-Fi and RS-485 (host_tcp_server.c:97, wifi_service.c:305, serial_port.c:315). No Identium-protocol path calls the tag-access builders: grep shows they are called only from host_interface.c.

**Remaining work.** 1. Add one shared EPC-length validator (even length, 2..62 bytes) and use it in ReadTagDataByEPC, WriteTagDataByEPC, WriteTagEPC (old and new EPC), KillTag and LockTag. 2. For selects over 255 bits, set Option BIT5 and send a 2-byte select length. Until then, reject EPCs over 31 bytes. 3. Limit READ_TAG to 62 hex characters, the same as the other handlers.

#### FW-48 — Metadata and field semantics mislabelled

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** silion_protocol.c is byte-identical to rev 2. Metadata bit 5 is still labelled RFU (:2383, :2463). PHASE2 is still stored as tagCrc (:2613-2616, :3332). ParseTagBuffer still calls it 'RFU - 2 bytes' (:3157) and still bounds-checks against rxIndex (:3063). Single inventory still hard-codes pcWord=0, rssi=0 and antenna=0 (:1242-1249). epcLengthBits is still inconsistent (:1237 versus :2619). SILION_SetProtocolMiller and PARAM_MILLER (c:813/823, h:143) now have callers: the new Gen2 profile feature uses them (SetGen2Profile at sillion_application.c:1282, GetGen2 'PROFILE' at :1372-1374), and the function is now linked (map:2532). It now works as the RF-mode/profile setter, but under the wrong name. The bad metadata has spread further: SILION_Application_PublishTag (sillion_application.c:27-32) sends every tag to Identium_Protocol_ReportTag, HOST_SendTag and now TagLog_Append. That includes SINGLE_POLL tags carrying the fake ANT 0 / RSSI 0 (:1988-1996). Those values end up in the Identium binary header (identium_protocol.c:497/499), in the JSON 'at' and 'ri' fields (:578-582), and in the flash tag log (tag_log.c:167-168). tagCrc and epcLengthBits have no consumers outside silion_protocol.c (grep), so those two problems are cosmetic.

**Remaining work.** 1. Rename the PHASE fields (bit 5 = PHASE; the trailing 2 bytes are PHASE2, not tagCrc) or drop them. 2. Rename SetProtocolMiller / PARAM_MILLER to SetRfMode / PARAM_RFMODE. 3. Make epcLengthBits count EPC bits only. 4. For 0x21 single inventory, request metadata with Option BIT4 or mark ANT/RSSI invalid before they reach the Identium protocol and the flash log. 5. In ParseTagBuffer, bound against 5+LEN and check that the metadata flags equal 0x00BF. 6. Cross-check pcWord against the EPC length and handle XPC.

#### FW-50 — Response grammar is inconsistent and polluted by unsolicited DEBUG lines

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** The DEBUG lines are still unconditional in SILION_Application_TransactionWithTimeout, with no #if guard. The DEBUG strings are at sillion_application.c:58 (TX_TIMEOUT), :77 (RX_TIMEOUT), :88 (FRAME_ERROR), :94 (RX_UART_ERROR), :112 (CMD_MISMATCH) and :133 (STATUS). HOST_SendString calls HOST_Transport_SendString(HOST_Transport_GetActive(), ...) (host_interface.c:630-636). In rev 3 that function also sends to TCP port 5000 clients (host_transport.c:676-679 -> Host_Tcp_Server_Send), to Wi-Fi (:680-683) and to RS-232/485 (:701-704), so the DEBUG lines can now reach network and serial hosts. The new Identium config and vendor paths also run through the same transaction function (configuration_service.c:2253 SetSession, vendor_config.c:1262-1263 SetRegion/SetPower). Whether a DEBUG line can land inside a binary vendor session on the same USB or serial link is unverified; I did not trace the mux ownership far enough. The response grammar is unchanged. GET_STATUS replies only STATUS,INVENTORY or STATUS,IDLE, with no OK (host_interface.c:1221-1238). Errors are still bare, e.g. ERROR,INVALID_COMMAND (:2640). READ_DATA is still sent as a header, then one HOST_SendString per byte, then CRLF (sillion_application.c:1900-1923). One sub-item is fixed: W5500 SEND now waits for SEND_OK or TIMEOUT on socket 0 (w5500.c:940-985) and socket 1 (:1397-1440). Socket 3 is gated the same way through SendIdle in GetTxFreeSize (:1902-1923), and the TCP host server buffers replies in a 2 KB ring (host_tcp_server.c:10, 45-63).

**Remaining work.** 1. Use one grammar: every ASCII command ends with OK,<CMD> or ERROR,<CMD>,<code>. This covers GET_STATUS, the GET_* commands, the SINGLE_POLL/READ_TAG data+OK replies, and busy or invalid-argument errors. 2. Put the DEBUG,* lines behind a compile-time or runtime flag, or move them to a diagnostic channel, and return error codes to the caller instead. 3. Build READ_DATA in one buffer and send it in one call. 4. Document the ASCII grammar, which now runs on USB, RS-232/485, TCP 5000 and Wi-Fi.

#### FW-51 — Lax numeric/hex parsing and weak range checks in command handlers

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** The parsing helpers are unchanged. HOST_ParseHexByte still uses sscanf "%x" (host_interface.c:537), HOST_ParseUint16 still uses sscanf "%u" (:559), and HOST_ParseHexBytes still uses sscanf "%2x" (:609-613). None of them checks an end pointer. SINGLE_POLL is still a prefix match (strncmp with length 11 at :1034-1038). It does not reject trailing characters and parses the timeout with atoi (:1048-1051), so "SINGLE_POLLING" and "SINGLE_POLL,100x" are both accepted. SET_ANTENNA still accepts tx and rx up to 255 (:1660-1670). READ_TAG still checks bank <= 255U and the always-true address <= 0xFFFFFFFFUL (:1850-1852), while WRITE_TAG limits bank to 3 (:2273). LOCK_TAG and KILL_TAG still parse %lx with always-false > 0xFFFFFFFFUL checks (:2087-2096, :2176-2183), and WRITE_TAG keeps its always-false checks (:2274-2275). The new rev 3 handlers are mixed. TAGLOG_READ (:245-254), SET_ANTENNAS (:508-511) and FW_DATA/FW_BEGIN (:776, :800) use strtoul with end-pointer checks. SET_GPO (:2397), SET_HTTP (:2509), SET_BUZZER (:2574) and HOST_ParseGen2 (:423) use sscanf without trailing-junk checks. The parser can now be reached over TCP 5000, Wi-Fi and RS-485 (host_tcp_server.c:97, wifi_service.c:305, serial_port.c:315).

**Remaining work.** 1. Replace the sscanf helpers with one strict strtoul/end-pointer parser and use it in all handlers, including the new SET_GPO, SET_HTTP, SET_BUZZER and SET_GEN2. 2. Match command names exactly up to the ',' or end of line (SINGLE_POLL), and drop atoi. 3. Apply explicit ranges: bank 0-3 in READ_TAG; antenna 1..N, where N is the port count the module reports. 4. Remove the dead 0xFFFFFFFF comparisons and limit password digit counts. 5. Add host tests for malformed input.

#### FW-52 — UART error handling: FE/NE/PE never detected, ORE drops the byte

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** The only change to uart.c in rev 3 is #if defined(USART3) guards (diff --strip-trailing-cr shows 4 changed lines); uart.h is byte-identical. USART_IRQHandling (uart.c:367) still tests only ORE, TXE, TC and RXNE, and never reads FE, NE or PE. The ORE branch (:391-410) is gated on CR3.EIE. EIE is set by USART_ReceiveByteIT (:349-350), so the branch does fire, but it reads SR and DR (:396-397), which throws away the byte in DR, and then raises USART_EVENT_ORE. The RXNE branch (:531-560) passes every byte up as RX_BYTE whatever the FE/NE/PE state. USART_EVENT_FE, NE and PE (uart.h:118-120) are still never raised, so rxFE, rxNE and rxPE (sillion_application_port.c:66-68, 189-205) can never be set. Rev 3 adds an ORE counter for the host port (serial_port.c:325-328). Bytes that arrive with noise or framing errors on the new RS-485 host link still go straight into the host parser (serial_port.c:306-316).

**Remaining work.** 1. Read SR once on entry. If PE, FE, NE or ORE is set, read DR once, count the error by type, raise the matching event, and drop or flag the byte instead of passing it up as RX_BYTE. 2. Handle ORE in the RXNE path instead of gating it on EIE. 3. Remove the misleading EIE writes and comments at uart.c:323 and :349-350. 4. Report the counters through rxFE, rxNE and rxPE and the serial-port diagnostics. 5. Test with an injected baud mismatch and RS-485 line noise.

#### FW-53 — Latent bugs in the UART driver (currently unused paths)

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** uart.c only gained USART3 guards and uart.h is byte-identical. (a) USART_SendDataIT (uart.c:277-306) still has no Len==0 check, and the ISR still does TxLen-- (:442). SILION_SendFrame still has no zero-length check (silion_protocol.c:244-269). The new rev 3 callers do guard against zero (serial_port.c:133-143; BridgeSend at sillion_application.c:2504-2506), so this stays latent. (b) The OVER8 encoding `((fraction & 0x7) << 1)` is unchanged (:655). (c) The rounding carry is still lost by `& 0xF` (:637) and `& 0x7` (:653). Both (b) and (c) are latent: both F411 UARTs are fixed at 115200 with OVER16 (Firmware/BSP/f411_custom/board.c:235/240, 250/255). With PCLK2 at 96 MHz and PCLK1 at 48 MHz (Src/main.c:177-181, 205-206) the formula gives BRR 52.0625 and 26.0625, with no carry (I computed this by hand). (d) Clock and reset still cover only USART1-3 (uart.c:9-14, 154-159), and only USART1 is treated as PCLK2 (:613-620). This sub-item is OBSOLETE for this target: the F411 board uses only USART1 (RFID) and USART2 (host) (uart_dispatch.c:37-45; LEAD_FACTS pin map PA9/PA10 and PA2/PA3), so USART6 is not needed. (e) USART_SendData and USART_ReceiveData still have unbounded loops (:192/233/245), but both are discarded from the F411 image (map:834/836). (f) The USART_Handle_t fields are still not volatile (uart.h:36-46). TxBusyState is now also polled from main by BridgeIsTxReady (sillion_application.c:2492).

**Remaining work.** 1. Reject Len==0 in USART_SendDataIT, or guard TxLen==0 in the ISR, and reject frameLength==0 in SILION_SendFrame. 2. Fix the OVER8 encoding and the fraction carry (or use the integer formula (PCLK+baud/2)/baud) before any baud-rate or oversampling option is exposed. 3. Mark the ISR-shared handle fields volatile. 4. Delete the unused polled API or add timeouts to it. USART6/UART4/UART5 support is no longer needed for the F411 board.

#### FW-54 — The GPIO driver uses non-atomic ODR read-modify-write and orders EXTI setup unsafely

`NOT FIXED` · rev 2: LOW → now: MEDIUM

**Rev 3 evidence.** Paths are relative to 271026/_fw/src/STM32_Firmware. Compared with rev 2, the only change to Firmware/Drivers/gpio/gpio.c is the addition of #if defined(GPIOF)/(GPIOG) guards. Every defect from the card is still there. (1) Output writes are a non-atomic read-modify-write of ODR: gpio.c:162-163 (WriteToOutputPin uses ODR |= and &= ~) and :173 (Toggle uses ODR ^=). I disassembled GPIO_WriteToOutputPin in the shipped build/f411-release ELF: 0x0800B916-0x0800B91E is ldr [r0,#0x14], then orreq/bicne, then str [r0,#0x14]. The application code never uses BSRR; only the bootloader does (Bootloader/boot_staging.c:41-56). (2) EXTI setup order is still wrong. IMR is set at :86, but EXTICR is not programmed until :110-111. The `else return;` at :108 leaves the line unmasked, and PR is never cleared. No rev 3 code uses GPIO_DRV_MODE_IT, so this part is latent. (3) Read, Write and Toggle still do no pin or port checks (:150-174). (4) DeInit (:133-148) covers ports A-H. The F411 only has A-E and H, so this sub-item is moot on the target. NEW EXPOSURE: the RS-485 DE and RE_N pins (PB12/PB13, f411_custom/board_config.h:56-62) are now written in interrupt context. The path is USART2_IRQHandler (uart_dispatch.c:39-44), then Serial_Port_UartEvent on TX_CMPLT (serial_port.c:315-321), then StartNextChunk, then SetDriverEnable(0) (:138-143, with the writes at :74-87). Meanwhile the main loop does unmasked read-modify-writes on other GPIOB bits: relays PB2/PB10 through Industrial_IO_SetOutput (industrial_io.c:176), and ESP32 SCL PB6 and EN PB9 (esp32_link.c:405, 410, 426). Suppose the USART2 TC interrupt arrives between that ldr and str. The main loop then writes back the old PB12/PB13 = 1. The RS-485 driver stays on and the receiver stays off until the reader transmits again. In a polled exchange the reader never hears the next request, so it never transmits and the port stays locked. Rev 2 said this had to be fixed before any output timing moved into ISRs. Rev 3 has now moved one there, which is why I raised the severity from low to medium.

**Remaining work.** 1. Rewrite GPIO_WriteToOutputPin to write BSRR: set with (1U<<pin), reset with (1U<<(pin+16)). This one change removes the race for every caller. Make Toggle atomic too: read ODR once, then write BSRR. Until then, at minimum wrap the main-loop GPIOB writes in CriticalSection_Enter/Exit. 2. Fix the EXTI order: resolve the port code first and return before touching EXTI if it is unsupported. Then program EXTICR, then RTSR/FTSR, then clear PR, and only then set IMR. 3. Add pin <= 15 and non-NULL checks (or asserts) to Read, Write and Toggle. 4. On F411 hardware, run RS-485 traffic while the relays and the ESP32 reset are toggled, and confirm DE never sticks.

#### FW-55 — ring_buffer.c/.h are empty; three ad-hoc queues are hand-rolled

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Paths are relative to 271026/_fw/src/STM32_Firmware. (1) The shared ring module is still an empty stub. Firmware/Common/ring_buffer.c:1-8 is only a header comment and ring_buffer.h:1-13 is only an include guard. A grep finds nothing that includes ring_buffer.h. (2) The stale overflow-counter bug is unchanged. SILION_ClearRxQueue (Application/Silion/sillion_application_port.c:328-348) resets rxOverflow at :343 but not rxOverflowReported, which is only reset at init (:99). SILION_ProcessRxQueue still compares the two at :272-280. ClearRxQueue is called from sillion_application.c:241, 1550, 1840, 1961, 2324 and 2469, and now also from SILION_SetRawMode (:355-361) on every module-bridge entry and exit. In raw mode SILION_ProcessRxQueue returns at :243-246, before the overflow check, so an overflow during a bridge session is cleared without being reported. After that, the next pass raises a spurious ERROR_BUFFER_OVERFLOW. (3) The number of hand-rolled queues went up. The VCP TX queue went away with Services/VCP, but there are now eight, each with its own conventions: - Silion RX: port.c:40-45, :126-144 - host RX: host_transport.c:36-37 - USB CDC TX: usb_cdc_transport.c:14-15 - Silion async tag queue: silion_protocol.c:121-124 - serial TX: serial_port.c:32-33 (modulo wrap at :243) - host TCP TX: host_tcp_server.c:14-16 - HTTP upload queue: http_upload.c:58-59 - Identium tag queue: identium_protocol.c:71-72. hostcheck/ has no ring or queue unit test. The offline buffer (Services/storage/tag_log.c) is a separate on-flash design and is not built on a shared ring.

**Remaining work.** 1. Implement ring_buffer.c/.h as one SPSC ring: power-of-two size, free-running indices, single-store publish, an overflow counter and a contiguous-span API. Add a host unit test for it in hostcheck/. 2. Move the eight ad-hoc queues onto it where it fits, keeping the critical sections the multi-producer queues need. 3. In SILION_ClearRxQueue, report any pending overflow first, or reset rxOverflowReported together with rxOverflow. Also report overflows that happen in raw/bridge mode before SetRawMode clears them.

#### FW-59 — Clang -Wextra/-Wconversion and analyzer results (own code only)

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** I re-ran the rev 2 method on both trees with identical scope. Method: Apple clang 21, --target=thumbv7em-none-eabi, -fsyntax-only, the same 11 warning flags, and stub libc headers in the scratchpad. Scope: Firmware/ minus Middlewares and USB_DEVICE, plus Src/ minus system/sysmem/syscalls/hal_msp. Because USB_DEVICE is excluded, my totals differ from the 89 in the rev 2 audit. Unique own-code warnings went from 75 on rev 2 to 95 on rev 3 with F429 defines, and 94 with -DSTM32F411xE -DTARGET_BOARD=2. F411 breakdown: - 36 sign-conversion - 23 implicit-int-conversion - 11 comma - 7 missing-variable-declarations - 7 cast-align - 4 missing-prototypes - 3 unreachable-code - 2 unused-parameter - 1 unused-value. Clang also reports 5 errors, 'non-ASM statement in naked function', at Src/stm32f4xx_it.c:88, 102, 117, 132 and 147. These are the while(1) loops after FAULT_RECORD_ENTER. GCC accepts them and they are unreachable at runtime. Specific items: - spi.c is byte-identical to rev 2, so spi.c:236 still has `(uint16_t*)pSPIHandle->pTxBuffer++;`. It is latent on the F411: SPI_IRQHandling sits in the map's discarded sections (map:823, inside lines 106-1331), and the SPI runs in 8-bit DFF (f411_custom/board.c:267). - Comma-operator if-bodies remain at gpio.c:136-147, spi.c:79-81 and uart.c:154-157. - timeoutMs is still unused (sillion_application.c:1805). - Globals are still unscoped: firmware_configuration.c:61-66, sillion_application.c:19, sillion_application_port.c:73, plus new fault_record.c:27. - Unreachable code at sillion_application.c:1588, industrial_io.c:255 and fault_record.c:180. Build: CFLAGS has only -Wall (Makefile:98-99). -Werror is only an optional EXTRA_CFLAGS (:27). -Wextra -Werror is applied only to the host tests (:217, :232). There is no cppcheck or MISRA step.

**Remaining work.** 1. Fix spi.c:236 (pTxBuffer += 2) even though it is latent on the F411. 2. Brace the 11-14 comma-operator bodies. 3. Use or remove timeoutMs. 4. Make the shared globals static or declare them in headers, and add the missing prototypes (HOST_SendTag, the USART2/I2C1 IRQ handlers). 5. Remove the unreachable code. 6. Fix the sign-conversion masks. 7. Make the naked fault handlers asm-only. 8. Put -Wextra -Wconversion -Werror on Firmware/ (excluding Middlewares) in the Makefile and .cproject, and add a cppcheck/MISRA step.

#### FW-61 — Protocol identity: the code implements the Silion/Moduletech serial protocol

`NOT FIXED` · rev 2: INFO → now: INFO

**Rev 3 evidence.** Paths are relative to 271026/_fw/src/STM32_Firmware. Firmware/Protocols/silion/silion_protocol.c and .h are byte-identical to rev 2 (diff -q), so the wire format is still correct and still undocumented. Banners still name the wrong files: 'impinj.c' at silion_protocol.c:2 and 'impinj.h' at .h:2. The only reference is still 'PySilion docs/PROTOCOL.md' (silion_protocol.c:29-30, .h:19-22), and the header still says 'STM32F429 USART' (.h:24-25). Nothing cites the Impinj RFID Series Protocol Document. There are no unit tests: hostcheck/*.c only builds SILION_Tag_t fixtures (identium_protocol_host_test.c:92-94, tag_log_host_test.c:160) and never calls SILION_CalculateCRC, BuildCommandFrame or ValidateFrame. Tools/serial_link_test.py:90-99 re-implements the CRC in Python for a live-device script. That does not test the C code and uses none of the Appendix 6 vectors. Region naming is unchanged: SILION_REGION_INDIA 0x08 (silion_protocol.h:87) and PRODUCT_RFID_DEFAULT_REGION 8U (Config/product_config.h:45).

**Remaining work.** 1. Add the protocol-identity note (Silion/Moduletech, identical on the wire to the EX10 protocol document, citing §3.1-3.3 and Appendices 2, 4 and 6). 2. Fix the impinj.c/impinj.h banners and the F429 wording. 3. Add host tests in hostcheck/ for SILION_CalculateCRC, BuildCommandFrame and ValidateFrame using the Appendix 6 vectors and the AA48 Moduletech frame. 4. Fix the region constant names and settle the India code per module. 5. Verify on an E-series module.

#### FW-62 — Host command set as implemented (reference map)

`NOT FIXED` · rev 2: INFO → now: LOW

**Rev 3 evidence.** Paths are relative to 271026/_fw/src/STM32_Firmware. The legacy ASCII parser is still compiled and runs every loop (Src/main.c:145-146, comment 'Legacy ASCII parser remains on native USB during migration'). Its command set has grown: - FW_BEGIN/FW_DATA/FW_END/FW_APPLY/FW_ABORT/FW_STATUS (host_interface.c:774-842, dispatched at :1327-1332) - TAGLOG_STATUS/READ/CLEAR (:1291-1319), where TAGLOG_CLEAR calls TagLog_Clear - REBOOT (:1276) - GET_DIAG/CLEAR_DIAG (:1247-1259) - Wi-Fi settings (:129-184) - DEBUG_FAULT, DEBUG builds only (:1334-1344). It is reachable over USB CDC, over RS-232/RS-485 (serial_port.c:313), and again over TCP 5000: host_tcp_server.c:24 listens on PRODUCT_ETHERNET_TCP_PORT 5000U (product_config.h:13) and pushes bytes at :97. A grep of host_interface.c, host_tcp_server.c and host_transport.c finds no authentication. A new COM-route mux sends FF/EE frames and the IOGET/IPSET/... board commands to the bridge (host_transport.c:55-70, :90-93); everything else reaches the ASCII parser. Over-long lines are still truncated and then executed: the buffer is 320 B (:311), characters past 319 are silently dropped (:2676-2683), and the command runs at CR. The SET_POWER parser (:1597-1636) has no range check; the only limit is SILION_Application_SetPower rejecting values above 5000 (sillion_application.c:1071). There is no spec document and no ASCII regression test in hostcheck/. Tools/fw_update.py and serial_link_test.py are live-device scripts. I raised the severity to low because of the truncation defect and the larger unauthenticated network surface; the security side belongs to the security findings.

**Remaining work.** 1. Decide whether to retire or freeze the ASCII command set, and write a versioned spec covering framing, the 320-character limit, every command and response, and the errors. 2. Add host regression tests for the parser. 3. Reject over-long lines instead of truncating them. 4. Range-check SET_POWER in the parser. 5. Disable TCP 5000 in production builds, or gate the destructive and privileged verbs (TAGLOG_CLEAR, FW_*, REBOOT, Wi-Fi credentials). 6. Document the port map (5000, 8080, 9000, UDP 15000).

#### REQ-05 — Identium's own mechanical and harness reference documents contradict each other

`NOT FIXED` · rev 2: INFO → now: INFO

**Rev 3 evidence.** The rev 3 delivery (271026/) contains STM32_Firmware.zip, Gerber_PCB1_2026-10-13.zip, PickAndPlace_PCB1_2026_10_13.xlsx and DESIGN JUSTIFICATION.docx. Per LEAD_FACTS, the non-firmware files are byte-identical to the 231026 package. No ICD, corrected INB-1 specification or mechanical/harness drawing was supplied. The text of DESIGN JUSTIFICATION.docx has no hits for J50, PSAM, M12, gland, enclosure, mounting, outline, ICD, casing or standoff. A grep of the firmware (Firmware/, Src/, Boards/) for J50, PSAM, CHSGND, M12, gland, enclosure, standoff, INB-1 and 'interface control' finds nothing. Config/product_config.h:12 and :22 still carry the F429 defaults comment and reader name 'RFID-F429-DEV'. Identium's own conflicting references have not changed since before the rev 2 audit: PCB/Identium_INB1_Design_Specification.pdf/.docx is dated 17 May 2026, and the reader casing-design and redesign-brief .md files are dated July 2026. A firmware drop cannot close this.

**Remaining work.** Everything from rev 2 is still open: 1. Issue one controlled ICD per product (PCB outline, hole pattern, height zones, connector edge, LED positions, cable/bulkhead choice). 2. Fix the redesign brief §8 outline-versus-placement conflict and the §7 M12-A pin count. 3. Correct INB-1 so J50 reads CHSGND x2 + M1/M2, not PSAM, and decide on chassis ground and PoE pass-through. 4. Review the next schematic, PCB and harness against the ICD.

#### REQ-06 — Module-compatibility envelope: only a single-port, Silion-protocol module drawing about 2.5 W or less fits CN1

`NOT FIXED` · rev 2: INFO → now: INFO

**Rev 3 evidence.** Base path: 271026/_fw/src/STM32_Firmware/. Hardware: the 271026 Gerber zip and pick-and-place file are byte-identical to 231026 (checked with cmp). Netlist (net.py comp CN1 / comp F8): CN1 5 V is on CN1.1 and CN1.2, behind F8 1206L150 (1.5 A PTC). Module EN (CN1.5) is tied to 3V3 through R67 10k (net.py comp R67). Rev 3 documents that there is deliberately no enable GPIO (Firmware/BSP/f411_custom/board_config.h:25-33). That closes rev-2 sub-item 4, and the F411 build target now exists (Makefile:52-62). The firmware envelope is otherwise unchanged. (1) There is still only a Silion driver: Firmware/Protocols holds silion/ and an empty host/. A grep for Impinj/EX10/E710 finds only stale file-header comments (silion_protocol.h:2,19; silion_protocol.c:2). (2) Single antenna: PRODUCT_RFID_TX/RX_ANTENNA = 1 (Firmware/Config/product_config.h:46-47), and CAPABILITIES reports ANTENNAS=1 (Firmware/Application/Host/host_interface.c:2343). (3) RF power is still not tied to the CN1 budget. SILION_Application_SetPower caps at 5000 (Firmware/Application/Silion/sillion_application.c:1071). The new vendor-JSON path caps rp/wp at 3300 (Firmware/Services/configuration/vendor_config.c:1211,1220). (4) No supported-module matrix: the drop holds no documentation except Bootloader/micro-ecc README/LICENSE, and DESIGN JUSTIFICATION.docx has none.

**Remaining work.** Write the supported-module matrix (module, connector, pinout source, current at max dBm, protocol, driver status). Then either fix the CN1/F8 power path or document the reduced-power limit. Clamp power to the per-module/connector limit on every path: host SET_POWER (5000), vendor JSON rp/wp (3300) and stored settings. Add an E710 driver or a module-type abstraction for the planned alternative module, including region-code mapping. Note in the matrix that EN is hard-tied, so the firmware cannot power-cycle a hung module.

#### PRG-02 — Keeping the vendor SDK and RFID Manager compatible keeps Silion/Moduletech identity on the wire and a hard-coded foreign update server

`NOT FIXED` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** The drop contains no written decision on (a) vendor-tool compatibility versus (b) an Identium re-release, and no SDK, RFID Manager or updater re-release. Tools/ holds only Python test scripts plus fw_sign.py/fw_update.py for the board's own image. Rev 3 takes option (a) in code without that decision. Port 8080 emulates the vendor 'ARM7 mainboard' (Firmware/Services/configuration/module_bridge.c:18-41) and relays the 'Moduletech' frames (module_bridge.c:66-70). UDP 15000 discovery replies with board type 1 = Hc32f46x (Firmware/Services/configuration/discovery_service.c:13,25,53), so vendor identity goes on the wire. USB still uses the ST VID 1155 (0x0483), PID 22336 (0x5740) and the strings 'STMicroelectronics'/'STM32 Virtual ComPort' (Firmware/USB_DEVICE/App/usbd_desc.c:65-69). The default reader_name is still 'RFID-F429-DEV' (Firmware/Config/product_config.h:22, reader_settings.c:273). The only Identium identity is the HTTP User-Agent 'IdentiumReader' (Firmware/Services/ethernet/http_upload.c:691). The host-side items (FTP updater 43.138.155.217, RfidDemo credentials, private.pem in the vendor zips) are untouched because no tooling was delivered. New in this drop: Identium's own production firmware-signing private key ships at keys/fw_signing_key.pem, which repeats the private-key-in-deliverable pattern this finding warned about.

**Remaining work.** Record the (a)/(b) decision in writing. If (a), document the vendor-identity exposure (Hc32f46x board type, Moduletech frames, vendor update channel) and accept it formally. Under (b), replace the FTP updater and embedded credentials with signed HTTPS updates from Identium infrastructure, remove private.pem from the bundles and rotate those keys. Either way: an Identium VID/PID and USB strings, a production reader name, and no private keys in any deliverable (rotate fw_signing_key.pem).

#### PRG-03 — Compliance, certification, NRE and environmental qualification are not addressed

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** 271026 contains the firmware zip plus the same Gerber, pick-and-place and design-justification files as 231026, with no NRE/payback table, compliance matrix, ESD/EFT/surge/RE/CE/thermal acceptance criteria or radio type-approval plan. A grep of rev 3 own code, Tools/ and the Makefile for EMC|ESD|certif|compliance|WPC|NRE|qualif|regulat|FCC|ETSI finds nothing relevant. The BOM still has no 2.4 GHz antenna or U.FL/IPEX pigtail for U19 ESP32-C3-WROOM-02U (net.py find antenna/IPEX/U.FL all empty). Rev 3 now actually drives that radio: it adds Wi-Fi STA/AP settings (Firmware/Services/configuration/reader_settings.h:63-76) and runs WiFi_Service_Task in the main loop (Src/main.c:137). The type-approval and antenna gap is therefore live, not theoretical. Firmware regulatory side: the default region is 8 (India, Firmware/Config/product_config.h:45). But SILION_Application_SetRegion still accepts North America and China (Firmware/Application/Silion/sillion_application.c:1148-1153), and the vendor JSON path accepts any region from 0 to 255 before that check (vendor_config.c:1177-1183).

**Remaining work.** All program items remain. Write the NRE/payback table. Build a compliance matrix with acceptance criteria: ESD +/-8 kV contact / +/-10 kV air, EFT, surge on VIN and RS-485, RE/CE, and -20 to +60 C. Plan WPC/ETA type-approval for the ESP32-C3 and the UHF transmitter. Choose an approved antenna and add the antenna and pigtail to the BOM. Budget pre-compliance scans. Firmware: lock the region to what the deployment market allows, validate it in the vendor-JSON path too, and make it depend on the module type.

#### R2-07 — 'SILION_REGION_INDIA' is 0x08 (CE_LOW, up to 867.5 MHz) and is the new default; the real India code 0x04 is rejected by the SET_REGION whitelist, which accepts the undocumented 0x0A

`NOT FIXED` · rev 2: HIGH → now: HIGH

**Rev 3 evidence.** The code is unchanged:
- 0x08 is still named India: '#define SILION_REGION_INDIA 0x08U' (silion_protocol.h:87).
- The default is still 8: PRODUCT_RFID_DEFAULT_REGION 8U (product_config.h:45). ReaderDefaults applies it (firmware_configuration.c:85-86).
- The SetRegion whitelist still allows only 0x08, 0x01, 0x06 and 0x0A, so the real India code 0x04 is rejected (sillion_application.c:1148-1156).
- The module-document values (CE_LOW 0x08 up to 867.5 MHz, INDIA 0x04) are taken from the rev 2 evidence (module protocol Appendix 2). I did not re-open that document.

New in rev 3:
- The vendor-tool static config accepts any 'rf.region' from 0 to 255 (vendor_config.c:1176-1182) and saves it with ReaderSettings_Save (vendor_config.c:1229-1249).
- At boot that value replaces readerConfig.region (firmware_configuration.c:452-457). ConfigureReader sends readerConfig.region with no whitelist check (sillion_application.c:342-351).
- So the whitelist can be bypassed: 0x04 can only be set through that path, and so can any uncertified code.
- GET_CURRENT_REGION (0x67) is used only by a host query (sillion_application.c:739-757). It is never used to read back after a set.

**Remaining work.** - Codes: set SILION_REGION_INDIA to 0x04, rename 0x08 to CE_LOW, and default to 0x04.
- One whitelist: build a single validation function from the SKU's certified regions and apply it in SetRegion, in vendor_config, and on boot load of the persisted region.
- Read back with 0x67 after every set.
- Verify the transmit band with a spectrum analyser.

#### R2-08 — Identium binary TCP GPIO commands (0x02 GPI upload, 0x16 get GPI, 0x17 set GPO) are not implemented; only a JSON set_gpo/get_gpi variant exists

`NOT FIXED` · rev 2: HIGH → now: HIGH

**Rev 3 evidence.** The binary GPIO commands are still not implemented:
- No source under Firmware/ handles 0x16 (get GPI) or 0x17 (set GPO with duration), or sends the 0x02 GPI-status upload. A grep of Protocol, ethernet, configuration, GPIO and serial finds no such command codes.
- The Identium TCP protocol document defines all three: 0x02 GPI upload, 0x16 get GPI and 0x17 set GPO (Identium UHF Reader TCP Protocol Document V1.0 p.2). The 0x17 data has a GPO number, a state and a duration in seconds after which the output reverts (p.3).

What exists instead:
- The 0xEE config port handles only 0x22, 0x24 and 0x1E (configuration_port.c:20-22, dispatch 106-131).
- On serial/USB, a 0xFF byte goes to Module_Bridge (host_transport.c:271-276). That bridge emulates the vendor 'ARM7 mainboard': IOGET, IOSET, GIO and SIO (module_bridge.c:18-41, 395-417).
- The active TCP link carries only JSON set_gpo (identium_command.c:359-527) and gpi_changed (identium_protocol.c:1096-1147).
- Socket 3 runs the legacy ASCII server (host_tcp_server.c).

Rev 3 adds a vendor-tool GPIO path, but not the Identium binary command set this item is about.

**Remaining work.** - Framing: implement the 0xFF reader-to-server and 0xEE server-to-reader framing (logical name, data length, type, flags, status, and CRC on serial).
- Commands: add 0x16, 0x17 (with timed revert) and the 0x02 GPI-change upload, all sharing gpio_service.
- Alternative: if Identium decides the JSON dialect or the vendor IOGET/IOSET is the target instead, record that decision in the spec.

#### R2-12 — A/B slot selection is wrong: once slot A is valid, every save erases slot B, which holds the newest copy, so a power cut rolls the config back to the first save ever made

`NOT FIXED` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** - Same logic: steady-state V2 slot selection is identical to rev 2. The code is 'else if(validA2 != 0U) { targetAddress = CONFIG_STORAGE_ADDRESS_B; ... nextSequence = recordA.sequence + 1U; }' (configuration_storage.c:524-529, rev2 :282-287). It never compares sequences when both slots are valid.
- Result: once A is valid, every save erases B, which holds the newest copy, and rewrites it with seqA+1. A power cut during that erase or program rolls the config back to the first record ever written.
- Load: it still picks the higher sequence (390-411).
- Migration only: only the one-off V1-to-V2 migration branch compares sequences (502-522).
- F411: each such save now erases a 128 KB sector (sector 7, configuration_storage.c:15-18).
- No test: hostcheck/ has no configuration-storage test.

**Remaining work.** - Slot choice: target the invalid slot or the one with the older sequence (IsSequenceNewer is already there).
- Sequence: set nextSequence = max(seqA, seqB) + 1, and never erase the slot holding the newest valid record.
- Test: add a host test with a power cut simulated after each erase and program.

#### R2-13 — USB TX queue silently truncates output mid-line, is pumped only one 64-byte packet per main-loop pass, and its overflow counter is never read

`NOT FIXED` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** - Truncation: USB_CDC_Transport_Send still accepts bytes until the 4096-byte ring is full, then breaks and returns a partial count (usb_cdc_transport.c:74-96; size in usb_cdc_transport.h:6).
- Return ignored: HOST_Transport_SendString still discards the result with '(void)HOST_Transport_LinkSend(' (host_transport.c:686). Legacy TAG and response lines are therefore cut mid-line.
- Overflow counter unread: USB_CDC_Transport_GetTxOverflowCount (usb_cdc_transport.c:122-125) has no caller. In the shipped F411 map it is listed under 'Discarded input sections' (rfid_controller_f411.map:1005).
- Pump rate: Task still submits one packet of 64 bytes or less (usb_cdc_transport.c:165-179). OnTxComplete does not chain the next packet (104-108). Task runs once per HOST_Interface_Task (host_interface.c:871).
- SYNC_POLL: the drain loop still runs in 32-tag batches (sillion_application.c:2172-2245). It calls PublishTag, which calls HOST_SendTag (:27-32), and nothing pumps USB inside the loop.
- What is new: GetFreeSpace (usb_cdc_transport.c:99-102), used only by the module bridge (module_bridge.c:128-133); config-port replies check the full length (configuration_port.c:81). Neither covers the legacy output path this item is about.

**Remaining work.** - Whole messages: make the send all-or-nothing per message, or act on the shortfall, and raise a fault from the overflow counter.
- Throughput: chain packets from the TX-complete callback, or submit the contiguous run as one multi-packet transfer.
- SYNC_POLL: pump USB inside it, or make it non-blocking.

#### R2-14 — USB TX stalls indefinitely when no host application has the port open; DTR/line state is ignored, and stale data is delivered later

`NOT FIXED` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** - DTR ignored: CDC_SET_CONTROL_LINE_STATE is still an empty case (usbd_cdc_if.c:236-238).
- Stuck flag: usbTxInFlight is set when a packet is submitted (usb_cdc_transport.c:174-175). Only OnTxComplete clears it (104-108), or Reset on CDC Init/DeInit (usbd_cdc_if.c:161, 174). There is no timeout.
- Effect: Task returns early while a transfer is in flight (usb_cdc_transport.c:142-146). If the host closes the COM port with an IN transfer pending, the ring fills, later output is dropped, and stale data comes out on reopen.
- Active transport: USB stays active once it has claimed the parser (host_transport.c:546-549). There is no idle release on USB.
- Design note: the comment at module_bridge.c:52-55 says the vendor tools do not use DTR. That is a deliberate choice, but it does not explain the missing in-flight timeout or flush.

**Remaining work.** - Timeout: add a TX-in-flight timeout that flushes the IN endpoint and resets the ring.
- DTR: track it from SET_CONTROL_LINE_STATE (wValue bit 0) where host tools assert it; drop or do not queue output while it is low, and flush on its rising edge.
- Idle release: consider releasing the USB active transport after an idle period, like the module bridge's 30 s.

#### R2-16 — GPO duration expiry restores the previous state, but both Identium specs require reverting to the opposite of the commanded state

`NOT FIXED` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Firmware/Services/GPIO/gpio_service.c:315 still saves the current state ("gpioPulsePreviousState[index] = gpioOutputStates[index];") before applying the new one. On expiry, :139-154 writes gpioPulsePreviousState back to the output and to gpioOutputStates. This is the same logic as rev 2 (rev 2 gpio_service.c:284). Example: GPO already at 1, then {state:1,duration:3} gives no pulse. Both Identium specs require the opposite of the commanded state on expiry. No host test covers this: the hostcheck tests are protocol, tag_log, boot_image and firmware_update only.

**Remaining work.** On expiry, drive (1 - commanded state), save the commanded state per output and drop gpioPulsePreviousState. Add a host or bench test: set state 1 with duration 0, then state 1 with duration 2, and expect 0 after 2 s.

#### R2-17 — Any running GPO pulse blocks every set_gpo, including commands for other outputs and attempts to cancel the pulse, for up to 3600 s

`NOT FIXED` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Firmware/Services/GPIO/gpio_service.c:235-244 still returns GPIO_SERVICE_ERR_BUSY with 20003 "gpo op is running" when GPIO_Service_AnyPulseActive() (:23-36) finds any active pulse. The check runs before per-item validation and ignores which output the command targets. Durations up to 3600 s are still accepted (:279). The same function also serves ASCII SET_GPO (Firmware/Application/Host/host_interface.c:2407) and the module-bridge IOSET (Firmware/Services/configuration/module_bridge.c:286), so all three paths are blocked. No spec citation was added.

**Remaining work.** Track pulses per output. A new command on an output should replace or cancel that output's timer, and commands on other outputs should proceed. If a reference reader really rejects these commands, cite the spec section and reject only the outputs that collide.

#### R2-19 — GPIs have no debounce and are sampled only by the main loop, which can block for seconds; short pulses are missed, contact bounce floods events, and GPO pulse expiry is delayed

`NOT FIXED` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Firmware/Services/GPIO/gpio_service.c:157-190 still samples all inputs once per call and reports any change at once: no filter time, no stable count, no rate limit. GPIO_Service_Task is called only from the foreground loop (Src/main.c:142), and no GPI EXTI or timer sampling exists. GPO pulse expiry (:139-155) and buzzer off (:130-137) also run only there. The loop can still block for seconds: SILION_WaitForResponse busy-polls up to timeoutMs (Firmware/Application/Silion/sillion_application_port.c:432-478), and the single-inventory path waits 5000+timeoutMs per transaction (sillion_application.c:1979-1981, 2116). FW-001 section 6 requires 10 ms GPI debouncing in firmware.

**Remaining work.** Sample GPIs from a 1 ms SysTick or timer hook through a debounce filter (10 ms per FW-001, configurable). Latch edges so short pulses are reported and rate-limit the events. Move GPO and buzzer expiry into the timer context, or make the module transactions non-blocking.

#### R2-21 — The config socket (8080) has no keep-alive or idle timeout and waits indefinitely for the client to close

`NOT FIXED` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Firmware/Services/configuration/configuration_port.c still has no timeout. Line 284-286 returns while an ESTABLISHED client has sent nothing. Lines 269-271 return forever after a response while g_waitingForClientClose is set ("let the client close the connection cleanly"). Partial frames return at :306-308 and :332-334. Socket 1 is freed only on CLOSE_WAIT or CLOSED (:344-380), on a network change (reader_settings.c:654), or when a serial-link module session takes over an idle TCP module session (module_bridge.c:598-613). W5500_StartTCPServer1 (Firmware/Devices/w5500/w5500.c:1177-1270) writes buffer size, mode, port, OPEN and LISTEN but never Sn_KPALVTR. The only keep-alive write is for socket 0 (w5500.c:705). TCP module-bridge sessions on 8080 are also persistent with no idle limit: MODULE_BRIDGE_LINK_IDLE_MS applies only to serial links (module_bridge.c:57, 785-791). Only one socket listens on 8080, so one half-open or silent client blocks the vendor configuration tools indefinitely.

**Remaining work.** Set Sn_KPALVTR on socket 1, as is done for socket 0. Add application idle timeouts: a few seconds with no complete request, a short grace period after the response, and an idle limit for TCP module sessions. On timeout, DISCON/CLOSE and re-listen. Consider closing actively after the one-shot EE response.

#### R2-22 — set_gpo truncates gpo/state to uint8 before validation, so out-of-range values alias to valid ones; the 5-item sample command is rejected

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Firmware/Protocol/identium_command.c is unchanged from rev 2. It is absent from the MODIFIED list in rev2_rev3_filelist.txt, and a diff of lines 1-600 against the rev 2 tree is empty. :442 "outputNumber = (uint8_t)value;" and :466 "state = (uint8_t)value;" still narrow before range checks, so gpo 257 / state 256 alias to GPO1 / state 0. :413 still rejects a 5th array item with 20003, so the sample server's gpo 1..5 command drives nothing. The 'gpo op is running' rejection (gpio_service.c:235-244) still prevents re-triggering a pulse.

**Remaining work.** Range-check the uint32 values (gpo 1..N, state <= 1, duration <= 3600) before narrowing, or pass uint32 into GPIO_Service_SetOutputs. Accept up to the spec maximum and report unsupported gpo numbers (4 and 5 on the F411) per item without aborting the whole command. Let a new command restart or override an active pulse.

#### R2-26 — Config storage reserves 256 KB and erases a whole 128 KB sector for a 24-byte record on every save, with no skip when the value is unchanged

`NOT FIXED` · rev 2: LOW → now: MEDIUM

**Rev 3 evidence.** Configuration_Storage_Save still erases a whole 128 KB sector for each save and writes one record at the sector base, with no append log (Firmware/Services/configuration/configuration_storage.c:554 erase, :559-568 program at targetAddress). The record is now V2, about 124 bytes, versioned and migrated from V1 (configuration_storage.h:13-35; configuration_storage.c:30-31, 311-326, 413-446), but the storage scheme is unchanged. Unchanged values are still saved. The parsers set foundAny whenever a key is present (e.g. configuration_service.c:1283-1288 for hb_cylce), and ApplyRuntimeConfig and ApplyStaticConfig save whenever changed != 0 (configuration_service.c:2160-2165, 2240-2265). There is no memcmp against g_persistentConfiguration; only SaveSession skips equal values (:471-475). The rev 2 slot bug is still there: the slot choice at configuration_storage.c:524-535 is the same as rev 2 configuration_storage.c:276-292. Once both slots are valid, every save goes to slot B (sector 7) with sequence A.seq+1 (=2). Sector 7 takes all the wear, and a power loss during a later save falls back to the very first saved configuration. On the F411 target the space cost is worse. Configuration takes sectors 6 and 7 (256 KB) (configuration_storage.c:15-18) and the reader settings take sector 5 (128 KB) (reader_settings.c:29-31). Together that is 384 KB of the 512 KB flash (Firmware/Common/firmware_image.h:19-21; Boards/f411/STM32F411CEUX_FLASH.ld:48-51). That leaves the application a 112 KB slot that is already about 83% full (LEAD_FACTS: about 92.6 KB). Missing functions such as the binary TCP protocol, Modbus and the E710 driver must fit in that space. One side improvement: network, name and work state now persist via reader_settings.c as an append log (:534-571). That sector has no A/B copy, which is a separate issue.

**Remaining work.** Skip the save when the candidate equals the stored record (memcmp). Fix the slot choice: write to the slot opposite the newest valid record. Replace the whole-sector-per-save scheme with an append log inside a pre-erased sector. Re-plan the F411 flash map: for example, keep settings in two of the 16 KB sectors 1-3 and give the application the 128 KB sectors. Then update firmware_image.h, the linker script and the bootloader to match.

#### R2-31 — The RX line buffer is not cleared on disconnect, so the first command after a reconnect is corrupted

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** The line assembler is reset only when '\n' arrives (Firmware/Protocol/identium_protocol.c:965-983), on overflow (:143-149) and in Init (:931-934). On disconnect, Identium_Protocol_Task still only clears identiumWasConnected (:1187-1191), and the reconnect branch (:1193-1211) does not touch identiumRxIndex or identiumRxSeekingObject. Ethernet_Service_ConnectionLost notifies only HttpUpload (ethernet_service.c:42-50). No OnConnect/OnDisconnect hook exists (identium_protocol.h:18-45). The new seek-for-'{' logic (:995-1011) does not help. After a partial line such as {"command_type":"sync_ti, the seeking flag is already 0, so the next command's bytes are appended to the stale fragment, the same corruption shown in rev 2. FW-35 also remains: a CLOSE_WAIT socket is closed without draining unread RX (ethernet_service.c:183-188). The Wi-Fi transport feeds the same assembler (firmware_configuration.c:516-529). The host test only covers tag buffering across a disconnect (hostcheck/identium_protocol_host_test.c:148-175), not RX framing.

**Remaining work.** Add Identium_Protocol_ResetRx() (index=0, discard=0, seeking=1). Call it from Ethernet_Service_ConnectionLost and the Wi-Fi equivalent, or whenever Identium_Protocol_Task sees the connection change state. Drain RX before closing on CLOSE_WAIT. Add a host test for the reconnect case.

#### R2-32 — Command framing and validation are lenient and undocumented: LF is required, any substring match counts, and remote commands are unauthenticated

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Firmware/Protocol/identium_command.c and identium_command.h are byte-identical to rev 2 (diff -q). ExtractCommandType still runs strstr over the whole line (identium_command.c:50) and does not parse the object. A nested key such as {"command_data":{"command_type":"reboot"}} still matches and reaches reboot (:690). The value copy only rejects a backslash (:75-88), and the reply echoes commandType unescaped into JSON (identium_protocol.c:283-292). Dispatch still happens only on LF (identium_protocol.c:965-977). AppendByte stores NUL bytes (:152), and the line is handed on as a C string (:972-976), so a NUL still truncates the command. One small change: bytes before the first '{' are now discarded (:995-1011). There is no authentication or allow-list anywhere. reboot, set_static_conf with reset (:817-830), set_gpo (:706) and inventory pause/resume run for whoever is at the configured server address. For context, rev 3 also opens an unauthenticated listening TCP port 5000 (host_tcp_server.c:20-25; product_config.h:13). It feeds the legacy ASCII parser, which accepts REBOOT (host_interface.c:1276) and the FW_BEGIN/FW_DATA/FW_APPLY update commands (host_interface.c:774-842). This widens the unauthenticated control surface.

**Remaining work.** Write down the framing, or use the spec's binary framing. Parse each line with the strict JSON parser already in configuration_service.c and read command_type only from the top level. Escape echoed strings. Add authentication or an allow-list (at least for reboot, set_static_conf, set_gpo and FW_*), and decide whether port 5000 should be open by default.

#### R2-34 — Each save blocks the superloop for roughly 1-2 s (128 KB sector erase), and set_static_conf can do this during a running inventory, overflowing the 256-byte module RX queue

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Configuration_Storage_Save still runs a whole-sector HAL_FLASHEx_Erase (configuration_storage.c:259-279, called at :554), and on the F411 that sector is 128 KB (sectors 6/7, :15-18). Because of the slot bug in R2-26, repeated saves always erase sector 7. The save still runs inline from the main-loop command path: Ethernet_Service_Task (main.c:136) calls Identium_Protocol_PushTcpByte, then Identium_Command_ProcessJson, then set_static_conf (identium_command.c:817-830), then ApplyStaticConfig. ApplyStaticConfig (configuration_service.c:2188-2292) and ApplyRuntimeConfig (:2124-2186) still have no SILION_Application_IsInventoryRunning check; only ApplyTempStaticConfig has one (:2312). ApplyRuntimeConfig is also reached from the vendor config path (vendor_config.c:1053). The module RX queue grew from 256 B (rev 2 sillion_application_port.c:19) to 4096 B (rev 3 :19). That probably does not help. The vector table sits in flash (system_stm32f4xx.c:96-110, VECT_TAB_SRAM not defined), and no RAM-resident ISR or flash routine was found. On the single-bank F411, flash fetches stall during an erase, so the USART1 RX ISR probably cannot run at all and tags arriving during the erase are lost. This is ST's documented behaviour, but the reference manual is not in the audit folder, so it is unverified here. The erase duration was not re-measured on the F411 (no datasheet in the folder); rev 2 estimated about 1-2 s.

**Remaining work.** Reject persistent saves while inventory runs (as the temp path does), or pause the module around the save. Move to an append log in a pre-erased small sector so erases are rare and can be scheduled when idle. If an erase must happen during reads, run the ISR and vectors from RAM.

#### R2-37 — CRC32 covers only the data payload, not the header (magic, version, dataSize, sequence)

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** configuration_storage.c:246-249 (V2 validate) and l.542-545 (save) still compute Configuration_Storage_Crc32 over &record->data, sizeof(record->data) only. magic, version, dataSize and sequence (l.54-57) are unprotected. Slot selection uses the unprotected sequence (l.390-401). Added since rev 2: ValidateData range checks on the payload (l.251-255) and a V1-to-V2 migration. The new reader_settings.c copies the same pattern: CRC over slot->data only (l.304, 356, 533), with an unprotected sequence (l.372, 530).

**Remaining work.** Compute the CRC over the whole record before crc32, in both configuration_storage.c and reader_settings.c. Keep programming the CRC word last as the commit marker. Bump the record version and keep migrating the old records.

#### R2-38 — Factory reset is partial and has no hardware path

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** The reset is still only Configuration_Storage_GetDefaults() applied through set_static_conf {"reset":1} (configuration_service.c:2222-2238). It now restores every field of the larger Configuration record (configuration_storage.c:328-361). It still does not touch ReaderSettings, where IP, mask, gateway, server, region, power, Wi-Fi and work state now live: ReaderSettings_GetDefaults (reader_settings.c:208) is static and is called only on load or invalid-storage paths (l.362, 399, 436, 458). No public reset API exists (reader_settings.h:90-117). The new vendor 'clear' command 0x20 (vendor_config.c:1316-1338) calls the same reset, ignores its result, skips it when the module is not idle, and still returns OK. Its comment claims it 'restores every stored default'. Neither path reboots unless reboot:1 is sent. A grep of Firmware/Src/Boards finds no GPI- or key-held-at-boot reset path. Default IP is 192.168.150.50 (reader_settings.c:215-218), not the 192.168.1.100 named in the remaining work. Recovery is still possible over USB: the module bridge IPSET (module_bridge.c:32, 434) works on a USB CDC session.

**Remaining work.** Define factory reset as restoring both Configuration and ReaderSettings (network, region/power, server, Wi-Fi, work state), then reboot. Return the real result from vendor clear. Add a hardware path, e.g. a GPI held at power-up (KEY1/KEY2 are NRST/BOOT0, so no spare key exists). Align the default IP with the spec.

#### R2-39 — USB RX ignores CDC flow control. Every OUT packet is re-armed at once and pushed into the shared 256-byte host queue, so bytes are dropped whenever the main loop is busy

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** usbd_cdc_if.c:267-281 is unchanged. CDC_Receive_HS pushes every byte into HOST_Transport_PushByte, then always calls USBD_CDC_SetRxBuffer and USBD_CDC_ReceivePacket. The comment at l.277 still says 'Always re-arm OUT reception'. The only change is HOST_RX_QUEUE_SIZE 256 -> 1024 (host_transport.h:12; rev 2 l.10). The queue is still shared by USB, Ethernet, serial and Wi-Fi (host_transport.c:395-400), and it still drops and counts bytes when full (l.415-417). The main loop can still block in SYNC_POLL for up to 65535 ms + 5000 ms (host_interface.c:1170, sillion_application.c:1981). This firmware is built for the F411 (usbd_cdc_if.c CDC_Receive_HS is linked, f411-release map l.3315).

**Remaining work.** Re-arm the OUT endpoint only when the queue has room for a full 64-byte packet. Otherwise NAK and re-arm from the task. Give USB its own RX queue, separate from the network transports.

#### R2-40 — USB PHY clock is gated on suspend and never ungated on resume

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** usbd_conf.c:300 still calls __HAL_PCD_GATE_PHYCLOCK in the suspend callback. The resume callback's USER CODE 3 is still empty (usbd_conf.c:320-326). The HAL V1.8.5 WKUINT handler still does not clear STOPCLK (stm32f4xx_hal_pcd.c:1285-1310). No code calls UNGATE_PHYCLOCK (grep of Firmware). On the F411 the stack is bound to the OTG_FS core with low_power_enable=DISABLE (usbd_conf.c:394-413). Both callbacks are linked in the F411 release image (build/f411-release/rfid_controller_f411.map:3375-3378).

**Remaining work.** Call __HAL_PCD_UNGATE_PHYCLOCK(hpcd) in the resume callback, or remove the gating, since low power is disabled. Test host suspend/resume on Windows and Linux on the F411 board.

#### R2-41 — Device reports itself self-powered with a 100 mA MaxPower

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** USBD_SELF_POWERED is still 1U (usbd_conf.h:76), so usbd_cdc.c:179-184 still reports bmAttributes 0xC0 with MaxPower USBD_MAX_POWER 0x32 = 100 mA (usbd_def.h:82). The premise still holds on the 13-Oct board netlist (net.py): USB-C1 VBUS (A4B9/B4A9) goes through fuse F1 to TPS2116 U13.6. U13.2 and U13.7 drive net `$1N593`, which is the 5V rail: TP11 '5V', relay coils U4/U9/U16 and fuse F8. So the board can draw its 5V rail from VBUS while claiming to be self-powered at 100 mA.

**Remaining work.** Report bus-powered with a realistic MaxPower (up to 500 mA), or detect which TPS2116 input is active and set the descriptor to match. Measure the board's VBUS current with the module idle and running.

#### R2-44 — GPI changes during a TCP outage are lost; no snapshot is sent on connect; get_gpi emits a spurious gpi_changed event

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** identium_command.c is byte-identical to rev 2. get_gpi still calls Identium_Protocol_ReportGpiChange, which emits an event_type 'gpi_changed' line, before the command_reply (identium_command.c:765-778). Identium_Protocol_SendJsonLine still returns 0 when not connected (identium_protocol.c:170-173), and ReportGpiChange ignores that result (l.1147), so a change during an outage is lost. On connect, Identium_Protocol_Task sends only a one-time sync-time request and an immediate heartbeat, with no GPI snapshot (l.1189-1211). gpio_service.c:161-171 still takes the first sample after boot as a silent baseline. The callback is wired only to ReportGpiChange (firmware_configuration.c:339-340).

**Remaining work.** Send a full GPI snapshot when an upload session is established, which also covers the boot baseline. Answer get_gpi with a distinct reply carrying the states, not a change event. Optionally queue the last GPI change while offline and send it on reconnect.

#### R2-46 — Register-driver headers #define ENABLE/DISABLE/GPIO_PIN_SET/RESET, which collide with the CMSIS/HAL enums; rev 2 only partly fixed the HAL name clash

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** gpio.h, spi.h and uart.h are byte-identical to rev 2 (diff). gpio.h:95-96 still has an unguarded `#define GPIO_PIN_SET 1U`. ENABLE/DISABLE sit inside #ifndef at gpio.h:108-113, spi.h:100-103 and uart.h:123-126. The guard does not help, because CMSIS declares ENABLE/DISABLE as enum constants, not macros. The new rev-3 driver i2c.h:118-123 repeats the same ENABLE/DISABLE macros, so the hazard has spread to one more header. I checked this with a syntax-only probe (system clang, F411 defines, the Makefile -I set, test file in the scratchpad). A TU with `#include "gpio.h"` before stm32f4xx_hal.h gives 4 errors (stm32f4xx.h:192-193, stm32f4xx_hal_gpio.h:69-70). i2c.h first gives 2 errors (stm32f4xx.h:192-193). The tree builds only because no TU uses that include order.

**Remaining work.** Prefix the driver macros (GPIO_DRV_PIN_SET/RESET, DRV_ENABLE/DISABLE), including in the new i2c.h, or move the drivers to the CMSIS/LL types. Then add a compile check that includes each driver header before stm32f4xx_hal.h.

#### R2-47 — configuration_port.c dead and stray code, and duplicated 1.4 KB buffers

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** configuration_port.c:28 declares `g_started`. It is written at :232, :252, :362, :379 and :384, and grep finds no read anywhere. The stray bare block is still there: `status = W5500_Socket1_GetStatus();` at :248 is followed by `{` at :249. Content is still built in g_contentBuffer (:26, :101, :149-159) and then copied byte by byte into g_responseBuffer+10 (:71-74). The F411 release map lists g_contentBuffer at 0x588 and g_responseBuffer at 0x588 (rfid_controller_f411.map:4222-4225). g_rxBuffer grew from 64 B (rev 2 configuration_port.c:11) to 0x806 = 2,054 B (map:4226, set by :17), so the module now holds about 4.9 KB of static RAM. On the F411 that matters more, because RAM is about 91% committed (LEAD_FACTS).

**Remaining work.** Delete g_started and the stray block. Build the JSON straight into g_responseBuffer+10, or make SendResponse write the header in front of a caller buffer, and drop g_contentBuffer (saves 1,416 B).

#### R2-48 — USB CDC application buffers waste about 4 KB of RAM

`NOT FIXED` · rev 2: LOW → now: LOW

**Rev 3 evidence.** usbd_cdc_if.c and usbd_cdc_if.h are identical to rev 2 (diff -q). APP_RX_DATA_SIZE and APP_TX_DATA_SIZE are still 2048 (usbd_cdc_if.h:52-53). UserTxBufferHS is still registered with length 0 (usbd_cdc_if.c:159) and is never used. CDC_Transmit_HS sets the caller's buffer (:303), and the caller is usbTxPacket[64], filled from the 4 KB usbTxQueue (usb_cdc_transport.c:11-12, 172, 179). UserRxBufferHS is re-armed for each 64-byte packet (usbd_cdc_if.c:278-279). Both 2 KB buffers are in the shipped F411 image (rfid_controller_f411.map:4548-4552, 0x800 each). That build uses the full-speed core (hpcd_USB_OTG_FS, map:4561), so packets are at most 64 B.

**Remaining work.** Set APP_RX_DATA_SIZE to 64 (the full-speed max packet size) and remove UserTxBufferHS, or transmit straight from usbTxQueue. That recovers about 4 KB of RAM on the F411.

#### R2-52 — F429 bench outputs use PE0/PE1, which on the Discovery kit are the SDRAM byte-mask lines, contradicting the comment's reason for avoiding PE7

`NOT FIXED` · rev 2: INFO → now: INFO

**Rev 3 evidence.** The F429 bench mapping is unchanged: OUTPUT1/OUTPUT2 = GPIOE pins 0/1 (industrial_io.c:32-35), and the comment still avoids PE7 because of the SDRAM (:17-22). The only change in that block is one comment line (:21), from diffing against rev 2. The F429 build is still present and is still the Makefile default (Makefile:33 `BOARD ?= f429`). The F411 target does not repeat the problem. Its outputs are PB2/PB10/PA0 and its inputs PB3/PA15/PB4/PB15 (f411_custom/board_config.h:125-144). These match the netlist pin map: relay gate drives through 100R R51/R52/R49 on PB2/PB10/PA0, and GPI RC networks on PB15/PA15/PB3/PB4 (u7_pinmap.txt pins 10, 20, 21, 28, 38-40). Output 4 is NULL and guarded (industrial_io.c:169-173, 201-205).

**Remaining work.** Bench-only. Move the F429 test outputs to Discovery header pins with no on-board load, or correct the comment. Consider making f411 the Makefile default, since the F411 is now the product board.

#### R2-54 — Clang results for own code: 0 errors in the Debug configuration; new modules are warning-clean; the vsnprintf wrapper disables format checking

`NOT FIXED` · rev 2: INFO → now: LOW

**Rev 3 evidence.** Configuration_Service_Append (configuration_service.c:579-584) is still a variadic vsnprintf wrapper (:601) with no __attribute__((format(printf,3,4))), and it has 6 call sites (:771, :777, :1065, :1079, :1088, :1119). The target build still uses `-std=gnu11 -Wall` only, with no -Wextra or -Werror (Makefile:98). Only the hostcheck builds use -Werror. I ran a syntax-only clang pass: F411 defines, the Makefile -I set, -Wall -Wextra -Wconversion -Wshadow -Wformat=2, libc stub headers in the scratchpad, 62 own TUs. Result: 63 unique own-code warnings, up from 53. The rev-1 files have exactly the rev-2 counts: spi.c 21, silion_protocol.c 12, uart.c 11. New rev-3 code is no longer warning-clean: esp32_link.c:206,357,363, esp32_link_protocol.h:119, wifi_service.c:552,562, tag_log.c:131 and host_transport.c:245 all warn (benign integer-promotion casts). -Wformat-nonliteral fires at configuration_service.c:601, confirming the wrapper is unchecked. Clang also now reports 5 errors in Src/stm32f4xx_it.c:88-147, for the C `while(1)` that follows the basic-asm FAULT_RECORD_ENTER in the naked fault handlers. GCC accepts this and the code is unreachable, but it means the rev-2 '0 errors' result no longer holds for clang.

**Remaining work.** Add the format attribute to Configuration_Service_Append. Fix the conversion warnings in the rev-1 and new modules. Enable -Wextra, then -Werror, for the target build in CI. Drop the C statements from the naked handlers, leaving the asm alone, or mark them unreachable with __builtin_unreachable().

### PARTIAL (64)

#### FW-07 — No configuration persistence; session/target/Q not applied at boot and settings revert to defaults

`PARTIAL` · rev 2: HIGH → now: MEDIUM

**Rev 3 evidence.** MOSTLY FIXED (compiled and linked: ReaderSettings_Save map:1470-1472, Configuration_Storage_Save present in map). New reader_settings record v4 (reader_settings.h:32-87) on the F411 in flash sector 5 at 0x08020000 (reader_settings.c:28-31), with CRC, versioned migrations v1-v3 (:340-470) and a sanity check (:309-338). It persists region, read/write power, tx/rx antenna, Gen2 target/Q/profile, IP/mask/gateway/DNS, a UID-derived unicast MAC (:230-238), server IP/port, reader name, work state, HTTP upload and Wi-Fi settings. The session is kept in configuration_storage, F411 sectors 6/7 (configuration_storage.c:9-18). The linker keeps the app off sectors 5-7 (Boards/f411/STM32F411CEUX_FLASH.ld:48-51). Setters persist: SET_REGION/SET_POWER (host_interface.c:1623, :1736), SET_ANTENNA(S) (:1678, :2622), SET_SESSION (:1786), SET_GEN2 (:2598). At boot, region, power and antenna are restored (firmware_configuration.c:452-462), network is applied from storage (:502-503, ReaderSettings_ApplyNetwork), and session plus Gen2 target/Q/profile are sent to the module (:188-210). The upload target is read from settings (ethernet_service.c:127-128). STILL WRONG: (a) reader_settings is a single-sector append log that erases the whole 128 KB sector when full and then programs (reader_settings.c:540-575). A power cut in that window loses every setting, including IP, server and region, and the unit falls back to defaults; rev 2's A/B was safer. (b) configuration_storage still has the rev-2 slot-selection bug: when both records are valid it always overwrites B (configuration_storage.c:524-529), even when B is the newest. (c) The serial mode (RS-232/RS-485) is compile-time only: PRODUCT_SERIAL_DEFAULT_MODE (product_config.h:41-43) passed at firmware_configuration.c:373-376, and Serial_Port_SetMode has no caller outside serial_port.c. The baud rate is fixed. (d) Only one antenna port and one power pair are stored: SET_ANTENNAS rejects differing ports (host_interface.c:515-518), so there is no multi-antenna set and no per-antenna power. (e) The stored region is not validated (see FW-02). The default IP is 192.168.150.50 (reader_settings.c:215-218), not the Utility doc's 192.168.1.100.

**Remaining work.** 1) Make reader_settings power-safe: use two sectors with A/B or ping-pong so an erase never removes the only valid copy, or write the new record to the alternate sector before erasing. 2) Fix Configuration_Storage_Save to always overwrite the OLDER record (compare sequences when both are valid). 3) Add persisted, runtime-settable serial mode, baud and RS-485 parameters. 4) If the reader is multi-port, persist the antenna set and per-antenna power. 5) Validate region and power on load and on save. 6) Align the default IP with Identium's utility (192.168.1.100) or document the deviation.

#### FW-09 — Firmware and GUI implement a new ASCII protocol; none of Identium's existing host protocols are supported

`PARTIAL` · rev 2: HIGH → now: HIGH

**Rev 3 evidence.** IMPROVED (linked in F411 map): reader HTTP POST ActMode now exists. http_upload.c:686-700 builds 'POST %s HTTP/1.1' with Content-Type application/json and Content-Length, takes commands from a 2xx reply body containing command_type (:597-624), and sends reader_name as 'name/IP' over HTTP (:286-305). It is selected by uploadMode (reader_settings.h:28-30; ethernet_service.c:109-123, 140-143). The sync_time_req and reader_exception events exist (identium_protocol.c:210-215, 241-247). The Identium Utility/RFID Manager configuration framing (EE 00 len cmd flags data [CRC16]) is handled on TCP 8080 (configuration_port.c:289-345) and on USB and serial (host_transport.c:56-69, 234-284). Module-protocol FF frames and the ARM7-mainboard commands are bridged for Reader Manager/ReaderSense (module_bridge.c:18-41). STILL MISSING: (a) Identium TCP Protocol V1.0 binary active mode, i.e. the 0xFF reader->server frames with logical name, status code and cmds 0x01/0x02/0x04/0x05/0x16-0x18, and the 0xEE server->reader 0x01 tag ACK (spec: Identium UHF Reader TCP Protocol Document V1.0, section 'reader to server communication format'). Grep finds no such encoder in Firmware/Protocol. The default upload mode is TCP (memset 0 = READER_SETTINGS_UPLOAD_TCP, reader_settings.h:29), which sends newline-JSON to 192.168.150.100:9000, a dialect no Identium TCP V1.0 server accepts. (b) Modbus RTU/TCP (addr 0x02, reg 0xB00 x125): grep 'modbus' finds only a CRC comment (host_transport.c:248). (c) Tag JSON still uses the compact keys ep/at/rc/ri/fq (identium_protocol.c:543, :579), not the reader TagInfo keys (epc, antenna, read_count, protocol, rsi, bank_data, first/lastseen_timestamp; ActModeDataFormat example). (d) client_ack=1 and crc_enable=1 are rejected as unsupported (configuration_service.c:1471-1499), so V1.0/reader tag confirmation is absent.

**Remaining work.** 1) Implement the TCP V1.0 binary codec (FF/EE framing, BE length, 4-byte status, logical name, 0x01 tag upload with optional server ACK and blocking, 0x02 GPI, 0x04 heartbeat with board/module/SW/antenna fields, 0x05 fault, 0x16/0x17/0x18 replies, CRC16 on serial). Make it a selectable upload mode, also on RS-232/485. 2) Implement Modbus RTU (addr 0x02) and Modbus-TCP (reg 0xB00 x125), and resolve the clash with the config listener on 8080. 3) Emit reader TagInfo field names in HTTP mode (or make the key set selectable) and implement client_ack. 4) Add byte-exact conformance tests built from the spec examples.

#### FW-11 — No native USB device stack; the 'USB' transport is the Discovery kit's ST-LINK VCP on USART1

`PARTIAL` · rev 2: MEDIUM → now: LOW

**Rev 3 evidence.** FIXED for the F411 board. The USB stack is bound to USB_OTG_FS under STM32F411xE: usbd_conf.c:396-440 sets Instance USB_OTG_FS, full speed, embedded PHY, VBUS sensing disabled (correct, because PA9 is USART1 TX to the RFID module per u7_pinmap.txt), and FIFOs Rx 0x80/Tx0 0x40/Tx1 0x80 (:436-438, within the 1.25 KB). MSP init sets PA11/PA12 to AF10 (usbd_conf.c:77-104). OTG_FS_IRQHandler is at usb_irq.c:4-10 and is vectored in Boards/f411/startup_stm32f411ceux.s:212. The clock gives exactly 48 MHz: 8 MHz/4*96 = 192 MHz VCO, PLLQ 4 (Src/main.c:167-212). The stack is initialised at firmware_configuration.c:352-353, and the F411 map links OTG_FS_IRQHandler (map:3439-3441), MX_USB_DEVICE_Init (:3349-3351) and USBD_Start (:2330-2332). The USART1/ST-LINK fallback is gone: HOST_Transport_LinkSend sends USB only when CDC is ready and otherwise returns 0 (host_transport.c:695-715). RS-232/RS-485 is a separate HOST_TRANSPORT_SERIAL with its own DE/RE handling (serial_port.c:180-220, 296-315). The vendor configuration and module-bridge protocols run over CDC (host_transport.c:56-69). REMAINING: the descriptors still use ST's VID 1155 (0x0483) and PID 22336 (0x5740) with 'STMicroelectronics' / 'STM32 Virtual ComPort' strings (usbd_desc.c:65-69). DTR, suspend and disconnect are not handled (CDC_SET_CONTROL_LINE_STATE empty, usbd_cdc_if.c:236-238); see FW-05. JSON active mode is not offered over USB. Enumeration on the real board is unverified: no hardware test evidence is in the drop.

**Remaining work.** 1) Replace ST's VID/PID and strings with Identium-owned or licensed IDs (usbd_desc.c:65-69). 2) Handle CDC_SET_CONTROL_LINE_STATE (DTR) and suspend/disconnect to release host ownership (ties to FW-05). 3) Run enumeration, throughput and plug/unplug tests on the F411 board with Windows and Linux hosts, and record the results.

#### FW-16 — Cloud client does not implement any Identium host protocol

`PARTIAL` · rev 2: HIGH → now: HIGH

**Rev 3 evidence.** IMPROVED: the active-mode client now has a real HTTP/1.1 POST transport per reader ActMode. http_upload.c:686-700 sends the request line, Content-Type and Content-Length. Commands are taken from 2xx reply bodies (:597-624). reader_name is sent as 'name/IP' (:286-305), with retries and a timeout (:22-30). Server IP/port, upload mode, HTTP URL and reader name are persistent (reader_settings.h:41-61) and used by ethernet_service.c:109-128. The reader_exception and sync_time_req events exist (identium_protocol.c:210-247), and a tag queue with batching exists (identium_protocol.c:599-700). STILL MISSING: (a) the binary Identium TCP Protocol V1.0 (0xFF/0xEE). No encoder exists, and the default TCP mode sends newline-JSON (identium_protocol.c:625, reader_settings.h:29), which a V1.0 server rejects. (b) Tag confirmation: client_ack=1 is refused as unsupported (configuration_service.c:1471-1499). (c) reader TagInfo keys: ep/at/rc/ri/fq are used instead (identium_protocol.c:543, :579). (d) Modbus-TCP is absent (grep). (e) update_fw_by_http still answers 'not supported operation' (identium_command.c:887-897). (f) The HTTP target must be a literal IP: a hostname URL disables upload (ethernet_service.c:109-121), so there is no DNS. (g) The SPI-flash tag log is written (sillion_application.c:31) but never replayed to the server. TagLog is read only by the ASCII TAGLOG_READ command (host_interface.c:234-289), so tags missed during an outage are not uploaded later. The lead's host test also shows heavy drops on a slow server (LEAD_FACTS: 'dropped 1744, expired 256').

**Remaining work.** 1) Implement TCP V1.0 binary active mode (0x01 with ACK/blocking, 0x02, 0x04, 0x05, 0x16-0x18, logical name, CRC on serial) as a selectable or default mode. 2) Implement client_ack tag confirmation, and replay the SPI-flash tag log to the server on reconnect. 3) Use reader TagInfo key names in HTTP mode (or make the format selectable). 4) Add DNS for hostname URLs. 5) Implement update_fw_by_http, or remove it from the advertised command set. 6) Implement Modbus-TCP per the Identium Modbus doc.

#### FW-17 — No OTA, no bootloader and no configuration persistence

`PARTIAL` · rev 2: HIGH → now: CRITICAL

**Rev 3 evidence.** IMPROVED: a bootloader now exists in F411 sector 0, 16 KB (Bootloader/bootloader_f411.ld:6-9; built per LEAD_FACTS at 6,220 B). The app is at 0x08004000/112 KB (Boards/f411/STM32F411CEUX_FLASH.ld:51), with VTOR relocation (boot_main.c:133-138). Images are signed with a SHA-256 hash plus an ECDSA P-256 signature (boot_image.c:6-51), and the header is checked for board ID, load address and size (firmware_image.c:6-27). The 2 MB staging area is in the GD25Q32 at 0x200000 (firmware_image.h:93-99). Install is copy-and-verify, retried if power is lost (boot_main.c:48-116). Updates arrive with FW_BEGIN/FW_DATA/FW_END/FW_APPLY over any host link (host_interface.c:749-760, 798-805). Configuration persistence: see FW-07. STILL WRONG: (a) CONFIRMED by the lead auditor: the ECDSA P-256 private key ships in the deliverable at keys/fw_signing_key.pem, and its public half equals g_bootPublicKey in Bootloader/boot_public_key.c. Makefile:164 and Tools/fw_sign.py:194/199 default to that path. Anyone with the zip can sign images that every unit's bootloader installs. (b) The update path has no authentication: the FW_* commands are accepted from TCP 5000, Wi-Fi, USB and serial alike (the ASCII parser is fed by all four, host_transport.c:387-436), so the signature is the only gate, and (a) defeats it. (c) There is no rollback or trial boot. The bootloader erases the app and installs any validly signed image (boot_main.c:56-89), then jumps after only a vector sanity check (:118-139). A signed image that boots but cannot run the update service leaves the unit needing SWD. There is no anti-downgrade: version is a string that is never compared (firmware_image.h:62; firmware_update.c:406-432). (d) The network OTA command update_fw_by_http still returns 'not supported' (identium_command.c:887-897). (e) The configuration_storage A/B slot-selection bug is still present (configuration_storage.c:524-529). (f) The shipped update image was built from a dirty tree (LEAD_FACTS: '...-dirty.tlfw').

**Remaining work.** 1) BEFORE ANY PILOT UNIT IS FLASHED: generate a new signing key offline (HSM or offline machine), never ship it, remove keys/ from the source deliverable, rebuild the bootloader with the new public key, and treat every unit already flashed with the exposed key as compromised (re-flash its bootloader over SWD). Then rotate the hostcheck/test key separation, and change the Makefile SIGN_KEY default so it does not point into the repo. 2) Add a trial-boot and confirm mechanism (the app marks itself good after self-test, otherwise the bootloader restores the previous image from a kept copy in SPI flash). Add an anti-rollback version counter. 3) Restrict FW_* to an authenticated or explicitly enabled maintenance session, or at least to USB/serial. 4) Implement update_fw_by_http (or drop it) using the same signed-image path. 5) Fix the configuration_storage slot selection and make reader_settings power-safe (FW-07). 6) Build release images from tagged, clean trees only.

#### FW-22 — Fixed 5 s response timeout ignores command execution time

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Paths are in the rev 3 tree (271026/_fw/src/STM32_Firmware) unless marked R2. The core fix from rev 2 is still in place. SILION_Application_TransactionWithTimeout passes the timeout down to SILION_WaitForResponse (Firmware/Application/Silion/sillion_application.c:34-72). SingleInventory waits 5000U + timeoutMs (:1979-1981) and SynchronousInventory does the same (:2114-2116). Write, EPC write, lock and kill wait 10000U (:1553, :1649, :1724, :1785). None of the rev 2 remaining work was done. The R2-to-R3 diff of sillion_application.c touches these functions only to swap in SILION_Application_PublishTag. (1) There is still no sane cap on the host timeout. SINGLE_POLL accepts `value >= 1 && value <= 65535` (Firmware/Application/Host/host_interface.c:1053-1056), and SYNC_POLL accepts 1..65535 (:1169-1171). R2 had the same check at :360 and :473. (2) SingleInventory still makes 3 attempts (:1959) with 300 ms gaps. SINGLE_POLL,65535 can therefore still block the superloop for about 3 x 70.5 s, roughly 212 s. SILION_WaitForResponse refreshes the watchdog inside its loop (sillion_application_port.c:446), so no watchdog reset ends the stall. Ethernet, Wi-Fi, the config port, the tag log and the host tasks (Src/main.c:128-145) all stop for that time. (3) Every failure still ends in NO_TAG and return 1. The status branch at :2006-2016 is still empty ("Other failures are also retried temporarily"), followed by NO_TAG at :2029. (4) ReadTagData still ignores its timeoutMs argument. It sends 1000U to the module (:1847-1849) and waits a fixed 6000U (:1859-1862). (5) The SynchronousInventory attempt loop still does not flush the RX queue. It only calls ClearFrame and ClearUartFlags (:2081-2090). New exposure in rev 3: SINGLE_POLL and SYNC_POLL now also arrive over the unauthenticated TCP port 5000 (Firmware/Services/ethernet/host_tcp_server.c:94-98) and the Wi-Fi host socket (Firmware/Services/wifi/wifi_service.c:302-305). Any LAN client can therefore stall the reader for about 3.5 minutes per command.

**Remaining work.** 1. Cap the SINGLE_POLL and SYNC_POLL timeouts to a bounded range, for example 30 s or less (host_interface.c:1053 and :1169). 2. Do not retry 3 times when the timeout is long, so worst-case blocking stays bounded. 3. Preferably convert module transactions into a non-blocking state machine, so network, USB and tag-log servicing continues. 4. Return distinct TIMEOUT / UART / FRAME / STATUS=xxxx errors instead of NO_TAG + OK (:2006-2033). 5. Make ReadTagData use timeoutMs for both the module timeout and the MCU wait. 6. Flush the RX queue at the start of each SynchronousInventory attempt. 7. Once FW-24 is fixed, restrict these commands on network transports.

#### FW-26 — No 32.768 kHz LSE crystal, so the RTC cannot keep wall-clock time and VBAT floats

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Hardware (the 271026 board is the 13-Oct board). VBAT no longer floats. U7.1 VBAT is on net `$1N269`, the same net as VDD on U7.24/36/48 (work/u7_pinmap.txt; net.py pin U7.1). There is still no LSE crystal. PC14/PC15 are now Wiegand WGD0/WGD1, going to CN2.3/CN2.4 with 10k pull-ups R71/R69 (net.py net WGDO/WGD1). X1 on PH0/PH1 is the only MCU crystal, and there is no backup cell. Firmware. There is still no RTC: HAL_RTC_MODULE_ENABLED is commented out (Inc/stm32f4xx_hal_conf.h:64), and a grep finds no RTC/BDCR/LSEON/SNTP code in Firmware, Src or Inc. The epoch is held only in RAM and is zeroed at init (Firmware/Services/Time/time_service.c:31-33). Its only setter is the JSON sync_time command (Firmware/Protocol/identium_command.c:678). Elapsed time is still a 32-bit millisecond delta (time_service.c:67-71), so it wraps 49.7 days after the last sync. New mitigations: the reader now sends a sync_time_req event once connected (identium_protocol.c:197-216, :1200-1204). Each tag-log record now flags whether its time is Unix time or uptime (tag_log.h:33, tag_log.c:185-188). Effect: tags buffered offline after a power cycle, before the server reconnects and syncs, carry only uptime stamps.

**Remaining work.** 1. Hardware: a respin is needed for battery-backed time. That means an LSE crystal (PC14/PC15 are now used by Wiegand, so the pins must be re-planned) plus a coin cell or supercap on VBAT through a diode-OR. Otherwise accept host-provided time only and document it. 2. Firmware: add SNTP over a W5500 or ESP32 UDP socket. 3. Re-request sync periodically, not just once per connection. 4. Use 64-bit elapsed time. 5. Persist the last known epoch, and mark records as unsynced across resets. 6. Report a time-not-synced state on every host channel and in the uploaded tag data.

#### FW-30 — Async tag path drops data under load (small RX ring/tag queue, one tag per loop pass, 115200 baud)

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Improved and compiled into the image. (1) The module RX ring grew from 256 to 4096 bytes: SILION_APP_RX_QUEUE_SIZE is 4096U (Firmware/Application/Silion/sillion_application_port.c:19 and .h:8), where R2 had 256U at the same lines. (2) The active-mode upload queue now holds 256 tags in 8 KB, with drop and expire counters (Firmware/Protocol/identium_protocol.c:63-79, :474-479), reported by diagnostics (Firmware/Services/system/diagnostics.c:142-146). (3) Every tag is also queued to the flash backup log (PublishTag at sillion_application.c:25-30; tag_log.c:473-499). Unchanged. The async tag queue is still 32 entries (Firmware/Protocols/silion/silion_protocol.h:127; the file is byte-identical to rev 2, diff -q). Overflow is counted as asyncBadPacketCount (silion_protocol.c:2259-2266, :2859-2865). SILION_Application_Task still pops one tag per main-loop pass (sillion_application.c:1468-1482), while each pass now also runs the Ethernet, Wi-Fi (I2C polling), config-port, protocol, GPIO, host and tag-log tasks (Src/main.c:128-145) on a 96 MHz F411. The module UART still runs at 115200 (product_config.h:36), and no set-baud command exists. The UART is still byte-per-interrupt: a grep of uart.c finds no DMA or IDLE handling. StopInventory still flushes the RX ring before sending AA49 (sillion_application.c:2324). Output paths are still not all-or-nothing. The port-5000 TX ring drops the rest of a line when full (host_tcp_server.c:136-142). The tag-log RAM queue of 64 drops when full (tag_log.c:482-486). The async-queue overflow is not reported in diagnostics. The lead's host test shows a slow HTTP server dropping 1744 of 2000 live uploads; the flash log is the intended backstop. Loop pass time under a real tag burst on target is unverified, because no on-target measurement was delivered.

**Remaining work.** 1. Drain the async queue completely each pass, or enlarge it, and report its overflow separately from bad packets. 2. Add backpressure: pause the module when the sinks are full. 3. Use DMA with UART idle-line detection, and raise the module baud rate with the set-baud command. 4. Parse pending bytes instead of flushing them in StopInventory. 5. Make the host TCP, USB and serial sinks all-or-nothing per line, and count their drops. 6. Measure the loop pass time and the per-sink drops on the F411 at the maximum tag rate, and deliver the results.

#### FW-43 — Build hygiene: Debug -O0 only, broken Release config, case-mismatched includes and absolute paths

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Fixed. A command-line Makefile now builds debug and release for both boards (Makefile:42-96): release is -Os -g, and F411 debug is -Og. The shipped ELFs confirm it: the producer string in build/f411-release/rfid_controller_f411.elf shows "-g -Os", and the one in build/f411-debug shows "-g3 -Og". The F411 target can now be built: Boards/f411/STM32F411CEUX_FLASH.ld, Boards/f411/startup_stm32f411ceux.s and Drivers/CMSIS/.../stm32f411xe.h ship (rev2_rev3_filelist), and a CubeIDE F411 configuration was added (.cproject:175-261). I ran a case-sensitive include scan over all own sources: no case mismatches, and only disabled-module HAL headers do not resolve. The duplicate USART_ApplicationEventCallback is now weak in uart.c:659 and sillion_application_port.c:219-220, with the strong one in uart_dispatch.c:18. The F411 map links the uart_dispatch.o copy (map line 1979). Still present. (1) The CubeIDE "Release" configuration still lists only the 5 HAL/CMSIS include directories (.cproject:135-139), but host_interface.c:2 includes "sillion_application.h", which needs Firmware/Application/Silion. The IDE Release build still cannot compile. (2) There is no -Wextra or -Werror in the firmware CFLAGS (Makefile:98-99); warnings-as-errors is only opt-in through EXTRA_CFLAGS. (3) There is no CI configuration in the tree. (4) Both .launch files still hard-code C:\Users\Identium\... (RFID_Controller_F429.launch:42 and the new RFID_Controller_F411.launch:42). (5) The shipped update image was built from a dirty tree (rfid_controller_f411-v0.10.1-1-gfcae054-dirty.tlfw). Build outputs and the signing key ship inside the source zip. (6) The .cproject cpuclock is still 16.

**Remaining work.** 1. Fix or delete the CubeIDE Release configuration, so the IDE and the Makefile agree. 2. Add -Wextra, and -Werror in CI. 3. Add a CI job on Linux that builds both boards in both configurations and runs make hostcheck. 4. Stop shipping per-user .launch files and build outputs. 5. Build release images only from a clean, tagged tree. 6. Keep keys out of the source package.

#### FW-44 — No controller firmware version/identity; Silion naming throughout

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Improved. A hand-maintained mainboard version, 1.0.0.8, now exists (Firmware/Config/product_config.h:52-56). It is reported in the UDP discovery reply (Firmware/Services/configuration/discovery_service.c:55-58), in the module-bridge "VER" reply (module_bridge.c:490-493) and in the HTTP User-Agent (http_upload.c:691-698). The reader name is now a persisted setting (reader_settings.c:603). The MAC is derived from the MCU UID (reader_settings.c:229-237). FW_STATUS reports the version of the image in staging flash (host_interface.c:842-856; firmware_update.c:412-432). Still missing or wrong. (1) GET_VERSION and GET_SERIAL still return only the RFID module's values (host_interface.c:1352-1364 calls SILION_Application_GetVersion; :1372-1384 calls GetSerial). (2) No build-injected version or git hash is compiled into the application. The image version (v0.10.1-1-gfcae054-dirty, Makefile:165-170) exists only in the .tlfw header, and `strings` of build/f411-release/rfid_controller_f411.bin shows no "0.10". So the binary calls itself 1.0.0.8 while its update image calls it 0.10.1. (3) The heartbeat carries no version (identium_protocol.c:1029). (4) Discovery reports the board type as `DISCOVERY_BOARD_HC32F46X 1U` (discovery_service.c:13, :53), but the FW spec requires mainboard_type 0x10 (spec text section 7, "0x10 (INB-1)"). (5) The default reader name on the F411 board is still "RFID-F429-DEV" (product_config.h:22, reader_settings.c:273). (6) The USB descriptors are still ST's: VID 1155, "STMicroelectronics", "STM32 Virtual ComPort" (Firmware/USB_DEVICE/App/usbd_desc.c:65-69), and these strings are present in the F411 binary. (7) No controller serial number is reported on any host channel.

**Remaining work.** 1. Generate fw_version.h at build time (semver from git describe, hash, date, board) and use the same string for the .tlfw header. 2. Report it from GET_VERSION alongside the module version, and in the heartbeat and config replies. 3. Report a controller serial derived from UID_BASE in GET_SERIAL. 4. Use mainboard_type 0x10 where the Identium protocols carry it. 5. Default the reader name to an Identium, UID-based name. 6. Replace the ST USB VID/PID and strings with Identium ones.

#### REQ-01 — Most required I/O and connectivity features are missing from both the schematic and the firmware

`PARTIAL` · rev 2: HIGH → now: HIGH

**Rev 3 evidence.** Much of the I/O now exists on the real board. Firmware/BSP/f411_custom/board_config.h:121-153 matches the 13-Oct netlist. I traced the GPIs with net.py: CN10.1→R57→Q6→R44→PB3, CN10.2→R56→Q7→R43→PA15, CN10.3→R55→Q8→R42→PB4 and CN5.1→R58→Q5→R41→PB15, which equals INPUT1-4. Active-low inversion is handled (board_config.h:124, industrial_io.c:46). There are 3 relays on PB2/PB10/PA0, and OUTPUT4 is NULL (:137-144). The PC13 buzzer is driven by GPIO_Service_Beep (gpio_service.c:102-117). The shared RS-232/RS-485 port uses select PB14 and DE/RE on PB12/PB13 (board_config.h:41-67; Serial_Port_Init is called at firmware_configuration.c:373). Wi-Fi runs through the ESP32-C3 over I2C (WiFi_Service_Init at firmware_configuration.c:529). GPIO can also be controlled through the Reader Manager IOGET/IOSET/GIO/SIO commands (module_bridge.c:24-29) and through JSON set_gpo/get_gpi. Still missing. (1) Wiegand. The hardware now exists (PC14/PC15 go to CN2.3/CN2.4 with 10k pull-ups, nets WGDO/WGD1), but a case-insensitive grep for wiegand finds no code, and board_config.h:19-20 says the pins are not connected. The FW spec section 6 requires Wiegand-26/34/66 output, and the design spec requires every legacy host interface, Wiegand included, to be preserved. (2) There is no GPI debounce. GPIO_Service_Task compares raw samples each pass (gpio_service.c:155-187), but the spec requires 10 ms. (3) The status LED is only switched on at board init (f411_custom/board.c:212-222). There is no read or status indication, and the board has one LED (PB8) where the spec lists Power, Status and Read LEDs. (4) There is no BLE code; the ESP32-C3 is used for Wi-Fi only. (5) There is no GPIO-readable reset button: KEY1 is NRST and KEY2 is BOOT0 (u7_pinmap pins 7 and 44). (6) The binary TCP-protocol GPI/GPO frames (0x02 / 0x16 / 0x17) are not implemented. (7) The design asks for 4 logical GPOs but the board has 3 relays.

**Remaining work.** 1. Implement Wiegand 26/34/66 emission on PC14/PC15, respecting their 3 mA sink-only limits, or add a driver stage in hardware. Make it configurable, default off. Fix the board_config.h comment. 2. Add 10 ms GPI debounce. 3. Drive the LED as a status/read indicator, and add LEDs or a GPIO reset button in the next board spin. 4. Document the 3-relay / 4-GPO mapping. 5. Implement BLE, or state that it is not offered. 6. Add the binary-protocol GPI report, get-GPI and set-GPO commands.

#### REQ-04 — Requirements traceability summary (firmware as delivered)

`PARTIAL` · rev 2: INFO → now: INFO

**Rev 3 evidence.** Newly implemented and reachable in rev 3: (1) the F411 target: Makefile:52-63, clock at Src/main.c:167-211, USB OTG_FS at usbd_conf.c:47-105, BSP in f411_custom/board.c. (2) Persisted settings for network, server IP/port, region, power, antenna, Gen2, name, upload mode and Wi-Fi (Firmware/Services/configuration/reader_settings.h:40-85), stored in F411 sector 5 (reader_settings.c:27-30). (3) A MAC derived from the MCU UID (reader_settings.c:229-237). (4) HTTP upload mode (http_upload.c; reader_settings.h:29-30) and Wi-Fi upload through the ESP32 (active_upload_transport.c). (5) An offline tag log on the GD25Q32 (tag_log.c). (6) Signed firmware update with a bootloader (host_interface.c:752-830; Bootloader/). (7) Reader Manager compatibility: UDP discovery on 15000 and a module bridge on 8080. Still open: (a) The Identium TCP Protocol V1.0 binary active mode is not implemented. A grep finds no 0x16/0x17/0x18 command handling; Ethernet upload sends JSON lines or HTTP. (b) Modbus RTU and Modbus-TCP are absent. The only hit is a CRC16-Modbus comment at host_transport.c:248, and TCP 8080 is still taken by the config port and bridge (configuration_port.h:7), where the spec puts Modbus-TCP. (c) Wiegand is absent (see REQ-01). (d) Ethernet has no DHCP: the discovery reply hard-codes reply[16]=0 (discovery_service.c:46), and DHCP exists only on Wi-Fi. The default IP is 192.168.150.50 (reader_settings.c:216-219), and the MAC is locally administered 02:1D:.. rather than an OUI. (e) India is still region 0x08 (silion_protocol.h:87, product_config.h:45). (f) The offline log has 65,280 slots: half of the 4 MB flash, 32-byte records, minus 2 sectors (tag_log.c:41-48, gd25q.h:25-26). That is below the >=100k EPC-128 requirement, and the lead's host test kept 65,376. (g) Time is not RTC-backed (FW-26). (h) mainboard_type 0x10 is not reported (FW-44). (i) Unauthenticated network control, together with the exposed signing key, undermines OTA (FW-24).

**Remaining work.** 1. Implement the TCP Protocol V1.0 binary active mode and Modbus RTU/TCP, and resolve the port-8080 conflict with the spec. 2. Add Wiegand. 3. Add Ethernet DHCP, an OUI-based MAC and the spec default IP. 4. Correct the India region code. 5. Raise the offline log to at least 100k EPC-128 records: shrink the record, rebalance the staging area, or use a larger flash. 6. Add SNTP or RTC time. 7. Report the firmware identity. 8. Authenticate network control and rotate the signing key. 9. Re-run this traceability matrix against the F411 build after these changes.

#### PRG-04 — Program assessment: TRL about 3-4

`PARTIAL` · rev 2: INFO → now: INFO

**Rev 3 evidence.** Paths are relative to 271026/_fw/src/STM32_Firmware/. Firmware sub-items have moved. (1) The firmware now builds for the F411. Makefile:52-61 (BOARD=f411: CHIP_ID 0x431, -DSTM32F411xE -DTARGET_BOARD=2, Boards/f411/STM32F411CEUX_FLASH.ld, Boards/f411/startup_stm32f411ceux.s). .cproject:176-242 adds an 'F411' configuration (MCU STM32F411CEUx, TARGET_BOARD=2). The shipped build/f411-release/rfid_controller_f411.map links the f411_custom objects. (2) The F411 BSP is resolved against the netlist. board_config.h:27-153 matches work/u7_pinmap.txt: USART1 PA9/PA10; SPI1 PA5-7; W5500 CS PA4, INT PB0, RST PB1; flash CS PA1; relays PB2/PB10/PA0; GPI PB3/PA15/PB4/PB15; ESP32 I2C PB6/PB7, EN PB9. (3) Settings storage moved to F411 sectors 5-7 (reader_settings.c:28-31, configuration_storage.c:9-18). (4) The 13-Oct board adds U6 GD25Q32 on PA1 (u7_pinmap pin 11), so offline-storage hardware now exists. Still open at program level: the tag log keeps about 65k records (<100k) and is never drained automatically (FW-01). The production signing private key ships at keys/fw_signing_key.pem (lead-confirmed). FW-12 is unchanged. There is no Modbus protocol and no Wiegand code (grep finds only a CRC16-Modbus comment, host_transport.c:248). There is no 4th output (board_config.h:143-144, OUTPUT4 NULL) and no IP-reset key (KEY1=NRST pin 7, KEY2=BOOT0 pin 44). The 271026 drop has no on-hardware bring-up record. The shipped image was built from a dirty tree. Hardware gates (24 V chain etc.) belong to IDM-AUD-003 and were not re-verified here.

**Remaining work.** The verdict moves to TRL about 4: the firmware builds for the target, but on-hardware validation is unverified. Still NO-GO for pilot. Rotate the exposed signing key and stop shipping keys/. Close FW-01 (capacity and automatic drain) and FW-12. Implement the specified Modbus and Wiegand outputs, or formally descope them. Clear the IDM-AUD-003 hardware gates. Deliver a bring-up and test report on the 13-Oct board. Release only from a clean tagged tree.

#### REL-04 — No git history, no tests or CI, no firmware version, build outputs shipped, messy tree

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Improved: host tests are now delivered: hostcheck/identium_protocol_host_test.c, tag_log_host_test.c, boot_image_host_test.c and firmware_update_host_test.c. The lead built and ran them and all PASS. There is a make hostcheck target (Makefile:217-238) and 9 Python test tools in Tools/. A version string now lives in the update-image header (Common/firmware_image.h:61, char version[32]). It is filled from git describe (Makefile:165-170), and FW_STATUS reports the staged image's version (host_interface.c:760, 842-845). REGRESSED sub-item: build outputs ship again: build/ (152 MB), Debug/ (77 MB), F411/ (47 MB), 508 .o files. Rev 2 had removed them. Still missing: no .git, .gitignore, README, CHANGELOG, Identium LICENSE, .editorconfig or CI config (find returns only the micro-ecc README/LICENSE). Tools/__pycache__/*.pyc is shipped. The running binary carries no version of its own: GET_VERSION reports only the module version (host_interface.c:1352-1363), and the mainboard version is the constant 1.0.0.8 (Config/product_config.h:53-56). The shipped update image is from a dirty tree: its header at offset 0x20 reads v0.10.1-1-gfcae054-dirty. The private signing key ships in keys/ (separate critical finding). Line endings are mixed: sillion_application.c is CRLF in rev 3 and LF in rev 2.

**Remaining work.** Deliver the source as a git repo with a .gitignore excluding build/, Debug/, F411/, keys/ and __pycache__. Embed the version and build hash in the application binary and report them via GET_VERSION, the Identium heartbeat and discovery. Build release images only from clean tags. Add a README, CHANGELOG, licence headers and .editorconfig, then normalise line endings. Add CI that runs make BOARD=f411 CONFIG=release and make hostcheck with -Werror.

#### FW-01 — No non-volatile storage for the >=100k-tag offline buffer (hardware and firmware)

`PARTIAL` · rev 2: CRITICAL → now: CRITICAL

**Rev 3 evidence.** Hardware and a log now exist. The 13-Oct board has U6 GD25Q32 (4 MB) on SPI1 with CS on PA1 (work/u7_pinmap.txt pin 11; board_config.h:108-110). There is a driver (Devices/spi_flash/gd25q.c) and a ring log (Services/storage/tag_log.c) with 32-byte records, a sequence number, a 48-bit time and a CRC16 (tag_log.c:16-36). Every tag is logged: SILION_Application_PublishTag calls TagLog_Append (sillion_application.c:25-31), and TagLog_Task runs in the main loop (main.c:149). Still not met: (a) Capacity is below 100k. Only the lower 2 MB is used (tag_log.c:38-40), giving a minimum of (512-2)x128 = 65,280 records (tag_log.c:47-48). The lead's host test kept 65,376 after 300k tags. The upper 2 MB is reserved for firmware staging (firmware_image.h:98-99), although the app slot is only 112 KB (Boards/f411/STM32F411CEUX_FLASH.ld:51). (b) There is no automatic drain to Identium hosts. The Identium upload path uses a 256-tag RAM queue that drops the oldest tag (identium_protocol.c:63, :472-479) and expires tags after clear_buf_time (:430-446). Nothing in Protocol/, Services/upload or Services/ethernet references TagLog. client_ack is forced to 0 (configuration_storage.c:133-135). The flash log can only be pulled with the legacy ASCII TAGLOG_READ, 20 records per command (host_interface.c:192-300), and the host has to track the sequence number itself. The spec requires automatic drain on reconnect (IDM-INB1-FW-001 section 5, p.7) and ack-then-delete (section 5.4, p.8). Lead's slow-HTTP test: 0 uploaded, 1744 dropped, 256 expired. (c) The 64-record RAM queue drops reads when full (tag_log.c:52, :482-485), and queued reads are lost on power-off.

**Remaining work.** Raise log capacity to at least 100k EPC-128 records. For example, cut staging to about 256 KB, which leaves about 3.75 MB, or about 120k records; or fit the larger flash the spec calls for. Implement a drain state machine that replays the log to the Identium TCP/HTTP host on reconnect, with an ack-advanced read cursor persisted in flash. Route RAM-queue overflow and expiry to the log instead of dropping. Flush pending records on brown-out. Prove 100k offline reads and a full drain on real hardware.

#### FW-06 — Any boot-time init failure hangs the MCU forever in Error_Handler before the watchdog is armed

`PARTIAL` · rev 2: HIGH → now: LOW

**Rev 3 evidence.** Mostly addressed. The IWDG now starts before any peripheral or external chip, and a failed start is degraded, not fatal (firmware_configuration.c:290-305). Failures of the W5500, the network apply, Ethernet init and the RFID module now degrade instead of returning STATUS_ERROR (:476-490, :502-510, :516-526, :542-550). The RFID module is retried every 10 s (:239-268). The CPU fault handlers save a record and reset at once (stm32f4xx_it.c:83-150 via FAULT_RECORD_ENTER; fault_record.c:147-178, NVIC_SystemReset). What remains: Error_Handler still records the fault (main.c:278) and then spins with IRQs disabled (:279-282). It relies on the IWDG, about 22-62 s (watchdog.h:24-28). A clock-setup failure (HSE/PLL, main.c:194-197 and 208-211) happens before the IWDG starts, so the unit halts for good; the code's own comment says so (main.c:274-277). There is no CSS or HSI fallback. A USB init failure still calls Error_Handler (usb_device.c:73-88, usbd_conf.c:277), which means a watchdog reboot loop on every boot. Industrial_IO_Init can still return STATUS_ERROR (firmware_configuration.c:333-337), but on the F411 only a NULL input port can fail it (industrial_io.c:140-145), so this is not reachable at run time. W5500_Init always returns 1 after the reset pulse (w5500.c:239-288). W5500_ReadVersion (w5500.c:423) is never called, and the W5500 is never re-initialised at run time.

**Remaining work.** Make Error_Handler record the fault and then call NVIC_SystemReset. Fall back to HSI when the HSE fails, running degraded without USB, or enable CSS. Make USB init failures non-fatal. Add a W5500 VERSIONR==0x04 presence check and a periodic re-init while Ethernet is degraded.

#### FW-08 — Hard-coded multicast MAC 11:22:33:44:55:66 and static IP on every unit, with no DHCP and no persistence

`PARTIAL` · rev 2: HIGH → now: MEDIUM

**Rev 3 evidence.** Fixed: the live network settings now come from persistent settings. ReaderSettings_ApplyNetwork calls W5500_SetNetworkConfigValues(ip, mask, gw, mac) (reader_settings.c:591-599), from firmware_configuration.c:502-510. The old constant W5500_SetNetworkConfig, still hard-coded to 11:22:33:44:55:66 (w5500.c:299-391), is no longer called and the linker discards it (build/f411-release/rfid_controller_f411.map:735, in 'Discarded input sections'). The default MAC is locally administered unicast, 02:1D plus a CRC32 of the 96-bit UID (reader_settings.c:230-238). Multicast MACs are rejected (:316). Settings are CRC32-protected in F411 sector 5 (reader_settings.c:28-31, 292-307). They can be set from the vendor tools (vendor_config.c:1118-1135). UDP discovery on port 15000 reports the MAC and IP (discovery_service.c:10-60), and the static JSON reports the live MAC (configuration_service.c:1129-1130). Still open: there is no Ethernet DHCP; the code itself says it is 'not supported by this firmware' (vendor_config.c:1118-1119). The default IP is still 192.168.150.50/24 with gateway 192.168.150.1 (reader_settings.c:215-228), not the Identium 192.168.1.100 (FW spec p.4 example reader_name 'Identium_reader/192.168.1.100'). The board has no IP-reset key (KEY1=NRST, KEY2=BOOT0; u7_pinmap pins 7/44) and the firmware has no factory-reset path. The MAC is not IEEE-assigned and is a 32-bit hash, so collisions across a large fleet are possible but unlikely. The dead W5500_SetNetworkConfig is still in the source.

**Remaining work.** Add a W5500 DHCP client with a static fallback. Change the default to the Identium 192.168.1.100/24 (gateway 192.168.1.254), or document the 192.168.150.x default. Provide a reset-to-defaults path in firmware, since the board has no button for it. Plan IEEE OUI/MA-S MACs for production. Delete W5500_SetNetworkConfig.

#### FW-18 — Identium SDK and RFID Manager reach the reader as a TCP client on port 8080 and send raw 0xFF module frames; board listens only on 5000

`PARTIAL` · rev 2: HIGH → now: LOW

**Rev 3 evidence.** The main defect is fixed in reachable code. Port 8080 (configuration_port.h:7) now picks the protocol from the first byte of each connection. 0xEE starts a one-shot config request. Any other first byte starts a persistent module-bridge session through Module_Bridge_Begin (configuration_port.c:293-301), and the session is then served by Module_Bridge_Task (:259-261). In module_bridge.c, 0xFF frames are sized as LEN+5 (:365), so the 5-byte FF 00 03 1D 0C no longer stalls, and they are forwarded to the module UART through SILION_Application_BridgeSend (:386; sillion_application.c:2495-2529, raw mode at :2446-2448). Module bytes are relayed back with frame-boundary tracking (:536-610). The 32 x FF reset burst is dropped (:359-362). Configuration_Port_Task runs in the main loop (Src/main.c:138). The ASCII server is also back on TCP 5000 (host_tcp_server.c:24, Firmware/Config/product_config.h:13), and a UDP 15000 discovery responder exists (discovery_service.c:10, :63-100). The same bridge is available on USB CDC and RS-232/485 (host_transport.c:273-308). Three things remain. (a) Nothing listens on 8090: grep finds no 8090 anywhere. Rev-1 FW-18 found that the SDK's RebootReader connects to <ip>:8090 and that the SDK falls back to 8090. (b) 8080 is a single W5500 socket (socket 1), so a second connection, for example an EE request opened while a bridge session is up, cannot be accepted. Whether the vendor tools ever do this is unverified. (c) Nothing in the drop shows an acceptance run against the unmodified RFID Manager or SDK. There is only a script, Tools/reader_manager_connect_test.py, which was not run.

**Remaining work.** (1) Add an 8090 listener for the RebootReader frame (FF 00 00 40 CC DC) and the SL-board fallback, or document that the API is unsupported. (2) Either add a second 8080-capable socket or confirm that the tools never open concurrent 8080 connections. (3) Bench acceptance: run the unmodified RFID Manager and the Java and C# SDK demos against an F411 unit over TCP 8080, USB and RS-232/485. Cover inventory, read/write and reconnect.

#### FW-19 — The SDK sends in-band ASCII board commands (IOGET, IOSET/SIO, IPGET/IPSET, VER/INITUPFV) and an identity probe; none exist

`PARTIAL` · rev 2: HIGH → now: LOW

**Rev 3 evidence.** The legacy board verbs are now emulated in reachable code. module_bridge.c handles IOGET, which returns 'IOGET' plus the GPI bitmap. The 6-byte echoed IOGET was one of the replies rev-1 FW-19 found the SDK identity probe accepts, so no Silion 'SLPO' string is sent. The other verbs handled are IOSET (8-byte, replies IOSETOK), IPGET (25-byte reply with the MAC at offset 17), IPSET (saved to ReaderSettings, applied after 1.5 s), GIO, SIO (async-frame replies) and VER (4-byte mainboard version 1.0.0.8, product_config.h:53-56). The handlers are at :396-503 and the IPGET/IPSET functions at :296-345. These verbs work on TCP 8080 (configuration_port.c:293-301) and, at line start, on USB and RS-232/485 (host_transport.c:90-93 token list, :273-308). GPIO calls are mapped to GPIO_Service/Industrial_IO (:250-294). Remaining gaps: (a) INITUPFV (board firmware upgrade via the vendor tool), CONFIGRX/WX and SM6E are silently consumed byte by byte (:505-507), so the tool times out instead of getting a clear refusal. Board updates go through the Identium FW_BEGIN/FW_DATA path instead (host_interface.c:764-806). (b) The module power-off/on board verbs used by one SDK loader overload (rev-1 FW-19 verifier note) are not handled. (c) The compatibility decision is not documented in the drop. board_config.h:21 refers to docs/notes/..., but no docs/ folder ships. (d) The reply formats came from reverse engineering. No captured SLD1030/UMB1230 session or bench run against the unmodified SDK is in evidence, so byte-exactness is unverified.

**Remaining work.** (1) Reply to INITUPFV and the other unsupported verbs with an explicit error, or document that they are unsupported, so tools do not hang. (2) Decide on the module power-off/on verbs (the hardware cannot switch module power, see FW-20). (3) Write down the SDK compatibility strategy and the identity reply choice. (4) Bench-verify byte-exact replies with the unmodified RFID Manager and the Java/C# SDKs, covering GetGPI, SetGPO, IP get/set and detection, on TCP and serial.

#### FW-20 — Module firmware can no longer be upgraded or recovered in the field; board refuses to boot if module not in app image

`PARTIAL` · rev 2: HIGH → now: MEDIUM

**Rev 3 evidence.** Two of the three rev-2 asks are now implemented. (1) A module pass-through exists. A TCP 8080 session, or a USB/RS-232/485 link, takes the module through SILION_Application_BridgeAcquire (sillion_application.c:2422-2451). That call stops inventory and sets raw mode, so the local Silion parser is suspended. The bridge then forwards frames unchanged (module_bridge.c:349-391, :536-610; host_transport.c:273-308). (2) Boot now degrades gracefully. If Firmware_Configuration_StartReader(3U) fails, firmware_configuration.c:542-550 sets ERROR_RFID_INIT and SYSTEM_DEGRADED_RFID instead of calling Error_Handler. Ethernet, USB and the bridge keep running, and Firmware_Configuration_Task (:239-268, called from main.c:143) retries every 10 s, but only while the module is idle, so it does not interfere with a bridge session. Still open: (a) Nothing follows the module set-baud opcode 0x06. USART_SetBaudRate is called only at init (Firmware/Drivers/uart/uart.c:120), and the RFID UART is fixed at PRODUCT_RFID_BAUDRATE 115200 (product_config.h:36). The SDK detector's set-baud frame selects 115200 (rev-1 FW-18 evidence), so only a loader that picks another rate would break; that is unverified. (b) The loader overload that power-cycles the module uses board verbs the bridge drops (module_bridge.c:505-507). The hardware cannot power-cycle the module either: board_config.h:25-26 says module EN is tied to 3V3, and the netlist shows CN1.5 going only to R67. (c) SILION_Application_BridgeRelease (sillion_application.c:2453-2480) only stops and restarts inventory. It does not re-run ConfigureReader, so a module that was reset or upgraded during a session runs with whatever RF settings it then holds until the board reboots. This is a risk; it is unverified on hardware. (d) Error_Handler still halts with IRQs disabled (Src/main.c:279-282), and only the watchdog recovers it, after about 22-62 s.

**Remaining work.** (1) Follow a 0x06 set-baud by calling USART_SetBaudRate after the module acknowledges, or document that loaders must stay at 115200. (2) Decide how the power-cycle loader overload is handled; module power is not switchable in hardware. (3) Re-run ConfigureReader, SetSession and Gen2 after a bridge release, or at least after detecting a module reset or bootloader phase (0x0C), before resuming inventory. (4) Make Error_Handler log and call NVIC_SystemReset. (5) Bench test a full module firmware upgrade and a recovery from a corrupted module app over TCP 8080, USB and RS-232/485 with the unmodified SDK loaders and RFID Manager.

#### REQ-02 — The parity claims omit Wiegand and the IP/Wi-Fi reset button, which the SLD1030 and UMB1230 have and this board lacks

`PARTIAL` · rev 2: HIGH → now: HIGH

**Rev 3 evidence.** Hardware (13-Oct board, which is the 271026 hardware): a Wiegand port now exists. net.py shows net WGDO = U7.3 (PC14), CN2.3 and R71 (10k to 3V3), and net WGD1 = U7.4 (PC15), CN2.4 and R69. Each net has only the connector, the pull-up and the MCU pin: no ESD protection or buffer. Per LEAD_FACTS, these backup-domain pins are limited to 3 mA sink and 2 MHz. There is still no IP/Wi-Fi reset button: KEY1.4 is on NRST (U7.7) and KEY2.4 is on BOOT0 (U7.44). Firmware: a grep for wiegand|WGD|PC14|PC15|factory|long press over Firmware, Src, Inc and Boards finds only board_config.h:20, which wrongly says 'PC14/PC15 not connected'. There is no Wiegand encoder, pin definition or configuration. Spec IDM-INB1-FW-001 section 6 requires Wiegand-26/34/66 output mode, and its acceptance step 6 is 'Wiegand emission verified on scope' (pdftotext lines 546, 797). There is no reset-input handling either; industrial_io.c handles only 4 GPI and 3 relays. A partial workaround exists for a wrong IP. Network settings are now persistent, IPSET through the bridge saves ip/mask/gw/mac (module_bridge.c:314-345), the bridge also works over USB and RS-232 (host_transport.c:273-308), and UDP 15000 discovery replies with the current IP (discovery_service.c:63-100). With physical USB access, a misconfigured unit can therefore be recovered without a button.

**Remaining work.** Firmware: (1) Write a Wiegand 26/34/66 encoder on PC14/PC15. Drive the pins open-drain within the backup-domain limits, use timer-driven pulses of about 50-100 us at about 1-2 ms spacing, make the format configurable and default it to off, and feed each read tag to it. (2) Add BOARD_WIEGAND_* defines and correct the comment at board_config.h:19-20. (3) Verify on a scope (spec acceptance step 6). Hardware/respin: (4) Add ESD protection and a buffer or level-shifting on CN2. (5) Either add a dedicated IP/Wi-Fi reset input, or formally accept the USB + discovery recovery procedure in place of a button and update the parity table.

#### FW-25 — F411 board_config.h pin map contradicts the schematic's U7 pin assignments

`PARTIAL` · rev 2: MEDIUM → now: LOW

**Rev 3 evidence.** Firmware/BSP/f411_custom/board_config.h was rewritten. Every signal it maps now matches the 13-Oct netlist (271026/_fw/work/u7_pinmap.txt plus net.py traces). RFID is on USART1 PA9/PA10 (:27-31; U7.30/31 to CN1.9/10). PB5 (CN1.11 with R32 pull-up) is left as an input (:38-39). Host serial is USART2 PA2/PA3 (:43-47), with mux PB14 (:52-53), DE PB12 (:56-57), RE# PB13 (:61-62) and the RS-232 load switch on PA8 (:66-67). The ESP32 is on PB6/PB7 with EN on PB9 (:76-83). W5500 is on PA4/PB0/PB1 with SPI1, and flash CS is PA1 (:90-110). VBUS sense is NULL (:118-119). GPI1-4 are PB3, PA15, PB4 and PB15 (:125-132); these trace through Q6, Q7, Q8 and Q5 to CN10.1, CN10.2, CN10.3 and CN5.1, are active-low (:124), and have 10k pull-ups R46/R47/R60/R62 to 3V3. The relays are PB2 (Q3, U4, CN12), PB10 (Q4, U9, CN13) and PA0 (Q2, U16, CN14) (:137-142), and output 4 is NULL (:143-144). The LED is PB8 (:148-149) and the buzzer PC13 (R66 to Q1, :152-153). industrial_io.c inverts active-low inputs (:46-50) and skips output 4 (:120-127, :169-173). Inputs are set with MODER = input (:99; gpio.c:60-61), which releases the JTAG function on PA15/PB3/PB4. An F411 board.c exists, and the F411 build is real: Makefile:52-61 passes -DTARGET_BOARD=2, and the Board_F411_* and Industrial_IO_* symbols are linked in build/f411-release/rfid_controller_f411.map:1916-1956. One residual contradiction remains: board_config.h:19-20 says 'No LSE (PC14/PC15 not connected)', but the netlist puts Wiegand WGDO and WGD1 on PC14/PC15 (CN2.3/CN2.4, R71/R69). The file has no Wiegand pin defines. Firmware/Config/firmware_config.h:9-11 still defaults TARGET_BOARD to F429 when the macro is not passed.

**Remaining work.** (1) Correct the PC14/PC15 comment and add BOARD_WIEGAND_D0/D1 defines (PC14/PC15). (2) Consider defaulting TARGET_BOARD to F411, or failing the build when it is not given, so an IDE build cannot silently target the Discovery. (3) Add the netlist-diff CI check and a power-up I/O self-test that rev 2 asked for.

#### FW-28 — Watchdog is fed inside RFID wait loops with a 22-62 s timeout; faults are not reported

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Fault reporting has improved. The HardFault, MemManage, Bus, Usage and NMI handlers now use FAULT_RECORD_ENTER (Src/stm32f4xx_it.c:85, :100). That macro saves the stacked PC, LR, xPSR, CFSR, HFSR and the fault address registers into RAM that survives a reset, then calls NVIC_SystemReset (fault_record.c:170-178). Fault_Record_Init runs at boot (main.c:113). GET_DIAG (host_interface.c:1247; diagnostics.c:102-139) reports the reset cause (including IWDG), the last fault record and the per-task maximum loop times. The watchdog design itself is unchanged. Watchdog_Refresh() is still called inside SILION_DelayMs (sillion_application_port.c:421), SILION_WaitForResponse (:446) and SILION_WaitForTxComplete (:502). The timeout is unchanged: PRESCALER 6U and RELOAD 0x0FFFU (watchdog.h:37-38), about 22-62 s. There is still a single unsupervised refresh at Src/main.c:158. loop_monitor.h only measures tasks (LOOP_MONITOR_RUN at :50-56); no per-task check-in gates the refresh. A grep finds no DBGMCU freeze. SINGLE_POLL and SYNC_POLL still accept timeouts up to 65535 ms (host_interface.c:1054-1057, :1171). Error_Handler records its caller but still runs `__disable_irq(); while(1)` (main.c:278-282), so only the watchdog recovers it. Faults are not carried in the Identium heartbeat, which holds only reader_name and a counter (identium_protocol.c:1026-1031). Spec IDM-INB1-FW-001 acceptance item 13 requires the IWDG to reset if any task stalls for more than 5 s (pdftotext line 843). That item is not met.

**Remaining work.** (1) Remove Watchdog_Refresh from the three SILION wait helpers. (2) Add per-task alive flags and refresh only when every task has checked in. (3) Cap or remove the 65535 ms SINGLE_POLL/SYNC_POLL waits, or make the transactions non-blocking. (4) Lower the IWDG timeout to about 1-4 s so the 5 s stall criterion is met. (5) Set the DBGMCU APB1 IWDG freeze in debug builds. (6) Make Error_Handler reset after recording. (7) Put the reset cause and fault record into the Identium heartbeat or a fault event and into the HTTP heartbeat, not only into GET_DIAG.

#### FW-29 — Reset cause tests BORRSTF before PORRSTF; fault handlers capture nothing

`PARTIAL` · rev 2: MEDIUM → now: LOW

**Rev 3 evidence.** Paths are relative to 271026/_fw/src/STM32_Firmware. (1) The boot order is fixed. Firmware/Services/system/reset_cause.c:82-99 now checks PORRSTF before BORRSTF; the comment at :83-84 notes that a POR also sets BORRSTF. PINRSTF is checked only after IWDG/WWDG/SFT/POR/BOR (:100-106). (2) The fault handlers are real now. Src/stm32f4xx_it.c:62-66 declares NMI/HardFault/MemManage/BusFault/UsageFault naked, and each calls FAULT_RECORD_ENTER (:85, :100, :115, :130, :145). That macro (Firmware/Services/system/fault_record.h:87-98) picks MSP or PSP, switches to a private stack and branches to Fault_Record_Capture. Capture (fault_record.c:147-183) saves the stacked LR/PC/xPSR when the frame is valid (:158-173) and CFSR/HFSR/MMFAR/BFAR plus uptime (:59-63), then calls NVIC_SystemReset (:177-178). Fault_Record_Init (:73-99) takes the record over and enables the separate MemManage/Bus/Usage faults (:96-98); it is called first thing at Src/main.c:113. (3) .noinit exists. Boards/f411/STM32F411CEUX_FLASH.ld:173-179 defines it NOLOAD, and the store lives there (fault_record.c:19). The shipped map places .noinit at 0x20018570, size 0x38 (build/f411-release/rfid_controller_f411.map:4583). That is below the bootloader's RAM at 0x2001E000 (Bootloader/bootloader_f411.ld:8), and the bootloader leaves RCC->CSR alone (Bootloader/boot_main.c:16-18). (4) Reporting: GET_DIAG (Firmware/Application/Host/host_interface.c:1247-1257) prints the reset cause, the fault count and the last fault's PC/LR/CFSR/HFSR/MMFAR/BFAR (Firmware/Services/system/diagnostics.c:99-125). It is reachable over USB, serial, TCP 5000 and Wi-Fi. Still missing: Identium_Protocol_ReportReaderException (Firmware/Protocol/identium_protocol.c:1155-1161) has no caller anywhere, so no reader_exception / 0x05 fault event is ever pushed to the TCP or HTTP host. The raw RCC flags (Reset_Cause_GetRawFlags, reset_cause.c:148) are not in DIAG. LPWRRSTF is tested after PINRSTF (reset_cause.c:100-113), so a low-power reset would read as PIN. The record does not survive a power cycle and is not persisted to flash (fault_record.h:13-14). Error_Handler still halts with IRQs off (Src/main.c:271-283); its own comment (:274-277) says the reader stays halted if this happens before the watchdog starts.

**Remaining work.** Call Identium_Protocol_ReportReaderException, or a 0x05 fault event, after boot when the previous run ended in a fault, watchdog reset or brown-out, and for runtime degradations. Add the raw RCC flags to GET_DIAG. Classify LPWRRSTF before PINRSTF. Optionally copy the last fault and reset record to flash so it survives power loss. Make Error_Handler reset (NVIC_SystemReset after the record is saved) instead of halting when it runs before Watchdog_Init.

#### FW-32 — Module health, diagnostics and performance commands the product needs are not implemented

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Improved: the RF settings (region, read/write power, TX/RX antenna, Gen2 profile/target/Q) are now saved in MCU flash (Firmware/Services/configuration/reader_settings.h:45-53, :83-91) and applied to the module configuration at every boot (Firmware/Application/configuration/firmware_configuration.c:451-464). Because the module's EN is tied to 3V3 (Firmware/BSP/f411_custom/board_config.h:25-26), this covers the intent of AA40 permanent-save, except the power-on baud rate. A new transparent module bridge forwards any FF frame from Reader Manager over TCP 8080, USB CDC or serial (Firmware/Services/configuration/module_bridge.h:6-13, :27-31; module_bridge.c:341-391). PC tools can therefore run VSWR scans themselves (Firmware/Services/host/host_transport.c:174-178). Module firmware upgrade through the bridge is unverified: the bridge has no baud-rate handling (grep -i baud in module_bridge.c finds nothing). Not fixed in the firmware: silion_protocol.c and .h are byte-identical to rev 2, and nothing in the tree contains AA4A, CC33, AA40, AA58, FASTID or upgrade commands (grep). The firmware does no antenna, return-loss or temperature monitoring of its own. SILION_Application_GetTemperature still refuses while inventory runs (sillion_application.c:690-691), and its only caller is host_interface.c:1400. SILION_StartAsyncInventory still takes only the handle (silion_protocol.h:505), so the async heartbeat flag (BIT7) is never set or supervised. Tag reads still hard-code access password 00000000 (silion_protocol.c:1585-1595). SILION_GetGPO still builds its frame with SILION_CMD_SET_GPO (silion_protocol.c:1043), and it, SetGPO and GetGPI have no callers.

**Remaining work.** 1. Add firmware-side health monitoring: AA48 with CC33 0x01 during inventory, or periodic AA4A while idle. Feed antenna-disconnect, high return-loss and over-temperature results into reader_exception / 0x05 events and GET_DIAG. 2. Report temperature during inventory, or allow the read. 3. Set and supervise the module async heartbeat flag. 4. Persist or apply the module power-on baud rate if it is ever changed. 5. Verify module firmware upgrade end to end through the bridge, including any baud switch, or implement §9.9 and 0x01/0x08/0x09 in firmware. 6. Make the tag-read access password configurable. 7. Delete or fix SILION_GetGPO, SetGPO and GetGPI. 8. Optional: EX fast inventory, FASTID, 0x2A, 0x2D.

#### FW-35 — W5500 socket driver skips datasheet-required handshakes

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Fixed: a SEND_OK/TIMEOUT wait before the next SEND, with the flags cleared afterwards, now exists for socket 0 (Firmware/Devices/w5500/w5500.c:942-987, called at :1009), socket 1 (:1399-1455), socket 2 UDP (:1734-1751) and socket 3, where the check is non-blocking (:1900-1923). Socket 0 also gets TCP keep-alive (KPALVTR = 6, :659, :705). JSON lines are no longer split: the full length is checked against TX_FSR first (Firmware/Services/ethernet/ethernet_service.c:288-296). Not fixed: (a) TX_FSR and RX_RSR are still read once, never until two reads match (w5500.c:754-775, :914-930, :1273-1287; sockets 2 and 3 at :1635 and :1844). (b) CLOSE_WAIT is still answered with CLOSE, not DISCON, and unread RX is thrown away, at ethernet_service.c:183-185 (S0), configuration_port.c:344-353 (S1) and host_tcp_server.c:103-105 (S3). No DISCON command appears anywhere. (c) Sockets can still stick in INIT. W5500_StartTCPClient can return 0 with the socket left in INIT (w5500.c:717-745), and Ethernet_Service_Task then returns on every non-CLOSED state forever (ethernet_service.c:191-194). W5500_StartTCPServer1 can leave INIT the same way (w5500.c:1177-1270), and configuration_port.c:382-384 only clears g_started. Only socket 3 recovers from INIT (host_tcp_server.c:108-117). (d) The SPI TXE/RXNE waits are unbounded (w5500.c:69-84), and W5500_ReadRegisters always returns 1 (:156-190). (e) If no SEND_OK or TIMEOUT arrives within the 50 polls, a new SEND is issued anyway (w5500.c:966-986), and a TIMEOUT never triggers a close and reopen.

**Remaining work.** Read Sn_TX_FSR and Sn_RX_RSR until two reads match. On CLOSE_WAIT, drain RX into the parser, then issue DISCON, with CLOSE only as a timed fallback, for S0, S1 and S3. Time out INIT and other stuck non-CLOSED/LISTEN/ESTABLISHED states on S0 and S1 and close and reopen them (ethernet_service.c:191, configuration_port.c:382). Bound the SPI flag waits and return failure from register access. On a SEND timeout, close and reopen the socket instead of issuing another SEND.

#### FW-36 — No WiFi/BLE firmware; ESP32-C3 UART pins do not match stock ESP-AT; boot strap issues

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Improved: the STM32-side Wi-Fi stack now exists for the board's actual I2C wiring. Firmware/Devices/esp32/esp32_link.c (688 lines) and Firmware/Devices/esp32/esp32_link_protocol.h:39-50 define HELLO, STATUS, WIFI_CONFIG/CONNECT, SCAN and SOCK_OPEN/SEND/RECV, with a CRC-16 framed I2C link. Firmware/Services/wifi/wifi_service.c is initialised at firmware_configuration.c:528-529, polled at Src/main.c:137 and present in the F411 release map. The F411 BSP now matches the 13-Oct board: I2C1 on PB6/PB7 and ESP EN on PB9 (Firmware/BSP/f411_custom/board_config.h:72-87), with the host USART moved to USART2 on PA2/PA3 (:41-48), so the rev 2 PB6/PB7/PB9 clash is gone. An F411 build target exists (Makefile:52-63). The ESP-AT UART-pin premise is obsolete because the link is I2C (LEAD_FACTS pin map). Not fixed: the ESP32-C3 firmware is not in the delivery. esp32_link_protocol.h:6 says the header is shared with an ESP-IDF project, but STM32_Firmware.zip contains no such project. Without it the Wi-Fi path cannot work or be tested. There is no BLE support at all: grep for ble/bluetooth in Firmware, Src, Inc and Tools finds nothing, and the link protocol has no BLE command. There is no ESP32 firmware-update path (no OTA command in esp32_link_protocol.h:39-50). The status LED is defined (board_config.h:148-149) but never used, so there is no Wi-Fi link indication. IO9 (strap) is not under MCU control, which is a hardware limit (board_config.h:73-74). Context: the Identium firmware spec lists the WiFi/BT module as DNP (IDM-INB1-FW-001 p.3), so Wi-Fi is optional for the product.

**Remaining work.** Deliver the ESP-IDF companion firmware (I2C slave at 0x42, link protocol v1) in source with build instructions, and bench-test STA, AP, host socket and upload socket on the F411 board. Decide whether BLE provisioning or configuration is required; if so, add it to the link protocol and both firmwares. Define how the ESP32 is updated in the field: ESP OTA forwarded over the link, or a documented factory-only UART flash. Use the status LED for link state. Hardware: wire IO9 to a GPIO on the next spin if the MCU must re-flash the ESP32.

#### FW-37 — No readout protection, secure boot, image-integrity check or key storage

`PARTIAL` · rev 2: MEDIUM → now: CRITICAL

**Rev 3 evidence.** Improved: there is now a sector-0 bootloader (Bootloader/boot_main.c:1-19; Bootloader/bootloader_f411.ld), and the application moved to 0x08004000 (Boards/f411/STM32F411CEUX_FLASH.ld:51). Staged images are checked for SHA-256 and an ECDSA P-256 signature with micro-ecc against a built-in public key (Bootloader/boot_image.c:27-50), and the SHA-256 is re-checked after the copy (boot_main.c:85-89). make protect sets RDP level 1 and the BOR level (Makefile:192-199), and GET_DIAG reports the RDP level (Firmware/Services/system/diagnostics.c:35-47, :150-154). Defeated: the ECDSA P-256 private key ships in the zip at keys/fw_signing_key.pem, and its public half equals g_bootPublicKey (Bootloader/boot_public_key.c:11). The lead auditor confirmed this in LEAD_FACTS. Makefile:164 makes that file the default SIGN_KEY. The update path is the unauthenticated ASCII command set FW_BEGIN/FW_DATA/FW_END/FW_APPLY (Firmware/Application/Host/host_interface.c:754-760, :764-851), which is reachable over TCP port 5000 (Firmware/Config/product_config.h:13; Firmware/Services/ethernet/host_tcp_server.c:20-24, :97). So anyone holding the zip can sign an image and install it on every unit over the LAN. That remote path did not exist in rev 2. Other gaps: the installed application is never verified at boot. Boot_StartApplication only sanity-checks the vector table (boot_main.c:118-131). If the post-copy SHA check fails, the bootloader still jumps to the partly written application (boot_main.c:106-107, :147). There is no anti-rollback: the version is a free string (Firmware/Common/firmware_image.h:62) and boot_image.c never compares it. The boot sector is not write-protected: no WRP anywhere, and make protect sets only RDP and BOR. No key-management or production-programming document is in the drop: the docs/ paths cited at boot_main.c:4 and board_config.h:18-21 are not shipped. The shipped .tlfw was built from a dirty tree (LEAD_FACTS).

**Remaining work.** Blocker. Generate a new P-256 key pair offline (HSM or air-gapped), never commit or zip the private key, and remove keys/ from the deliverable and from git history. Rebuild the bootloader with the new public key and treat the exposed key as revoked: re-flash every unit that carries it over SWD. Authenticate the FW_* commands, or restrict them to USB or a local link. Add a monotonic anti-rollback version. Verify the application hash at every boot, or at least never jump after a failed install. Add WRP on sector 0 to make protect. Write the production-programming procedure (RDP1, WRP, BOR) and the key-custody procedure. Build release images only from clean tagged trees.

#### FW-38 — No self-test, factory-test mode or diagnostic reporting, and no per-unit provisioning

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Improved: GET_DIAG now exists (host_interface.c:1247-1265). It reports the reset cause, uptime, fault count, last-fault registers, per-task and loop maximum times, tag drop/expiry counts, stack use, BOR level, RDP level and degraded parts (Firmware/Services/system/diagnostics.c:74-184). Reset-cause and fault getters are now called (diagnostics.c:102-112). A start-up degradation model replaces the old halt (firmware_configuration.c:294-300, :476-490, :542-558), and System_Fault_Clear now runs only when nothing is degraded (:555-558). The MAC is derived per unit from the MCU UID as a locally administered unicast address (Firmware/Services/configuration/reader_settings.c:231-238). The buzzer pin is assigned and driven (board_config.h:151-153; Firmware/BSP/industrial_io.c:254-268). Not fixed: there is no power-on self-test. W5500_ReadVersion (w5500.c:423-439) still has no caller, and W5500_Init always returns 1 (w5500.c:239-287). A missing or dead W5500 therefore never sets SYSTEM_DEGRADED_ETHERNET (firmware_configuration.c:476-490). There is no RAM test, no clock check and no application-image check at boot (see FW-37). There is no factory-test mode: grep for factory or selftest finds only a brown-out comment. BOARD_LED_STATUS is defined (board_config.h:148-149) but never used, so there are no LED fault codes. There is no Identium serial-number or OUI MAC provisioning: the MAC is a CRC32 hash of the UID under a 02:1D prefix, so collisions are possible, and the only serial number is the USB string taken from the UID (usbd_desc.c:322). The fault and reset record is RAM-only (fault_record.h:13-14). The System_Fault code itself is not exposed in DIAG, and no 0x05 or reader_exception event is sent (identium_protocol.c:1155-1161 has no caller).

**Remaining work.** Add a power-on self-test: check W5500 VERSIONR = 0x04 inside W5500_Init and set degraded ETH on failure, check the GD25Q JEDEC ID, the module version, a short RAM test, the clock (HSE ready / PLL lock) and the image hash, and show results on the status LED and buzzer. Add a factory-test mode, entered by strap or authenticated command, that exercises the relays, GPIs, LED, buzzer, RS-232/RS-485 loopback, Ethernet link, USB, Wi-Fi link and the RFID module. Add per-unit provisioning: an Identium serial number and an owned-OUI MAC written at production, read back over a command. Persist the last fault and reset cause to flash, and expose System_Fault codes and 0x05 events.

#### FW-39 — The SDK's USB path is USB-HID (VID 0x2E88 / PID 0x4605), not a COM port

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Improved: USB is now ported to the F411 OTG_FS on PA11/PA12 (Firmware/USB_DEVICE/Target/usbd_conf.c:78-104, :402-417, VBUS sensing off), with an exact 48 MHz clock from PLLQ = 4 (Src/main.c:177-192). The CDC port now also carries Reader Manager module-bridge sessions, the tools' COMx connection (Firmware/Services/configuration/module_bridge.h:27-31; host_transport.c:174-178; usbd_cdc_if.c:274), so vendor tools that connect by COM port can use USB. Not fixed: the device is still CDC-only. Firmware/USB_DEVICE/App/usb_device.c:73-81 registers only USBD_CDC, and Middlewares/.../Class contains only CDC. No HID interface exists, and neither usage page 0x8C nor the 65-byte report framing appears (grep for hid/2E88/4605 finds nothing). The descriptors are still ST's defaults: VID 1155 (0x0483), PID 22336 (0x5740), 'STMicroelectronics' and 'STM32 Virtual ComPort' (Firmware/USB_DEVICE/App/usbd_desc.c:65-69). Whether the Identium SDK 'usb' URI or the demo can use a COM port instead of HID is unverified.

**Remaining work.** Choose the USB personality: either a composite CDC + vendor-HID device with the legacy 64/65-byte framing, or an Identium SDK/demo build that uses CDC, and test it with the SDK and demo. Replace ST's VID/PID and strings with an Identium-owned or sublicensed VID/PID and Identium manufacturer and product strings. Do not reuse 0x2E88 without the owner's permission.

#### FW-40 — RFID Manager discovers readers by UDP broadcast to port 15000; firmware has no UDP socket

`PARTIAL` · rev 2: MEDIUM → now: LOW

**Rev 3 evidence.** Fixed: there is now a UDP discovery responder. W5500 socket 2 opens in UDP mode (Sn_MR = 0x02) on port 15000 (Firmware/Devices/w5500/w5500.c:1561, :1575-1612; Firmware/Services/configuration/discovery_service.c:10, :72-76). It parses the EE 06 .. 50 search frame with an optional target-MAC filter (discovery_service.c:80-97) and sends a 47-byte reply carrying MAC, IP, mask, gateway, DNS, config port, mainboard version and work mode (:32-61). It is polled every loop from Configuration_Port_Task (Firmware/Services/configuration/configuration_port.c:245; Src/main.c:138). The network configuration now comes from persisted settings (reader_settings.c:591-593; firmware_configuration.c:503). The MAC is unique per unit and no longer multicast (reader_settings.c:231-238). IPGET/IPSET are handled over the TCP 8080 bridge (module_bridge.c:31-32, :299-338, :426-436). Remaining: the factory default is still 192.168.150.50/24 with gateway 192.168.150.1 (reader_settings.c:216-229). The Utility Document V1.0 §2.1 (p.1) specifies 192.168.1.100 with gateway 192.168.1.254 and requires the PC to be on the same segment. The reply is unicast to the requester (discovery_service.c:100). For an off-subnet PC, such as one set up per the Utility doc, the W5500 would ARP the 192.168.150.1 gateway, so the reply would likely never arrive. That is inferred from W5500 routing, not bench-tested. Module type and module firmware version are left zero (reply bytes 36 and 42-45 are never set), and the DHCP flag is fixed at 0 (:46). The reply layout was derived from the tools' parser (comment :16-31), not from a capture of a legacy unit. It has not been verified against RFID Manager on hardware.

**Remaining work.** Align the factory default with the Utility doc (192.168.1.100/24, gateway .254), or update the doc. Consider broadcasting the reply when the requester is off-subnet. Fill in the module type and module firmware version. Bench-test RFID Manager IP search and IPSET against the F411 board, ideally compared with a capture from a legacy unit.

#### FW-60 — Memory footprint and headroom with no plan for future protocols

`PARTIAL` · rev 2: LOW → now: MEDIUM

**Rev 3 evidence.** Paths are relative to 271026/_fw/src/STM32_Firmware. Done in rev 3: - An F411 linker script: Boards/f411/STM32F411CEUX_FLASH.ld:47 (128K RAM, no CCM) and :51 (112K flash at 0x08004000). - An F411 build target (Makefile:53-63), with a map/ELF shipped in build/f411-release. - Stack sized and guarded (see FW-56). - Configuration moved off the F429-only sectors onto F411 sectors 5/6/7 (reader_settings.c:29-31, configuration_storage.c:15-18). - The offline buffer and OTA staging placed on the GD25Q32 (firmware_image.h:96-99, tag_log.c:38-40). What remains: (1) Headroom is thin. From the map: .text 0x14434 + .rodata 0x2264 + .isr_vector 0x198 + .data 0x178 = 92,584 B of 114,688 B (81%, about 21.6 KB free). RAM: .data + .bss 0x183F8 + .noinit = 99,752 B, plus the 16.5 KB heap/stack reserve, leaves about 11 KB free (map:1447-4597). The debug build already does not fit at -O0 (Makefile:7-8). Modbus, Wiegand and an E710 driver are still to come. (2) The flash map does the opposite of the rev 2 advice. The application is boxed into sectors 1-4 (112 KB), while sectors 5-7 (384 KB, 75% of internal flash) hold small settings records (firmware_image.h:19-21). (3) The SPI flash is split 2 MB for the tag log and 2 MB for staging (tag_log.c:40, firmware_image.h:98-99), yet the image is capped at 112 KB (firmware_image.h:95). With 32-byte records (tag_log.c:42), capacity is (512-2) x 128 = 65,280 records; the lead's host test kept 65,376. That is below 100k. With a 128 KB staging area the log would hold about (992-2) x 128 = 126,720. (4) About 37 KB of RAM goes to tag queues in front of the log, per map .bss: http_upload g_queue 12,288 B, g_request 8,736 B, identiumTagBatchBuffer 8,193 B and identiumTagQueue 8,192 B. (5) No written per-feature memory budget is in the drop. The only documents in the tree are the micro-ecc README and LICENSE. The capacity shortfall in (3) defeats a core goal; it belongs as critical under the tag-buffer finding.

**Remaining work.** 1. Shrink OTA staging to about 128-256 KB (it only has to hold a 112 KB image plus header) and give the tag log the rest. That reaches 100k or more EPC-128 records with the existing 32-byte format. 2. Rework the internal flash map: settings in the 16 KB sectors 1-3, the application in sectors 4-7 (448 KB). Otherwise, justify the 112 KB cap against the planned Modbus, Wiegand and E710 features. 3. Write a flash/RAM budget per feature and keep it in the release notes. 4. Consolidate the duplicated RAM tag queues. 5. Drop stdio/malloc (findfp pulls in _malloc_r) in favour of small formatters.

#### FW-63 — Cost estimate: a transparent pass-through mode is cheap to build on the existing code

`PARTIAL` · rev 2: INFO → now: INFO

**Rev 3 evidence.** Paths are relative to 271026/_fw/src/STM32_Firmware. Rev 3 implements most of the bridge: - Module frames (FF) are forwarded, and IOGET/IOSET/GIO/SIO/VER/IPGET/IPSET are answered locally (Services/configuration/module_bridge.c:18-41). This works on TCP 8080, demultiplexed from the EE config protocol by first byte (configuration_port.c:295), and on USB and RS-232/RS-485 through the COM-route mux (host_transport.c:55-70). - Bridge mode gates the Silion application through the SILION_APP_BRIDGE state and raw mode (sillion_application.c:2445-2448; sillion_application_port.c:243-246, :355-361). - A UDP 15000 discovery responder exists (discovery_service.c:10-60) on the new Socket2 (w5500.h:165-168), and TCP 5000 runs on Socket3 (:171-176). - Tools/reader_manager_connect_test.py exercises the bridge, but no recorded result was delivered. Not done: - (iv) There is no 0x06 set-baud sniffing: a case-insensitive grep for 'baud' in module_bridge.c, sillion_application.c and silion_protocol.h finds nothing. There is no opcode filter either; every FF frame is forwarded (module_bridge.c:365-390). A baud change sent from a vendor tool would therefore desync USART1 from the module. I did not verify whether the module keeps the new baud rate across power cycles. - (iii) The module UART still uses the single-shot USART_SendDataIT (uart.c only gained USART3 guards). The bridge works around it by retrying while TX is busy (module_bridge.c:386-389, BridgeIsTxReady). - (vii) There is no HID class: no 'hid' anywhere in Firmware/USB_DEVICE or Services/USB. - The W5500 API is still a separate set of Socket0-3 functions, not socket-parameterised.

**Remaining work.** 1. Sniff opcode 0x06 replies and retune USART1, or block 0x06 in the bridge. 2. Optionally add a queued module TX path. 3. Decide on HID, or confirm Reader Manager does not need it. 4. Run reader_manager_connect_test.py and the real Reader Manager/ReaderSense workflows on F411 hardware, and record the results.

#### REQ-03 — Missing requirement I/O: no IP/Wi-Fi reset button, no TTL host UART header, and incomplete status LEDs

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Hardware (13-Oct board, which is the rev 3 target; u7_pinmap.txt and net.py): - There is still no user button. KEY1 goes to NRST (U7.7) and KEY2 to BOOT0 (U7.44). - There is still no TTL host UART header. PA2/PA3 go only to the U12.4/U12.7 mux. - LEDs: LED3 on PB8 through R10 330R is the status LED. LED2 and LED5 run from W5500 U1.27/U1.25 through R35/R36 (link/activity). LED4, the power LED, is now fed from the 5V net (TP11 '5V') through R68 330R, so the power-LED sub-item is addressed. - There is no Wi-Fi LED. The netlist has only LED2-LED5, and the ESP32 pins carry only strap pull-ups (R22-R24). Firmware (paths relative to 271026/_fw/src/STM32_Firmware): - LED and buzzer pins are assigned (BSP/f411_custom/board_config.h:148-153), but the status LED is only switched on once at init (f411_custom/board.c:213-223). There is no LED status driver. - There is no button, long-press or factory network restore; a grep for factory or long press only hits a comment in brownout.h. - IP and Wi-Fi settings are now persisted (reader_settings.h:34-75). They can be set over USB or RS-232 with IPSET through the COM route (host_transport.c:90-93, then module_bridge.c:312-340), and a unit can be found over UDP 15000 (discovery_service.c). That gives a cable-based way to recover a lost IP. - The 'reset' key and the 0x20 clear command restore only the Configuration_Storage fields (configuration_service.c:2223-2237, configuration_storage.c:328-360, vendor_config.c:1316-1338). ReaderSettings_GetDefaults is static and only runs when loading fails (reader_settings.c:208, 362-458), so nothing can restore the network defaults.

**Remaining work.** Hardware (next spin): - Add a panel-accessible config/reset button on a GPIO. - Add a 3.3 V TTL host UART header. - Add a Wi-Fi LED. Firmware: - Write an LED status driver on PB8 (link, RF active, error). - Add a command (and optionally a configurable GPI long-press) that restores the reader_settings network and Wi-Fi defaults. - Document the IPSET-over-USB/RS-232 recovery procedure.

#### REQ-07 — Identium-spec traceability summary (DS-001 v1.1, SCH-001, PCB-001, FW-001)

`PARTIAL` · rev 2: INFO → now: INFO

**Rev 3 evidence.** Firmware items improved. Signed update: the bootloader checks SHA-256 and an ECDSA P-256 signature on a staged image before installing it (Bootloader/boot_main.c:6-13). But the production private key ships in the drop at keys/fw_signing_key.pem, and it matches g_bootPublicKey (lead-confirmed in LEAD_FACTS.md). That defeats this sub-item on every unit. Readout protection: RDP level 1 + BOR via 'make protect' (Makefile:197-199). This is an SWD procedure, not enforced by the firmware, which only reports RDP (Firmware/Services/system/diagnostics.c:37-44). HAL CRYP/RNG are still commented out (Inc/stm32f4xx_hal_conf.h:43,63); this is acceptable because signature verification needs no TRNG. MAC: the hard-coded multicast MAC was replaced by a locally administered unicast MAC derived from the UID (Firmware/Services/configuration/reader_settings.c:230-238). Still missing: self-test, LED fault codes and factory test. The status LED is only switched on at board init (Firmware/BSP/f411_custom/board.c:211-222), and a grep for self-test/factory finds nothing. No board serial-number provisioning exists. The F411 .ioc exists but sets up only HSE and USB (Boards/f411/F411_custom.ioc:16-24, Mcu.PinsNb=6). Hardware: board files are identical to 231026. The BOM has no optocoupler (net.py find PC817/EL357/TLP/LTV/opto all empty), and the test points are power/GND only (net.py find TP: TP1-TP12), so there are no NRST/BOOT0/UART test points. VIN surge/pi-filter was not re-verified here (covered by IDM-AUD-003). The drop holds no spec change-control record.

**Remaining work.** Rotate the signing key, keep it out of every deliverable, and rebuild the bootloader with the new public key. Make RDP level 1 a mandatory production step, and optionally check it at boot. Add a power-on self-test, LED fault codes and a factory-test mode. Provision a board serial number. Complete the F411 .ioc with the real pin map. Hardware: opto-isolated GPIs or a recorded deviation, NRST/BOOT0/UART test points, and verified surge/pi-filter on VIN. Put the specs under change control with approved deviations.

#### PRG-01 — No manufacturing data package or design-review evidence was delivered, and the only CubeMX .ioc is for the F429 kit

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Package: 271026 now carries Gerber_PCB1_2026-10-13.zip (copper, mask, paste, silk, outline, PTH/NPTH/via drill files, drill drawing, FlyingProbeTesting.json, and a 96-byte How-to-order-PCB.txt), PickAndPlace_PCB1_2026_10_13.xlsx and DESIGN JUSTIFICATION.docx. All three are byte-identical to the 231026 package (checked with cmp). IDM-AUD-003 scored that package 3/10 for completeness (IDM-AUD-003 md line 60). Still missing: the schematic, stackup/fab notes, assembly drawing, IPC-D-356, STEP, ERC/DRC reports, native EasyEDA source and signed SCH-001/PCB-001 checklists. A schematic does exist on the designer's side: the firmware says its pin map was 'traced from docs/hardware/SCH_Schematic1_2026-10-13.pdf' (Firmware/BSP/f411_custom/board_config.h:18), but it was not delivered. The title block is unverified because no schematic was supplied. The justification docx closes by saying medium and low severity findings are ignored. Firmware side, fixed: an F411 build configuration exists. Makefile:52-62 sets STM32F411xE, TARGET_BOARD=2, Boards/f411/STM32F411CEUX_FLASH.ld and startup_stm32f411ceux.s; .cproject:176-242 has an F411 configuration (target_mcu STM32F411CEUx); and CMSIS stm32f411xe.h was added (rev2_rev3_filelist.txt). Partly done: Boards/f411/F411_custom.ioc targets STM32F411C(C-E)Ux / UFQFPN48 (:16-17), but it assigns only PH0/PH1/PA11/PA12 (Mcu.PinsNb=6, :18-24). So it cannot be used to check the board_config.h pin map.

**Remaining work.** Deliver the 13-Oct schematic (it exists per board_config.h:18) with a filled title block, plus stackup/fab notes (thickness, copper weight, finish, IPC class), assembly drawing, IPC-D-356, STEP, ERC/DRC reports and native EasyEDA source. Get the SCH-001/PCB-001 checklists signed by the hardware lead and a second reviewer. Complete F411_custom.ioc with every peripheral pin (SPI1, USART1/2, I2C1, GPIO) and check it against board_config.h.

#### REL-03 — README status overstates readiness and omits the real-board port

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Improved. The pasted chat text at rev-2 Src/main.c:10 and the stray comment at rev-2 :70 are gone (rev-3 main.c:9-10 is the plain template; :74-76 is empty). Makefile:1-29 now documents builds per target (BOARD=f429/f411, flash, image, protect, hostcheck). Still open: there is no README, status matrix, target-hardware statement or known-gaps list. The only text files in the tree and the zip are Bootloader/micro-ecc README_IDENTIUM.txt and LICENSE files. The source refers to design notes that were not delivered: docs/notes/f411-custom-board-port.md (Firmware/BSP/f411_custom/board_config.h:21), docs/design/0020-bootloader-and-signed-update.md (Bootloader/boot_main.c:4, Firmware/Services/update/firmware_update.h:6) and docs/hardware/SCH_Schematic1_2026-10-13.pdf (board_config.h:18). Src/main.c:9-14 still carries the ST copyright and licence header. Stale F429 wording remains in the F411 build: product_config.h:12 ('defaults from the F429 firmware'), :22 ('RFID-F429-DEV') and configuration_storage.h:10 ('owned by the F429 prototype'). Release hygiene: the shipped update image is rfid_controller_f411-v0.10.1-1-gfcae054-dirty.tlfw, built from a dirty tree (Makefile:165 uses git describe --dirty), while the firmware reports 1.0.0.8 (product_config.h:53-56). Makefile:7-8 says the F411 has a 128 KB code area, but the linker gives the app 112K (Boards/f411/STM32F411CEUX_FLASH.ld:51).

**Remaining work.** Deliver the README/status document: a feature status matrix traced to the requirements and audit findings, the target hardware (F411 board), per-target build instructions and a known-gaps list. Include the docs/ notes the code references. Replace the ST header in main.c with an Identium header. Remove the stale F429 comments and the 'RFID-F429-DEV' name from the F411 build. Ship only images built from a clean, tagged tree, with one version scheme across the image name, VER and the HTTP User-Agent. Fix the 128 KB vs 112 KB statement.

#### R2-01 — New 'Identium protocol' is newline-delimited JSON over a raw TCP client. It matches neither the Identium TCP Protocol V1.0 binary 0xFF/0xEE framing nor the reader HTTP POST active mode

`PARTIAL` · rev 2: HIGH → now: HIGH

**Rev 3 evidence.** Added: HTTP active mode. It sends a real HTTP/1.1 POST with Host, Content-Type: application/json and Content-Length (Firmware/Services/ethernet/http_upload.c:686-703). A 2xx response body containing "command_type" is passed to the command parser (http_upload.c:596-641). reader_name is sent as 'name/ip' (http_upload.c:286-300). The mode is selected via upload.http in EE 0x25 (Firmware/Services/configuration/vendor_config.c:946-966) and works on Ethernet (Firmware/Services/ethernet/ethernet_service.c:109-141, 282-285) and Wi-Fi (Firmware/Services/upload/active_upload_transport.c:71-72). It is linked in the shipped F411 release map (HttpUpload_Submit, HttpUpload_Task), and the host test passed 2000/2000 (LEAD_FACTS.md). Not done: TCP Protocol V1.0 binary framing is still absent. FW-001 §3.1 requires 0xFF/0xEE frames with commands 0x01/0x02/0x04/0x05/0x16/0x17/0x18, and a grep of Firmware/Protocol, Firmware/Services and Firmware/Application/Host finds no builder for them. The default upload mode is still newline-delimited JSON over raw TCP: uploadMode 0 via memset (reader_settings.c:213; reader_settings.h:29), and the heartbeat is a JSON line (Firmware/Protocol/identium_protocol.c:1029). The header still cites the undelivered 'Reader-Protocol-Specification' (identium_protocol.h:10-12). client_ack=1 and crc_enable=1 are rejected as unsupported (Firmware/Services/configuration/configuration_service.c:1485-1489), so the tag-data confirmation flow is missing. HTTP accepts only http:// with an IPv4 literal (http_upload.c:134; vendor_config.c:949), while FW-001 §3.2 shows an https URL.

**Remaining work.** Implement the TCP Protocol V1.0 binary codec as a selectable upload mode. It needs FF/EE framing, the BE 2-byte length, STAT32, the logical name, 0x01 with the client_ack confirmation, 0x02, 0x04 with board/module type, SW version, antenna list and counter, 0x05, the 0x16/0x17/0x18 replies, and CRC on serial only. Build a byte-for-byte conformance test from the spec examples. Get the JSON-over-TCP dialect specified and signed off, or drop it. Add HTTPS and DNS if the customer endpoint needs them.

#### R2-02 — Active-mode server IP/port, local port and reader_name are compile-time constants that remote config rejects and cannot persist

`PARTIAL` · rev 2: HIGH → now: MEDIUM

**Rev 3 evidence.** Fixed: server IP/port, reader name, IP/mask/gateway/DNS and MAC now live in persistent ReaderSettings_t (Firmware/Services/configuration/reader_settings.h:32-56), stored in its own sector (F411 sector 5, reader_settings.c:28-31). They can be written via EE 0x25/0x1F on 8080 (vendor_config.c:844-858 name, :904-930 tcp.ser_ip/ser_port, :1118-1167 ethernet) and via IPSET (module_bridge.c:312-340). They are used at runtime (ethernet_service.c:127-128), and events use ReaderSettings_GetName() (identium_protocol.c:213-214, 1029-1030). The static JSON ethernet block is built from live settings (configuration_service.c:1117-1131). The MAC is a unique, locally administered unicast address derived from the UID (reader_settings.c:230-238). Still wrong: (1) Defaults. IP 192.168.150.50 (reader_settings.c:215-218), whereas the Utility doc gives 192.168.1.100. Server 192.168.150.100:9000 with work state ACTIVE at first boot (reader_settings.c:242-248), whereas FW-001 §3 says active modes stay off until an endpoint is configured. reader_name 'RFID-F429-DEV' is the same on every unit (reader_settings.c:273, product_config.h:22). (2) No DHCP (vendor_config.c:1118-1119; discovery always reports DHCP=0, discovery_service.c:46). (3) The Identium host's own set_runtime_conf still rejects glob_params.name and upload.tcp/http (configuration_service.c:1335-1337, 1542-1545). Only the vendor EE port and the USB ASCII path can set them. (4) Config writes and IPSET are unauthenticated, so any LAN host can change them.

**Remaining work.** Set production defaults: 192.168.1.100, active upload off until an endpoint is configured, and a unique default reader_name (for example derived from the MAC). Add DHCP with a static fallback. Accept name and server endpoint in the Identium set_runtime_conf/set_static_conf. Consider authenticating the config/IPSET writes.

#### R2-03 — New port-8080 config listener (0xEE, Get-only, one request per connection) takes the port the SDK/RFID Manager and Modbus-TCP need, and closes their connections

`PARTIAL` · rev 2: HIGH → now: MEDIUM

**Rev 3 evidence.** Fixed: port 8080 now demultiplexes on the first byte. 0xEE is a config request; anything else starts a vendor module-bridge session (Firmware/Services/configuration/configuration_port.c:293-306). The bridge relays 0xFF module frames and answers IOGET/IOSET/VER/IPGET/IPSET (module_bridge.c:18-41). A UDP 15000 discovery responder runs on socket 2 (discovery_service.c:10,63-100). Write commands 0x25/0x1F/0x20/0x23 were added (configuration_port.c:186-210; vendor_config.h:20-23). The ASCII host server on port 5000 is back on socket 3 (Firmware/Services/ethernet/host_tcp_server.c:20-25; product_config.h:13). Remaining: (1) There is no Modbus-TCP or Modbus-RTU anywhere. A grep for modbus|MBAP finds only a CRC comment (Firmware/Services/host/host_transport.c:248), although the Modbus V1.0 doc and FW-001 §3.3 put Modbus-TCP on 8080. The port plan is unresolved, and an MBAP frame starts a bridge session instead. (2) Port 8080 serves only one client (socket 1). (3) TCP sessions have no idle timeout. The bridge idle end applies only to serial links (module_bridge.c:782-792). The config path waits indefinitely for the client to close (configuration_port.c:269-272, 339). Socket 1 has no keep-alive: KPALVTR is written only for socket 0 (Firmware/Devices/w5500/w5500.c:659,705), not in W5500_StartTCPServer1 (w5500.c:1177-1230). Opening a bridge session stops the reader's own inventory (Firmware/Application/Silion/sillion_application.c:2436-2448). So an idle or half-open client on 8080 stops active-mode reading until it disconnects or the unit reboots. Such a client needs no authentication: any first byte other than 0xEE is enough.

**Remaining work.** Settle the port plan with Identium: Modbus-TCP on 8080 per the spec versus the vendor tools. Implement Modbus RTU/TCP or move one service. Add an idle timeout and W5500 keep-alive on socket 1 for both the config and bridge paths, and release the module automatically when a TCP bridge session goes quiet. Consider more listening sockets. Gate bridge takeover of the module (authentication or explicit work-state rules) so a stray LAN connection cannot stop active-mode reading.

#### R2-04 — Persistent store holds only heartbeat, start_inv_atbeg and session; region, power, antenna, network and server settings still revert or are fixed

`PARTIAL` · rev 2: HIGH → now: MEDIUM

**Rev 3 evidence.** Fixed: ReaderSettings_t v4 persists region, read/write power (rfValid), antenna (rfAntennaValid), Gen2 target/Q/profile, network, server, name, upload mode/URL and Wi-Fi (reader_settings.h:32-87), with v1-v3 migration (reader_settings.c:341-479). The ActiveMode record has grown to the upload, watchdog, event and cus_param fields (configuration_storage.h:13-35). The legacy text commands now persist: SET_POWER and SET_REGION via HOST_PersistRegionPower, SET_ANTENNA via HOST_PersistAntenna, SET_GEN2 via HOST_PersistGen2 (Firmware/Application/Host/host_interface.c:336-399, called at :1623, :1678, :1736), and SET_SESSION via Configuration_Service_SaveSession (:1786). Stored RF settings are applied at boot (Firmware/Application/configuration/firmware_configuration.c:451-464). Storage uses sectors that exist on the F411: 5, 6 and 7 (configuration_storage.c:9-19; reader_settings.c:28-31), and the app is limited to 112K (Boards/f411/STM32F411CEUX_FLASH.ld:48-51). Work state comes from settings (configuration_service.c:571-574), and the active JSON is built from live config. Remaining: (1) The RS-232/RS-485 serial mode is neither stored nor settable. Serial_Port_SetMode has no caller outside serial_port.c, and the mode is fixed at compile time (firmware_configuration.c:373-376; product_config.h:41-43). (2) The static JSON still reports literals for UART parameters (configuration_service.c:1108-1109), Gen2 target 2 / q -1 / profile -1 (configuration_service.c:1082, although stored gen2 values exist, reader_settings.h:84-86) and GPO states 0 (configuration_service.c:1106-1107). (3) Power is single-antenna only.

**Remaining work.** Persist and expose the serial settings (mode, baud, Modbus address), with a runtime setter that drives the mux and DE/RE. Build the whole static JSON from live/stored values: gen2 target/q/profile, GPO states and UART parameters. Extend power to per-port values if multi-port modules are supported.

#### R2-05 — New rev-2 code is F429-only (OTG_HS on PB14/PB15, flash sectors 22/23 dual-bank, 168 MHz clock), so the real STM32F411 board still cannot be built and it collides with the F411 pin map

`PARTIAL` · rev 2: HIGH → now: LOW

**Rev 3 evidence.** The F411 now builds and the F429-only parts sit behind STM32F411xE. Paths are relative to 271026/_fw/src/STM32_Firmware.
- Build: the Makefile has a BOARD=f411 target (Makefile:52-61, -DSTM32F411xE -DTARGET_BOARD=2, Boards/f411/STM32F411CEUX_FLASH.ld). The shipped build/f411-release/rfid_controller_f411.map links OTG_FS_IRQHandler at 0x08017058 (map:3439-3441) and Configuration_Storage_Save at 0x08010ad0 (map:2729-2731).
- Config storage: on the F411 it uses FLASH_SECTOR_6/7 at 0x08040000/0x08060000 (configuration_storage.c:9-18). Reader settings use sector 5 (reader_settings.c:29-31). The app is linked to 0x08004000, 112 KB (STM32F411CEUX_FLASH.ld:51).
- USB: moved to OTG_FS on PA11/PA12 with GPIO_AF10_OTG_FS (usbd_conf.c:89-105). The handler is OTG_FS_IRQHandler (usb_irq.c:4-10). FIFOs are Rx 0x80 + Tx0 0x40 + Tx1 0x80 = 320 words (usbd_conf.c:437-439).
- Pins: PB14 is now the RS-232/RS-485 mux select (f411_custom/board_config.h:52-53), so the USB-on-PB14/PB15 collision is gone.
- Clock: HSE 8 MHz /4 x96 /2 = 96 MHz, with PLLQ=4 giving 48 MHz for USB (main.c:167-210).

What remains:
- Erases are not made safe. Each erase calls HAL_FLASHEx_Erase straight from flash (configuration_storage.c:259-279, 554). No code is placed in .RamFunc: the linker only reserves the section (ld:147-148) and no source puts anything in it. Nothing quiesces the interfaces: for example, Configuration_Service_SaveSession saves at configuration_service.c:480 without stopping inventory.
- Effect: a 128 KB sector erase on the single-bank F411 stalls every flash fetch and ISR. The rev 2 audit cited about 1-2 s from the ST datasheet; I did not re-check that figure. UART ORE is handled (uart.c:377-406), so the cost is lost RFID frames, not a lock-up. The IWDG is about 32 s (watchdog.h:37-38), so it does not reset.
- Layout is hard-coded: sector numbers and addresses are literal #if values in two .c files, and only a comment ties them to the linker script (ld:48-50, firmware_image.h:21). There is no static check.
- Flash use: three 128 KB sectors (384 KB) hold small records, while the app slot is ~83% full (LEAD_FACTS).

**Remaining work.** - Erase safely: run config and settings erases from RAM, or stop inventory and quiesce the UART/USB before a save, and report the stall.
- Tie layout to the linker: export the settings and config sector addresses from the linker script (or one board header) and add a static or link-time assert that the app region cannot overlap them.
- Check flash budget: re-check the app-slot headroom given that 384 KB of flash holds only small records.

#### R2-06 — New boot steps (Industrial_IO_Init, apply persisted session, remotely settable start-inventory-at-boot) hard-fail into Error_Handler before the watchdog is armed, which can hang the unit on every boot

`PARTIAL` · rev 2: HIGH → now: LOW

**Rev 3 evidence.** The main defect is fixed:
- The watchdog is now started first in Firmware_Configuration_Init, and a failure there is non-fatal (firmware_configuration.c:301-305).
- ConfigureReader, SetSession and the persisted start-inventory-at-boot are all in Firmware_Configuration_StartReader (firmware_configuration.c:166-226). If it fails, the unit sets ERROR_RFID_INIT and SYSTEM_DEGRADED_RFID instead of returning STATUS_ERROR (542-550). Firmware_Configuration_Task retries while the module is idle (239-268).
- W5500, Ethernet and Wi-Fi failures are also degraded, not fatal (476-529).

What remains:
- Industrial_IO_Init still returns STATUS_ERROR (firmware_configuration.c:333-337), and main.c:119-124 then calls Error_Handler. It fails only on a NULL port in the static IO table (industrial_io.c:120-125, 142-144), which means a mis-configured build. Such a build would boot-loop.
- Error_Handler still saves a fault record, then does __disable_irq(); while(1) (main.c:271-284). Recovery depends on the IWDG, about 32 s (comment main.c:274-277).
- Clock-setup failures call Error_Handler before the watchdog is armed (main.c:196, 210), and the unit then halts for good, as the comment admits.
- There is no boot-failure counter or safe-default fallback (grep for boot-count terms in Firmware/ and Src/ finds nothing).

**Remaining work.** - Error_Handler: make it record the fault and then call NVIC_SystemReset, not spin.
- Industrial_IO_Init: make its failure non-fatal, or turn it into a build-time check.
- Safe-boot fallback: add a counter in .noinit that falls back to default configuration and passive mode after N failed boots.
- Clock failure: consider an HSI fallback when HSE fails.

#### R2-09 — A partial or failed W5500 socket-0 send queues a truncated JSON fragment, never sends the rest and never closes the socket, so the NDJSON stream is corrupted

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** The truncated-fragment defect is gone:
- Ethernet_Service_SendComplete reads Sn_TX_FSR and refuses the whole message if it does not fit, so nothing is written (ethernet_service.c:288-303).
- W5500_Socket0_Send now waits for SEND_OK or TIMEOUT before issuing the next SEND (w5500.c:948-987, called at 1009).
- Tags are queued in a RAM ring, and a failed batch is kept intact for retry (identium_protocol.c:817-826).
- The Wi-Fi path is also all-or-nothing (active_upload_transport.c:73-75).

What remains:
- Socket 0 TX stays at the 2 KB reset default. Only socket 1 gets an explicit size (w5500.c:1180-1201). W5500_S0_TXBUF_SIZE is defined (w5500.h:25) but never written, and Send assumes a 2 KB buffer (w5500.c:1043-1050).
- s_buf_size is accepted up to 8192 (configuration_storage.c:129-130). With data_aggr mode 1, batches grow to 64 tags up to s_buf_size (identium_protocol.c:779-815). One tag object is about 68 bytes (format at :543 and :579).
- So with s_buf_size above about 2 KB and more than about 30 tags in a window, a batch can never pass the check at ethernet_service.c:293. Raw-TCP upload then stalls for good while the queue drops or expires tags. This is from reading the code; I did not run it.
- WaitPreviousSend gives up after about 40 ms and the next SEND goes out anyway. TIMEOUT is cleared but not handled as a failure locally (w5500.c:966-986).
- client_ack is forced to 0 (configuration_storage.c:133-135), so tags already popped after SEND are lost if the link drops.

**Remaining work.** - Fix the size mismatch: clamp s_buf_size, and the aggregation batch, to the socket 0 TX size. Or enlarge Sn_TXBUF_SIZE for socket 0, or split batches into several complete JSON lines.
- Send-timeout handling: after a SEND timeout or a missing SEND_OK, close and reconnect.
- Delivery: feed upload from the flash tag log with a resend or ACK option so the link can be lossless.

#### R2-10 — USB CDC enumerates with ST's VID/PID and 'STMicroelectronics' strings and carries only the legacy ASCII command set, with no Identium protocol

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** The descriptors are unchanged:
- usbd_desc.c is not in the rev2-to-rev3 modified list.
- It still sets USBD_VID 1155 (ST's 0x0483), USBD_MANUFACTURER_STRING "STMicroelectronics", USBD_PID_HS 22336 (0x5740) and product "STM32 Virtual ComPort" (usbd_desc.c:65-69).
- There is no HID interface: there is no HID class under Firmware/USB_DEVICE or Middlewares, and no 0x2E88/0x4605.

What improved:
- CDC RX (usbd_cdc_if.c:267-282) now feeds a mux in host_transport.c, not only the legacy ASCII parser.
- 0xEE frames, with an optional CRC16-Modbus trailer, go to the config port (host_transport.c:244-261, 278-283).
- 0xFF frames go to the vendor module bridge (host_transport.c:271-276).
- Vendor tokens such as IOGET and IPSET are recognised (host_transport.c:90-93).

Still missing: the Identium TCP-protocol frames over a serial link (CRC variant), and Modbus RTU.

**Remaining work.** - Identity: use an Identium-owned or properly licensed VID/PID, with Identium manufacturer and product strings.
- USB personality: decide it as FW-39 asks, either CDC carrying the Identium serial protocol with CRC, or adding the HID interface the SDK expects (composite if needed).

#### R2-11 — Emitted tag/event JSON differs from the reader PDF and from the configuration the firmware itself advertises (tag_json_format, events, data_aggr)

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** What improved:
- data_aggr batching is implemented: tags wait for the aggregation window and are sent as one batch (identium_protocol.c:779-845). The live mode and timeval are now advertised (configuration_service.c:971-975), where rev 2 had a static string (rev2 configuration_service.c:736).
- A sync_time_req event exists (identium_protocol.c:213).

What remains:
- Tag keys are still ep, at, rc, ri, fq (identium_protocol.c:543, 579).
- The advertised format is still the fixed {1,2,5,8} (configuration_service.c:23, emitted at 1004-1024). It cannot be set: set_runtime_conf has no tag_json_format key (configuration_service.c:1281-1540, 1705-1740).
- So 'lt' is advertised and never sent, 'fq' is sent and never advertised, and bd, pt and ft are absent.
- tag_coming is still not emitted (no match in Firmware/), although ActModeDataFormat defines it (p.2, example p.7).
- Events 4 and 5 are rejected by validation (configuration_storage.c:157-168).

**Remaining work.** - Wire format: confirm it against a capture from a real Identium reader.
- Emit exactly the fields that tag_json_format selects, and make tag_json_format settable.
- Add lt and the bd/pt/ft fields, and the tag_coming event.

#### R2-15 — Host ownership is never released on USB unplug or suspend, so output falls back to USART1 and inventory keeps running

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Fixed part: the rev-2 fallback that sent USB output to another port is gone. Rev 2 host_transport.c:349-353 called VCP_SendString when USB was not configured. Rev 3 removed Services/VCP (rev2_rev3_filelist 'Only in R2'). HOST_Transport_LinkSend (Firmware/Services/host/host_transport.c:695-714) now returns 0 and drops the bytes unless USB_CDC_Transport_IsReady(), which means dev_state==CONFIGURED (usb_cdc_transport.c:55-58, usb_device.c:97). Still open: vbus_sensing_enable stays DISABLE on the F411 (Firmware/USB_DEVICE/Target/usbd_conf.c:415). The suspend callback (usbd_conf.c:293-308) and CDC_DeInit_HS (Firmware/USB_DEVICE/App/usbd_cdc_if.c:171-177) only call USBD_LL_Suspend or USB_CDC_Transport_Reset, which only clears the TX ring (usb_cdc_transport.c:110-120). Nothing calls HOST_Transport_Release or SILION_Application_StopInventory on USB loss. The only release callers are the ASCII RELEASE command (host_interface.c:961-995), the TCP-5000 disconnect (host_tcp_server.c:35-37) and WiFi (wifi_service.c:276-279). After a cable pull, hostActiveTransport stays USB. host_transport.c:559-565 then throws away every byte from the RS-232/485 port, TCP 5000 and WiFi, so the legacy ASCII parser is locked out until USB is re-plugged and RELEASE is sent, or the unit reboots. Inventory keeps running and HOST_SendTag output is silently dropped. Tags still reach the flash log through SILION_Application_PublishTag (sillion_application.c:26-31).

**Remaining work.** On USB suspend, CDC DeInit or a DTR drop held past a timeout: call HOST_Transport_Release() and stop inventory if USB started it, as the Ethernet and WiFi paths already release. Or add an ownership idle timeout so other ports can claim the parser. Add a bench test: start inventory over USB, unplug, then confirm RS-485 and TCP-5000 ASCII commands are accepted and the RF stops.

#### R2-18 — New industrial_io.c treats the F411 'production' pin map as final, but that map is the rev-1 FW-25 map that contradicts the schematic; 4 outputs exposed against 3 relays

`PARTIAL` · rev 2: MEDIUM → now: LOW

**Rev 3 evidence.** Pin map now matches the 13-Oct netlist (checked with net.py). Firmware/BSP/f411_custom/board_config.h:137-144 sets OUT1=PB2, OUT2=PB10, OUT3=PA0 and OUT4=NULL. Netlist: PB2 (U7.20)->R51->Q3->relay U4->CN12, PB10 (U7.21)->R52->Q4->U9->CN13, PA0 (U7.10)->R49->Q2->U16->CN14. board_config.h:124-132 sets GPI1=PB3, GPI2=PA15, GPI3=PB4, GPI4=PB15 (active low). Netlist: CN10.1->R57->Q6->R44->PB3, CN10.2->R56->Q7->R43->PA15, CN10.3->R55->Q8->R42->PB4, CN5.1->R58->Q5->R41->PB15, each with a 10k pull-up to 3V3 (R46/R47/R60/R62). The JTAG pins are released because GPIO_Init rewrites MODER (Drivers/gpio/gpio.c:57-61). Industrial_IO_Init no longer drives PB12-PB15. The 'already defined' comment is gone (industrial_io.c:21). Industrial_IO_GetOutputCount() reports 3, but only GET_CAPABILITIES uses it (host_interface.c:2345). Still open: GPIO_SERVICE_MAX_OUTPUTS remains INDUSTRIAL_IO_COUNT=4 (gpio_service.c:8, industrial_io.h:6), so gpo 4 passes validation (gpio_service.c:252-266). The apply loop (:308-327) has already driven the earlier items and written gpioOutputStates[3] when Industrial_IO_SetOutput fails on the NULL port (industrial_io.c:169-173). Result: partial application and error 20003 "invalid set_gpo item" instead of 2 "not supported". Silkscreen labels for OUT1-3 and GPI1-4 are unverified (no silkscreen check done).

**Remaining work.** Validate gpo against Industrial_IO_GetOutputCount() in the pre-validation loop and answer gpo 4 with 'not supported' before any output is driven. Confirm the OUT and GPI numbering against the silkscreen and cable pinout. Note: FW-001 section 6 maps GPO1 to a TPL7407L transistor output that this board does not have; that is a spec/hardware decision to record.

#### R2-20 — No buzzer, LED, Wiegand or reset-button firmware; a code comment wrongly says the controller has no buzzer output

`PARTIAL` · rev 2: MEDIUM → now: MEDIUM

**Rev 3 evidence.** Buzzer added: BOARD_BUZZER PC13 (Firmware/BSP/f411_custom/board_config.h:152-153), Industrial_IO_SetBuzzer and HasBuzzer (industrial_io.c:252-275), GPIO_Service_Beep (gpio_service.c:102-119), the ASCII SET_BUZZER command (host_interface.c:2569-2583) and BUZZER=1 in GET_CAPABILITIES. All are linked in the F411 image (build/f411-release map: GPIO_Service_Beep at 0x08013ec0). Still open: (1) GPO 5 is not mapped to the buzzer. set_gpo rejects gpo>4 as 'not supported' (gpio_service.c:252-266) and the parser rejects a 5th item (identium_command.c:413). The wrong comment remains at gpio_service.c:257-258 ("this four-output controller does not have that physical output"). (2) The LED on PB8 is only switched on once at board init (board.c:212-223). There is no status or activity LED driver. (3) Wiegand: the 13-Oct board now routes PC14/PC15 to CN2.3/CN2.4 (WGDO/WGD1, 10k pull-ups R71/R69, u7_pinmap.txt pins 3-4). The firmware has no Wiegand code (grep finds none), and board_config.h:20 wrongly says "PC14/PC15 not connected". FW-001 section 6 requires a Wiegand-26/34/66 output mode (default off). (4) Reset button: the hardware still has only KEY1=NRST and KEY2=BOOT0, so there is nothing for firmware to read.

**Remaining work.** Map Identium set_gpo gpo 5 to the buzzer, or document that it is deliberately not supported, and fix the comment. Add a status LED driver (link, inventory, fault). Implement Wiegand output on PC14/PC15 within the backup-domain pin limits (sink only, 3 mA, 2 MHz) and correct the board_config.h comment. A GPIO-readable reset or factory-default button needs a hardware change.

#### R2-23 — Time_GetEpochMs keeps elapsed time in 32 bits, so timestamps wrap 49.7 days after the last sync_time (and the reader never requests a sync)

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Improved: the reader now emits sync_time_req. Identium_Protocol_SendSyncTimeRequest (Firmware/Protocol/identium_protocol.c:197-225) is sent on the first connection after boot (:1197-1206); rev 2 had no sync_time_req at all. Still open: Firmware/Services/Time/time_service.c:65-71 still computes elapsed time in 32 bits ("uint32_t elapsedMs;" then base + elapsedMs). If no new sync_time arrives within 2^32 ms (~49.7 days), timestamps jump back. The request goes only once per boot (comment at :1199-1200). If event 6 is not subscribed at that moment, :202-207 returns 1 and the request is marked done, so it is never sent later. The wrap now also corrupts persisted flash tag-log timestamps (Firmware/Services/storage/tag_log.c:149) as well as GPI events (identium_protocol.c:1118).

**Remaining work.** Keep a 64-bit millisecond counter extended in the SysTick ISR, or re-base the epoch periodically. Re-send sync_time_req periodically (for example daily) and when event 6 is enabled later, not just once per boot.

#### R2-24 — Legacy ASCII TCP server on port 5000 removed with no replacement; the Ethernet host-transport paths are now dead code

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Premise reversed: TCP 5000 is back. New Firmware/Services/ethernet/host_tcp_server.c listens on socket 3 at PRODUCT_ETHERNET_TCP_PORT (:20-25). It is initialised in firmware_configuration.c:514, polled from host_interface.c:872 and pushes HOST_TRANSPORT_ETHERNET bytes (:97). The Ethernet output branch is live again (host_transport.c:676-679), and Host_Tcp_Server_Task is linked in the F411 image (map: 0x0801342c). 0xFF module pass-through is provided on 8080 through module_bridge (configuration_port.c:294-303). Still open: (1) no access control on TCP 5000. host_tcp_server.c has no authentication, and the full ASCII set, including FW_BEGIN/FW_APPLY (host_interface.c:798-831) and network/WiFi settings, is reachable from the LAN. That matters more given the exposed signing key at keys/fw_signing_key.pem (LEAD_FACTS). (2) No Modbus-TCP anywhere: grep finds only a comment in host_transport.c. (3) Socket 3 has no keep-alive (W5500_Socket3_Listen, w5500.c:1806-1830) and no idle timeout. A half-open client keeps hostActiveTransport=ETHERNET, which locks USB and serial ASCII control out (host_transport.c:559-565). (4) W5500_StartTCPServer (w5500.c:539) is still in the source but uncalled; the linker discards it (map line 739). Src/main.c:145 still says "Legacy ASCII parser remains on native USB", which is stale.

**Remaining work.** Add access control to the TCP 5000 control port, at least for FW_* and settings commands, or allow it to be disabled. Add Sn_KPALVTR and an idle timeout on socket 3. Implement or formally descope Modbus-TCP (FW-001 lists Modbus). Delete W5500_StartTCPServer and fix the stale main.c comment.

#### R2-25 — New Error_Handler hang paths: HSE start failure (no CSS/HSI fallback) and USB init failure stop the reader with IRQs disabled before the watchdog

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** The USB part is better. Rev 3 starts the IWDG before USB: Watchdog_Init is at Firmware/Application/configuration/firmware_configuration.c:301 and MX_USB_DEVICE_Init at :353. In rev 2 the order was reversed (watchdog at :401, USB at :195). Error_Handler now also saves a fault record before halting (Src/main.c:278). So a USBD_Init/RegisterClass/Start failure (Firmware/USB_DEVICE/App/usb_device.c:73-88), or the unknown-speed branch in HAL_PCD_ResetCallback (Firmware/USB_DEVICE/Target/usbd_conf.c:277), now ends in an IWDG reset after about 22-62 s (Firmware/Services/watchdog/watchdog.h:24-28) instead of a permanent hang. The HSE path is unchanged. On the F411, SystemClock_Config still uses HSE and the PLL with no fallback: Src/main.c:185-197 and :208-211 call Error_Handler when HAL_RCC_OscConfig or HAL_RCC_ClockConfig fails. A grep for EnableCSS/CSSON/RCC_OSCILLATORTYPE_HSI in Src, Firmware and Inc finds nothing. This failure happens at Src/main.c:101, before Fault_Record_Init (:113) and before the watchdog is armed. Error_Handler still runs __disable_irq() and an endless loop (Src/main.c:279-282). Its own comment (main.c:274-277) admits that during clock set-up 'the reader stays halted'. A board whose crystal fails to start at power-on is therefore dead, with no reset and no report. USB failure is still fatal, not degraded: a persistent USB fault now gives a reboot loop about every 30 s.

**Remaining work.** If the HSE fails, fall back to HSI+PLL (USB can stay off), raise a degraded fault and enable CSS (NMI_Handler already records faults: Src/stm32f4xx_it.c:82-85). Make Error_Handler reset the MCU (NVIC_SystemReset after saving the record) instead of spinning with IRQs off. Treat MX_USB_DEVICE_Init failure as SYSTEM_DEGRADED_USB rather than calling Error_Handler.

#### R2-27 — Configuration_Service_GetWorkState is a stub returning 3 against its 1/0 contract, and ERR_INVALID and ERR_EXECUTION share the value 3

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** GetWorkState is no longer a stub. Firmware/Services/configuration/configuration_service.c:571-575 returns ReaderSettings_GetWorkState(), and config port command 0x22 sends that value (configuration_port.c:120). The values are READER_SETTINGS_WORK_PASSIVE = 1 and READER_SETTINGS_WORK_ACTIVE = 3 (reader_settings.h:15-16). The header contract was not updated: configuration_service.h:49 still says '1 = active upload mode, 0 = passive', which now means the opposite of what the code returns. The 1/3 encoding is not defined in any Identium document in the audit folder (unverified). The error codes are not fixed. configuration_service.h:10-11 still defines CONFIGURATION_SERVICE_ERR_INVALID 3U and CONFIGURATION_SERVICE_ERR_EXECUTION 3U, so 'invalid configuration' (configuration_service.c:2137) and 'configuration save failed' (:2170, :2276) send the host the same code.

**Remaining work.** Give EXECUTION its own code (and align it with the Identium error table). Correct the comment at configuration_service.h:49 to match the real 1/3 encoding, and record where that encoding comes from.

#### R2-28 — CubeMX .ioc is out of sync (no USB/HSE/PLL), custom code sits outside USER CODE blocks, and clock-ownership comments are stale

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Improved for the target board: Boards/f411/F411_custom.ioc:10-24 includes HSE (PH0/PH1), USB_OTG_FS and USB_DEVICE CDC, and the PLL values M=4, N=96 (:111-112) match Src/main.c:185-192. Problems remain: (1) The F411 .ioc carries only 6 pins (Mcu.PinsNb=6, :24): no SPI1, USART1/2, I2C1 or GPIO. It also belongs to a separate project ('ProjectName=F411_custom', 'MainLocation=Core/Src', :77, :82), so it cannot regenerate this tree. It is a reference only. (2) RFID_Controller_F429.ioc is byte-identical to rev 2 (still NVIC/RCC/SYS and one pin, :10-17). (3) SystemClock_Config, now with an #if F411 branch, still sits outside any USER CODE block (Src/main.c:167-260, between 'USER CODE END 3' at :160 and 'USER CODE BEGIN 4' at :263). (4) usbd_cdc_if.c is identical to rev 2: CDC_Receive_HS (:267-282) still has no USER CODE 11 markers, and the HS names are kept on the F411 FS core. (5) usb_hal_port.c (unchanged) says at :5-9 that USB works 'without calling HAL_Init() or HAL_RCC_ClockConfig()', but main.c:92 and :208 call both. There are still two SysTick owners: HAL_Init, and Time_Service_Init rewriting SysTick (time_service.c:35-39), with SysTick_Handler calling both tick functions (stm32f4xx_it.c:196-199). (6) USB_CDC_Transport_Task is still only called from HOST_Interface_Task (host_interface.c:869-871), and main.c:145-146 still says 'during migration'.

**Remaining work.** Choose one approach. Either complete the F411 .ioc (all pins and peripherals) and keep edits inside USER CODE blocks, or declare CubeMX reference-only and remove the stale F429 .ioc. Fix the usb_hal_port.c comment and keep one SysTick owner. Call USB_CDC_Transport_Task directly from the main loop.

#### R2-29 — Release build configuration lacks the USB middleware and new service include paths added to Debug (fatal include errors)

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Rev 3 adds a command-line Makefile with one shared include list for debug and release on both boards (Makefile:71-87, 89-96, 105). The release builds were produced and shipped: build/f411-release/rfid_controller_f411.elf and build/release/rfid_controller.elf, dated 27 Oct, and the .tlfw images were built from them. So a working release build now exists. However, the CubeIDE .cproject Release configuration is unchanged. Its include list is still only ../Inc plus HAL/CMSIS (.cproject:134-139), against Debug's 16 entries (.cproject:45-61), so it still fails on the USB and service headers. The new CubeIDE 'F411' configuration is debug-only (buildType.debug, .cproject:188). Makefile:29-30 claims the .cproject 'remains usable alongside this file', which is false for Release. The tree has no CI configuration, and -Werror is only an opt-in flag (EXTRA_CFLAGS, Makefile:27, 99).

**Remaining work.** Delete the broken .cproject Release configuration or make it mirror the Makefile. Name the Makefile as the official release build. Add CI that builds f411 debug and release with -Werror on every change.

#### R2-30 — Developer chat text pasted into the main.c copyright header (new at line 10), plus a rev-1 leftover at line 70 and stale comments

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** The chat text is gone. Src/main.c:10 again reads 'All rights reserved.'; rev 2 had the chat fragment there. main.c:69-71 is now an empty USER CODE PFP block, so the rev-1 'working lets go ahead now' leftover is gone. A case-insensitive grep for chat fragments ('dumb', 'lets go', 'why are you', 'small edits' and others) over Src, Inc, Firmware, Bootloader, Boards, Tools, hostcheck and the Makefile finds nothing. The stale ownership comments remain. host_transport.c:522-525 still says the transport that issued RELEASE 'is not allowed to immediately reclaim it', and host_interface.c:990-994 says the manager 'prevents the released transport from immediately reclaiming ownership'. Both contradict host_transport.c:50 ('No released-transport blacklist') and :634-637. The header is still the ST template ('Copyright (c) 2026 STMicroelectronics', main.c:9), not an Identium header.

**Remaining work.** Correct the two reclaim comments to match the code. Replace the ST template headers in Identium-authored files with Identium copyright/licence headers. Keep reviewing diffs before each drop.

#### R2-33 — Heartbeat semantics differ from Identium behaviour: count starts at 1, the counter advances on failures, and 0 cannot disable it

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Fixed: the counter now advances only after a successful send (Firmware/Protocol/identium_protocol.c:1047-1052), and the period timer is updated only on success (:1213-1218). Still present: (1) the first heart_beat carries 1, because nextCounter = counter + 1 at :1024 and the counter starts at 0 (:936). ActModeDataFormat p.7 only says the count rises by one per upload; the rev 2 capture showing 0 first was not re-checked. (2) hb_cylce is still limited to 5..7200 (configuration_service.c:1283; configuration_storage.c:127-128), so 0 cannot disable it. Heartbeats can be turned off by unsubscribing event 2 (identium_protocol.c:1042-1045), which fits the subscribe model in ActModeDataFormat. (3) The binary 0x04 heartbeat (mainboard_type, rfid_type, sw_version, num_ants, ant_numbers, hb_count) required by IDM-INB1-FW-001 section 3.1 p.4 is not implemented. A grep for heartbeat, 0xEE and mainboard finds only the JSON sender in identium_protocol.c and no binary frame encoder.

**Remaining work.** Start the count at 0 if Identium's reference behaviour requires it. Accept hb_cylce=0 as disabled, or document the event-2 equivalent. Implement the binary 0x04 heartbeat (mainboard_type 0x10) as part of the binary TCP protocol.

#### R2-35 — Legacy SET_SESSION bypasses the configuration service, so the reported, stored and actual module session can diverge

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Fixed for the legacy command: host_interface.c:1781-1785 now runs SILION_Application_SetSession() and then Configuration_Service_SaveSession(). Rev 2 host_interface.c:975-978 called only SetSession. SaveSession (configuration_service.c:462-489) persists the session and updates g_liveConfiguration.session. SET_GEN2 with fast=1 does the same (host_interface.c:2594-2595). What remains: (a) ApplyStaticConfig still compares against g_liveConfiguration.session, not against readerConfig.session, the value the module actually holds (configuration_service.c:2198, 2251-2253). (b) The new module bridge forwards raw module frames to the module (module_bridge.c:20-21; SILION_SetRawMode(1) at sillion_application.c:2446). A Set-Protocol-Config frame from the PC tool can therefore change the module's session without touching the config. BridgeRelease (sillion_application.c:2453-2480) does not re-apply the stored session, so the rev 2 failure mode can still happen: a later set_static_conf with the stored value skips the module and replies ok. Whether the vendor tool actually sends such a frame is unverified. (c) SET_SESSION changes the module before saving. If the save fails, the reply is ERROR but the module is already in the new session and the live config still holds the old one (host_interface.c:1782-1785).

**Remaining work.** Compare against readerConfig.session in ApplyStaticConfig/ApplyTempStaticConfig. Re-apply the persisted session (and Gen2/region/power) in SILION_Application_BridgeRelease, or read it back from the module. In SET_SESSION, roll the module back when SaveSession fails.

#### R2-36 — set_runtime_conf/set_static_conf reject any key other than hb_cylce, start_inv_atbeg and session, including keys the firmware itself returns in the get JSON

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Runtime parser extended. glob_params now accepts hb_cylce, s_buf_size, thwdt_net_timeout, thwdt_inv_timeout, start_inv_atbeg and remote_cmd_reply (configuration_service.c:1281-1334). command_data now accepts upload (recv_timeout, clr_r_buf_time, hw_inf, data_aggr, client_ack/crc_enable=0), events and cus_param (l.1705-1752). Unknown command_data and upload keys now return ERR_UNSUPPORTED 'not supported operation' (l.1754-1760, 2141-2148). Still broken for round trips: BuildActiveJson emits glob_params.name (l.872), upload.bin_msg_fmt_ver/tagcom_raw_mode (l.902), upload.http (l.939), upload.tcp (l.953), gpo_act (l.982), tag_json_format (l.1004) and gpi_trigger (l.1030). Each of these is rejected; 'name' gets the generic 'invalid configuration' (l.1335-1338). The static path is unchanged from rev 2: ParseStaticData accepts only protocol (l.1977), ParseStaticProtocol only gen2 (l.1917), and ParseStaticGen2 only session (l.1843-1846). Yet BuildStaticJson emits rf (l.1068), gen2 target/q/profile (l.1082), gpos, uart1/2 and ethernet (l.1122). set_static_conf over TCP goes straight to ApplyStaticConfig (identium_command.c:817-830). Region, power and network can only be set through the vendor config path, not through the Identium JSON.

**Remaining work.** Accept and ignore unchanged read-only fields, so a get-then-set round trip works. Add name, tag_json_format and the static rf/gen2/ethernet fields, or reject them per field with the unsupported code. Do the same in the static parser, which still fails the whole request on any key except protocol.gen2.session.

#### R2-43 — Config JSON reports GPO states as constant 0 and offers no GPI-trigger/GPO-action or power-on GPO config; a mandated reboot silently drops latched outputs

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Improved: unknown command_data keys such as gpo_act and gpi_trigger in set_runtime_conf now return ERR_UNSUPPORTED 'not supported operation' instead of a generic invalid (configuration_service.c:1754-1760, 2141-2148). Unchanged: BuildStaticJson still emits a literal gpos array with state 0 for all outputs (configuration_service.c:1106-1107). gpo_act [] (l.982) and gpi_trigger (l.1030) are still literals. Industrial_IO_GetOutput (industrial_io.c:185) still has no caller, and gpio_service.h has no output getter (l.20-45). In set_static_conf, gpos is still rejected with the generic invalid (l.1977-1980). identium_command.c is byte-identical to rev 2: set_runtime_conf always schedules a reset (l.810-813). After the reset, Industrial_IO_Init drives the outputs low (industrial_io.c:133-137) and gpio_service zeroes its state (gpio_service.c:95-99). Nothing restores latched or power-on GPO states, and nothing documents the loss.

**Remaining work.** Report live output states in the gpos array (through a gpio_service getter or Industrial_IO_GetOutput). Reject static gpos with the unsupported code. Either persist and restore power-on GPO states across the mandated reboot, or document that it resets the relays.

#### R2-45 — Every reconnect reuses the fixed source port 40001

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Paths are relative to 271026/_fw/src/STM32_Firmware. Only the HTTP upload mode changed. In ethernet_service.c:204 `sourcePort = PRODUCT_PROTOCOL_TCP_LOCAL_PORT;` (40001, product_config.h:17) still applies to the TCP-JSON mode on every attempt. TCP-JSON is the default mode: READER_SETTINGS_UPLOAD_TCP = 0 (reader_settings.h:29), and reader_settings.c:364 forces it when a V1 record is loaded. HTTP mode cycles through 50001-50999 (ethernet_service.c:205-209). Its counter `ethernetHttpSourcePort` is a static set to 0 (:33) and never seeded, so after every reset the first HTTP connection again uses 50001, the same 4-tuple as the first connection of the previous boot. The 30 s keep-alive (w5500.c:659,705) is not new: rev 2 had it at w5500.c:633,679. The close-on-CLOSE_WAIT path at ethernet_service.c:183-189 is unchanged. The Wi-Fi upload path leaves the port choice to the ESP32 (wifi_service.c:812). That firmware is not in this tree, so this part is unverified.

**Remaining work.** Rotate the source port in TCP-JSON mode too, not only HTTP. Seed the start point so it differs after each reset (MCU UID xor tick, or a value saved per boot). Keep the port out of the listener ports 5000/8080.

#### R2-50 — Repository and code hygiene: no README/CHANGELOG/version, empty dirs, no tests/CI, ST copyright on Identium code, two JSON parsers, magic numbers

`PARTIAL` · rev 2: LOW → now: LOW

**Rev 3 evidence.** Improved: Tools/ now holds 9 scripts and hostcheck/ holds 4 host tests. The Makefile has a hostcheck build (Makefile:215-233), the test builds use -Werror, and the lead auditor ran them (all PASS). PRODUCT_ETHERNET_TCP_PORT is now used (host_tcp_server.c:24). Reader name and server IP/port are now runtime settings with compile-time defaults (reader_settings.c:242-246, 273, 603). Still open: (1) No README or CHANGELOG at the tree root. (2) Empty directories remain: Firmware/Drivers/timer, Drivers/dma, Services/io, Services/rfid, Application/reader, Application/inventory and Protocols/host. Firmware/Application/Protocols is still a 0-byte file, Firmware/Protocol still sits beside Firmware/Protocols, and Tools/__pycache__ is shipped. (3) No CI configuration. (4) JSON parsing has grown to three hand-written parsers: identium_command.c:50,115 (strstr), configuration_service.c:33-250 (cursor-based) and the new vendor_config.c:31-280 (VendorJson_*). (5) The literal error code 20003U appears 16 times (e.g. gpio_service.c:229,240,273). (6) PRODUCT_RFID_TX/RX_ANTENNA, READ/WRITE_POWER, SESSION and both baud rates (product_config.h:36-50) are still unused, while firmware_configuration.c:89-98 hard-codes 1U/2500U. (7) The default reader name on the F411 product is still "RFID-F429-DEV" (product_config.h:22). (8) Src/main.c:9 still carries the ST copyright on Identium code. (9) The running firmware has no compiled-in version. FW_STATUS (host_interface.c:842-857) reports the version of the image in staging (firmware_update.c:412-430), which reads NONE on a unit flashed over SWD. The module-bridge VER reply returns a hand-kept 1.0.0.8 (product_config.h:53-56).

**Remaining work.** Add a README (boards, build, flash layout, signing) and a CHANGELOG. Compile a FW_VERSION from git describe into the app and report it in FW_STATUS, VER and the Identium status JSON. Remove the empty directories, the 0-byte file and __pycache__, and merge Protocol/ with Protocols/. Settle on one JSON parser. Name the error codes. Wire the PRODUCT_RFID_* defaults or delete them. Change the default name for the F411. Add Identium headers. Add a CI job that builds f411 and f429 and runs the hostcheck tests.

#### R2-51 — Protocol runs only on Ethernet socket 0; serial/USB transport, Modbus, HTTP, ACK flow and offline buffering are still missing

`PARTIAL` · rev 2: INFO → now: INFO

**Rev 3 evidence.** Added since rev 2: an HTTP upload mode (Services/ethernet/http_upload.c; ethernet_service.c:109-143). A Wi-Fi upload path through the ESP32 (active_upload_transport.h:6-7, .c:43-45). A flash tag log, appended for every tag (sillion_application.c:27-31; tag_log.c:40-47). A serial and USB route for the EE configuration protocol (host_transport.c:55-69, 245-261 -> Configuration_Port_HandleLinkRequest). Still open: (1) The Identium active-mode JSON protocol only runs over Ethernet socket 0 and Wi-Fi. Identium_Protocol_PushTcpByte is registered only at firmware_configuration.c:518 and :529, with no serial or USB path. (2) The serial CRC16 trailer is counted in the frame length (host_transport.c:246-249) but never verified (configuration_port.c:389-416). (3) No Modbus anywhere: grep finds only a CRC comment. The Modbus spec defines register 0xB00 (Modbus doc p.2) and TCP port 8080 (p.3), and 8080 is now taken by the EE config listener (configuration_port.h:7). (4) No MQTT, yet identium_protocol.h:10-12 still says the envelopes are 'shared with the HTTP and MQTT transports'. (5) There is no ACK flow. CLIENT_ACK is 0 (product_config.h:27), and client_ack=1 is rejected as unsupported (configuration_service.c:1471-1489). (6) The offline log holds about 65k records (32 B records in half of the GD25Q32; LEAD test kept=65,376), and nothing replays it to the server. identium_protocol.c and http_upload.c never call TagLog_*, so stored tags can only be read back with the ASCII TAGLOG_READ command (host_interface.c:234-289).

**Remaining work.** Roll-up item: the serious gaps belong to FW-01 (offline buffer, critical), FW-09 (Identium host protocols, high) and R2-03 (port 8080 / Modbus-TCP, high). Count them there, not here. Under this item: correct the identium_protocol.h:12 comment, verify or reject the serial CRC, implement Modbus RTU/TCP and client_ack, and replay the flash log to the server on reconnect.

#### R2-53 — Static footprint grew by about 17 KB of RAM; flash for the additions is about 29 KB at -Os (unlinked upper bound)

`PARTIAL` · rev 2: INFO → now: LOW

**Rev 3 evidence.** Improved: every shipped build now includes a linker .map (build/{debug,f411-debug,f411-release,boot-f411,boot-f429}/*.map), and the Makefile links with -Map and --print-memory-usage (Makefile:101-103). Measured from the F411 release map: .data 0x178 = 376 B (map:3899) and .bss 0x183f8 = 99,320 B (map:3973). That is 99,696 B of static RAM, against 27,897 B in rev 2. Largest symbols: http_upload g_queue 12,288 B (map:4355), g_request 8,736 B, identiumTagBatchBuffer 8,193 B, identiumTagQueue 8,192 B, usbTxQueue and Silion rxQueue 4,096 B each, plus the wasted UserTx/RxBufferHS (2x2,048 B). Adding _Min_Stack_Size 0x4000 and _Min_Heap_Size 0x200 (Boards/f411/STM32F411CEUX_FLASH.ld:41-42) commits about 116.6 KB of the 128 KB RAM (:47), about 91%. Flash is about 92.6 KB of the 112 KB slot (:51), about 83%. Nothing enforces a budget: the Makefile only prints $(SIZE) (:121, :158), and there is no CI.

**Remaining work.** Add a RAM/flash budget check to the f411 build, for example fail above 90% RAM or 85% flash after the reserves. Recover the known waste (R2-47, R2-48) before adding Modbus, ACK flow or E710 support. Track the numbers per release (overlaps FW-60).

### FIXED (6)

#### FW-03 — Stack buffer overflow in VCP_SendTag (char buffer[160]) for long EPCs (unbounded sprintf)

`FIXED` · rev 2: NONE → now: NONE

**Rev 3 evidence.** Firmware/Services/VCP/ was removed in rev 3 (rev2_rev3_filelist.txt: 'Only in R2/Firmware/Services: VCP'), and VCP_SendTag no longer exists (grep finds no VCP_ symbol in Firmware/Src/Inc). The legacy tag line moved to HOST_SendTag in Firmware/Application/Host/host_interface.c:640-718. It keeps the rev 2 fix: `char buffer[256];` at :642, and all three formatting steps (prefix :652-668, the %02X loop :670-690, the RSSI/ANT/FREQ/TIME tail :692-712) use snprintf(&buffer[pos], sizeof(buffer)-pos, ...) and return early when written<0 or written>=remaining. The worst case (epc[64], silion_protocol.h:193-204) is still 189 bytes, below 256. EPC length is bounded at the source: silion_protocol.c:1211, :2560 and :3292 reject epcBytes > sizeof(epc), and the Identium queue rejects it again at identium_protocol.c:462-468. The weak stub at sillion_application_port.c:551-555 does nothing. The F411 map shows the strong host_interface.o HOST_SendTag linked (build/f411-release/rfid_controller_f411.map:1762-1764) and the weak .o copy in the discarded sections (:687-688). The new TAGLOG_READ line (host_interface.c:237-289, line[128]) is also bounded: TAG_LOG_EPC_MAX_BYTES=16 (tag_log.h:29) gives at most about 88 characters.

**Remaining work.** Nothing is needed for the overflow. Optional low-priority hardening only: (a) cap the parsers at the Gen2 maximum of 62 bytes rather than 64 (silion_protocol.c:1211/2560/3292); (b) add a host test that formats a 62/64-byte EPC through HOST_SendTag (hostcheck/ has no host_interface test); (c) count tags dropped on a formatting failure instead of returning without a trace (:658-666, :702-710); (d) in HOST_SendTagLogRecords, clamp the accumulated pos (host_interface.c:281) in case the record format grows.

**Skeptic.** An independent skeptic tried to refute this claim against the F411 build and agreed.

#### FW-04 — A malformed SINGLE_POLL/SYNC_POLL permanently locks the host command parser

`FIXED` · rev 2: LOW → now: NONE

**Rev 3 evidence.** Firmware/Application/Host/host_interface.c (rev 3): the malformed SINGLE_POLL value path calls `goto host_command_done;` at :1067. Both SYNC_POLL error paths do the same, at :1178 (sscanf fails, 0 or >65535) and :1190 (trailing garbage after SYNC_POLL). The label host_command_done: is at :2644 and resets the parser (`hostRxIndex = 0U; hostRxBuffer[0] = '\0';` at :2650-2651), then `continue;` keeps draining the queue. An awk search for `return` inside HOST_Interface_Task (:869-2685) finds only a comment at :1085, so none of the many new rev-3 command branches (FW_*, TAGLOG_*, SET_GEN2, SET_HTTP ...) can skip the reset. The task is reachable: main.c:146 runs LOOP_MONITOR_RUN(LOOP_TASK_HOST, HOST_Interface_Task()), and it is linked in the F411 map (.text.HOST_Interface_Task at map:1765-1766).

**Remaining work.** The lock-up is gone. Robustness items from the rev-1 card are still open (low/quality): SINGLE_POLL still uses atoi (:1051), so 'SINGLE_POLL,100junk' is accepted and an overlong value is undefined behaviour; SYNC_POLL/SET_SESSION and others still use sscanf "%u" without an end check (:1163, :1773). Switch to strtoul plus an end-pointer check, as the newer TAGLOG_READ parser already does (:248-255). There is still no host test of malformed-then-valid command sequences (hostcheck/ covers protocol, tag log, boot image and update only). The ~1,800-line task has no dispatch-table refactor.

**Skeptic.** An independent skeptic tried to refute this claim against the F411 build and agreed.

#### FW-27 — Clock tree left at HSI 16 MHz with no PLL; HSE_VALUE mismatch and no 48 MHz USB clock

`FIXED` · rev 2: LOW → now: NONE

**Rev 3 evidence.** The F411 build now has its own clock tree. Src/main.c:167-211 is selected by -DSTM32F411xE (Makefile:61; F411 CubeIDE configuration .cproject:214). It sets voltage scale 1, HSE on, PLLM=4, PLLN=96, PLLP=/2 and PLLQ=4. That gives a 2 MHz VCO input, a 192 MHz VCO, 96 MHz SYSCLK and an exact 48 MHz USB clock, with APB1 /2 (48 MHz), APB2 /1 and FLASH_LATENCY_3. It matches Boards/f411/F411_custom.ioc:104-116 (HSE 8 MHz, PLLM 4, PLLN 96, SYSCLK 96 MHz, 48 MHz Q clock). The F429 branch keeps 168 MHz (:213-258). HSE_VALUE is 8 MHz (Inc/stm32f4xx_hal_conf.h:102-104). USB now uses OTG_FS on PA11/PA12 with AF10 and an OTG_FS_IRQHandler under STM32F411xE (Firmware/USB_DEVICE/Target/usbd_conf.c:47-105, usb_irq.c:4-9). That matches the netlist, where PA11/PA12 go to D1 (USBLC6). The rcc.c register decode gives correct PCLK1/PCLK2 for the F411 settings (Firmware/Drivers/rcc/rcc.c:13-89). The original defect (HSI 16 MHz, no PLL, wrong HSE_VALUE, no 48 MHz clock) is gone on the target board. Hygiene left over, not functional: (1) Two timebases: HAL programs SysTick, then Time_Service_Init reprograms it through raw registers (time_service.c:35-39). HAL_GetTick is overridden (usb_hal_port.c:10-13), and that file's comment at :5-8 wrongly says HAL_Init is not called. (2) rcc.c duplicates the clock maths with BOARD_HSE_FREQUENCY_HZ (rcc.h:24-26). (3) There is no HAL_RCC_EnableCSS. (4) The .cproject cpuclock is still "16" (.cproject:27, 120, 198).

**Remaining work.** Nothing required for the defect itself. Optional cleanup: keep one timebase (drop the raw SysTick reprogramming), use HAL_RCC_GetPCLKxFreq instead of rcc.c, enable HSE CSS with an NMI handler, set the .cproject cpuclock to 96/168, and fix the stale comments in usb_hal_port.c and hal_conf.

**Skeptic.** An independent skeptic tried to refute this claim against the F411 build and agreed.

#### FW-10 — Firmware cannot be built for the real STM32F411 board

`FIXED` · rev 2: HIGH → now: NONE

**Rev 3 evidence.** The firmware now builds for the F411 custom board. Makefile:52-61 (BOARD=f411): CHIP_ID 0x431, Boards/f411/STM32F411CEUX_FLASH.ld, Boards/f411/startup_stm32f411ceux.s, -DSTM32F411xE -DTARGET_BOARD=2. .cproject:176-242 adds an 'F411' CubeIDE configuration (MCU STM32F411CEUx, STM32F411xE, TARGET_BOARD=2, F411 linker script). Drivers/CMSIS/.../Include/stm32f411xe.h is added (rev2_rev3_filelist). The linker script has 128K RAM and a 112K FLASH slot at 0x08004000, keeping sectors 5-7 for settings (STM32F411CEUX_FLASH.ld:45-52). Settings and config storage sit in F411 sectors 5/6/7 (reader_settings.c:28-31; configuration_storage.c:9-18). The F411 clock is 8 MHz HSE to 96 MHz with an exact 48 MHz USB clock (main.c:167-212). USB uses OTG_FS on the F411 (usbd_conf.c:47-48, 78-91). The F411 BSP pins match the netlist (Firmware/BSP/f411_custom/board_config.h:27-153 vs work/u7_pinmap.txt). The shipped build/f411-release/rfid_controller_f411.map links the f411_custom BSP and the F411 startup. Lead measured about 92.6 KB of the 112 KB slot and about 116.6 of 128 KB RAM.

**Remaining work.** Nothing for FW-10 itself. Watch items: flash headroom is about 17% and RAM about 9%. F411 debug builds cannot use -O0 (Makefile:7-8). No on-hardware bring-up of the F411 build appears in the drop.

**Skeptic.** An independent skeptic tried to refute this claim against the F411 build and agreed.

#### FW-56 — The stack reservation is below the computed worst case and there is no stack-overflow detection

`FIXED` · rev 2: LOW → now: NONE

**Rev 3 evidence.** Paths are relative to 271026/_fw/src/STM32_Firmware. Reserve: Boards/f411/STM32F411CEUX_FLASH.ld:42 sets _Min_Stack_Size = 0x4000 (16 KB), and STM32F429ZITX_FLASH.ld:48 matches. The shipped map confirms it (build/f411-release/rfid_controller_f411.map:1445), and the reserve ends at 0x2001C7A8, below the 0x20020000 top of RAM (map:4597). Runtime guard: Firmware/Services/system/stack_monitor.c:37-64 paints the stack and :78-95 measures the high-water mark. :16-34 arms a 32-byte no-access, execute-never MPU region at the bottom of the reserve. It is called first thing in Src/main.c:113-114 and is linked (map:3132-3134). The high-water mark is reported as STACK_USED/STACK_SIZE (diagnostics.c:149-152). HardFault is now useful: the HardFault/MemManage/BusFault/UsageFault/NMI handlers are naked (Src/stm32f4xx_it.c:62-66, 98-120). They switch to a 512-byte private stack (fault_record.h:77-98) and save CFSR, HFSR, MMFAR, BFAR and the stacked PC/LR when the stacking succeeded. Then they call NVIC_SystemReset (fault_record.c:147-176). MemManage, BusFault and UsageFault are enabled at fault_record.c:96-98. Analysis outputs: -fstack-usage is set in Makefile:99, and 82 .su files ship in build/f411-release. Every frame is static, and the largest is HOST_Interface_Task at 840 B (host_interface.c:869), far below 16 KB.

**Remaining work.** Info only. 1. Record a measured STACK_USED from a soak test on the F411 board, or a call-graph worst case, in the release notes. 2. The guard is 32 bytes. A frame larger than that (for example the 840 B one) can step past it without touching it, so detection is likely but not guaranteed. Consider a 256 B-1 KB guard. 3. Force an overflow on hardware once and confirm the result is a MemManage record plus a reset, not a lockup.

**Skeptic.** An independent skeptic tried to refute this claim against the F411 build and agreed.

#### R2-42 — No GPI polarity handling: the real board's BC847 stages invert, but the F411 path reports raw pin levels

`FIXED` · rev 2: LOW → now: NONE

**Rev 3 evidence.** New in rev 3: industrial_io.c:46-50 defines INDUSTRIAL_IO_INPUT_LEVEL(raw) as raw^1 when BOARD_INPUTS_ACTIVE_LOW is set. It is applied in Industrial_IO_GetInput (l.230-233) and GetAllInputs (l.290-293). f411_custom/board_config.h:124 sets BOARD_INPUTS_ACTIVE_LOW 1U, with inputs PB3/PA15/PB4/PB15 (l.125-132). Rev 2 industrial_io.c had no such flag (grep). industrial_io.c is the only file that calls GPIO_ReadFromInputPin, so gpio_service, get_gpi and the bridge IOGET all see the inverted value. The netlist confirms the polarity: for example, PB15 (U7.28) connects through R41 to the Q5 MMBT2222A collector with R62 10k pull-up, and Q5's base is driven from CN5.1 via R58. Q6/Q7/Q8 with R44/R43/R42 form the same stages for PB3/PA15/PB4. Input energised gives a low pin, which the firmware reports as 1.

**Remaining work.** No firmware work. Add a factory/bench test that applies 24 V to each GPI and checks that the reported state is 1, and 0 when open.

**Skeptic.** An independent skeptic tried to refute this claim against the F411 build and agreed.

## Appendix A — candidates refuted or unresolved

None.

## Appendix B — reviewer coverage

### SEC — Security and the update chain

Scope: I read Bootloader/* in full (boot_main, boot_image, boot_staging, boot_flash, boot_startup, boot_watchdog.h, linker scripts, micro-ecc README), plus Firmware/Common/firmware_image.{c,h}, sha256.c length handling, Services/update/firmware_update.{c,h}, the FW_* handler and dispatch in host_interface.c, host_transport.c claim logic, host_tcp_server.c, configuration_port.c, the vendor_config.c write paths, module_bridge.c forwarding and IPSET, discovery_service.c, the wifi_service.c host socket, reader_settings defaults, brownout.c, diagnostics RDP reporting, tag_log chip-erase, gd25q init, Tools/fw_sign.py, Tools/fw_update.py, the Makefile (protect/flash/image/hostcheck), and the rev-2 tree for comparison. I also checked FW-001 §3/§8/§10/§13 and design spec F-07/§7.5. I ran no firmware binaries or tree scripts. No private key bytes were read into any output. Verified correct (strengths):
(1) Sound signature design. ECDSA P-256 over SHA-256 of the first 128 header bytes, which bind the board ID, load address, image size, version and payload SHA-256 (firmware_image.h:53-68; boot_image.c:33-48). micro-ecc is pinned to commit 541b3a7, unmodified, verify-only, with unused curves disabled (README_IDENTIUM.txt; Makefile:135-138). The header is bounds-checked before any flash use (firmware_image.c:11-24). The header is read once into RAM, and both the signature and the post-copy hash are checked against that RAM copy. (2) Interrupted uploads are safe. The header is written last (firmware_update.c:359-380). The old staging header sector is erased by the first payload page, because erasedEnd starts at 0 (:200-209). Finish validates the header, size and SHA-256 (:312-357), and Apply reads the header back. Offsets are strictly sequential, retries are idempotent, and lengths are bounded (:257-310). FW_* parsing uses strtoul with end checks, and 128-byte chunks fit the 320-byte line buffer (host_interface.c:311, 749-812). (3) An interrupted install is retried from intact staging. The state word only clears bits, so it needs no erase (firmware_image.h:44-51). The IWDG is kicked during erase/program (boot_flash.c:16-21; boot_main.c:77). Bootloader faults reset the MCU (boot_startup.c:41-44). (4) The vector table is checked and VTOR set before the jump. The app's SystemInit uses VECT_TAB_OFFSET 0x4000 (system_stm32f4xx.c:96-110). The shipped f411-release app is linked at 0x08004000 (map). Bootloader RAM is 0x2001E000+, clear of the app .noinit at 0x20018570 (maps). (5) Host tests cover tampering with the payload, signed fields, the signature, board/load/size, state and truncation (boot_image_host_test.c:106-162). (6) Both shipped .tlfw images have a correct SHA-256 and a valid signature. The shipped bootloaders embed the production key, not the test key. The f411-release .hex matches the .bin except for 8 bytes of alignment fill. (7) brownout.c avoids option-byte writes under a debugger or when RDP is not 0 (:42-59). GET_DIAG reports RDP and BOR (diagnostics.c:41-44,150). (8) Wi-Fi passwords are masked on read-back (configuration_service.c:775,784; vendor_config.c:658-663), and SET_WIFI enforces an 8-character minimum for non-empty keys. (9) UDP 15000 discovery is read-only (discovery_service.c:63-101). The MAC is now derived from the UID as a locally administered unicast address (reader_settings.c:230-239). (10) DEBUG_FAULT is compiled only with DEBUG and is absent from the release map. The Tools scripts contain no other secrets. (11) The application and bootloader use the same GigaDevice JEDEC check, and the app clears the BP bits (gd25q.c:194-210). Unverified: ECDSA verify time on the 16 MHz HSI against the roughly 32 s IWDG (not measured; a large margin is expected). Debug access to SRAM under RDP 1 (RM0383 not opened). The ESP32 firmware and its Wi-Fi security are not in the tree. No on-target update test is in the delivery: boot_main/boot_staging/boot_flash are covered by no test. Not re-reported: FW-24, R2-32 and R2-03 (the JSON/8080 lack of authentication in rev 2) except where rev 3 adds new paths (SEC-02). FW-37 and FW-17 are largely addressed by rev 3 (a bootloader, signed images and an RDP target now exist); what remains open is in SEC-03 to SEC-07.

### HWI — Board bring-up and hardware/firmware consistency

Scope: HWI (board bring-up and hardware/firmware consistency) for rev 3 F411 build (TARGET_BOARD=2). Compared Firmware/BSP/f411_custom/{board_config.h,board.c}, Firmware/BSP/industrial_io.c, Src/main.c SystemClock_Config, every peripheral init path (uart.c, spi.c, i2c.c, gpio.c, rcc.c, gd25q.c, w5500.c, esp32_link.c, serial_port.c, usbd_conf.c, usb_irq.c, uart_dispatch.c, time_service.c, Bootloader/boot_staging.c) against 271026/_fw/work/u7_pinmap.txt, net.py queries (U7, U1, U2, U5, U6, U8, U12, U19, CN1, CN2, CN5, CN7, CN10-CN15, Q1-Q8, U4/U9/U16, all relevant resistors) and the top assembly/silkscreen image. Confirmed the F411 release map contains Board_F411_*, OTG_FS_IRQHandler, I2C1 handlers, USART1/2 handlers, and no F429 BSP or OTG_HS code. No binaries or scripts from the firmware tree were run. VERIFIED CORRECT (strengths; rev 3 has effectively fixed the pin map; FW-25/R2-18/R2-42 are rechecked separately):
- Clock: HSE crystal mode (X1 8 MHz, 4-pad 3225, 22 pF on PH0/PH1). PLLM=4 gives a 2 MHz VCO input. N=96 gives a 192 MHz VCO. P=2 gives 96 MHz SYSCLK. Q=4 gives exactly 48 MHz for OTG_FS. AHB/1, APB1/2 = 48 MHz, APB2/1 = 96 MHz, FLASH_LATENCY_3, VOS scale 1 (main.c:167-212). 96 MHz rather than 100 MHz is the correct choice, because no 100 MHz PLL setting gives an exact 48 MHz. HSE_VALUE is 8 MHz (stm32f4xx_hal_conf.h). The F411_custom.ioc RCC values match. rcc.c PCLK math matches, so USART BRR works out to 115246 baud on USART1 and 115108 on USART2 (<0.1% error). I2C CCR=40 and TRISE=15 give 400 kHz. VTOR offset 0x4000 matches the app base 0x08004000. - SPI1 shared bus: PA5/PA6/PA7 AF5 are common to U1.33-35 and U6.6/U6.2/U6.5 (SCLK/SO/SI correct). W5500 CS is PA4 (U1.32). Flash CS is PA1 (U6.1, R11 pull-up), and its ODR is set high before it becomes an output (board.c:102-114). Mode 0 at 96/8 = 12 MHz suits both parts. Both drivers wait for BSY before raising CS. All SPI use is from the superloop (no SPI/EXTI ISR exists), so no arbitration is needed. The W5500 is reset for 2 ms then given 50 ms (w5500.c:267-283). GD25Q WP#/HOLD# are pulled up (R25/R33). The JEDEC/BP-clear/WIP handling is sound. The bootloader holds W5500 CS high, sets each CS through BSRR before switching it to output, runs at 2 MHz, and resets GPIOA/SPI1 before jumping (boot_staging.c:98-190). - USART1 PA9/PA10 AF7 to CN1.9/CN1.10, 115200 8N1. USART1 is on APB2, so it is clocked from PCLK2. - USART2 PA2/PA3 AF7 to U12 1A/2A. Mux S=PB14 (low selects the SP3232 1B1/2B1, high selects the SP3485 1B2/2B2; R18 pull-up). DE=PB12 (R21 pull-down) and RE#=PB13 (R20 pull-up), driven as separate lines with RE# following DE. DE is released on TC, not TXE (uart.c TC path), and the echo guard is correct. PA8 drives TPS22919 ON (R19 pull-up), which feeds SP3232 VCC (U2.16), and the firmware powers it only in RS-232 mode. (Comments call U2 a MAX3232; it is an SP3232EEY. This is cosmetic.)
- Relays: PB2 drives R51/Q3/U4/CN12 "GPO 1", PB10 drives R52/Q4/U9/CN13 "GPO 2", and PA0 drives R49/Q2/U16/CN14 "GPO 3", which matches the silkscreen. Gate pull-downs R50/R53/R54 hold the relays off before firmware runs, and outputs are written low at init. Output 4 is NULL. - GPIs: CN10.1 goes through R57/Q6/R44 to PB3, CN10.2 through R56/Q7/R43 to PA15, CN10.3 through R55/Q8/R42 to PB4, and CN5.1 through R58/Q5/R41 to PB15. These are NPN inverters with 10k collector pull-ups and 1k/100 nF filters. The firmware inverts them (BOARD_INPUTS_ACTIVE_LOW). The JTAG pins PA15/PB3/PB4 are released by switching them to input mode. - ESP32-C3: PB6 = IO6/SCL (U19.5), PB7 = IO10/SDA (U19.10), I2C1 AF4 open-drain at 400 kHz with 4.7k pull-ups. EN PB9 (U19.2) is driven open-drain (R17 10k + C66 1 uF) with 15 ms low and 800 ms boot delay, and the bus-recovery sequence is sound. The strapping pins are pulled up (IO2 R23, IO8 R24, IO9 R22 with boot jumper J2), so the module boots normally from flash. - USB: OTG_FS on PA11 (DM) / PA12 (DP) through USBLC6-2 (D1.4/D1.6), AF10. VBUS sensing is disabled; the HAL sets NOVBUSSENS and clears VBUSBSEN (stm32f4xx_ll_usb.c:330-338), so PA9 stays with USART1. Forced device mode leaves PA10 free. The FIFOs (Rx 0x80, Tx0 0x40, Tx1 0x80) total 320 words, the OTG_FS limit (ST CubeMX CDC template). - PB5/CN1.11 is held as an input with the R32 pull-up (see HWI-05). Buzzer on PC13 and LED on PB8 are mapped correctly (see HWI-08/09). The W5500 INT on PB0 is configured with a pull-up but never read; the firmware polls the chip over SPI, which is acceptable. - All 36 bonded GPIOs on UFQFPN-48 are accounted for. The firmware uses no pin that is absent from the board, and nothing reconfigures SWD (PA13/PA14). - BOR level 3 (2.7 V) is set for the F411. The fault record in .noinit (0x20018570) lies outside the bootloader RAM (0x2001E000-0x2001FFFF). MISMATCHES / UNDRIVEN FUNCTIONS found: Wiegand PC14/PC15 (HWI-01/02); RS-485 mode unreachable (HWI-03); PB5 module reset never driven (HWI-05); LED static (HWI-09); GPO 4 accepted and the GPO numbering differs from FW-001 (HWI-06); ESP counterpart firmware missing (HWI-07). Minor observations not raised as findings:
- board.c:90-100 sets W5500 CS and RST as outputs before writing them high, which gives a brief low glitch on each. This is harmless, because SCK is not yet in AF mode and W5500_Init later applies a full 2 ms reset. - serial_port.c:63 likewise drops PA8 low for a few cycles at init. - The board has no VBUS-sense pin (all GPIOs are used), so USB unplug cannot be detected and the D+ pull-up is enabled without VBUS. This is a hardware limit; see R2-15/R2-41. - Any Wiegand driver added later must follow HWI-02. Not re-reported because they are covered by the recheck stage: HSE-failure hang before the watchdog (R2-25), unbounded SPI busy-waits (FW-42), GPI debounce (R2-19), USB self-powered descriptor (R2-41), and the hard-coded MAC/IP (FW-08, still in w5500.c:296-330). Unverified:
- The PC14/PC15 5 V tolerance (ST pin table not opened). - The HNB09A05 buzzer type and voltage. - The RFID module's actual CN1 pinout (CN1.9 = module RXD and CN1.11 = RST follow the firmware comments and IDM-AUD-003 inference; no module manual is in the package). - Whether the shipped f411-release ELF matches this source exactly (it was built from a dirty tree). Its map is consistent with the source.

### BUF — Offline tag buffer (SPI flash tag log)

Paths are relative to /Users/identiumadhish/Desktop/CLaude/PCB/Anas/271026/_fw/src/STM32_Firmware/ unless stated otherwise. What I read in full: Firmware/Services/storage/tag_log.c and .h, Firmware/Devices/spi_flash/gd25q.c and .h, hostcheck/tag_log_host_test.c, and Firmware/Services/update/firmware_update.c (staging backend). Callers and related code I checked: sillion_application.c:25-32 plus the three PublishTag call sites; host_interface.c:193-307 and :1286-1320; firmware_configuration.c:440-520; Src/main.c:132-159; Identium protocol queueing (identium_protocol.c:36-70, 416-519, 1066-1094, 197-226, 1200-1204); http_upload.c queue; time_service.c; board.c:85-155; firmware_image.h:80-106; boot_staging.c:120-135; boot_main.c:92-112; the F411 linker script; the netlist for U6, R11, R25 and R33; and the f411-release map. tag_log.o is linked; g_tagLog is 0x81C bytes in .bss (map line 4413). Answers to the brief:
- Record format: 32 bytes, little-endian. seq u32, 48-bit ms time, antenna, rssi s8, readCount u8, a byte with 6-bit EPC length plus TRUNC/EPOCH flags, 16 bytes of EPC (a full EPC-128 fits; longer EPCs are truncated and flagged), then CRC16-CCITT over bytes 0-29. Slot = seq mod 65,536. - Partition map of the GD25Q32: 0x000000-0x1FFFFF is the tag log (512 x 4 KB sectors). 0x200000-0x3FFFFF is firmware staging (2 MB for an image of at most 112 KB plus a 256-byte header). Configuration is not on SPI flash; it lives in MCU flash sectors 5-7. - The 65,376 figure: 510 full sectors x 128 = 65,280, plus the 96 records in the head sector (300,000 mod 128). One sector is always kept pre-erased. erases = 2,343 = floor(300,000/128). I did not compile or run any firmware code. Strengths, checked and found correct:
- Wear across the 512 log sectors is perfectly uniform; staging cycles only on updates. - Power-loss handling at mount is sound. Torn programs are skipped (tag_log.c:394-402). The erase-ahead sector is re-verified, and erased if needed, before the oldest-record scan (:406-421). Every record is checked by CRC plus slot/sequence consistency (:209-227). The host tests cover torn program, torn first record of a sector, torn erase and a random-garbage chip, and all pass. Worst-case loss is the 64-entry RAM queue plus the module queue. - Sequence wrap at 2^32 is handled correctly by SeqDiff and the power-of-two mapping. - Ordering is strictly FIFO by sequence number, and TAGLOG_READ pages with a NEXT cursor. - Programs and erases are started and then polled, so the main loop is not blocked. The code never reads while the chip is busy (the host model asserts this at tag_log_host_test.c:88). firmware_update waits for any tag-log operation before touching the flash (firmware_update.c:57-61). - SPI1 sharing with the W5500 is safe. All bus traffic runs from the main loop; no IRQ handler touches SPI (I checked uart_dispatch.c and usb_irq.c). The flash CS is driven high before the SPI pins are configured (board.c:102-114). CS#, WP# and HOLD# have 10k pull-ups to 3V3. - Driver hygiene is good: WEL is verified before every program or erase (gd25q.c:118-125); the code waits for BSY to clear before releasing CS (:63-73); page-boundary and range checks are present (:252-293); it waits for WIP after a reset and releases the chip from deep power-down. - Mount time is about 2.5 s worst case and sits between watchdog refreshes (firmware_configuration.c:492-499). - When full, the log overwrites the oldest records, which matches the spec semantics. However, nothing counts or reports undelivered records being overwritten (folded into BUF-01). - On the F429 build the log is a no-op (BOARD_TAG_LOG_ENABLED 0). Requirement verdict: >=100k EPC-128 is NOT met with the fitted 4 MB part as partitioned (65,280 guaranteed). The 4 MB part can reach 100,096 only if staging shrinks to about 960 KB or less and the slot mapping becomes non-power-of-two. A 16 MB W25Q128 gives 261,888 records at a half/half split, after the JEDEC/size fixes in BUF-09. The spec's own 500k target needs about 16 MB for the log alone. Unverified, with reasons:
- GD25Q32E datasheet figures (100k P/E endurance, chip-erase and sector-erase maxima): the datasheet is not in the package, so I used the code's own comments (45 ms typical / 400 ms worst-case sector erase, chip erase \"several seconds\"). - Real tag read rates: I estimated them from the 115200-baud link, not measured them. - Whether HOST_SendString truncates TAGLOG_READ replies when a transport TX buffer is full: not traced. Prior items: FW-01 and R2-51 (offline buffer missing) are now partly addressed by real storage, so rev 3 adds new facts, recorded in BUF-01 and BUF-02. FW-26 and R2-23 (time) are referenced in BUF-08, FW-24 in BUF-05 and FW-30 in BUF-11. I did not re-report any of them.

### PRO — Identium host protocol conformance

SCOPE: I read in full Protocol/identium_protocol.c, identium_command.c (byte-identical to rev 2, so R2-16/17/22/31/32 are unchanged), Services/host/host_transport.c, ethernet/http_upload.c, host_tcp_server.c, ethernet_service.c, upload/active_upload_transport.c, configuration/configuration_port.c, discovery_service.c and module_bridge.c, plus the relevant parts of vendor_config.c, configuration_service.c, configuration_storage.c, reader_settings.c, host_interface.c (TAGLOG, FW_*, dispatch) and gpio_service.c. I checked the shipped F411 release map: HttpUpload_*, Module_Bridge_*, Discovery_Service_Task, Host_Tcp_Server_Task and Configuration_Port_HandleLinkRequest are all linked; Identium_Protocol_ReportReaderException is discarded. I compared against the TCP V1.0, Modbus V1.0, Utility V1.0 and the reader's ActModeDataFormat document PDFs, the Impinj EX10 protocol (§3.1/3.2/5.6) and IDM-INB1-FW-001. Nothing was executed. COMMAND COVERAGE TABLE (I = implemented, P = partial, M = missing)
TCP Protocol V1.0 binary active mode (reader as TCP client):
- Framing FF/EE, NLEN, DLEN BE16, CMD, FLG, STAT32, name, CRC on serial: M on the active socket, which carries NDJSON (ethernet_service.c:145-161 -> identium_protocol.c:956-1016). The 8080/COM config channel reuses the header with NLEN=0, no CRC and ignored flags (configuration_port.c:60-69; PRO-07). - Reader to server: 0x01 tag upload M (JSON tag_read); 0x02 GPI M (JSON gpi_changed); 0x04 heartbeat with board/module/sw/antenna/count M (JSON counter only); 0x05 fault M (PRO-06); 0x16/0x17/0x18 responses M (JSON command_reply). - Server to reader: 0x01 tag ACK M (client_ack=1 rejected by JSON, silently dropped by vendor path, PRO-08); 0x16 get GPI M (JSON get_gpi P, answers with a gpi_changed event); 0x17 set GPO M (JSON set_gpo P, R2-16/R2-17 unchanged); 0x18 restart M (JSON reboot I). Prior refs FW-09, FW-16, R2-01, R2-08; nothing new in rev 3. reader ActMode (HTTP), new in rev 3:
- POST with the command taken from a 2xx body: I (http_upload.c:686-703, 588-643). Limits in PRO-02, PRO-05, PRO-09. - reader_name 'name/IP': I (http_upload.c:286-307). - tag_read: P (short keys, no protocol, bank_data or first/last-seen timestamps; R2-11). - reader_exception: M in practice (PRO-06). - tag_coming: M (codes 4/5 rejected; reader says it is not implemented anyway). - heart_beat: I (counter starts at 1, R2-33; cadence bug PRO-05). - sync_time_req: P (PRO-06). - Commands: sync_time I (identium_command.c:659-688, epoch ms >= 1e12); set_gpo P; reboot I (reply, then reset after 100 ms; in HTTP mode the reply POST is usually lost to the reset, low). - reader 'Socket mode' is 'reserved'; the NDJSON-over-TCP dialect follows an unsupplied spec (R2-01). Modbus V1.0: RTU (address 2, FC03 at 0x0B00 x125 registers = 250 B), Modbus-TCP (MBAP on 8080) and the additional-data option are all M. A grep of all own code for modbus/0xB00/MBAP finds nothing. Prior refs R2-51, R2-03. New rev-3 interaction is in PRO-04. Utility / RFID Manager, new in rev 3:
- 8080 transparent module bridge: I (module_bridge.c). Its FF LEN CMD DATA CRC framing matches EX10 §3.1 (module_bridge.c:150-151, 365), and the 0xAA49 'Moduletech' stop frame is correct per §3.2 (SubCRC F3). - COM route on USB CDC and RS-232/485: I (host_transport.c:56-70). - IOGET, IOSET, GIO, SIO, VER, IPGET, IPSET: I (module_bridge.c:395-499). IPSET rejects multicast and FC:FF placeholder MACs but does not check that mask and gateway are consistent (low). - INITUPFV comm-board firmware upgrade over the network (Utility p.14): M. It is silently consumed byte by byte (module_bridge.c:505-507; FW-19). Field update exists only through proprietary ASCII FW_* commands. - UDP 15000 discovery: I (PRO-10). - Default IP 192.168.1.100: not followed (FW-08/R2-02). - update_fw_by_http: answers 'not supported' (identium_command.c:887-897). Error codes: the JSON replies use 0/1/2/3 and 20003/20004. Busy ('gpo op is running') and 'invalid set_gpo item' share 20003, and an invalid sync_time epoch returns 3 (execution) rather than 1 (low). The config channel uses status 0/1/2. None of these codes are defined in the supplied docs: unverified. STRENGTHS (checked and correct):
- The HTTP client is solid for its scope. It handles Content-Length, chunked (with trailers), close-delimited bodies and 1xx interim responses (http_upload.c:409-583). It validates the URL against request-line injection (:221-228), bounds the request size and resends on reconnect. It delivers a body only if it names command_type, which avoids reply ping-pong (:605-624). - R2-09 is fixed: TCP sends are all-or-nothing after a TX free-space check (ethernet_service.c:288-303), and batches are removed only after a full send (identium_protocol.c:817-828). - The packed tag queue is bounded and order-preserving, and its drop and expire counters are exposed in GET_DIAG (diagnostics.c:142-146). - s_buf_size is limited to 1500..8192 (configuration_service.c:1290-1297), which matches the 8193-byte batch buffer, so no overflow is possible (identium_protocol.c:668-679). - The heartbeat counter now advances only after a successful send (identium_protocol.c:1047-1052), a partial fix for R2-33. - The port-5000 disconnect releases host ownership (host_tcp_server.c:29-43), an improvement for FW-05. - 0xEE lengths are bounded (host_transport.c:251, configuration_port.c:325). The bridge forwards only whole frames. - Discovery honours the target-MAC filter (discovery_service.c:91-97). - set_runtime_conf honestly rejects unsupported client_ack/crc_enable/hw_inf values. - The design team supplied Python interop tools for TCP JSON, the config port, Reader Manager and serial. STILL OPEN, NOT RE-REPORTED: R2-31 (the RX line buffer is still not reset on disconnect, identium_protocol.c:1187-1191); R2-23 (32-bit elapsed time in Time_GetEpochMs); R2-45 (fixed source port 40001 in TCP mode); default reader name 'RFID-F429-DEV' (product_config.h:22, FW-44/R2-02). UNVERIFIED (no hardware, no legacy captures): the true meaning of clr_r_buf_time in Identium readers; the CRC16 algorithm intended for the serial 0xEE frames; whether legacy readers broadcast discovery replies; the byte layouts of the IOSET, SIO and IPSET replies; RFID Manager/ReaderSense behaviour against this bridge; HTTP behaviour against real servers (http_upload.c, module_bridge.c, configuration_port.c and discovery_service.c have no host tests).

### RFM — RFID module control

Scope read: all of Firmware/Application/Silion/* (rev 3), Firmware/Services/configuration/module_bridge.c, vendor_config.c (RF, work-state and deferred-execution paths), reader_settings.c/.h, firmware_configuration.c, the RF handlers in host_interface.c, the vendor mux in host_transport.c, configuration_port.c session handling, the F411 BSP RFID pins, uart_dispatch.c, the rcc/uart baud code, and the TagLog append path. Rev 2 to rev 3 diff (cmp/diff): silion_protocol.c/.h and uart.h are byte-identical to rev 2. The prior protocol-layer items (FW-13, FW-14, FW-21, FW-23, FW-31, FW-41, FW-45, FW-46, FW-47, FW-52) are therefore unchanged in rev 3 and left to the recheck stage. The region constants are also unchanged (product_config.h:45 default 8 = CE_LOW 865.7-867.5 MHz; silion_protocol.h:87), so R2-07/FW-02 still apply as written. AA48's start reply is still unchecked (FW-12). SET_POWER still allows up to 5000 (50.00 dBm) at sillion_application.c:1071, while the vendor path caps it at 3300 (REQ-06 clamp still open). The F411 release map contains Module_Bridge_Task, BridgeAcquire, VendorConfig_*, SetGen2, Firmware_Configuration_Task and TagLog_Append, so everything above is in the shipped build. Verified correct / strengths:
(1) CRC: the firmware's nibble-table CRC reproduces the protocol document vectors (0x03 -> 1D0C, 0x04 -> 1D0B), and module_bridge's canned Stop frame CRC (03 91) is correct. (2) Warm MCU reset is handled: ConfigureReader sends AA49 first (sillion_application.c:213-241). 0x04 in the APP layer returns success per protocol document §4.4, so warm reboots configure cleanly. The run-phase check reads the Program byte at rxBuffer[5] (0x12 = APP), the right offset. (3) Region, antenna and power are set before the first RF emission (warm-up inventory at :505-526). (4) Boot no longer hangs when the module fails: the watchdog is armed first (firmware_configuration.c:301-305), RFID degrades, and a retry loop runs (239-268, 542-550). This is a real improvement on R2-06, but see RFM-06. (5) Persisted session and Gen2 target/Q/profile are applied at boot (191-211). Host text RF changes are persisted only after the module accepts them (host_interface.c:1619-1623, 1674-1679, 1733-1737). (6) The new Gen2 encodings match protocol document §7.8: Target option 0x01 static / 0x00 dynamic, Q parameter 0x12, RF mode parameter 0x02. GetGen2 uses bounded snprintf/strncat. The profile whitelist reflects the document's note that an unsupported RF mode is acknowledged but reverts to the default. (7) Every SILION_Application_* entry point refuses to run unless IDLE, so nothing talks to the module while it is bridged. (8) The STOP reply is recognised by the 'Moduletech'+AA49 marker (silion_protocol.c:3700-3720), and HOST_EnsureInventoryStopped retries a lost STOP. (9) Module UART: USART1 on PA9/PA10, AF7, with an RX pull-up, so an absent module gives clean timeouts rather than noise. It runs 8N1 at 115200. BRR is computed from the PLL-aware RCC (96 MHz APB2 -> ~115,246 baud, 0.04% error). USART1_IRQHandler services the RFID handle on the F411 (uart_dispatch.c:37-53). Pins match the netlist: U7.30->CN1.9, U7.31->CN1.10. (10) The firmware comment that PA8/TPS22919 powers the RS-232 transceiver is correct: per the netlist, U8 OUT feeds U2.16 (SP3232). The module 5 V is unswitched and EN is tied high. That is a hardware limit (IDM-AUD-003 F-EL-12), not a firmware error. (11) Bridge mechanics are sound. The TX copy outlives the call, and TxBusyState is checked to avoid the FW-23 silent drop. The module-frame tracker (LEN+5) is correct, and local replies are inserted only at frame boundaries. A bridge path for raw-frame vendor module tools now exists, which bears on FW-20 (recheck stage). (12) Tag log entries are queued without blocking (64-entry RAM queue). (13) The loop monitor gives a measurable per-task maximum time through GET_DIAG. Unverified (no hardware, binaries not run): real loop-pass times and tag-loss rates; what CN1.11 is on the module actually fitted; whether the vendor tools send module region codes in rf.region; the identity and protocol of the planned E710 module; the module's power-on baud and power-on-to-APP state. Tools/*.py were not executed.

### ROB — Robustness and memory safety

Scope: rev 3 tree, own code. Read in full or in the relevant parts: host_interface.c (new rev-3 handlers and the byte parser), host_transport.c (vendor mux and queues), configuration_port.c, module_bridge.c, vendor_config.c (JSON navigator, filter, UTF-8 decoder, deferred execution), configuration_service.c (JSON parser and apply paths), identium_protocol.c (line assembler, tag ring, batch builder), identium_command.c (extractors), http_upload.c, wifi_service.c, esp32_link.c, i2c.c, w5500.c (sockets 0-3), ethernet_service.c, host_tcp_server.c, discovery_service.c, serial_port.c, usb_cdc_transport.c, usbd_cdc_if.c (receive path), uart.c, uart_dispatch.c, sillion_application_port.c, sillion_application.c (rev-3 diff and bridge code), tag_log.c, gd25q.c, firmware_update.c, reader_settings.c, configuration_storage.c, watchdog.c/.h, fault_record.c/.h, stm32f4xx_it.c, stack_monitor.c, loop_monitor.c, reset_cause.c, brownout.c, diagnostics.c, time_service.c, main.c, firmware_configuration.c, the F411 linker script, the bootloader (boot_main, boot_staging, linker scripts, startup), and the shipped f411-release map, .su and ELF (disassembled with llvm-objdump; nothing executed). silion_protocol.c, silion_protocol.h, uart.h and spi.c are byte-identical to rev 2 (CRLF-normalised diff); their known defects are rechecked separately and not repeated here. Strengths confirmed:
- Stack: the reserve is now 16 KB at the top of RAM with a 32-byte MPU no-access guard (STM32F411CEUX_FLASH.ld:42; stack_monitor.c:16-64). The stack is painted at boot and the high-water mark is reported in GET_DIAG. Largest frames in the shipped .su: HOST_Interface_Task 840 B, VendorConfig_SetActive 696 B, Configuration_Storage_Save 576 B, ReaderSettings_Save 504 B. The only recursion, Configuration_JsonSkipValue (88 B per level), is capped at depth 12 (configuration_service.c:230). Estimated worst case including 4-level ISR nesting (USB 0, USART 5, I2C 6, SysTick 15) is about 5 KB, so the reserve is ample. The map shows about 14 KB of unused RAM between heap end 0x200187a8 and the guard at 0x2001C000, which absorbs frames that could skip the small guard. Map figures match the lead's: .bss 0x183f8, .noinit 0x38 at 0x20018570, heap 0x200, stack 0x4000. _sbrk stops the heap at the guard. - Fault capture is well designed. The naked handlers switch to a private 512-B stack, validate the frame address and MSTKERR/STKERR, record PC/LR/xPSR/CFSR/HFSR/MMFAR/BFAR in .noinit with magic and check words, then reset. MemManage, BusFault and UsageFault are enabled separately. The bootloader RAM is confined to the top 8 KB so the record survives (bootloader_f411.ld:1-9). Error_Handler records its caller. - Watchdog: it is now armed at the start of Firmware_Configuration_Init, before any peripheral (firmware_configuration.c:290-305). The bootloader kicks it during install. Peripheral failures now degrade the unit instead of halting it, except Industrial_IO_Init (prior R2-06). - Reset cause now tests POR before BOR (reset_cause.c:86-99). - Brown-out: BOR level 3 is programmed only on unprotected chips with no debugger attached (brownout.c:37-80), which matches FLASH_VOLTAGE_RANGE_3. - Parsers found bounded:
  - configuration_service JSON: length-bounded strings, overflow-checked integers, depth limit. - vendor_config: span-bounded navigation, UTF-8 capacity checks, filter overflow flag, 2048-byte request cap. - HTTP response parser: 128-byte line cap, 2000-byte body cap, content-length cap of 1e6. - URL parser (host and path bounded). - Identium RX line: discarded on overflow. - Tag ring: EPC length validated when written. - BuildTagBatch: bounded by maximumBytes and the 8193-byte buffer; s_buf_size is limited to 1500-8192. - ESP32 link: payload length capped at 53 before the CRC index is used. - W5500 socket RX/TX: clamped to the buffer size and to RSR/FSR. - UDP discovery: copy bounded to 32 bytes. - Module bridge: frames up to 259 bytes fit the 1024-byte buffer, and PushLinkBytes clamps. - Config-port TCP path: dataLength checked against 2048. - FW update receiver: offset and length checks. - HOST_SendTag and TAGLOG line formatting: worst case 97 bytes in a 128-byte line, with a 16-byte EPC cap. - ISR/main sharing:
  - The SILION RX ring is SPSC with volatile 16-bit indices and a clear under PRIMASK. - Host RX push, USB TX ring and serial TX ring are updated under PRIMASK. - No DMA is used (the Drivers/dma folder is empty). - All tick comparisons are wrap-safe. - The I2C driver has loop-count timeouts; the ESP32 link has transfer timeouts, retries, 9-pulse bus recovery and EN reset. - GD25Q waits are bounded, and GD25Q_Init waits for an erase left running by a reset. - TagLog writes are non-blocking. Not re-reported because they belong to prior items being rechecked:
- SILION parser issues: FW-31, FW-45, FW-46, FW-47, FW-41. - UART driver: FW-23, FW-52, FW-53. - Unbounded SPI flag waits: FW-42. - Blocking superloop and watchdog fed inside waits: FW-34, FW-28. - Configuration storage: A/B selection R2-12, CRC scope R2-37, erase per save R2-26. - Config-port idle: R2-21. - Error_Handler hang paths: FW-06, R2-06, R2-25. - USB RX/TX flow control: R2-39, R2-13, R2-14. - HTTP drop counts: in the lead's host test results. Unverified:
- The exact GD25Q32E chip-erase time. - The RM0383 flash read-stall statement; the reference manual was not opened, and the design's own comment at firmware_update.c:106-107 is cited instead. - Cycle timings in ROB-12, which are estimates from the disassembly of the shipped ELF. - MISO idle level with a dead W5500 (ROB-06 holds for 0x00 or 0xFF reads, at 40 or 20 ms per pass respectively).

### NET — Networking and Wi-Fi/BLE

Scope: networking and Wi-Fi/BLE in firmware rev 3. Source root: /Users/identiumadhish/Desktop/CLaude/PCB/Anas/271026/_fw/src/STM32_Firmware. Files read in full: Firmware/Devices/w5500/w5500.c/.h, Services/ethernet/{ethernet_service,host_tcp_server,http_upload}.c, Services/upload/active_upload_transport.c, Services/wifi/wifi_service.c, Devices/esp32/esp32_link.c and the protocol header, Services/configuration/discovery_service.c, the relevant parts of configuration_port.c, reader_settings.c, module_bridge.c and vendor_config.c, the I2C driver init/IRQ paths, the F411 BSP, the main loop, and the f411-release map. I checked the netlist with net.py (U1, U19, CN11, J2, R22/R23/R24/R28). Nothing was executed except net.py. What I checked and found correct (strengths):
- W5500 bring-up and SPI:
  - Reset is 2 ms low then a 50 ms wait (w5500.c:267-285), which meets the datasheet minimum of 500 µs or more. - SPI1 runs mode 0 at 96 MHz / 8 = 12 MHz (board.c:260-274; main.c clock comments). - The flash CS is deselected before the first W5500 transfer (board.c:102-112). The GD25Q driver never reconfigures SPI and nothing touches SPI from an ISR, so I found no bus contention. - PMODE pins 43-45 float, giving auto-negotiation. Link is read from PHYCFGR. - Socket map:
  - S0: active upload client (TCP, or HTTP with rotating source ports 50000-50999). - S1: port 8080 config / module bridge. - S2: UDP 15000 discovery, now implemented (relevant to FW-40). - S3: port 5000 host text server. - S4-S7 unused. S1-S3 are set to 2 KB each, S0 keeps the 2 KB default. BSB values and 2 KB pointer-wrap handling are correct. - Socket robustness:
  - SEND-in-progress handshakes were added for S0, S1 and S3 (partial progress on FW-35; the RX_RSR single read remains, for the recheck). - S0 has a 30 s keep-alive and exponential reconnect backoff from 1 to 30 s, and CLOSE_WAIT is handled. - SendComplete pre-checks free space, which fixes R2-09 for messages up to 2 KB (see NET-03). - HTTP parser: handles the status line, Content-Length, chunked encoding with extensions and trailers, close-delimited bodies, 1xx interim replies, and 204/304. The body is bounded at 2,000 B. Only bodies containing "command_type" are executed, which avoids an ack-POST loop. The Host header includes the port. URL validation rejects control characters. The timeout is clamped to 2-120 s and is wrap-safe. - ESP link (STM32 side):
  - The STM32 is the I2C master: I2C1 on PB6/PB7 at 400 kHz, with CCR/TRISE correct for PCLK1 = 48 MHz (i2c.c:153-249). The ESP is the slave at 0x42. - Frames carry magic, version, sequence, command, socket, length and flags, with CRC-16 in both directions. Reply length is bounded at 53 or less before the CRC check, and sequence/command/socket are matched. - Stale replies are re-read. Retries are bounded, transfers time out at 80 ms with an abort, a 9-clock bus recovery runs, and EN is reset after 3 failures. - The ER IRQ closes transfers (i2c.c:797-851), and there is a guard against being addressed as a slave (esp32_link.c:646-658). - The 216-byte WIFI_CONFIG layout exactly fits SSID 32 and password 64. - Wi-Fi passwords are masked in readouts (configuration_service.c:775). Non-empty keys under 8 characters are rejected (host_interface.c:154-157). - Netlist: U19.5 = PB6 (R28 4.7k), U19.10 = PB7 (R30), U19.2 = EN = PB9 (R17) plus CN11.2. IO8 and IO2 are pulled up, and IO9 has the J2 download jumper. Observations below medium, not filed separately:
- RAM: http_upload alone uses about 23.7 KB (map: g_request 0x2220, g_queue 0x3000, g_body 0x7d0, rx 0x200), Wi-Fi queues about 5 KB, and the host TCP ring 2 KB. - SCAN_START/SCAN_RESULTS/RESET are defined but unused, and the status event bitmap is stored but never read. - g_transferEvent is not volatile and is cleared after the IT transfer starts (esp32_link.c:74, 209-215). It self-heals via the 80 ms timeout. - UDP ReceiveFrom/SendTo use one SPI transaction per byte. - The ESP is polled every 5 ms forever (there is no data-ready line), even with Wi-Fi disabled. Not covered by any host test: w5500.c, http_upload.c, wifi_service.c and esp32_link.c (Makefile:213-237 builds only identium_protocol.c, tag_log.c and the boot/update code). Prior items being rechecked separately, not re-filed:
- FW-08: Ethernet is static-only. A DHCP request (ip 0.0.0.0) is silently ignored (vendor_config.c:1118-1119), the discovery reply reports DHCP = 0, and every unit defaults to 192.168.150.50. The MAC is now derived from the UID (see NET-12). - R2-45: the TCP source port is still fixed at 40001 (ethernet_service.c:204). - FW-24: the command ports are still unauthenticated. Unverified:
- Whether the ESP32-C3 I2C slave can sustain 231-byte writes and 64-byte reads at 400 kHz; no ESP code exists to judge. - Whether a missing X2 crystal makes SPI read 0xFF. - The real Wi-Fi throughput on hardware.

### BLD — Build, release and engineering process

Scope covered (BLD). Makefile (all targets). .cproject (3 configurations), .project, .mxproject, both .launch files and both .ioc files. App linker scripts (F429/F411) and bootloader linker scripts. Both startup files. Tools/fw_sign.py and fw_update.py, and a skim of the other Tools scripts. hostcheck sources, stubs and the Makefile wiring. keys/. Every shipped build directory (map, ELF section headers, hex vs bin, .tlfw headers and signatures, DWARF producer). Licence files. rev2_rev3_filelist. Recheck items were reviewed so that only rev-3-new aspects are reported. Nothing from the firmware tree was executed. Checked and found correct (strengths):
(1) Rev 3 builds for the real STM32F411CEU6, which fixes the rev-2 blocker. The Makefile has F411/F429 × debug/release targets, -ffunction-sections/-fdata-sections/--gc-sections, -fstack-usage, map files and --print-memory-usage. A separate soft-float, -Os bootloader build compiles verify-only P-256 micro-ecc. (2) Safety nets: check-chip refuses to flash a build onto the wrong MCU (Makefile:175-183). Flashing verifies (-v). The 'protect' target sets RDP1 together with BOR and documents why Level 2 is avoided. Brownout.c skips option-byte writes while a debugger is attached. (3) The F411 linker script is right for the part: 128 KB RAM, no CCM, 16 KB stack sized for the MPU guard, a .noinit fault record, and the app region equals FW_APP_MAX_SIZE. The bootloader script asserts at least 4 KB of stack. Shipped bootloader: 6,220 B in 16 KB, boot_info at 0x08000100 as documented. SystemInit sets VTOR to 0x08004000. The bootloader sanity-checks SP/entry before jumping, and BootFlash_Sector maps F411 sectors correctly. (4) Shipped f411-release artifacts are self-consistent. Hex and bin differ only in gap fill. The .tlfw body equals the .bin, its SHA-256 matches, and its signature is valid under boot_public_key.c. The map's 83 objects equal the 83 delivered sources, and every meaningful literal string in the .bin is present in the delivered source. This is partial evidence that the source matches the binary; a byte-for-byte rebuild is unverified because no ARM toolchain was available. Built with GCC 14.3.1 (STM32CubeCLT 1.22.0) at -Os. IRQ handlers OTG_FS/USART1/SysTick resolve to strong definitions. RAM: .bss 99,320 + .data 376 + .noinit 56; stack top at 0x20020000. (5) Warning hygiene is good: clang -Wall over the 60 own F411 translation units gives 1 warning (the known spi.c:236), and -Wextra adds only 6 unused-parameter warnings. (6) Host tests are real engineering: tag-log torn program/erase and garbage-chip cases, SHA-256 known-answer vectors, cross-implementation signatures (python cryptography → micro-ecc), and update-receiver tests. The lead ran them and all pass. fw_update.py --no-apply is safe because the header page is written only at FW_APPLY (firmware_update.c:358-379). (7) GET_DIAG reports RDP and BOR levels, which enables an end-of-line check. The MAC is now a per-unit, locally administered unicast address derived from the UID, so no factory step is needed (32-bit hash; rare collisions; FW-08 is rechecked elsewhere). (8) micro-ecc's LICENSE is shipped with a pinned commit, the USB library's SLA0044 terms fit an STM32 product, and an F411 .ioc now exists with correct clock settings. Not settled / unverified: whether micro-ecc is truly unmodified upstream; whether the bootloader install and OTA were ever run on F411 hardware (no evidence delivered). File mtimes cluster at 27 Oct 15:49-16:03 for all bootloader/update/linker files, which suggests last-hour changes, but mtimes may come from a checkout and were not relied on. The tag-log capacity shortfall (65,280 records) and the 128 KB-sector erase stalls are owned by the storage/config reviews and referenced here only as consequences of the flash layout (BLD-02). Key paths: /Users/identiumadhish/Desktop/CLaude/PCB/Anas/271026/_fw/src/STM32_Firmware/Makefile, .../Tools/fw_sign.py, .../Firmware/Common/firmware_image.h, .../Boards/f411/STM32F411CEUX_FLASH.ld, .../Bootloader/boot_main.c, .../keys/fw_signing_key.pem (referenced by path only).

### REQ — Requirements coverage

Scope and method
- All paths are relative to 271026/_fw/src/STM32_Firmware/. - I read IDM-INB1-FW-001 Rev A and DS-001 v1.1 (converted with pdftotext), the AUD-002 REQ table and the Identium SDK documents (TCP V1.0, reader ActMode, Modbus V1.0, Impinj protocol region tables). - I traced the F411 call graph from Src/main.c:82-159 through firmware_configuration.c:278-561, and confirmed in build/f411-release/rfid_controller_f411.map that the modules involved are linked (tag_log, http_upload, wifi/esp32, module_bridge, discovery, firmware_update). - I verified the F411 pin map against the netlist with net.py. - Nothing was executed except net.py. The host-test results quoted come from LEAD_FACTS. Requirement coverage for rev 3, F411 target. Status, then evidence and prior refs. Items marked "recheck" are covered by earlier findings being rechecked separately and are not re-reported. Data and protocols
- **Offline buffer of at least 100k EPC-128 records: NOT MET (REQ-01).** The flash log holds 65,280 records, and the only way to read it is a manual ASCII pull. - **Identium TCP V1.0 binary active mode: NOT MET (recheck FW-09/FW-16/R2-01/R2-08).** Nothing in Protocol/, Services/upload, ethernet/ or configuration_port.c handles commands 0x16/0x17/0x18 or builds 0xFF upload frames. Active mode is newline-delimited JSON over TCP (identium_protocol.c:150-178, 521-579), and the 0x01 tag ACK is impossible (configuration_service.c:1484-1493). - **HTTP/JSON active mode: PARTIAL (REQ-05).** A real POST client exists, but it is http:// with literal IPv4 hosts only and sends non-reader keys. - **Modbus RTU+TCP: NOT MET (REQ-04).**
- **MQTT, HTTPS and HID (DS F-08): NOT MET.** grep for mqtt finds comments only; for HID see FW-39. Host links
- **RS-232: PARTIAL.** USART2 through the U12 mux, with an interrupt-driven TX ring (serial_port.c). The COM route carries legacy ASCII, the FF module bridge and EE config requests (host_transport.c:56-70). Baud is fixed at 115200. - **RS-485: NOT MET in a standard build (REQ-06).** DE/RE# handling itself is correct (serial_port.c:67-89; board_config.h:55-62). - **USB: PARTIAL.** Native OTG_FS CDC on PA11/PA12 with an exact 48 MHz clock (main.c:167-212; usbd_conf.c:78-104). VID/PID are still ST's (R2-10) and there is no HID (FW-39). - **Ethernet: PARTIAL.**
  - W5500 on SPI1, MAC derived from the MCU UID as a locally administered unicast address (reader_settings.c:226-238). - Persisted static IP, default 192.168.150.50. - Legacy TCP 5000 is restored (host_tcp_server.c); TCP 8080 carries config and the bridge; UDP 15000 answers discovery. - No DHCP (vendor_config.c:1118-1119; FW-08). - **Wi-Fi: PARTIAL; BLE: NOT MET (REQ-10).**

Board I/O
- **GPI/GPO/relays: PARTIAL.**
  - Netlist trace, all matching board_config.h:121-144:
    - GPI1 = PB3 (from CN10.1 via Q6)
    - GPI2 = PA15 (CN10.2 via Q7)
    - GPI3 = PB4 (CN10.3 via Q8)
    - GPI4 = PB15 (CN5.1 via Q5)
    - Relay 1 = PB2 (via Q3 to U4)
    - Relay 2 = PB10 (via Q4 to U9)
    - Relay 3 = PA0 (via Q2 to U16)
  - Active-low inputs are handled. - Access paths: JSON set_gpo/get_gpi/gpi_changed, vendor IOGET/IOSET/GIO/SIO, legacy SET_GPO/GET_GPI. - Still open: no debounce (R2-19), outputs revert to the previous state instead of the opposite (R2-16), one running pulse blocks all set_gpo (R2-17), no binary 0x02/0x16/0x17 (R2-08). - Observation: GPO1-3 map to the relays and GPO4 is rejected, whereas FW-001 §6 (p.9) maps GPO1 to a transistor output and GPO2-4 to the relays. Identium should fix the numbering for SLD1030 compatibility. - **Wiegand: NOT MET (REQ-09).**
- **Buzzer/LEDs: PARTIAL (REQ-09).** Hardware note, not verified against the datasheet in this review: PC13 sources base current through R66 into Q1, and PC13-15 are backup-domain pins. Pass to the hardware review. - **IP/Wi-Fi reset: NOT MET.** There is no button the firmware can read (hardware REQ-02/REQ-03), and the firmware has no fallback (REQ-09). Workaround: IPSET or vendor config over the USB/serial COM route. System functions
- **Time sync: PARTIAL (REQ-08).**
- **Field update: PARTIAL.**
  - Signed (ECDSA P-256, micro-ecc) staged update over USB, serial or TCP 5000. The bootloader verifies hash and signature, then copies and re-hashes (boot_main.c:48-116). The lead's host tests pass. - Gaps: REQ-02, REQ-03, the exposed signing key (lead-confirmed, not re-reported), no server-initiated or HTTPS update, and no module firmware update (FW-20). - **Configuration persistence: MOSTLY MET.**
  - ReaderSettings v4 (reader_settings.h:32-87) persists IP/mask/gateway/DNS/MAC, server IP and port, name, work state, region and power, antennas, Gen2 target/Q/profile, HTTP URL, and Wi-Fi settings. - The configuration record holds heartbeat, events and session. - Both are applied at boot (firmware_configuration.c:426-464). Storage is an append-only sector log with CRC32 and v1-v4 migration. - Gaps: serial settings (REQ-06), unvalidated region on load (REQ-07). A/B slot selection is rechecked under R2-12. - **India region: NOT MET (recheck FW-02/R2-07).**
  - The default is 0x08, which is CE_LOW 865-867 on China-certified modules (product_config.h:45; silion_protocol.h:87). - The real India code, 0x04 (Impinj protocol document Appendix 2, p.241), is rejected by the whitelist (sillion_application.c:1148-1156). - New in rev 3: the whitelist can be bypassed through the vendor static config (REQ-07). - **Watchdog/self-recovery: PARTIAL.**
  - Good: the IWDG starts before any peripheral (firmware_configuration.c:301-305). Boot continues in degraded mode when the W5500, RFID module or Wi-Fi fail, with an RFID retry every 10 s (:239-268, :476-550). Fault record, loop monitor, stack/MPU guard and brown-out level are in place. - Weak: the IWDG period is about 32 s (watchdog.h:37-38) and it is fed inside SILION_DelayMs (sillion_application_port.c:409-423; FW-28), so FW-001 checklist #13 (reset on a stall over 5 s) is not met. The network and inventory watchdogs default to off (product_config.h:32-33). - Industrial_IO_Init can still route to Error_Handler (:333-337), but on the F411 it cannot fail, because its pins are compile-time constants (industrial_io.c:114-151). - **Branding: NOT MET (REQ-11).**
- **Impinj E710 alternative: UNVERIFIED.** There is no EX10-specific code (grep ex10/impinj/e710: no hits). The Impinj document uses the same 0xFF framing and the same 0x0C and 0x97 commands, so the Silion driver may partly work. However, India-certified modules need region 0x04, which is rejected, and the driver supports one antenna only (FW-14). Strengths worth recording
- The firmware now really targets the STM32F411 board: Makefile BOARD=f411, an F411 linker script, HSE-to-PLL at 96 MHz with exact 48 MHz USB, and a pin map that matches the netlist. - The signed-update bootloader and the tag log both come with host tests. The log is designed to survive power loss: per-record CRC, torn-record skip, and a pre-erased next sector. - The upload queue is bounded, with dropped and expired counters exposed in GET_DIAG. - An HTTP POST client with retries and 8 KB batches. - Reader Manager compatibility: module bridge, IPSET/IOGET, and UDP 15000 discovery. - Persistent upload target, so R2-02 has been addressed. - A unique, locally administered MAC. - Boot continues in degraded mode instead of hanging. Not verified
- On-target behaviour. - Real tag rates, and therefore how many minutes of outage 65k records actually cover. - ESP32 firmware behaviour. - E710 compatibility. - Whether Reader Manager accepts a discovery board type other than 1.

### GAP — completeness critic

I found 5 new findings: GAP-01 medium, GAP-02 medium, GAP-03 low, GAP-04 low, GAP-05 info. None blocks. Nothing from the firmware tree was executed. **Source tree.** I listed the full source tree outside build/, Debug/ and F411/, and compared it against the rev2_rev3_filelist. Firmware/Application/Protocols is a 0-byte file. firmware_runtime.c and ring_buffer.c are 8-line stubs. **Files read in full or in the relevant parts:**
- Update and boot: firmware_update.c (full), firmware_image.c/.h, boot_main.c, boot_staging.c, boot_flash.c. - Settings: reader_settings.c (full: v1-v3 migration, append log, save), configuration_storage.c (sector map). - System services: watchdog.c/.h, brownout.c, system_fault.c, stack_monitor.c, the loop_monitor DWT set-up, diagnostics.c, time_service.c. - Start-up and interrupts: the stm32f4xx_it.c SysTick handler, the system_stm32f4xx.c VTOR diff against rev 2, the sysmem.c _sbrk limit, the main.c loop, and the firmware_configuration.c boot path, StartReader and retry. - identium_protocol.c: queue, stale discard, ReportTag, network and inventory recovery, Task. - Configuration: the configuration_service.c runtime keys and event filter, the vendor_config.c SetActive key forwarding and SetWorkState, module_bridge.c (header, IPSET/IPGET, release). - Upload and networking: ethernet_service.c, active_upload_transport.c, the wifi_service.c upload socket state machine, host_tcp_server.c (full), discovery_service.c (full). - Host side: host_transport.c (routing, PushByte, release); host_interface.c (FW_* handler, TAGLOG_STATUS/READ/CLEAR, SET_WIFI/GET_WIFI, hex parser). - Drivers: w5500.c (socket 2 UDP RX, socket buffer sizes, SetNetworkConfigValues), gd25q.c (full), tag_log.c (full), the serial_port.c RS-485 echo handling, i2c/esp32_link bus recovery, esp32_link_protocol.h, the usbd_conf.c VBUS setting, NVIC priorities, the gpio_service.c rev2/rev3 diff, product_config.h. **Documents and build outputs.**
- Read the full FW-001 text; also DS-001 §2.2, §3.1 F-01..F-08, §4.3 and §7. - reader has no thwdt definition. - Netlist components checked: U7, U8, U19, CN11, J2, R22, R49, R51, R52. - In the shipped f411-release build: the map (malloc/_sbrk linked) and the .su frame sizes. - Watchdog coverage in hostcheck/ and Tools/. **Checked and found correct (no new finding):**
- App VTOR is set to 0x08004000 (system_stm32f4xx.c:96-112, 181-182). - FW_DATA line length fits HOST_RX_BUFFER_SIZE (host_interface.c:311, 749-750 vs Tools/fw_update.py:29). - FirmwareUpdate_Write checks offset, repeat and size. The header check bounds imageSize and the staging size (firmware_image.c:11-24). State words need only bit clears (firmware_image.h:43-51). - The bootloader holds the W5500 CS high while it reads staging (boot_staging.c:107-108). - UDP RX advances by the full datagram length. All W5500 sockets use 2 KB buffers (total within 16 KB). - PushByte into the 1 KB host RX queue runs in a critical section. - RS-485 echo is suppressed, and I2C bus recovery exists. - USB VBUS sensing is off, which is correct because PA9 is USART1 TX. - hb_cylce is range-checked (5-7200). - No raw tick comparisons without subtraction were found in the services. - Tag-log slot mapping and mount survive the sequence counter wrapping. GD25Q init clears the BP bits and checks WEL. - Wi-Fi passwords are never echoed back (host_interface.c:56-58; configuration_service.c:753-785). - Discovery is read-only. **Unverified:**
- Boot-to-inventory time against FW-001 §10.1 (2 s); this needs hardware. - Whether the undelivered ESP32 firmware would implement its own OTA. - Relay gate pull-down values (R50/R53/R54 seen but not checked). **Claims resting on one reviewer.** I spot-checked NET-03 (socket 0 TX buffer is 2 KB) and NET-05 (the upload-open flag is never cleared on a remote close, wifi_service.c:475-540 and 765-800). Both are consistent with the code.

## Appendix C — how to verify any finding

Source: 271026/STM32_Firmware.zip, extracted read-only to 271026/_fw/src/STM32_Firmware/. Rev 2 for diffing: _audit2/fw2/RFID_Controller_F429_second_revision/. File-level diff: 271026/_fw/work/rev2_rev3_filelist.txt (58 new, 46 modified, 1 removed). Board pin map from the 13 Oct netlist: 271026/_fw/work/u7_pinmap.txt. Query it with: cd 231026/_a4 && python3 net.py comp U7.

To re-run the host tests, build them from source in a separate directory; do not run the shipped Makefile or binaries. The lead auditor's builds are in 271026/_fw/work/hostbuild/.

Do not copy keys/fw_signing_key.pem anywhere, and delete it from every copy of the delivery (SEC-01).

Prompt for a Claude session: "Read IDM-AUD-004 and 271026/_fw/work/. Verify BUF-01 and BUF-02 against tag_log.c, identium_protocol.c and the host test, and tell me whether you agree." Arguing against a finding with the source is welcome.

---

*Identium Tech Solutions · Confidential · Creating identities for all*
