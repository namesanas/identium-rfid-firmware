# 0014 — Host Serial Port (RS232 / RS485)

## Scope

Give the board's host USART the same command paths as the native USB CDC port, so a PC can configure and operate the reader over RS232 or RS485. The custom F411 board has a MAX3232 (RS232) and an SP3485 (RS485) on one shared USART; the F429 Discovery has no transceiver, and its host USART (USART1, PA9/PA10) is wired to the ST-LINK virtual COM port, which is used as the bench stand-in.

Out of scope here (follow-up work): runtime port settings from the `uart1` / `uart2` configuration keys, tag upload over serial (`upload.hw_inf` 2 = RS232, 7 = RS485), and RS485 Modbus addressing.

## Firmware changes

### Serial port service (`Services/serial/serial_port.c`)

Replaces the F429-only `Services/VCP/vcp_transport.c`.

- Received bytes enter the host router as `HOST_TRANSPORT_SERIAL` (previously they were tagged as USB and ignored once USB enumerated).
- Binary TX queue (2 KB), sent by interrupt.
- RS485: the driver-enable pin is raised before the first start bit and dropped on the USART transmission-complete interrupt (TC), i.e. after the last stop bit. Bytes received while the driver is enabled are discarded (own echo on transceivers that do not disable the receiver during TX).
- Mode select `Serial_Port_SetMode()`: RS232 never drives DE. The power-up mode is `PRODUCT_SERIAL_DEFAULT_MODE` (0 = RS232, 1 = RS485, overridable with `-D`).

| Board | DE / RE | Mode select |
|---|---|---|
| F429 Discovery | PG13 (green LED LD3 — lights while transmitting) | none |
| F411 custom | TBD (netlist) | TBD (netlist); high = RS485 assumed |

### Host router (`Services/host/host_transport.c`)

- The vendor-tool router ("COM route": `FF` module frames, `IOGET`/`VER`/… board commands, `EE` configuration requests) now has one instance per serial link — USB CDC and the host serial port — and replies on the link the request came from.
- `HOST_Transport_LinkSend()` / `HOST_Transport_LinkGetFreeSpace()` give the module bridge and the configuration port a link-independent output.
- Legacy ASCII works on the serial port; replies and `START` tag output go to the serial port when it owns the parser.
- A free legacy parser can only be claimed by a printable character. On a UART the LF that ends a `RELEASE` line arrives after the release has flushed the queue; before this change that LF re-claimed the parser for the serial port and locked USB/Ethernet out. RS485 line noise is filtered the same way.

### Other

- `module_bridge.c`: sessions run on TCP or on either serial link (`Module_Bridge_BeginLink` / `PushLinkBytes` / `IsLinkActive`), with the 30 s idle end applying to both serial links.
- `configuration_port.c`: `Configuration_Port_HandleLinkRequest(link, …)` replies on the requesting link.
- The legacy ASCII tag line moved from the VCP module to `host_interface.c` as `HOST_SendTag()`.

RAM: +2 KB (second router's EE frame buffer). Flash: +1 KB.

## Verification (F429 Discovery, ST-LINK VCP as the serial port)

`Tools/serial_link_test.py --port <COM>` runs legacy ASCII (`GET_STATUS`, `RELEASE`), EE `0x22` and `0x24`, `VER`, `IOGET` and a module pass-through (`FF 00 03 1D 0C`).

| Check | Result |
|---|---|
| Serial port (COM3), RS232 mode | all 6 PASS |
| USB CDC (COM8), regression | all 6 PASS |
| Serial port, RS485 mode | all 6 PASS |
| Legacy parser hand-over serial ↔ USB with `RELEASE` | correct in both directions |
| DE pin (GPIOG ODR bit 13 read over SWD) during 12 back-to-back replies | high while answering, low when idle; 12/12 replies complete |

Not yet verified: Ethernet regression (no wired link to the bench PC during this change), and electrical RS232/RS485 levels and DE timing on the real transceivers (custom board).
