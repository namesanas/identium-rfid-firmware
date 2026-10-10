#!/usr/bin/env python3
"""Standard-library-only TCP simulator for the TAGlink RFID reader protocol."""
from __future__ import annotations

import argparse
import random
import re
import socket
import threading
import time
from typing import Optional

HOST = "0.0.0.0"
PORT = 5000

EPCS = [
    "E2004703CA4068230FC80111", "E2004703CA4068230FC80112",
    "E2004703CA4068230FC80113", "E2004703CA4068230FC80114",
    "E2004703CA4068230FC80115", "E2004703CA4068230FC80116",
    "E2004703CA4068230FC80117", "E2004703CA4068230FC80118",
    "E2004703CA4068230FC80119", "E2004703CA4068230FC8011A",
    "E2004703CA4068230FC8011B", "E2004703CA4068230FC8011C",
    "E2004703CA4068230FC8011D", "E2004703CA4068230FC8011E",
    "E2004703CA4068230FC8011F", "E2004703CA4068230FC80120",
    "E2004703CA4068230FC80121", "E2004703CA4068230FC80122",
    "E2004703CA4068230FC80123", "E2004703CA4068230FC80124",
]

class ReaderSession:
    def __init__(self, conn: socket.socket, address: tuple[str, int], args: argparse.Namespace):
        self.conn = conn
        self.address = address
        self.args = args
        self.send_lock = threading.Lock()
        self.running = threading.Event()
        self.running.set()
        self.inventory = threading.Event()
        self.command_count = 0
        self.started_at = time.monotonic()
        self.reads = 0
        self.power = 2000
        self.session = 0
        self.gpo = {1: 0, 2: 0, 3: 0, 4: 0}
        self.wifi = {
            "ENABLE": "1", "MODE": "STA", "SSID": "RFID-Network",
            "password": "", "IF": "AUTO", "LINK": "ONLINE", "STATE": "CONNECTED",
        }
        self.region = "04"
        self.serial = f"SN{random.randint(100000, 999999)}"
        self.drop_scheduled = False

    def send_line(self, line: str) -> None:
        payload = (line + "\r\n").encode("ascii", errors="replace")
        try:
            with self.send_lock:
                if self.running.is_set():
                    self.conn.sendall(payload)
        except OSError:
            self.running.clear()
            self.inventory.clear()

    def tag_line(self) -> str:
        epc = random.choice(EPCS)
        rssi = random.randint(-68, -25)
        ant = random.randint(1, 4)
        freq = random.choice([902750, 915250, 927250])
        self.reads += 1
        return f"TAG,EPC={epc},RSSI={rssi},ANT={ant},FREQ={freq},TIME={int((time.monotonic()-self.started_at)*1000)}"

    def inventory_loop(self) -> None:
        while self.running.is_set() and self.inventory.is_set():
            self.send_line(self.tag_line())
            time.sleep(0.02)  # about 50 tag reports per second

    def _drop_connection(self) -> None:
        self.running.clear()
        self.inventory.clear()
        try:
            self.conn.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass

    def reply(self, line: str) -> None:
        if self.args.latency > 0:
            time.sleep(self.args.latency / 1000.0)
        self.send_line(line)
        if self.args.drop_after and self.command_count >= self.args.drop_after and not self.drop_scheduled:
            self.drop_scheduled = True
            threading.Timer(0.05, self._drop_connection).start()

    def handle(self, raw: str) -> None:
        raw = raw.strip("\r\n")
        if not raw:
            return
        self.command_count += 1
        if self.args.drop_after and self.command_count > self.args.drop_after:
            self._drop_connection()
            return
        if self.args.busy:
            if self.args.drop_after and self.command_count >= self.args.drop_after and not self.drop_scheduled:
                self.drop_scheduled = True
                threading.Timer(0.05, self._drop_connection).start()
            return

        # SET_WIFI password is allowed to contain commas; parse only the first four separators.
        if raw.startswith("SET_WIFI,"):
            parts = raw.split(",", 4)
            if len(parts) == 5:
                enabled, mode, ssid, password = parts[1:]
                if enabled not in ("0", "1") or mode not in ("STA", "AP") or "," in ssid or (password not in ("", "****") and not 8 <= len(password) <= 64):
                    self.reply("ERROR,SET_WIFI")
                else:
                    self.wifi["ENABLE"] = enabled
                    self.wifi["MODE"] = mode
                    self.wifi["SSID"] = ssid
                    if password != "****":
                        self.wifi["password"] = password
                    self.reply("OK,SET_WIFI")
                return

        parts = raw.split(",")
        cmd = parts[0]
        try:
            if raw == "GET_STATUS":
                self.reply("STATUS,INVENTORY" if self.inventory.is_set() else "STATUS,IDLE")
            elif raw == "GET_VERSION":
                self.reply("VERSION,BTVER=00000001,HWVER=00000002,FWDATE=20261001,FWVER=00000102,PROTO=00000001")
            elif raw == "GET_SERIAL":
                self.reply(f"SERIAL,YEAR=2026,SN={self.serial}")
            elif raw == "GET_TEMPERATURE":
                self.reply(f"TEMP,{random.randint(29, 43)}")
            elif raw == "START" and len(parts) == 1:
                self.inventory.set()
                self.reply("OK,START")
                threading.Thread(target=self.inventory_loop, daemon=True).start()
            elif raw == "STOP" and len(parts) == 1:
                self.inventory.clear()
                self.reply("OK,STOP")
            elif cmd == "SINGLE_POLL" and len(parts) == 2 and 0 <= int(parts[1]) <= 120000:
                duration_ms = int(parts[1])
                end = time.monotonic() + duration_ms / 1000.0
                while self.running.is_set() and time.monotonic() < end:
                    self.send_line(self.tag_line())
                    time.sleep(min(0.02, max(0.001, end - time.monotonic())))
                self.reply("OK,SINGLE_POLL")
            elif raw == "GET_POWER":
                self.reply(f"POWER,ANT=1,READ={self.power},WRITE={self.power}")
            elif cmd == "SET_POWER" and len(parts) == 2:
                power = int(parts[1])
                if not 500 <= power <= 3000 or power % 50:
                    self.reply("ERROR,SET_POWER")
                else:
                    self.power = power
                    self.reply("OK,SET_POWER")
            elif raw == "GET_REGION":
                self.reply(f"REGION,{self.region}")
            elif raw == "GET_ANTENNA":
                self.reply("ANTENNA,TX=1,RX=1")
            elif raw == "GET_SESSION":
                self.reply(f"SESSION,{self.session}")
            elif cmd == "SET_SESSION" and len(parts) == 2:
                value = int(parts[1])
                if value not in range(4):
                    self.reply("ERROR,SET_SESSION")
                else:
                    self.session = value
                    self.reply("OK,SET_SESSION")
            elif cmd == "READ_TAG" and len(parts) == 5:
                _, epc, bank_s, addr_s, words_s = parts
                bank, addr, words = int(bank_s), int(addr_s), int(words_s)
                if bank not in range(4) or words < 1 or words > 128 or addr < 0 or epc not in EPCS:
                    self.reply("ERROR,READ_TAG")
                else:
                    data = "".join(random.choice("0123456789ABCDEF") for _ in range(words * 4))
                    self.reply(f"READ_DATA,BANK={bank},ADDR={addr},WORDS={words},DATA={data}")
            elif cmd == "WRITE_TAG" and len(parts) == 6:
                _, epc, bank_s, addr_s, password, data = parts
                if epc not in EPCS or int(bank_s) not in range(4) or int(addr_s) < 0 or not re.fullmatch(r"[0-9A-Fa-f]{8}", password) or not data or len(data) % 4 or any(c not in "0123456789ABCDEFabcdef" for c in data):
                    self.reply("ERROR,WRITE_TAG")
                else:
                    self.reply("OK,WRITE_TAG")
            elif cmd == "WRITE_EPC" and len(parts) == 3:
                _, old_epc, new_epc = parts
                if old_epc not in EPCS or not new_epc or len(new_epc) % 4 or any(c not in "0123456789ABCDEFabcdef" for c in new_epc):
                    self.reply("ERROR,WRITE_EPC")
                else:
                    try:
                        EPCS[EPCS.index(old_epc)] = new_epc.upper()
                    except ValueError:
                        pass
                    self.reply("OK,WRITE_EPC")
            elif raw == "GET_GPI":
                self.reply(f"GPI,GPI1={random.randint(0,1)},GPI2=0,GPI3=0,GPI4=1")
            elif cmd == "SET_GPO" and len(parts) == 4:
                channel, value, duration = int(parts[1]), int(parts[2]), int(parts[3])
                if channel not in range(1, 5) or value not in (0, 1) or duration < 0:
                    self.reply("ERROR,SET_GPO")
                else:
                    self.gpo[channel] = value
                    self.reply("OK,SET_GPO")
                    if duration:
                        threading.Timer(duration / 1000.0, lambda: self.gpo.update({channel: 0})).start()
            elif raw == "GET_CAPABILITIES":
                self.reply("CAPABILITIES,GPI=4,GPO=4,ANT=4,BANKS=4")
            elif raw == "GET_WIFI":
                self.reply(f"WIFI,LINK={self.wifi['LINK']},STATE={self.wifi['STATE']},IP=192.168.4.1,RSSI=-48,HOST=1,UPLOAD=0,ENABLE={self.wifi['ENABLE']},MODE={self.wifi['MODE']},SSID={self.wifi['SSID']},IF={self.wifi['IF']},ERRORS=0,REJECTED=00/0")
            elif raw == "GET_DIAG":
                uptime = int((time.monotonic() - self.started_at) * 1000)
                self.reply(f"DIAG,RESET=POWER_ON,UPTIME_MS={uptime},COMMANDS={self.command_count},TAG_READS={self.reads},FREE_HEAP=65536,ERRORS=0")
            elif raw == "RELEASE":
                self.reply("OK,RELEASE")
            else:
                self.reply("ERROR,INVALID_COMMAND")
        except (ValueError, IndexError):
            self.reply(f"ERROR,{cmd}" if cmd else "ERROR,INVALID_COMMAND")

    def run(self) -> None:
        print(f"[+] Client connected: {self.address[0]}:{self.address[1]}")
        buffer = b""
        self.conn.settimeout(0.5)
        try:
            while self.running.is_set():
                try:
                    chunk = self.conn.recv(4096)
                    if not chunk:
                        break
                    buffer += chunk
                    while b"\n" in buffer:
                        line, buffer = buffer.split(b"\n", 1)
                        self.handle(line.decode("ascii", errors="replace"))
                except socket.timeout:
                    continue
                except (ConnectionResetError, BrokenPipeError, OSError):
                    break
        finally:
            self.running.clear()
            self.inventory.clear()
            try:
                self.conn.close()
            except OSError:
                pass
            print(f"[-] Client disconnected: {self.address[0]}:{self.address[1]}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=HOST, help="interface to bind (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=PORT, help="TCP port (default: 5000)")
    parser.add_argument("--busy", action="store_true", help="accept commands but never send replies")
    parser.add_argument("--drop-after", type=int, default=0, metavar="N", help="drop each client after N commands (0 disables)")
    parser.add_argument("--latency", type=int, default=0, metavar="MS", help="delay command replies by MS milliseconds")
    args = parser.parse_args()
    if args.drop_after < 0 or args.latency < 0 or not (1 <= args.port <= 65535):
        parser.error("port must be 1..65535 and latency/drop-after must be non-negative")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((args.host, args.port))
        server.listen(1)
        print(f"TAGlink reader simulator listening on {args.host}:{args.port}")
        print(f"Modes: busy={args.busy}, drop_after={args.drop_after or 'off'}, latency={args.latency}ms")
        while True:
            conn, address = server.accept()
            ReaderSession(conn, address, args).run()

if __name__ == "__main__":
    main()
