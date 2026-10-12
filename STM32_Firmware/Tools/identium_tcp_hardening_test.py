#!/usr/bin/env python3
"""
JSON-over-TCP hardening test for the STM32F429 Identium reader.

The reader must dial this PC on TCP port 9000.

Required framing checks:
  1. Fragmented JSON across multiple TCP writes.
  2. Multiple newline-delimited JSON commands in one write.
  3. Garbage before a valid JSON object.
  4. Oversized line followed by a valid frame.
  5. Leading whitespace / CRLF handling.
  6. Clean reconnect after the server closes the socket.

Optional tag-buffer check:
  --require-tags makes the test resume inventory, wait for at least one
  tag_read event, deliberately break the connection, and verify that a
  valid tag_read event is seen after reconnection.  Use this only with real
  tags in the RF field.
"""

from __future__ import annotations

import argparse
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
        self.invalid_lines = 0
        self.thread = threading.Thread(target=self._receiver, daemon=True)
        self.thread.start()

    def _receiver(self) -> None:
        buffer = bytearray()

        while not self.stop.is_set():
            try:
                data = self.conn.recv(4096)
            except socket.timeout:
                continue
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
                    self.invalid_lines += 1
                    print(f"[TEST] RX invalid JSON: {line!r} ({exc})")
                    continue

                with self.lock:
                    self.events.append(obj)

                print(f"[TEST] RX {json.dumps(obj, separators=(',', ':'))}")

        self.stop.set()

    def send_bytes(self, payload: bytes) -> None:
        self.conn.sendall(payload)
        print(f"[TEST] TX raw: {payload!r}")

    def send_command(self, command_type: str, command_data) -> None:
        payload = (
            json.dumps(
                {"command_type": command_type, "command_data": command_data},
                separators=(",", ":"),
            )
            + "\n"
        ).encode("utf-8")
        self.send_bytes(payload)

    def wait_for_reply(self, command_type: str, timeout: float = COMMAND_TIMEOUT) -> dict:
        deadline = time.monotonic() + timeout

        while time.monotonic() < deadline:
            with self.lock:
                for event in self.events:
                    if event.get("event_type") != "command_reply":
                        continue
                    data = event.get("event_data")
                    if not isinstance(data, dict):
                        continue
                    if data.get("command_type") != command_type:
                        continue
                    return data

            time.sleep(0.01)

        raise RuntimeError(f"no command_reply for {command_type}")

    def count_replies(self, command_type: str) -> int:
        with self.lock:
            return sum(
                1
                for event in self.events
                if event.get("event_type") == "command_reply"
                and isinstance(event.get("event_data"), dict)
                and event["event_data"].get("command_type") == command_type
            )

    def has_tag_event(self) -> bool:
        with self.lock:
            return any(event.get("event_type") == "tag_read" for event in self.events)

    def clear_events(self) -> None:
        with self.lock:
            self.events.clear()
        self.invalid_lines = 0

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
    conn.settimeout(0.25)
    return ReaderSession(conn, addr)


def valid_get_gpi() -> bytes:
    return b'{"command_type":"get_gpi","command_data":{}}\n'


def wait_until(predicate, timeout: float, description: str) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    raise RuntimeError(description)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--require-tags",
        action="store_true",
        help="Require real tag traffic during the short outage check",
    )
    parser.add_argument(
        "--outage-seconds",
        type=float,
        default=3.0,
        help="Server-side outage duration for the optional tag/reconnect check",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.outage_seconds < 0.5:
        raise SystemExit("--outage-seconds must be at least 0.5 seconds")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, PORT))
        server.listen(1)
        print(f"[TEST] Listening on {HOST}:{PORT}")
        print("[TEST] Reader must be configured to connect to this PC on TCP port 9000.")

        session = wait_for_connection(server, RECONNECT_TIMEOUT)

        try:
            payload = valid_get_gpi()

            # 1. Fragmented JSON frame.
            session.clear_events()
            split = 17
            session.send_bytes(payload[:split])
            time.sleep(0.25)
            session.send_bytes(payload[split:])
            reply = session.wait_for_reply("get_gpi")
            if reply.get("err_code") != 0:
                raise RuntimeError(f"fragmented get_gpi failed: {reply}")
            print("[TEST] PASS: fragmented JSON frame")

            # 2. Many complete frames in one TCP write.
            session.clear_events()
            session.send_bytes(payload * 12)
            wait_until(
                lambda: session.count_replies("get_gpi") >= 12,
                COMMAND_TIMEOUT,
                "expected 12 command replies from one TCP write",
            )
            print("[TEST] PASS: 12 JSON frames in one TCP write")

            # 3. Garbage before a valid JSON object on the same line.
            session.clear_events()
            session.send_bytes(b"garbage-prefix !! \t" + payload)
            reply = session.wait_for_reply("get_gpi")
            if reply.get("err_code") != 0:
                raise RuntimeError(f"garbage-prefix get_gpi failed: {reply}")
            print("[TEST] PASS: garbage prefix resynchronised")

            # 4. Oversized line must be discarded, but the next valid frame
            # must remain decodable.
            session.clear_events()
            oversized = b"{" + (b"A" * 2200) + b"}\n"
            session.send_bytes(oversized + payload)
            reply = session.wait_for_reply("get_gpi")
            if reply.get("err_code") != 0:
                raise RuntimeError(f"post-oversize get_gpi failed: {reply}")
            print("[TEST] PASS: oversized line discarded, next valid frame decoded")

            # 5. Leading whitespace plus CRLF.
            session.clear_events()
            session.send_bytes(b" \t" + payload.replace(b"\n", b"\r\n"))
            reply = session.wait_for_reply("get_gpi")
            if reply.get("err_code") != 0:
                raise RuntimeError(f"whitespace get_gpi failed: {reply}")
            print("[TEST] PASS: leading whitespace / CRLF accepted")

            if session.invalid_lines != 0:
                raise RuntimeError(f"reader emitted {session.invalid_lines} malformed JSON lines")

            # Optional tag-buffer check with real RF tags.
            if args.require_tags:
                session.clear_events()
                session.send_command("resume_inventory", {})
                reply = session.wait_for_reply("resume_inventory")
                if reply.get("err_code") != 0:
                    raise RuntimeError(f"resume_inventory failed: {reply}")

                wait_until(
                    session.has_tag_event,
                    10.0,
                    "no tag_read event observed before the outage; use --require-tags only with tags present",
                )
                print("[TEST] PASS: tag traffic observed before outage")

            # 6. Close server side and require the reader to reconnect.
            old_addr = session.addr
            session.close()
            print(
                f"[TEST] Closed TCP session; waiting {args.outage_seconds:.1f}s before accepting reconnect..."
            )
            time.sleep(args.outage_seconds)

            new_session = wait_for_connection(server, RECONNECT_TIMEOUT)
            try:
                if new_session.addr[0] != old_addr[0]:
                    raise RuntimeError("reader reconnect came from unexpected host")

                wait_until(
                    lambda: any(
                        event.get("event_type") == "heart_beat"
                        for event in new_session.events
                    ),
                    5.0,
                    "reader reconnected but no heartbeat was received",
                )
                print("[TEST] PASS: reconnect after server-side close")

                if args.require_tags:
                    wait_until(
                        new_session.has_tag_event,
                        5.0,
                        "no tag_read event observed after reconnect",
                    )
                    print("[TEST] PASS: valid tag traffic resumed after reconnect")

            finally:
                new_session.close()

        finally:
            if not session.stop.is_set():
                session.close()

    print("\nTCP HARDENING TEST: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
