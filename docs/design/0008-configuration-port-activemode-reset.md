# 0008 — Configuration Port ActiveMode Reset

## Issue

TCP configuration port command `0x24` (ActiveModeJson) still reset/closed the connection on the F429, while `0x22` and `0x1E` worked.

## Edited file

`Firmware/Services/configuration/configuration_service.c`

## Change

The ActiveModeJson builder was rewritten to avoid `vsnprintf()` entirely. It now appends JSON literals, unsigned integers, and the reader name through small bounded helpers.

## Why

The generated ActiveModeJson is only about 485 bytes, so the 1416-byte content buffer is sufficient. This isolates the active path from embedded C formatted-output behavior and keeps its temporary storage small.

StaticModelJson and the TCP 8080 framing were not changed.
