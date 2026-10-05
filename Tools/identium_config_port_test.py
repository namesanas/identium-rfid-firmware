#!/usr/bin/env python3
"""Small test client for the Identium reader configuration port (TCP 8080)."""

import argparse
import socket
import struct
import json

CMD_WORK_STATE = 0x22
CMD_ACTIVE_JSON = 0x24
CMD_STATIC_JSON = 0x1E


def build_request(command_id: int) -> bytes:
    content_length = 1 if command_id == CMD_WORK_STATE else 2
    return bytes([
        0xEE,
        0x00,
        (content_length >> 8) & 0xFF,
        content_length & 0xFF,
        command_id,
        0x01,
    ]) + bytes(content_length)


def recv_exact(sock: socket.socket, length: int) -> bytes:
    data = bytearray()
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            break
        data.extend(chunk)
    return bytes(data)


def read_response(sock: socket.socket) -> tuple[int, int, bytes]:
    header = recv_exact(sock, 10)
    if len(header) != 10:
        raise RuntimeError(f"short response header: {header.hex(' ')}")

    if header[0] != 0xFF or header[1] != 0x00:
        raise RuntimeError(f"invalid response header: {header.hex(' ')}")

    content_length = struct.unpack(">H", header[2:4])[0]
    command_id = header[4]
    status = struct.unpack(">I", header[6:10])[0]
    content = recv_exact(sock, content_length)

    if len(content) != content_length:
        raise RuntimeError("response content was truncated")

    return command_id, status, content


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="192.168.150.50")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument(
        "--cmd",
        choices=["state", "active", "static"],
        default="active",
    )
    args = parser.parse_args()

    command_id = {
        "state": CMD_WORK_STATE,
        "active": CMD_ACTIVE_JSON,
        "static": CMD_STATIC_JSON,
    }[args.cmd]

    request = build_request(command_id)

    print(f"Connecting to {args.host}:{args.port}")
    print(f"TX: {request.hex(' ')}")

    with socket.create_connection((args.host, args.port), timeout=5.0) as sock:
        sock.sendall(request)
        response_command, status, content = read_response(sock)

    print(f"RX command: 0x{response_command:02X}")
    print(f"RX status : {status}")
    print(f"RX content: {len(content)} bytes")
    print(f"RX hex    : {content.hex(' ')}")

    if response_command != command_id:
        print("ERROR: response command ID does not match request")
        return 1

    if status != 0:
        print("ERROR: reader reported non-zero status")
        return 1

    if command_id == CMD_WORK_STATE:
        if len(content) != 1 or content[0] not in (1, 3):
            print("ERROR: invalid work-state payload")
            return 1
        print("Work state:", "passive" if content[0] == 1 else "active")
        return 0

    json_bytes = content.rstrip(b"\x00 ")
    try:
        document = json.loads(json_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        print(f"ERROR: invalid JSON content: {exc}")
        return 1

    print(json.dumps(document, indent=2))
    print("JSON validation: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
