#!/usr/bin/env python3
"""Replay Reader Manager's "Connect reader" sequence against TCP 8080.

The sequence and timeouts are taken from ModuleAPI.dll
(HardWareDetector.GetHwVer, ARM7M5eReader.InitMrdr, ConnectView).
Every module reply is checked the same way ModuleAPI checks it:
FF header, echoed opcode, status 0000 and CRC.

Note: steps 9A 01 08 00 / 9A 01 00 01 write the module's reader
configuration exactly as Reader Manager does on every connect.
"""

import argparse
import json
import socket
import time

STOP_ASYNC = bytes.fromhex("FF0EAA4D6F64756C6574656368AA49F3BB0391")


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        for bit in range(8):
            top = crc & 0x8000
            crc = ((crc << 1) | ((byte >> (7 - bit)) & 1)) & 0xFFFF
            if top:
                crc ^= 0x1021
    return crc


def build_frame(opcode: int, args: bytes = b"") -> bytes:
    body = bytes([len(args), opcode]) + args
    crc = crc16(body)
    return b"\xFF" + body + bytes([crc >> 8, crc & 0xFF])


def recv_exact(sock: socket.socket, length: int) -> bytes:
    data = bytearray()
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise RuntimeError(f"connection closed after {data.hex(' ')}")
        data.extend(chunk)
    return bytes(data)


def module_command(sock: socket.socket, opcode: int, args: bytes = b"") -> bytes:
    frame = build_frame(opcode, args)
    sock.sendall(frame)
    head = recv_exact(sock, 2)
    if head[0] != 0xFF:
        raise RuntimeError(f"reply does not start with FF: {head.hex(' ')}")
    rest = recv_exact(sock, head[1] + 5)
    reply = head + rest
    data_len = reply[1]
    if crc16(reply[1:5 + data_len]) != ((reply[-2] << 8) | reply[-1]):
        raise RuntimeError(f"bad CRC: {reply.hex(' ')}")
    if reply[2] != opcode:
        raise RuntimeError(f"opcode mismatch: {reply.hex(' ')}")
    status = (reply[3] << 8) | reply[4]
    print(f"  TX {frame.hex(' ')}")
    print(f"  RX {reply.hex(' ')}  status={status:04X}")
    if status != 0:
        raise RuntimeError(f"module status {status:04X}")
    return reply[5:5 + data_len]


def connect(host: str, port: int, timeout: float) -> socket.socket:
    sock = socket.create_connection((host, port), timeout=5.0)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.settimeout(timeout)
    return sock


def detect(host: str, port: int) -> bytes:
    print("[1] detection connection (HardWareDetector.GetHwVer)")
    sock = connect(host, port, 0.3)
    sock.sendall(STOP_ASYNC)
    try:
        extra = sock.recv(1000)
        print(f"  stop-async reply: {extra.hex(' ') if extra else '(closed)'}")
    except socket.timeout:
        print("  stop-async: no reply within 300 ms (accepted by Reader Manager)")

    sock.settimeout(0.4)
    sock.sendall(b"IOGET")
    reply = recv_exact(sock, 6)
    print(f"  IOGET -> {reply.hex(' ')}")
    if reply[:5] != b"IOGET":
        raise RuntimeError("IOGET not echoed: board would not be detected as ARM7")
    print(f"  board = ARM7, GPI bitmap = {reply[5]:08b}")

    sock.settimeout(3.85)
    version = module_command(sock, 0x03)
    print(f"  model byte data[4] = {version[4]:02X} (33 = E310 family), "
          f"variant data[5] = {version[5]:02X}, data[7] = {version[7]:02X}")
    sock.close()
    return version


def init_session(host: str, port: int) -> None:
    print("[2] ClearRecvBuffer: connect, close, reconnect")
    connect(host, port, 1.0).close()
    time.sleep(0.2)
    sock = connect(host, port, 3.85)

    print("[3] init + parameter reads (all must succeed for CONNECTED)")
    version = module_command(sock, 0x04)
    if len(version) > 7 and version[7] == 0x80:
        module_command(sock, 0x05)
    module_command(sock, 0x61, b"\x05")
    module_command(sock, 0x9A, b"\x01\x08\x00")
    module_command(sock, 0x9A, b"\x01\x00\x01")
    try:
        module_command(sock, 0x65, b"\x02")
    except RuntimeError as exc:
        print(f"  HopAntTime failed (optional): {exc}")
    module_command(sock, 0x10, b"\x00\x00")

    sock.sendall(b"VER")
    print(f"  VER -> {recv_exact(sock, 4).hex(' ')}")

    module_command(sock, 0x62, b"\x01")
    module_command(sock, 0x62, b"\x01")
    module_command(sock, 0x6A, b"\x01\x00")
    module_command(sock, 0x6A, b"\x01\x08")
    print("  -> Reader Manager would now set IsConnect = true")

    print("[3b] mainboard commands on the session")
    sock.sendall(b"IOGET")
    reply = recv_exact(sock, 6)
    print(f"  IOGET -> {reply.hex(' ')}")
    for state in (1, 0):
        sock.sendall(b"IOSET\x01\x01" + bytes([state]))
        reply = recv_exact(sock, 7)
        print(f"  IOSET GPO1={state} -> {reply!r}")
        if reply != b"IOSETOK":
            raise RuntimeError("IOSET not acknowledged with IOSETOK")
    sock.sendall(b"IPGET")
    reply = recv_exact(sock, 25)
    ip = ".".join(str(b) for b in reply[5:9])
    mask = ".".join(str(b) for b in reply[9:13])
    gw = ".".join(str(b) for b in reply[13:17])
    mac = ":".join(f"{b:02X}" for b in reply[17:23])
    print(f"  IPGET -> ip={ip} mask={mask} gw={gw} mac={mac} port={reply[23] << 8 | reply[24]}")
    if reply[17] & 0x01:
        raise RuntimeError("reader MAC has the multicast bit set")
    sock.close()


def async_gpio(host: str, port: int) -> None:
    """GIO / SIO inside a running fast (async) inventory, as Reader Manager
    does when GPI/GPO are used while reading."""
    print("[5] fast inventory with GIO / SIO injected replies")
    sock = connect(host, port, 3.85)
    module_command(sock, 0x04)
    start = bytes.fromhex("AA") + b"Moduletech" + bytes.fromhex("AA48")
    # Minimal fast-read start: metadata 0x0000, option 0, search flags 0x0000.
    body = start + bytes.fromhex("0000 00 0000".replace(" ", ""))
    subcrc = sum(body[11:]) & 0xFF
    frame = build_frame(0xAA, body[1:] + bytes([subcrc, 0xBB]))
    sock.sendall(frame)
    time.sleep(0.5)
    sock.sendall(b"GIO")
    sock.sendall(b"SIO\x01\x01\x00")
    deadline = time.time() + 2.0
    data = b""
    sock.settimeout(0.2)
    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue
        if not chunk:
            break
        data += chunk
    found = {"GIO": False, "SIO": False}
    i = 0
    while i + 2 <= len(data):
        if data[i] != 0xFF:
            i += 1
            continue
        length = data[i + 1]
        frame_end = i + length + 7
        if frame_end > len(data):
            break
        frame_bytes = data[i:frame_end]
        if crc16(frame_bytes[1:-2]) == (frame_bytes[-2] << 8 | frame_bytes[-1]):
            payload = frame_bytes[5:5 + length]
            for key in found:
                if payload.startswith(key.encode()):
                    found[key] = True
            i = frame_end
        else:
            i += 1
    stop = build_frame(0xAA, b"Moduletech" + bytes.fromhex("AA49") + bytes([(0xAA + 0x49) & 0xFF, 0xBB]))
    sock.sendall(stop)
    print(f"  received {len(data)} bytes; GIO frame={found['GIO']} SIO frame={found['SIO']}")
    sock.close()
    if not all(found.values()):
        raise RuntimeError("GIO/SIO reply frame missing or corrupted in the stream")


def ee_request(host: str, port: int, cmd: int, content: bytes, timeout: float = 2.0) -> tuple[int, bytes]:
    sock = connect(host, port, timeout)
    sock.sendall(bytes([0xEE, 0x00, len(content) >> 8, len(content) & 0xFF, cmd, 0x01]) + content)
    header = recv_exact(sock, 10)
    length = (header[2] << 8) | header[3]
    body = recv_exact(sock, length)
    sock.close()
    status = int.from_bytes(header[6:10], "big")
    return status, body


def ee_checks(host: str, port: int, write_tests: bool) -> None:
    print("[4] EE configuration commands (one connection each)")
    status, body = ee_request(host, port, 0x22, b"\x00")
    print(f"  0x22 work state -> status={status} state={body[0]}")
    for cmd, name in ((0x24, "ActiveModeJson"), (0x1E, "StaticModelJson")):
        status, body = ee_request(host, port, cmd, b"\x00\x00")
        if b"\x00" in body:
            raise RuntimeError(f"{name} reply contains NUL padding (ReaderSense rejects it)")
        doc = json.loads(body.decode("utf-8"))
        print(f"  0x{cmd:02X} {name} -> status={status}, keys={list(doc)}")
    if write_tests:
        # Toggle the work state twice: leaves the reader as it was.
        for _ in range(2):
            status, body = ee_request(host, port, 0x23, b"\x01")
            print(f"  0x23 switch -> status={status} new state={body[0]}")
            time.sleep(2.5)
        status, body = ee_request(host, port, 0x22, b"\x00")
        print(f"  0x22 work state after two switches -> {body[0]}")


def udp_discovery(host: str) -> None:
    print("[6] UDP discovery (port 15000)")
    probe = bytes.fromhex("EE0600005001000000000000")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(1.0)
    sock.sendto(probe, (host, 15000))
    try:
        reply, source = sock.recvfrom(256)
    except socket.timeout:
        raise RuntimeError("no discovery reply")
    finally:
        sock.close()
    if len(reply) < 47:
        raise RuntimeError(f"discovery reply too short ({len(reply)} bytes)")
    print(f"  from {source[0]}: mac={':'.join(f'{b:02X}' for b in reply[10:16])} "
          f"ip={'.'.join(str(b) for b in reply[17:21])} port={reply[33] << 8 | reply[34]} work={reply[46]}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.150.50")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--write-tests", action="store_true",
                        help="also run reversible EE write commands (work-state switch x2)")
    parser.add_argument("--async-gpio", action="store_true",
                        help="also start a fast inventory and check GIO/SIO frames (needs tags optional)")
    args = parser.parse_args()

    detect(args.host, args.port)
    init_session(args.host, args.port)
    if args.async_gpio:
        async_gpio(args.host, args.port)
    ee_checks(args.host, args.port, args.write_tests)
    udp_discovery(args.host)
    print("PASS")


if __name__ == "__main__":
    main()
