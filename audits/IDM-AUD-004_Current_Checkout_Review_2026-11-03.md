# IDM-AUD-004 — Current Checkout Review

Updated findings • 3 November 2026 • INB1 firmware • Internal use

This is a revised, limited source-review edition of the 2 November audit. It replaces the wording of the findings explicitly reviewed below for this checkout. The original Word report is retained unchanged as the historical delivery audit.

## Decision and scope

No pilot-release approval is supported by this review. Automatic offline delivery, capacity, authenticated control and runtime update recovery remain open. The historical signing-key exposure must also be resolved. Internal bench work is the appropriate scope pending fixes and validation.

Baseline: Git c099c87bc61882cbe0df4cfed34e64dafc49ef6f. The working tree already contained one blank-line modification in STM32_Firmware/Src/main.c; it was preserved. No firmware was modified by this document update.

Method: static inspection of local source and audit documents. No firmware rebuild, host-test execution, hardware test, original-ZIP inspection or electrical review was performed. Existing test outcomes are attributed to their documents. This is not a re-audit of all 79 new findings or all 130 carried-forward findings.

Evidence boundary: only audit 004 in Markdown/Word and README_AUDITS.md are present locally. The audit index points to other reports and evidence directories that are absent. Original requirements and hardware conclusions are not independently certified here.

## Corrections that matter

- Confirmed: no automatic log replay; roughly 65k record capacity; unauthenticated network control; lossy HTTP error handling; no runtime rollback; defective A/B configuration selection.

- Fixed in source: TAGLOG_CLEAR is restricted to the log partition. Failed-install bootloader fall-through is fixed, but that does not establish full verification on every boot.

- Incorrect for this checkout: ESP32 firmware absent, F411 ESP link disabled by pin conflicts, Git/design documents absent, and reader-settings sector erased on every save.

- Unresolved: whether the local signing key is the previously exposed key, production release provenance, unreviewed audit claims and F411 hardware behavior.

The original severity totals, scorecard and verification-vote counts describe the old delivery audit. They are not carried forward as measurements of this checkout. Unreviewed findings are neither accepted nor dismissed.

## Reviewed findings

### BUF-01 — Automatic offline replay is absent

Status: CONFIRMED

The flash log is separate from the live upload queue. TagLog_Read is called by the manual TAGLOG_READ host handler, not by an automatic upload drain. The live queue drops old entries when full (256 entries or its byte limit) and expires entries after the configured age; the default is 8 seconds. These are defaults and bounds, not a guarantee of exactly 8 seconds of retention under every load.

Source evidence: STM32_Firmware/Firmware/Application/Host/host_interface.c:261; STM32_Firmware/Firmware/Protocol/identium_protocol.c:416; STM32_Firmware/Firmware/Config/product_config.h:29

Required action / closure evidence: Implement persistent FIFO replay with an acknowledged delivery cursor, reconnect recovery and explicit accounting for records overwritten before acknowledgement. Test disconnect/reconnect, reset during replay and duplicate handling.

### BUF-02 — Tag-log capacity remains below 100,000 records

Status: CONFIRMED

The lower 2 MB of a 4 MB chip holds 32-byte records. The advertised guaranteed capacity is (512 - 2) x 128 = 65,280 records, with additional retention depending on head position. The upper 2 MB remains reserved for update staging. The original 65,376-record test result is historical; it was not reproduced in this review. The 100k target is taken from the audit, not independently revalidated against the original requirements package.

Source evidence: STM32_Firmware/Firmware/Services/storage/tag_log.c:40; STM32_Firmware/Firmware/Common/firmware_image.h:98; STM32_Firmware/hostcheck/tag_log_host_test.c:269

Required action / closure evidence: Repartition storage and revise sequence-to-slot mapping safely, or qualify a larger supported flash part. Add a requirement-level capacity check and migration/power-loss tests.

### SEC-02 — Network host control lacks authentication

Status: CONFIRMED

The host TCP server listens on the default port 5000 and forwards traffic into the host command transport. Firmware update and TAGLOG_CLEAR handlers do not require an authenticated session. Signed-image verification is present, but it is not authorization for network control. Installing arbitrary code would additionally require an accepted signing key or another bypass; unauthenticated access alone does not establish that capability.

Source evidence: STM32_Firmware/Firmware/Services/ethernet/host_tcp_server.c:24; STM32_Firmware/Firmware/Application/Host/host_interface.c:798; STM32_Firmware/Firmware/Application/Host/host_interface.c:1306

Required action / closure evidence: Define authenticated administrative access and gate destructive/control operations. Restrict exposure during bench work. Retain signature verification independently of authentication.

### SEC-03 — No trial boot or runtime rollback

Status: CONFIRMED

The bootloader retries installation and marks a successfully copied image INSTALLED. There is no application-confirmed trial state or retained known-good application fallback in the inspected boot flow. A signed image that installs correctly but fails at runtime can leave the unit without a usable remote update path. Installation retry is not runtime rollback.

Source evidence: STM32_Firmware/Bootloader/boot_main.c:138; STM32_Firmware/Bootloader/boot_main.c:162

Required action / closure evidence: Design trial boot, confirmation and recovery before deployment. Validate with signed images that crash before host services start and with power loss during install.

### PRO-02 — HTTP errors can discard uploads

Status: CONFIRMED

HttpUpload_FinishResponse changes the state to IDLE before checking whether a response is 2xx. The 2xx check controls command-body delivery, not successful acknowledgement of the uploaded batch. The failure path also gives up after HTTP_MAX_ATTEMPTS. Automatic flash replay is absent, so failed uploads have no demonstrated automatic recovery.

Source evidence: STM32_Firmware/Firmware/Services/ethernet/http_upload.c:588; STM32_Firmware/Firmware/Services/ethernet/http_upload.c:780

Required action / closure evidence: Keep pending data until an accepted server acknowledgement. Test redirects, 4xx, 5xx, disconnects and timeouts; use a defined retry/backoff policy and durable queue.

### REQ-02 — F411 slot is 112 KB; bootloader is not literally immutable

Status: CORRECTED / PARTLY OPEN

FW_APP_MAX_SIZE remains 112 KB for F411. No supported bootloader update protocol was identified. However, "immutable" is too strong: the source does not establish a physically immutable bootloader. The original report itself notes that application code could rewrite an unprotected bootloader. Also, the remaining 384 KB is reserved for reader settings and two configuration sectors; describing it simply as unused omits those allocations. Historical application/RAM usage percentages have not been remeasured.

Source evidence: STM32_Firmware/Firmware/Common/firmware_image.h:95; STM32_Firmware/Firmware/Services/configuration/configuration_storage.c:15; STM32_Firmware/Firmware/Services/configuration/reader_settings.c:30

Required action / closure evidence: Plan application growth, settings allocation, recovery and key rotation together. Do not treat ad-hoc bootloader self-rewriting as a supported safe update mechanism.

### SEC-05 — Failed-install fall-through fixed; ordinary-boot verification still limited

Status: PARTLY FIXED

Boot_Install hashes staged first-block data plus programmed application bytes before writing the initial vector block. Boot_CheckStaging returns false after failed installation, and main calls Boot_StartApplication only when that check permits it. The audit claim that a failed/hash-mismatched install falls through is superseded. On an ordinary boot with no pending image, Boot_StartApplication still checks vector bounds rather than re-verifying the full installed application signature/hash.

Source evidence: STM32_Firmware/Bootloader/boot_main.c:108; STM32_Firmware/Bootloader/boot_main.c:202; STM32_Firmware/Bootloader/boot_main.c:175

Required action / closure evidence: Close the failed-install fall-through subclaim at source level. Keep the ordinary-boot integrity question open and validate interrupted-install recovery on F411 hardware.

### BUF-05 — TAGLOG_CLEAR no longer erases the staging partition

Status: FIXED IN SOURCE

The clearing state now calls GD25Q_StartBlockErase for blocks below TAG_LOG_BLOCK_COUNT, which is derived from the lower-half log size. It no longer performs a chip erase. The specific staging-destruction finding is closed at source level. This does not close separate findings about sequence handling, reset recovery or flash contention.

Source evidence: STM32_Firmware/Firmware/Services/storage/tag_log.c:533; STM32_Firmware/Firmware/Services/storage/tag_log.c:539; STM32_Firmware/hostcheck/tag_log_host_test.c:327

Required action / closure evidence: Retain the staging-preservation regression test and exercise clear/update/reset interactions on hardware; tests were inspected, not executed here.

### NET-01 — ESP32-C3 source and an enabled F411 link are present

Status: CORRECTED / PARTLY OPEN

ESP32_Firmware is an ESP-IDF project with Wi-Fi, sockets and I2C link code. The C3 branch assigns SCL to GPIO6 and SDA to GPIO10. F411 board_config.h enables the ESP link and assigns STM32 PB6/PB7, with PB9 for EN. The esp32_link.h comment saying GPIOs collide is stale relative to these definitions. Therefore "no ESP32 firmware, so Wi-Fi cannot work" is not valid for this checkout. End-to-end Wi-Fi operation on the actual board remains unverified here; the BLE portion is not revalidated.

Source evidence: ESP32_Firmware/main/main.c:39; ESP32_Firmware/main/i2c_link.c:20; STM32_Firmware/Firmware/BSP/f411_custom/board_config.h:75; STM32_Firmware/Firmware/Devices/esp32/esp32_link.h:19

Required action / closure evidence: Correct the stale comment, build the C3 target and test association, host traffic, reconnect and reboot on F411 hardware. Assess BLE separately against requirements.

### SEC-01 — Historical signing-key exposure is not cleared

Status: UNRESOLVED EVIDENCE

The original audit reports that its delivery ZIP contained a private key matching the bootloader public key. That ZIP and its verification artifacts are not available in this checkout. A local keys/fw_signing_key.pem exists and is not tracked by Git; the Makefile defaults to that path. These observations do not prove whether it is the leaked key or a replacement. No private-key contents were copied into this report, and no key comparison or rotation was performed.

Source evidence: STM32_Firmware/Makefile:164; Local file existence and git ls-files checks; original audit SEC-01 (historical evidence only).

Required action / closure evidence: Keep this release gate open until key provenance/rotation and the installed bootloader trust root are established. Git exclusion alone does not undo a previous leak.

### BLD-03 — Repository context exists; reproducibility is unproven

Status: CORRECTED / PARTLY OPEN

This checkout includes Git history, README build instructions, developer guides and design 0020. The original claim that these are absent applies to the audited ZIP, not this repository. The pre-existing tracked modification is a blank line in main.c. No clean rebuild, toolchain lock verification or comparison against a released binary was performed, so reproducibility remains unproven.

Source evidence: Git HEAD c099c87bc61882cbe0df4cfed34e64dafc49ef6f; README.md; docs/notes/dev-guide-stm32-firmware.md; docs/design/0020-bootloader-and-signed-update.md

Required action / closure evidence: Use a documented toolchain and clean revision for release artifacts, retain source-to-binary provenance and exclude signing secrets from delivery packages.

### BLD-05 — Tests exist, but requirement coverage remains incomplete

Status: CORRECTED / PARTLY OPEN

The Makefile contains host checks for protocol, tag log, image verification and update receiving. The tag-log test explicitly expects 65,280 capacity, which does not assert the audit's 100k requirement. The open-items note records F429 successful-update and bad-signature hardware checks, while F411 update and tag-log checks remain listed as untested. These are recorded historical results, not newly witnessed test runs. A blanket "no on-target evidence" statement is too broad.

Source evidence: STM32_Firmware/Makefile:213; STM32_Firmware/hostcheck/tag_log_host_test.c:269; docs/notes/open-items.md: Verified on hardware / Not yet tested on hardware

Required action / closure evidence: Add acceptance tests for capacity, acknowledged replay, interrupted settings writes and bad runtime images. Execute F411 hardware tests before release.

### ROB-02 — Reader settings append records; erasure occurs at rollover

Status: CORRECTED / PARTLY OPEN

ReaderSettings_Save appends to erased slots. It erases the single reader-settings sector only when the slot area is full or the next slot is not erased. Therefore "every save erases the sector" is incorrect for this code. Power loss during rollover erase can still destroy all records in that sector. Separately, configuration_storage.c uses two other sectors and has the slot-selection defect described in CUR-01. The open-items note claiming reader-settings rollover is never erased is also stale.

Source evidence: STM32_Firmware/Firmware/Services/configuration/reader_settings.c:511; STM32_Firmware/Firmware/Services/configuration/reader_settings.c:541; STM32_Firmware/Firmware/Services/configuration/configuration_storage.c:17

Required action / closure evidence: Keep rollover power-loss resilience open. Distinguish the append-only reader-settings store from the separate A/B configuration store; validate both with fault injection.

### CUR-01 — A/B configuration selection overwrites the newest slot

Status: CONFIRMED

Configuration_Storage_Save checks both V2 records, but whenever A is valid it selects B and sets the new sequence to A.sequence + 1, without comparing the two V2 sequences. Starting with A sequence 1, the next save writes B sequence 2; later saves keep erasing B and writing sequence 2 while A stays at 1. A power cut while rewriting B can lose the most recent saved settings and fall back to stale A. This is not a claim that all settings are inevitably lost.

Source evidence: STM32_Firmware/Firmware/Services/configuration/configuration_storage.c:524

Required action / closure evidence: Compare valid V2 sequence numbers, write the older/invalid slot and advance from the newest sequence. Test repeated saves, sequence wrap and power loss at erase/program/verification boundaries.

## Work order for closure

- Resolve signing-key provenance and establish the intended bootloader trust root; define authenticated administrative access.

- Implement durable acknowledged replay and HTTP error retention, then meet the capacity requirement with safe sequence mapping and storage migration.

- Fix A/B configuration selection and make reader-settings rollover survive power interruption.

- Design trial boot/recovery alongside application sizing, configuration allocation and key rotation.

- Build and test both targets; on F411 validate Wi-Fi, tag logging, staging preservation, interrupted updates, repeated settings saves and reconnect replay.

- Recheck the remaining audit register against code, original requirements and hardware evidence before issuing a new release verdict.

## Coverage register: 79 original revision-3 findings

Each entry below identifies an original audit claim. Titles of entries marked NOT REVALIDATED are historical claim labels, not current conclusions. Detailed dispositions above govern reviewed entries. The 130 carried-forward revision-2 entries are not individually revalidated in this edition; consult the preserved original for their historical status.

- SEC-01 | UNRESOLVED EVIDENCE | Historical signing-key exposure is not cleared

- BUF-01 | CONFIRMED | Automatic offline replay is absent

- BUF-02 | CONFIRMED | Tag-log capacity remains below 100,000 records

- RFM-01 | NOT REVALIDATED | Original claim: No duplicate suppression or roll-up: every raw read goes into the flash log, so a tag in the field fills the 65k log in minutes

- SEC-02 | CONFIRMED | Network host control lacks authentication

- SEC-03 | CONFIRMED | No trial boot or runtime rollback

- PRO-02 | CONFIRMED | HTTP errors can discard uploads

- RFM-02 | NOT REVALIDATED | Original claim: The region from vendor static configuration is saved without validation, acknowledged before it is applied, and sent to the module at every boot, bypassing the SET_REGION whitelist

- RFM-03 | NOT REVALIDATED | Original claim: Any module-bridge session stops local reading and buffering, and a TCP bridge session is never timed out; a stray 0xFF byte on serial/USB, or any non-0xEE first byte on TCP 8080, opens one

- RFM-04 | NOT REVALIDATED | Original claim: An RFID module reset or hang at runtime is never detected or recovered, and the module reset line (CN1.11 on PB5) is never driven

- NET-01 | CORRECTED / PARTLY OPEN | ESP32-C3 source and an enabled F411 link are present

- HWI-01 | NOT REVALIDATED | Original claim: The Wiegand port added to the board (CN2 on PC14/PC15) has no firmware; the status LED, tag-read beep and IP reset are also missing

- REQ-02 | CORRECTED / PARTLY OPEN | F411 slot is 112 KB; bootloader is not literally immutable

- REQ-04 | NOT REVALIDATED | Original claim: Modbus RTU/TCP, one of the three required Identium host protocols, is absent, and a Modbus-TCP master on port 8080 stops inventory

- REQ-05 | NOT REVALIDATED | Original claim: HTTP active mode does not follow the reader's published active-mode format or FW-001: http:// only, literal IPv4 hosts only, non-standard fields, no charset

- SEC-04 | NOT REVALIDATED | Original claim: No anti-rollback: any older image signed with the key installs, and the header has no monotonic version

- SEC-05 | PARTLY FIXED | Failed-install fall-through fixed; ordinary-boot verification still limited

- SEC-06 | NOT REVALIDATED | Original claim: Device protection is a manual, optional step: no write protection on the bootloader/key sector, RDP only through 'make protect', no hardware watchdog

- SEC-07 | NOT REVALIDATED | Original claim: No key rotation or revocation path: one compiled-in key, no key ID, and the bootloader cannot be updated in the field

- SEC-08 | NOT REVALIDATED | Original claim: One idle or half-open client on TCP 5000 can lock the host command and update path until reboot: no keep-alive or idle timeout on the new socket

- SEC-11 | NOT REVALIDATED | Original claim: Wi-Fi AP can be enabled with no password (allowed and documented), with the same default SSID on every unit

- BUF-09 | NOT REVALIDATED | Original claim: Driver and bootloader accept only a GigaDevice 32 Mbit part; a Winbond second source (likely under the non-Chinese supply-chain goal) silently disables the tag log and OTA staging

- BUF-05 | FIXED IN SOURCE | TAGLOG_CLEAR no longer erases the staging partition

- BUF-08 | NOT REVALIDATED | Original claim: Offline timestamps cannot be recovered: before time sync they are uptime of an unidentified boot, and the time-sync bootstrap is off by default and not awaited before buffered tags are sent

- HWI-03 | NOT REVALIDATED | Original claim: RS-485 mode can only be selected at compile time; every shipped build runs RS-232, so the RS-485 port is unusable

- PRO-07 | NOT REVALIDATED | Original claim: 0xEE frames on the RS-232/RS-485/USB COM route: the CRC is never checked or sent, flags are ignored, and CLEAR/SWITCH accept any payload

- RFM-06 | NOT REVALIDATED | Original claim: With the module absent or dead, each degraded-mode retry blocks the whole main loop for about 5.5 s every 15.5 s, and boot is delayed by about 16.5 s

- ROB-02 | CORRECTED / PARTLY OPEN | Reader settings append records; erasure occurs at rollover

- ROB-06 | NOT REVALIDATED | Original claim: The W5500 is never health-checked: a dead or wedged chip goes undetected and adds 20-40 ms of socket re-open polling to every main-loop pass, starving tag processing

- HWI-02 | NOT REVALIDATED | Original claim: Wiegand lines go straight from the field connector to the backup-domain pins PC14/PC15, with 3V3 pull-ups and no level translation or protection

- HWI-04 | NOT REVALIDATED | Original claim: Non-atomic GPIOB ODR writes race between the RS-485 DE/RE switching in the USART2 ISR and main-loop writes to relays, LED and ESP32 EN on the same port

- HWI-06 | NOT REVALIDATED | Original claim: set_gpo still accepts GPO 4, which does not exist on this 3-relay board, so a multi-output command half-applies and returns the wrong error; GPO numbering also departs from FW-001 §6

- BUF-04 | NOT REVALIDATED | Original claim: Every read is written to flash continuously (online or offline): uniform but heavy wear reaches the rated endurance in about 8-25 months at 100-300 reads/s, with no erase counting, verify or failure reporting

- BUF-06 | NOT REVALIDATED | Original claim: After TAGLOG_CLEAR, logging restarts mid-sector; a reboot then loses the post-clear records in that sector, and before the next sector boundary it resets the sequence counter to 0

- BUF-07 | NOT REVALIDATED | Original claim: TAGLOG_READ silently skips records whenever the flash is still erasing after 50 ms and advances NEXT past them, so replay during live inventory loses data

- BUF-10 | NOT REVALIDATED | Original claim: Tag-log failure is silent: init result ignored, no degraded flag or host event, and a reset during a chip erase disables the log and OTA staging for the whole boot

- PRO-04 | NOT REVALIDATED | Original claim: Any non-0xEE client on TCP 8080, or a single 0xFF noise byte on the serial host port, takes over the RFID module and silently stops active-mode inventory

- PRO-05 | NOT REVALIDATED | Original claim: HTTP mode ignores hb_cylce: every TCP reconnect is treated as a new session and sends a heartbeat at once, so heartbeats fire every ~1-2 s against servers that close idle connections

- PRO-06 | NOT REVALIDATED | Original claim: reader_exception is never sent (dead code, discarded from the F411 image) and lacks the reader timestamp; sync_time_req lacks event_data; module failure is invisible to the server

- PRO-10 | NOT REVALIDATED | Original claim: UDP discovery replies only by unicast to the requester, so readers on another subnet (including factory-default units) cannot be found; board type reported as HC32F46x

- RFM-05 | NOT REVALIDATED | Original claim: Releasing the module bridge does not re-sync module state; the bridge forwards every opcode unfiltered and swallows the PC's first STOP even when the local STOP failed

- RFM-07 | NOT REVALIDATED | Original claim: The 4096-byte RX ring added in rev 3 adds no tag buffering: bytes are parsed at once into a 32-entry queue that loses tags, is popped one tag per loop pass, and has no backpressure

- NET-03 | NOT REVALIDATED | Original claim: TCP active upload stalls permanently when data aggregation is on and s_buf_size is larger than the 2 KB socket buffer

- NET-04 | NOT REVALIDATED | Original claim: Wi-Fi service stops moving data between about 24.9 and 49.7 days of uptime (32-bit deadline wrap)

- NET-05 | NOT REVALIDATED | Original claim: Wi-Fi upload socket is never reopened after the server closes it

- NET-06 | NOT REVALIDATED | Original claim: Ethernet and Wi-Fi host-port output is silently cut mid-line when the 2 KB queue fills

- NET-09 | NOT REVALIDATED | Original claim: No TLS, DNS or MQTT anywhere, and the ESP link protocol cannot even request them

- BLD-03 | CORRECTED / PARTLY OPEN | Repository context exists; reproducibility is unproven

- BLD-04 | NOT REVALIDATED | Original claim: No production programming package or procedure: flashing rebuilds from source and defaults to the debug build, the bootloader sector is not write-protected, and ESP32-C3 firmware, serial-number and end-of-line steps are missing

- BLD-05 | CORRECTED / PARTLY OPEN | Tests exist, but requirement coverage remains incomplete

- BLD-06 | NOT REVALIDATED | Original claim: The firmware binary carries no version or identity of its own; three version schemes disagree

- BLD-07 | NOT REVALIDATED | Original claim: fw_sign.py takes the board ID from the command line and does not validate the binary against it; a mis-signed F429 image installs on an F411 and leaves it unbootable

- BLD-08 | NOT REVALIDATED | Original claim: Delivery reintroduces ~276 MB of build output, including stale pre-bootloader images linked at 0x08000000, Windows executables, .pdb/.pyc files and a test private key

- REQ-11 | NOT REVALIDATED | Original claim: None of the FW-001 branding items are implemented, and new code presents a foreign identity (HC32F46x board type, "RFID-F429-DEV", STMicroelectronics USB strings)

- GAP-01 | NOT REVALIDATED | Original claim: New rev-3 network/inventory watchdogs reboot the reader during every backhaul outage and continuously in passive mode, discarding queued tags

- GAP-02 | NOT REVALIDATED | Original claim: Coprocessor (ESP32-C3) firmware cannot be updated in the field: no update command in the STM32-ESP link and the MCU cannot reach the download strap

- SEC-10 | NOT REVALIDATED | Original claim: Bootloader robustness: it never starts the IWDG, reads staging without waiting for the SPI flash to finish (WIP), and handles a failed INSTALLED write badly

- SEC-13 | NOT REVALIDATED | Original claim: Firmware binary and tag data sit in plaintext in external SPI flash; RDP gives no protection of firmware IP

- HWI-08 | NOT REVALIDATED | Original claim: The buzzer on PC13 (backup-domain pin) is driven as a FAST push-pull current source, the Q1 base floats until firmware runs, and the buzzer type is unconfirmed

- HWI-09 | NOT REVALIDATED | Original claim: The only MCU status LED (PB8) is switched on once at board init and never driven again

- BUF-11 | NOT REVALIDATED | Original claim: The 64-record RAM queue does not cover the worst-case erase at the rates its own comment claims

- PRO-08 | NOT REVALIDATED | Original claim: Vendor-tool config (0xEE 0x25) silently drops client_ack=1 and crc_enable=1 and replies OK

- PRO-11 | NOT REVALIDATED | Original claim: Reader logical name is capped at 32 chars and copied from JSON with escape sequences intact; truncation inside an escape breaks every event

- RFM-08 | NOT REVALIDATED | Original claim: No module abstraction for the planned E710 alternative: the Silion API leaks into 12 files and rev 3 adds E310-specific hard-coding

- ROB-09 | NOT REVALIDATED | Original claim: EE frame length arithmetic wraps for declared lengths of 0xFFFA or more, so a 6-byte frame is dispatched with dataLength up to 65535

- ROB-11 | NOT REVALIDATED | Original claim: Host command line buffer is not cleared when a transport is released on disconnect, so a partial line from the dropped client prefixes the next client's first command

- ROB-12 | NOT REVALIDATED | Original claim: USB_CDC_Transport_Send and Serial_Port_Send copy with interrupts disabled, and module-bridge relays now pass them up to 992 bytes at a time, longer than the RFID UART can tolerate

- ROB-13 | NOT REVALIDATED | Original claim: ESP32 link: g_transferEvent is written by the ISR but not declared volatile, and is cleared only after the interrupt transfer has started

- NET-10 | NOT REVALIDATED | Original claim: HTTP over Ethernet waits at least 1 s before every new connection after a normal server close

- NET-12 | NOT REVALIDATED | Original claim: The MAC address is a 32-bit hash of the MCU ID in the locally administered range, stored in settings and overwritable over the network

- BLD-09 | NOT REVALIDATED | Original claim: F429/F411 configuration drift: the default board is the dev kit, the product image reports 'RFID-F429-DEV', the IDE F411 config is debug-only, and TARGET_BOARD silently falls back to F429

- BLD-10 | NOT REVALIDATED | Original claim: The new F411 CubeMX project describes only the clock and USB, so it cannot generate or validate the board pin map; the F429 .ioc is stale

- BLD-11 | NOT REVALIDATED | Original claim: Memory-layout constants are duplicated with no link-time checks; the bootloader's promise to preserve the fault record is not enforced

- BLD-12 | NOT REVALIDATED | Original claim: Warning policy not enforced in firmware builds; naked fault handlers contain C statements

- BLD-13 | NOT REVALIDATED | Original claim: No third-party notice file for binary distribution (micro-ecc BSD-2, ST HAL BSD-3, CMSIS Apache-2.0, ST USB SLA0044)

- GAP-03 | NOT REVALIDATED | Original claim: Factory defaults are development settings: active upload on by default, dialling a hard-coded 192.168.150.100:9000

- GAP-04 | NOT REVALIDATED | Original claim: An IP/MAC change does not reset the port-5000 socket, so a connected host session is orphaned and keeps the legacy parser locked

- SEC-12 | NOT REVALIDATED | Original claim: Update chain deviates from FW-001 §8; design note missing; Identium-protocol OTA unsupported; no TLS; app does not pre-check the signature

- GAP-05 | NOT REVALIDATED | Original claim: The 32-byte MPU stack guard is smaller than many stack frames (up to 840 B), so an overflow can skip it

CUR-01 is a separately recorded current-code defect and is not added to the original count of 79. Review coverage: 13 original findings dispositioned; 66 not revalidated; one additional current-code finding.

## Document provenance

Original retained: IDM-AUD-004_Firmware_Rev3_ReAudit.docx

Original DOCX SHA-256: f054ba0cd2727e9e1c070b906d8384ab3fecba3af70f07701a15675edeab34ed

Companion Markdown has the same updated report text. Source paths are relative to the repository root; line references describe the current checkout and can change with later edits. No secret key material is included.
