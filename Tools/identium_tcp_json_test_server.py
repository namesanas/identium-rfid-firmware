#!/usr/bin/env python3
"""
Minimal ReaderSense-style JSON-over-TCP development server.

The firmware is expected to connect OUTBOUND to TCP port 9000 and
exchange newline-delimited JSON objects on the same socket.
"""

from __future__ import annotations

import json
import socket
import threading

HOST = "0.0.0.0"
PORT = 9000


def send_command(conn: socket.socket, command_type: str, command_data) -> None:
    message = {
        "command_type": command_type,
        "command_data": command_data,
    }
    payload = (json.dumps(message, separators=(",", ":")) + "\n").encode("utf-8")
    conn.sendall(payload)
    print(f"TX command: {payload.decode().rstrip()}")


def receiver(conn: socket.socket, stop: threading.Event) -> None:
    buffer = bytearray()

    while not stop.is_set():
        try:
            data = conn.recv(4096)
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
                print(f"RX event: {json.dumps(obj, separators=(',', ':'))}")
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                print(f"RX invalid JSON: {line!r} ({exc})")

    stop.set()
    print("Reader disconnected.")


def main() -> None:
    stop = threading.Event()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, PORT))
        server.listen(1)
        print(f"Listening on {HOST}:{PORT}")
        print("The F429 test firmware defaults to 192.168.150.100:9000.")

        while True:
            conn, addr = server.accept()
            print(f"Reader connected from {addr[0]}:{addr[1]}")
            stop.clear()

            with conn:
                thread = threading.Thread(target=receiver, args=(conn, stop), daemon=True)
                thread.start()

                while not stop.is_set():
                    try:
                        command = input("Command [resume/pause/gpo/reboot/quit]: ").strip().lower()
                    except EOFError:
                        command = "quit"

                    if command == "resume":
                        send_command(conn, "resume_inventory", {})
                    elif command == "pause":
                        send_command(conn, "pause_inventory", {})
                    elif command == "gpo":
                        send_command(
                            conn,
                            "set_gpo",
                            [{"gpo": 1, "state": 1, "duration": 1}],
                        )
                    elif command == "reboot":
                        send_command(conn, "reboot", {})
                    elif command == "quit":
                        stop.set()
                        break
                    elif command:
                        print("Unknown command.")

            if command == "quit":
                break


if __name__ == "__main__":
    main()
