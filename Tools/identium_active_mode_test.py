#!/usr/bin/env python3
"""
ActiveMode interoperability test for the STM32F429 Identium firmware.

The reader uploads JSON-over-TCP to this PC on port 9000. ActiveMode and
StaticModel readback are obtained from the reader's configuration port 8080.

The test exercises the ActiveMode fields that are actually implemented by the
F429 firmware:
  * glob_params: s_buf_size, watchdogs, remote_cmd_reply
  * upload: recv_timeout, clr_r_buf_time, data_aggr
  * events: codes 1/2/3/6
  * cus_param
  * validation of unsupported client_ack/crc_enable/hw_inf combinations
  * sync_time_req on a new connection

It restores the default ActiveMode profile before exiting successfully.
"""

from __future__ import annotations

import argparse
import json
import socket
import time
from typing import Any

SERVER_HOST = "0.0.0.0"
SERVER_PORT = 9000
CONFIG_PORT = 8080
COMMAND_TIMEOUT = 8.0
CONFIG_TIMEOUT = 5.0
RECONNECT_TIMEOUT = 35.0


def log(message: str) -> None:
    print(f"[TEST] {message}", flush=True)


def recv_line(conn: socket.socket, timeout: float = COMMAND_TIMEOUT) -> dict[str, Any]:
    conn.settimeout(timeout)
    buffer = bytearray()
    while True:
        chunk = conn.recv(4096)
        if not chunk:
            raise RuntimeError("reader disconnected while waiting for JSON")
        buffer.extend(chunk)
        while b"\n" in buffer:
            line, _, buffer = buffer.partition(b"\n")
            line = line.strip(b"\r \t")
            if not line:
                continue
            try:
                return json.loads(line.decode("utf-8"))
            except json.JSONDecodeError:
                continue


def recv_command_reply(conn: socket.socket, command_type: str) -> dict[str, Any]:
    deadline = time.monotonic() + COMMAND_TIMEOUT
    while time.monotonic() < deadline:
        remaining = max(0.1, deadline - time.monotonic())
        obj = recv_line(conn, remaining)
        if obj.get("event_type") != "command_reply":
            continue
        data = obj.get("event_data")
        if isinstance(data, dict) and data.get("command_type") == command_type:
            return data
    raise RuntimeError(f"timeout waiting for {command_type} reply")


def send_command(conn: socket.socket, command_type: str, command_data: Any) -> dict[str, Any]:
    message = {
        "command_type": command_type,
        "command_data": command_data,
    }
    conn.sendall((json.dumps(message, separators=(",", ":")) + "\n").encode("utf-8"))
    log(f"TX {command_type}: {command_data!r}")
    reply = recv_command_reply(conn, command_type)
    log(
        f"RX {command_type}: err_code={reply.get('err_code')} "
        f"err_string={reply.get('err_string')!r}"
    )
    return reply


def send_raw(conn: socket.socket, payload: str) -> dict[str, Any]:
    conn.sendall(payload.encode("utf-8"))
    line = recv_line(conn)
    data = line.get("event_data")
    if not isinstance(data, dict):
        raise RuntimeError(f"unexpected raw-command reply: {line!r}")
    log(
        f"RX raw: command_type={data.get('command_type')!r} "
        f"err_code={data.get('err_code')} err_string={data.get('err_string')!r}"
    )
    return data


def recv_exact(conn: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = conn.recv(size - len(data))
        if not chunk:
            raise RuntimeError("configuration port closed before full response")
        data.extend(chunk)
    return bytes(data)


def read_config_json(reader_ip: str, command_id: int) -> Any:
    request = bytes([0xEE, 0x00, 0x00, 0x02, command_id, 0x01, 0x00, 0x00])
    deadline = time.monotonic() + CONFIG_TIMEOUT
    last_error: Exception | None = None

    while time.monotonic() < deadline:
        try:
            with socket.create_connection((reader_ip, CONFIG_PORT), timeout=1.0) as conn:
                conn.settimeout(2.0)
                conn.sendall(request)
                header = recv_exact(conn, 10)
                if header[0] != 0xFF or header[1] != 0x00:
                    raise RuntimeError(f"invalid header: {header.hex()}")
                data_length = (header[2] << 8) | header[3]
                status = (
                    (header[6] << 24)
                    | (header[7] << 16)
                    | (header[8] << 8)
                    | header[9]
                )
                payload = recv_exact(conn, data_length)
                if status != 0:
                    raise RuntimeError(f"command 0x{command_id:02X} status={status}")
                if command_id == 0x22:
                    return payload[0]
                return json.loads(payload.rstrip(b"\x00 ").decode("utf-8"))
        except Exception as exc:  # noqa: BLE001
            last_error = exc
            time.sleep(0.2)

    raise RuntimeError(f"configuration read failed: {last_error}")


def wait_for_reader(server: socket.socket) -> socket.socket:
    server.settimeout(RECONNECT_TIMEOUT)
    conn, addr = server.accept()
    log(f"Reader connected from {addr[0]}:{addr[1]}")
    return conn


def expect_error(conn: socket.socket, command_type: str, command_data: Any) -> None:
    reply = send_command(conn, command_type, command_data)
    if reply.get("err_code") == 0:
        raise AssertionError(f"{command_type} unexpectedly succeeded")


def main() -> int:
    parser = argparse.ArgumentParser(description="F429 ActiveMode test")
    parser.add_argument("--reader-ip", required=True)
    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((SERVER_HOST, SERVER_PORT))
        server.listen(1)
        log(f"Listening on {SERVER_HOST}:{SERVER_PORT}")

        conn = wait_for_reader(server)
        try:
            baseline = read_config_json(args.reader_ip, 0x24)
            baseline_static = read_config_json(args.reader_ip, 0x1E)
            log(
                f"Baseline: heartbeat={baseline['glob_params']['hb_cylce']} "
                f"s_buf_size={baseline['glob_params']['s_buf_size']} "
                f"data_aggr={baseline['upload']['data_aggr']}"
            )

            reply = send_command(
                conn,
                "set_runtime_conf",
                {
                    "glob_params": {
                        "hb_cylce": 11,
                        "s_buf_size": 1600,
                        "thwdt_net_timeout": 0,
                        "thwdt_inv_timeout": 0,
                        "remote_cmd_reply": 1,
                    },
                    "upload": {
                        "recv_timeout": 12,
                        "clr_r_buf_time": 9,
                        "data_aggr": {"mode": 1, "timeval": 2000},
                    },
                    "events": [1, 2, 3],
                    "cus_param": "ACTIVEMODE-TEST",
                },
            )
            if reply.get("err_code") != 0:
                raise AssertionError(f"valid ActiveMode update rejected: {reply}")
            conn.close()
            conn = wait_for_reader(server)

            active = read_config_json(args.reader_ip, 0x24)
            assert active["glob_params"]["hb_cylce"] == 11
            assert active["glob_params"]["s_buf_size"] == 1600
            assert active["upload"]["recv_timeout"] == 12
            assert active["upload"]["clr_r_buf_time"] == 9
            assert active["upload"]["data_aggr"] == {"mode": 1, "timeval": 2000}
            assert active["events"] == [1, 2, 3]
            assert active["cus_param"] == "ACTIVEMODE-TEST"
            assert active["upload"]["client_ack"] == 0
            assert active["upload"]["crc_enable"] == 0
            log("PASS: extended ActiveMode fields persisted and read back correctly")

            # Nested partial update: changing only timeval must preserve mode.
            reply = send_command(
                conn,
                "set_runtime_conf",
                {"upload": {"data_aggr": {"timeval": 2500}}},
            )
            if reply.get("err_code") != 0:
                raise AssertionError(f"nested update rejected: {reply}")
            conn.close()
            conn = wait_for_reader(server)
            nested = read_config_json(args.reader_ip, 0x24)
            assert nested["upload"]["data_aggr"] == {"mode": 1, "timeval": 2500}
            log("PASS: nested partial update preserved sibling field")

            # Atomically reject a mixed update: valid heartbeat + invalid buffer size
            # must not commit the valid part.
            expect_error(
                conn,
                "set_runtime_conf",
                {"glob_params": {"hb_cylce": 12, "s_buf_size": 1499}},
            )
            mixed = read_config_json(args.reader_ip, 0x24)
            assert mixed["glob_params"]["hb_cylce"] == 11
            assert mixed["glob_params"]["s_buf_size"] == 1600
            log("PASS: mixed valid/invalid update committed nothing")

            # Duplicate configuration keys are rejected rather than last-key-wins.
            duplicate_payload = (
                '{"command_type":"set_runtime_conf",'
                '"command_data":{"glob_params":{"hb_cylce":14,"hb_cylce":15}}}\n'
            )
            duplicate_reply = send_raw(conn, duplicate_payload)
            assert duplicate_reply.get("err_code") != 0
            duplicate_state = read_config_json(args.reader_ip, 0x24)
            assert duplicate_state["glob_params"]["hb_cylce"] == 11
            log("PASS: duplicate JSON configuration key rejected")

            expect_error(
                conn,
                "set_runtime_conf",
                {"glob_params": {"s_buf_size": 1499}},
            )
            assert read_config_json(args.reader_ip, 0x24)["glob_params"]["s_buf_size"] == 1600
            log("PASS: invalid s_buf_size rejected without changing stored value")

            expect_error(
                conn,
                "set_runtime_conf",
                {"upload": {"data_aggr": {"timeval": 49}}},
            )
            assert read_config_json(args.reader_ip, 0x24)["upload"]["data_aggr"]["timeval"] == 2500
            log("PASS: invalid aggregation timeval rejected without changing stored value")

            expect_error(
                conn,
                "set_runtime_conf",
                {"upload": {"client_ack": 1}},
            )
            log("PASS: unsupported JSON-over-TCP client_ack request rejected")

            expect_error(
                conn,
                "set_runtime_conf",
                {"upload": {"crc_enable": 1}},
            )
            log("PASS: unsupported JSON-over-TCP CRC request rejected")

            # Verify sync_time_req. This requires a new TCP session with event 6 enabled.
            reply = send_command(
                conn,
                "set_runtime_conf",
                {"events": [1, 2, 3, 6]},
            )
            if reply.get("err_code") != 0:
                raise AssertionError(f"event-6 update rejected: {reply}")
            conn.close()
            conn = wait_for_reader(server)

            first = recv_line(conn, COMMAND_TIMEOUT)
            if first.get("event_type") != "sync_time_req":
                raise AssertionError(f"expected sync_time_req, got {first!r}")
            log("PASS: sync_time_req emitted on a new connection when event 6 is subscribed")

            # Restore default profile. The command itself keeps command replies enabled.
            send_command(
                conn,
                "sync_time",
                1789981401488,
            )
            reply = send_command(
                conn,
                "set_runtime_conf",
                {
                    "glob_params": {
                        "hb_cylce": 10,
                        "s_buf_size": 1580,
                        "thwdt_net_timeout": 0,
                        "thwdt_inv_timeout": 0,
                        "remote_cmd_reply": 1,
                    },
                    "upload": {
                        "client_ack": 0,
                        "crc_enable": 0,
                        "recv_timeout": 10,
                        "clr_r_buf_time": 8,
                        "hw_inf": 1,
                        "data_aggr": {"mode": 0, "timeval": 1000},
                    },
                    "events": [1, 2, 3],
                    "cus_param": "",
                },
            )
            if reply.get("err_code") != 0:
                raise AssertionError(f"restore failed: {reply}")
            conn.close()
            conn = wait_for_reader(server)

            final_active = read_config_json(args.reader_ip, 0x24)
            final_static = read_config_json(args.reader_ip, 0x1E)
            assert final_active["glob_params"]["hb_cylce"] == 10
            assert final_active["glob_params"]["s_buf_size"] == 1580
            assert final_active["upload"]["data_aggr"] == {"mode": 0, "timeval": 1000}
            assert final_active["events"] == [1, 2, 3]
            assert final_active["cus_param"] == ""
            assert final_static["protocol"]["gen2"]["session"] == baseline_static["protocol"]["gen2"]["session"]

            print("\nACTIVE MODE TEST: PASS")
            return 0
        finally:
            try:
                conn.close()
            except Exception:
                pass


if __name__ == "__main__":
    raise SystemExit(main())
