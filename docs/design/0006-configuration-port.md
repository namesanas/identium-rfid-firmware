# 0006 — Configuration Port (TCP 8080)

## Goal
Add the company's local configuration port (TCP 8080) and a live configuration readback model without removing the working JSON-over-TCP control path or legacy ASCII path.

## Added
- `Firmware/Services/configuration/configuration_service.c/.h`
  - Builds live `ActiveModeJson` and `StaticModelJson` snapshots from the current F429 prototype state and protocol defaults.
  - Exposes work state as protocol value 3 (active upload mode).
- `Firmware/Services/configuration/configuration_port.c/.h`
  - Implements the documented TCP 8080 configuration-port server on W5500 Socket 1.
  - One client/one request per connection.
  - Implements command 0x22 (work state), 0x24 (ActiveModeJson), and 0x1E (StaticModelJson).
  - Uses the documented EE request / FF response framing and big-endian lengths.
- `Firmware/Devices/w5500/w5500.c/.h`
  - Added Socket 1 TCP server, RX, TX and close helpers while leaving Socket 0 as the outbound protocol connection.
- `Firmware/Application/configuration/firmware_configuration.c`
  - Initializes configuration service and configuration port.
- `Src/main.c`
  - Services the configuration port from the main loop.
- `Tools/identium_config_port_test.py`
  - Test client for TCP 8080.

## Deliberate limitations of this step
- Configuration write commands (`set_runtime_conf`, `set_static_conf`, `set_temp_static_conf`) are not enabled yet.
- No flash persistence is added in this step.
- The JSON readback reports the fields currently represented by this F429 prototype; unsupported product interfaces are not fabricated as working hardware.
- HTTP, MQTT and binary TCP remain future transport steps.

## Validation
- Configuration JSON builders were compiled with GCC syntax checking.
- Active and Static JSON outputs were parsed with Python `json.loads` successfully.
