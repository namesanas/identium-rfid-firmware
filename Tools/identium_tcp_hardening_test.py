#!/usr/bin/env python3
"""
Step 7A JSON-over-TCP hardening test for the STM32F429 Identium reader.

The reader is expected to dial this PC on TCP port 9000.
The tests exercise the framing/resynchronisation cases explicitly called out
in the protocol specification, without changing reader configuration.
"""

from __future__ import annotations

import json
import socket
import threading
import time

HOST = "0.0.0.0"
PORT = 9000
COMMAND_TIMEOUT = 5.0
RECONNECT_TIMEOUT = 20.0


class ReaderSession:
    def __init__(self, conn: socket.socket, addr: tuple[str, int]) -> None:
        self.conn = conn
        self.addr = addr
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.events: list[dict] = []
        self.thread = threading.Thread(target=self._receiver, daemon=True)
        self.thread.start()

    def _receiver(self) -> None:
        buffer = bytearray()

        while not self.stop.is_set():
            try:
                data = self.conn.recv(4096)
            except OSError:
                break

            if not data:
                break

            buffer.extend(data)

            while b"\n" in buffer:
                line, _, buffer = buffer.partition(b"\n")
                line = line.strip(b"\r \t")
                if not line:
                    continue

                try:
                    obj = json.loads(line.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                    print(f"[TEST] RX invalid JSON: {line!r} ({exc})")
                    continue

                with self.lock:
                    self.events.append(obj)

                print(f"[TEST] RX {json.dumps(obj, separators=(',', ':'))}")

        self.stop.set()

    def send_bytes(self, payload: bytes) -> None:
        self.conn.sendall(payload)
        print(f"[TEST] TX raw: {payload!r}")

    def send_get_gpi(self) -> None:
        payload = (
            json.dumps(
                {"command_type": "get_gpi", "command_data": {}},
                separators=(",", ":"),
            )
            + "\n"
        ).encode("utf-8")
        self.send_bytes(payload)

    def wait_for_reply(self, timeout: float = COMMAND_TIMEOUT) -> bool:
        deadline = time.monotonic() + timeout

        while time.monotonic() < deadline:
            with self.lock:
                for event in self.events:
                    if event.get("event_type") != "command_reply":
                        continue
                    data = event.get("event_data")
                    if not isinstance(data, dict):
                        continue
                    if data.get("command_type") != "get_gpi":
                        continue
                    if data.get("err_code") != 0:
                        raise RuntimeError(f"get_gpi failed: {data}")
                    return True

            time.sleep(0.01)

        return False

    def clear_events(self) -> None:
        with self.lock:
            self.events.clear()

    def close(self) -> None:
        self.stop.set()
        try:
            self.conn.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        try:
            self.conn.close()
        except OSError:
            pass
        self.thread.join(timeout=1.0)


def wait_for_connection(server: socket.socket, timeout: float) -> ReaderSession:
    server.settimeout(timeout)
    conn, addr = server.accept()
    print(f"[TEST] Reader connected from {addr[0]}:{addr[1]}")
    conn.settimeout(2.0)
    return ReaderSession(conn, addr)


def valid_get_gpi() -> bytes:
    return b'{"command_type":"get_gpi","command_data":{}}\n'


def main() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, PORT))
        server.listen(1)
        print(f"[TEST] Listening on {HOST}:{PORT}")
        print("[TEST] Reader must be configured to connect to this PC on TCP port 9000.")

        session = wait_for_connection(server, RECONNECT_TIMEOUT)

        try:
            # 1. Fragmented JSON frame.
            session.clear_events()
            payload = valid_get_gpi()
            split = 17
            session.send_bytes(payload[:split])
            time.sleep(0.25)
            session.send_bytes(payload[split:])
            if not session.wait_for_reply():
                raise RuntimeError("fragmented frame was not decoded")
            print("[TEST] PASS: fragmented JSON frame")

            # 2. Two complete frames in one TCP write.
            session.clear_events()
            session.send_bytes(payload + payload)
            deadline = time.monotonic() + COMMAND_TIMEOUT
            replies = 0
            while time.monotonic() < deadline:
                with session.lock:
                    replies = sum(
                        1
                        for event in session.events
                        if event.get("event_type") == "command_reply"
                        and isinstance(event.get("event_data"), dict)
                        and event["event_data"].get("command_type") == "get_gpi"
                        and event["event_data"].get("err_code") == 0
                    )
                if replies >= 2:
                    break
                time.sleep(0.01)
            if replies < 2:
                raise RuntimeError(f"expected two command replies, got {replies}")
            print("[TEST] PASS: two JSON frames in one TCP write")

            # 3. Garbage directly before a valid JSON object on the same line.
            session.clear_events()
            session.send_bytes(b"garbage-prefix !! \t" + payload)
            if not session.wait_for_reply():
                raise RuntimeError("parser did not resynchronise at opening brace")
            print("[TEST] PASS: garbage prefix resynchronised")

            # 4. Oversized line must be discarded, but the next valid frame must work.
            session.clear_events()
            oversized = b"{" + (b"A" * 2200) + b"}\n"
            session.send_bytes(oversized + payload)
            if not session.wait_for_reply():
                raise RuntimeError("valid frame after oversized line was not decoded")
            print("[TEST] PASS: oversized line discarded, next valid frame decoded")

            # 5. Leading whitespace before a valid object.
            session.clear_events()
            session.send_bytes(b" \t" + payload)
            if not session.wait_for_reply():
                raise RuntimeError("leading-whitespace frame was not decoded")
            print("[TEST] PASS: leading whitespace accepted")

            # 6. Close the server side and require the reader to reconnect.
            old_addr = session.addr
            session.close()
            print("[TEST] Closed TCP session; waiting for reader reconnect...")
            new_session = wait_for_connection(server, RECONNECT_TIMEOUT)
            try:
                if new_session.addr[0] != old_addr[0]:
                    raise RuntimeError("reader reconnect came from unexpected host")

                new_session.clear_events()
                deadline = time.monotonic() + RECONNECT_TIMEOUT
                heartbeat_seen = False
                while time.monotonic() < deadline:
                    with new_session.lock:
                        heartbeat_seen = any(
                            event.get("event_type") == "heart_beat"
                            for event in new_session.events
                        )
                    if heartbeat_seen:
                        break
                    time.sleep(0.05)

                if not heartbeat_seen:
                    raise RuntimeError("reader reconnected but no heartbeat was received")
                print("[TEST] PASS: reconnect after server-side close")
            finally:
                new_session.close()

        finally:
            if not session.stop.is_set():
                session.close()

    print("\nSTEP 7A TCP HARDENING TEST: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
