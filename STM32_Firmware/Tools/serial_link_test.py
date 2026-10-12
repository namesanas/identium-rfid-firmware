#!/usr/bin/env python3
"""
Host serial port (RS232 / RS485) test for the Identium RFID controller.

Exercises every path the firmware routes on a serial link, in an order
that keeps them independent:

  1. Legacy ASCII        GET_STATUS, RELEASE    -> "STATUS,...", "OK,RELEASE"
  2. Configuration (EE)  0x22 work state        -> FF 00 ... status 0
  3. Configuration (EE)  0x24 ActiveModeJson    -> valid JSON
  4. Mainboard command   VER                    -> 4-byte version
  5. Mainboard command   IOGET                  -> "IOGET" + GPI bitmap
  6. Module pass-through FF 00 03 1D 0C         -> module version frame

Steps 4-6 open a module-bridge session, which stays open until the link
has been idle for 30 s; run the script again after that.

On the F429 Discovery the host USART is the ST-LINK virtual COM port, so
no adapter is needed: pass that COM port. On the custom board use a USB
RS232 or USB RS485 adapter.

Requires pyserial:  python -m pip install pyserial
"""

import argparse
import json
import struct
import sys
import time

import serial

CMD_WORK_STATE = 0x22
CMD_ACTIVE_JSON = 0x24

MODULE_GET_VERSION = bytes([0xFF, 0x00, 0x03, 0x1D, 0x0C])


def read_exact(port: serial.Serial, length: int, timeout_s: float) -> bytes:
    data = bytearray()
    deadline = time.monotonic() + timeout_s
    while len(data) < length and time.monotonic() < deadline:
        chunk = port.read(length - len(data))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def read_line(port: serial.Serial, timeout_s: float) -> str:
    data = bytearray()
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        chunk = port.read(1)
        if not chunk:
            continue
        if chunk == b"\n":
            break
        data.extend(chunk)
    return data.decode("ascii", errors="replace").strip()


def ee_request(command_id: int, content: bytes) -> bytes:
    return bytes([
        0xEE, 0x00,
        (len(content) >> 8) & 0xFF, len(content) & 0xFF,
        command_id,
        0x01,                      # reply required, no CRC
    ]) + content


def ee_exchange(port: serial.Serial, command_id: int, content: bytes):
    request = ee_request(command_id, content)
    print(f"    TX {request.hex(' ')}")
    port.write(request)

    header = read_exact(port, 10, 3.0)
    if len(header) != 10 or header[0] != 0xFF or header[1] != 0x00:
        raise AssertionError(f"bad reply header: {header.hex(' ')}")

    length = struct.unpack(">H", header[2:4])[0]
    status = struct.unpack(">I", header[6:10])[0]
    body = read_exact(port, length, 3.0)
    if len(body) != length:
        raise AssertionError(f"reply truncated: {len(body)}/{length} bytes")
    if header[4] != command_id:
        raise AssertionError(f"reply for command 0x{header[4]:02X}, expected 0x{command_id:02X}")
    return status, body


def module_crc(data: bytes) -> int:
    """RFID module CRC over LEN..DATA (nibble-table CRC-16, init 0xFFFF,
    same as SILION_CalculateCRC in the firmware)."""
    table = [0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
             0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF]
    crc = 0xFFFF
    for byte in data:
        crc = (((crc << 4) | (byte >> 4)) & 0xFFFF) ^ table[crc >> 12]
        crc = (((crc << 4) | (byte & 0x0F)) & 0xFFFF) ^ table[crc >> 12]
    return crc


def test_legacy(port):
    port.write(b"GET_STATUS\r\n")
    line = read_line(port, 5.0)                # allows for the ~3 s boot after a reset
    print(f"    RX {line!r}")
    if not line.startswith("STATUS,"):
        raise AssertionError("no STATUS reply")

    # Hand the legacy parser back so other ports can use it.
    port.write(b"RELEASE\r\n")
    line = read_line(port, 2.0)
    print(f"    RX {line!r}")
    if line != "OK,RELEASE":
        raise AssertionError("no RELEASE acknowledgement")


def test_work_state(port):
    status, body = ee_exchange(port, CMD_WORK_STATE, bytes(1))
    print(f"    status={status} content={body.hex(' ')}")
    if status != 0:
        raise AssertionError(f"status {status}")


def test_active_json(port):
    status, body = ee_exchange(port, CMD_ACTIVE_JSON, bytes(2))
    if status != 0:
        raise AssertionError(f"status {status}")
    config = json.loads(body.rstrip(b"\x00").decode("utf-8"))
    print(f"    {len(body)} bytes of JSON, top-level keys: {', '.join(sorted(config))}")


def test_ver(port):
    port.write(b"VER")
    reply = read_exact(port, 4, 2.0)
    if len(reply) != 4:
        raise AssertionError(f"short VER reply: {reply.hex(' ')}")
    print(f"    mainboard version {'.'.join(str(b) for b in reply)}")


def test_ioget(port):
    port.write(b"IOGET")
    reply = read_exact(port, 6, 2.0)
    if reply[:5] != b"IOGET" or len(reply) != 6:
        raise AssertionError(f"bad IOGET reply: {reply.hex(' ')}")
    print(f"    GPI bitmap 0x{reply[5]:02X}")


def test_module(port):
    port.write(MODULE_GET_VERSION)
    header = read_exact(port, 2, 2.0)
    if len(header) != 2 or header[0] != 0xFF:
        raise AssertionError(f"no module reply: {header.hex(' ')}")
    rest = read_exact(port, header[1] + 5, 2.0)     # CMD ST_HI ST_LO DATA CRC_HI CRC_LO
    frame = header + rest
    if len(rest) != header[1] + 5:
        raise AssertionError(f"truncated module reply: {frame.hex(' ')}")
    crc = struct.unpack(">H", frame[-2:])[0]
    if module_crc(frame[1:-2]) != crc:
        raise AssertionError(f"module reply CRC mismatch: {frame.hex(' ')}")
    status = struct.unpack(">H", frame[3:5])[0]
    print(f"    {frame.hex(' ')}")
    if frame[2] != 0x03 or status != 0:
        raise AssertionError(f"unexpected module reply (cmd 0x{frame[2]:02X}, status 0x{status:04X})")


def main() -> int:
    parser = argparse.ArgumentParser(description="Identium host serial port test")
    parser.add_argument("--port", required=True, help="COM port, e.g. COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--skip-module", action="store_true",
                        help="skip the module pass-through test (no RFID module fitted)")
    args = parser.parse_args()

    tests = [
        ("legacy ASCII GET_STATUS", test_legacy),
        ("EE 0x22 work state", test_work_state),
        ("EE 0x24 ActiveModeJson", test_active_json),
        ("mainboard VER", test_ver),
        ("mainboard IOGET", test_ioget),
    ]
    if not args.skip_module:
        tests.append(("module pass-through get version", test_module))

    failures = 0
    with serial.Serial(args.port, args.baud, timeout=0.1) as port:
        time.sleep(0.2)
        port.reset_input_buffer()

        for name, test in tests:
            print(f"[{name}]")
            try:
                test(port)
                print("    PASS")
            except (AssertionError, ValueError) as exc:
                failures += 1
                print(f"    FAIL: {exc}")
                time.sleep(0.6)            # let a half-finished frame time out
                port.reset_input_buffer()

    if failures:
        print(f"\nSERIAL LINK TEST: {failures} FAILED")
        return 1

    print("\nSERIAL LINK TEST: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
