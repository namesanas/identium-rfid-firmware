# IDM-AUD-004 — Small fixes implementation record

3 November 2026. Applies to the working-tree changes based on c099c87bc61882cbe0df4cfed34e64dafc49ef6f. This implementation record supersedes the pre-fix source-review status only for the items below. The original delivery audit and earlier current-checkout review remain historical snapshots.

## Result

Eight localized defects have source fixes, and three broader findings have the agreed limited fixes. No board was flashed. Source changes and host checks are not a substitute for on-target validation or pilot-release approval.

## Eight localized fixes

- CUR-01 — A/B configuration saving now compares the valid sequence numbers, writes the older or invalid slot, and advances from the newest copy. Repeated saves, interrupted programming at every word and sequence wrap are covered by the new configuration-storage test.
- ROB-09 — Serial/USB EE frame assembly uses 32-bit length arithmetic. Link dispatch also checks the complete declared length, including a declared CRC trailer, without narrowing to 16 bits. This does not implement CRC verification or settle protocol flag semantics.
- ROB-11 — Releasing host ownership clears the partial ASCII command and overflow state as well as the transport byte queue, preventing a new owner from inheriting an unfinished command.
- ROB-13 — The ESP32 ISR event flag is volatile and cleared before starting interrupt-driven transmission, so an immediate completion/error cannot be overwritten by the caller.
- NET-04 — Wi-Fi retry/poll deadlines use armed elapsed-time timers. Expired timers disarm rather than becoming pending again after the signed half-range. Completion handling refreshes the sampled tick before evaluating newly armed timers.
- NET-03 — Raw TCP tag batches are bounded by both configured batch size and available transport capacity. An 8192-byte configuration therefore no longer traps a batch behind a 2048-byte transport. HTTP retains its separate batching path.
- BUF-07 — Tag-log reads distinguish busy/read-error from missing records. Manual paging stops at a retryable sequence and reports that same NEXT value rather than silently advancing past it. The original boolean read API remains available. Missing/corrupt records retain the earlier skip behavior.
- SEC-08 — TCP 5000 closes and releases an inactive connection after 60 seconds. Received bytes and successfully accepted outgoing bytes refresh activity, so an actively streaming client is not disconnected merely for being receive-only. A peer deliberately sending traffic can still retain ownership; this is not authentication or denial-of-service protection.

## Three limited fixes

- HWI-06 — All requested GPIO outputs are checked against the physical board output count before any relay is changed. F411 output 4 is rejected with the unsupported-operation response. The logical numbering/specification disagreement remains open; hardware failures during an otherwise valid multi-output operation are not rolled back.
- RFM-03 — A TCP module bridge closes after 30 seconds without received host bytes or accepted outgoing data, then releases module ownership through the existing grace-period path. Failed send attempts no longer refresh activity. Accidental bridge-entry triggers, service authorization and full module-state restoration remain open.
- PRO-02 — Non-2xx HTTP responses, disconnections and response timeouts retain the active request and retry with delays of 1, 2, 4, 8, 16 and then 30 seconds. A complete 2xx response releases it. The previous three-attempt discard is removed. This is RAM-only retry, not persistent replay: reset or deliberate URL reconfiguration can still discard the in-flight request; prolonged failures can exhaust queues. Permanent 4xx errors block later queued events until the server/configuration problem is corrected. A response lost after server acceptance can cause a duplicate on retry.

## Validation

- F411 and F429 release application/bootloader build targets pass. Modified translation units were compiled with warnings treated as errors; these were incremental builds, not full clean rebuilds.
- F411 linked application: 93,124 bytes flash of 112 KB; 116,696 bytes RAM of 128 KB. F429 linked application: 92,744 bytes flash and 116,696 bytes RAM.
- The full make hostcheck suite passes: protocol, tag log, image validation for both boards, firmware-update receiver, plus three new test programs.
- New/extended regression coverage includes 8192-byte batch configuration over a 2048-byte transport; busy flash read/retry; HTTP 302/400/503 responses, timeout, more than three failures and eventual 204 acknowledgement; repeated settings saves, interrupted writes and sequence wrap; timer wrap; rejecting nonexistent GPIO before writes; TCP idle release across tick wrap and keeping active output alive.
- An additional test-runner defect was corrected: on Windows, Make now explicitly compiles and runs .exe names. Old extensionless executables in the supplied build directory had otherwise been executed instead of the newly compiled tests. The apparent signature-validation failure disappeared when the newly built executables were selected; no signing or cryptographic firmware code was changed.

Reproduce from Git Bash in STM32_Firmware with the ARM tools, native GCC and make on PATH:

    make hostcheck
    make BOARD=f411 CONFIG=release EXTRA_CFLAGS=-Werror
    make BOARD=f429 CONFIG=release EXTRA_CFLAGS=-Werror

Host-test logs: STM32_Firmware/build/audit-hostcheck.log. Build logs: STM32_Firmware/build/audit-f411.log and audit-f429.log. Build artifacts remain in their usual release folders. No production image was signed or installed.

## Required hardware follow-up

- Confirm F411 repeated save/reboot behavior and power interruption during settings writes.
- Exercise real ESP32 I2C completion/error interrupts, retry scheduling and Wi-Fi reconnects.
- Send oversized/incomplete EE frames and reconnect after a partial ASCII command on USB, serial and TCP.
- Verify busy-flash TAGLOG_READ paging during real erases and inventory.
- Check TCP 5000 and Reader Manager bridge timeout/reconnect behavior, including long-running streams and module updates.
- Test HTTP errors, lost replies and recovery against a real server over Ethernet and Wi-Fi.

The signing-key exposure, authenticated control, >=100k-record capacity, automatic persistent offline replay, runtime update rollback and reader-settings rollover resilience remain outside this small-fix set.
