# 0004 — Hardware I/O Command Fixes

## Why this fix exists

The first hardware test of design 0003 found two bugs:

1. `set_gpo` returned `20004 / "set_gpo field missing"` even though `gpo`, `state`, and `duration` were present.
2. `gpi_changed` emitted `"timestamp":lu` instead of a numeric timestamp.

## Fixes

- `Firmware/Protocol/identium_command.c`: corrected JSON object-member lookup so keys after the first comma are recognized. This fixes parsing of `state` and `duration` in `set_gpo`.
- `Firmware/Protocol/identium_protocol.c`: replaced `%llu` timestamp formatting with a small uint64-to-decimal formatter because the embedded printf configuration on the F429 does not support the previous long-long format correctly.

## Verification

- `identium_command.c` syntax check: PASS.
- `identium_protocol.c` syntax check: PASS.
- `gpio_service.c` syntax check: PASS.
- `industrial_io.c` syntax check: PASS.
- Python test-server syntax check: PASS.

The host GCC checks show only the existing CMSIS pointer-size warnings when ARM code is compiled on a 64-bit PC.
