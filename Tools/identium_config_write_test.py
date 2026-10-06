#!/usr/bin/env python3
"""
Automated Step-6 configuration write test for the F429 Identium firmware.

The firmware acts as a TCP client toward the test server on port 9000.
Configuration readback is checked through the separate TCP configuration
port 8080.

Run from the project root/network host:
    python Tools/identium_config_write_test.py --reader-ip 192.168.150.50

The script intentionally leaves the reader at its baseline configuration at
successful exit: heartbeat=10 s, start-at-boot=0, Gen2 session=0.
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
ACCEPT_TIMEOUT = 60.0
COMMAND_TIMEOUT = 8.0
CONFIG_TIMEOUT = 5.0
RECONNECT_TIMEOUT = 35.0


def log(message: str) -> None:
    print(f"[TEST] {message}", flush=True)


def recv_json_reply(conn: socket.socket, command_type: str) -> dict[str, Any]:
    conn.settimeout(COMMAND_TIMEOUT)
    buffer = bytearray()

    while True:
        chunk = conn.recv(4096)
        if not chunk:
            raise RuntimeError("reader disconnected before command_reply")

        buffer.extend(chunk)

        while b"\n" in buffer:
            line, _, buffer = buffer.partition(b"\n")
            line = line.strip(b"\r \t")
            if not line:
                continue

            obj = json.loads(line.decode("utf-8"))
            if obj.get("event_type") != "command_reply":
                continue

            event_data = obj.get("event_data")
            if not isinstance(event_data, dict):
                raise RuntimeError(f"invalid command_reply payload: {obj!r}")

            if event_data.get("command_type") != command_type:
                continue

            return event_data


def send_command(
    conn: socket.socket,
    command_type: str,
    command_data: Any,
    **extra: Any,
) -> dict[str, Any]:
    message = {
        "command_type": command_type,
        "command_data": command_data,
        **extra,
    }
    payload = (json.dumps(message, separators=(",", ":")) + "\n").encode("utf-8")
    conn.sendall(payload)
    log(f"TX {command_type}: {message['command_data']!r}")
    reply = recv_json_reply(conn, command_type)
    log(
        f"RX {command_type}: err_code={reply.get('err_code')} "
        f"err_string={reply.get('err_string')!r}"
    )
    return reply


def wait_for_reader(server: socket.socket, timeout: float = RECONNECT_TIMEOUT) -> socket.socket:
    server.settimeout(timeout)
    conn, addr = server.accept()
    log(f"Reader connected from {addr[0]}:{addr[1]}")
    return conn


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
                    raise RuntimeError(f"invalid config response header: {header.hex()}")

                data_length = (header[2] << 8) | header[3]
                status = (
                    (header[6] << 24)
                    | (header[7] << 16)
                    | (header[8] << 8)
                    | header[9]
                )
                payload = recv_exact(conn, data_length)

                if status != 0:
                    raise RuntimeError(
                        f"config command 0x{command_id:02X} returned status {status}"
                    )

                if command_id == 0x22:
                    return payload[0]

                text = payload.rstrip(b"\x00").decode("utf-8")
                return json.loads(text)
        except Exception as exc:  # noqa: BLE001 - retry until timeout
            last_error = exc
            time.sleep(0.2)

    raise RuntimeError(f"configuration port read failed: {last_error}")


def recv_exact(conn: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = conn.recv(size - len(data))
        if not chunk:
            raise RuntimeError("configuration port closed before full response")
        data.extend(chunk)
    return bytes(data)


def assert_equal(actual: Any, expected: Any, label: str) -> None:
    if actual != expected:
        raise AssertionError(f"{label}: expected {expected!r}, got {actual!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description="F429 Identium Step-6 configuration write test")
    parser.add_argument("--reader-ip", required=True, help="Reader IP address used for config port 8080")
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
                f"session={baseline_static['protocol']['gen2']['session']}"
            )

            original_upload = baseline["upload"]
            original_events = baseline["events"]
            original_tag_format = baseline["tag_json_format"]

            # 1. Permanent runtime partial update. This must restart the reader.
            reply = send_command(
                conn,
                "set_runtime_conf",
                {"glob_params": {"hb_cylce": 12}},
            )
            assert_equal(reply["err_code"], 0, "set_runtime_conf heartbeat")
            conn.close()

            conn = wait_for_reader(server, RECONNECT_TIMEOUT)
            active = read_config_json(args.reader_ip, 0x24)
            assert_equal(active["glob_params"]["hb_cylce"], 12, "persisted heartbeat")
            assert_equal(active["upload"], original_upload, "deep-merge upload preservation")
            assert_equal(active["events"], original_events, "deep-merge event preservation")
            assert_equal(active["tag_json_format"], original_tag_format, "deep-merge tag format preservation")
            log("PASS: partial runtime update persisted and unrelated fields were preserved")

            # 2. Invalid heartbeat is rejected and does not reboot the reader.
            reply = send_command(
                conn,
                "set_runtime_conf",
                {"glob_params": {"hb_cylce": 4}},
            )
            if reply["err_code"] == 0:
                raise AssertionError("invalid heartbeat was accepted")
            active_after_invalid = read_config_json(args.reader_ip, 0x24)
            assert_equal(active_after_invalid["glob_params"]["hb_cylce"], 12, "heartbeat after invalid update")
            log("PASS: invalid heartbeat rejected without changing stored value")

            # 3. Temporary static config must be rejected while inventory runs.
            reply = send_command(conn, "resume_inventory", {})
            assert_equal(reply["err_code"], 0, "resume_inventory")

            reply = send_command(
                conn,
                "set_temp_static_conf",
                {"protocol": {"gen2": {"session": 1}}},
            )
            assert_equal(reply["err_code"] != 0, True, "temp config while running error")
            assert_equal(
                reply["err_string"],
                "invalid operation, please pause the inventory first",
                "temp config while running error string",
            )
            log("PASS: temp static config correctly requires paused inventory")

            reply = send_command(conn, "pause_inventory", {})
            assert_equal(reply["err_code"], 0, "pause_inventory")

            # 4. Temporary session applies live but must not be saved.
            reply = send_command(
                conn,
                "set_temp_static_conf",
                {"protocol": {"gen2": {"session": 1}}},
            )
            assert_equal(reply["err_code"], 0, "set_temp_static_conf")
            temp_static = read_config_json(args.reader_ip, 0x1E)
            assert_equal(temp_static["protocol"]["gen2"]["session"], 1, "temporary session")
            log("PASS: temporary session applied live")

            # 5. A later permanent runtime update must not accidentally persist
            # the temporary session overlay.
            reply = send_command(
                conn,
                "set_runtime_conf",
                {"glob_params": {"hb_cylce": 13}},
            )
            assert_equal(reply["err_code"], 0, "set_runtime_conf after temp static")
            conn.close()

            conn = wait_for_reader(server, RECONNECT_TIMEOUT)
            active_after_runtime = read_config_json(args.reader_ip, 0x24)
            static_after_runtime = read_config_json(args.reader_ip, 0x1E)
            assert_equal(active_after_runtime["glob_params"]["hb_cylce"], 13, "runtime heartbeat after temp static")
            assert_equal(
                static_after_runtime["protocol"]["gen2"]["session"],
                0,
                "temporary session did not leak into persistent configuration",
            )
            log("PASS: temporary session was not accidentally persisted by a later runtime save")

            # 6. Permanent static session write.
            reply = send_command(
                conn,
                "set_static_conf",
                {"protocol": {"gen2": {"session": 2}}},
            )
            assert_equal(reply["err_code"], 0, "set_static_conf session")
            static_now = read_config_json(args.reader_ip, 0x1E)
            assert_equal(static_now["protocol"]["gen2"]["session"], 2, "permanent session")
            log("PASS: permanent static session update applied and saved")

            # 7. Explicit reboot; persisted values must survive restart.
            reply = send_command(conn, "reboot", {})
            assert_equal(reply["err_code"], 0, "reboot")
            conn.close()

            conn = wait_for_reader(server, RECONNECT_TIMEOUT)
            active_after_reboot = read_config_json(args.reader_ip, 0x24)
            static_after_reboot = read_config_json(args.reader_ip, 0x1E)
            assert_equal(active_after_reboot["glob_params"]["hb_cylce"], 13, "heartbeat after reboot")
            assert_equal(static_after_reboot["protocol"]["gen2"]["session"], 2, "session after reboot")
            log("PASS: permanent runtime/static values survived reboot")

            # 8. Temp static remains non-persistent across reboot.
            reply = send_command(
                conn,
                "set_temp_static_conf",
                {"protocol": {"gen2": {"session": 1}}},
            )
            assert_equal(reply["err_code"], 0, "second temp session")
            assert_equal(
                read_config_json(args.reader_ip, 0x1E)["protocol"]["gen2"]["session"],
                1,
                "second temporary session",
            )

            reply = send_command(conn, "reboot", {})
            assert_equal(reply["err_code"], 0, "reboot after temporary change")
            conn.close()
            conn = wait_for_reader(server, RECONNECT_TIMEOUT)

            static_after_temp_reboot = read_config_json(args.reader_ip, 0x1E)
            assert_equal(
                static_after_temp_reboot["protocol"]["gen2"]["session"],
                2,
                "temporary session not persisted",
            )
            log("PASS: temporary session was not persisted")

            # 9. Empty configuration is rejected, then reset + reboot returns the
            # supported configuration fields to factory defaults.
            reply = send_command(
                conn,
                "set_static_conf",
                {},
            )
            if reply["err_code"] == 0:
                raise AssertionError("empty set_static_conf was unexpectedly accepted")
            log("PASS: empty static configuration rejected")

            reply = send_command(
                conn,
                "set_static_conf",
                {},
                reset=1,
                reboot=1,
            )
            assert_equal(reply["err_code"], 0, "reset + reboot")
            conn.close()
            conn = wait_for_reader(server, RECONNECT_TIMEOUT)

            final_active = read_config_json(args.reader_ip, 0x24)
            final_static = read_config_json(args.reader_ip, 0x1E)
            assert_equal(final_active["glob_params"]["hb_cylce"], 10, "final heartbeat")
            assert_equal(final_active["glob_params"]["start_inv_atbeg"], 0, "final start-at-boot")
            assert_equal(final_static["protocol"]["gen2"]["session"], 0, "final session")
            log("PASS: reset returned the supported configuration fields to defaults")

            print("\nSTEP 6 CONFIGURATION WRITE TEST: PASS")
            return 0
        finally:
            try:
                conn.close()
            except Exception:
                pass


if __name__ == "__main__":
    raise SystemExit(main())
