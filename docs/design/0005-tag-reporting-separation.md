# Identium Protocol Migration — Step 4

## Goal
Separate company-protocol tag reporting from the legacy VCP/ASCII transport.

## Edited files
- `Firmware/Application/Silion/sillion_application.c` — calls `Identium_Protocol_ReportTag()` at the three existing tag-output points (async tag, single-tag inventory, buffered tag batch). This makes the RFID application the owner of the company-protocol tag event.
- `Firmware/Services/VCP/vcp_transport.c` — removed the Identium protocol call from `VCP_SendTag()`. VCP now remains purely the legacy ASCII/ST-LINK-VCP output path during migration.

## Why
Previously `VCP_SendTag()` secretly performed two jobs: legacy ASCII output and company-protocol JSON tag reporting. That would make the eventual removal of ASCII unnecessarily risky. Step 4 separates the paths while keeping both outputs alive for rollback.

## Not implemented yet
`tag_coming` is intentionally not added yet because the supplied protocol specifies the event and subscription code but does not specify the firmware-side re-arm/absence algorithm for deciding when an EPC has entered the field again. We will not invent that behavior until the company's existing software/firmware behavior is verified.

## Verification target
With a real RFID tag in the field, the PC test server should receive a valid `tag_read` event while the legacy ASCII output may still be present on the old USB path.
