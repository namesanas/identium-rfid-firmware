#!/usr/bin/env python3
"""
Sends a signed firmware image (.tlfw, from Tools/fw_sign.py or
`make image`) to a reader and installs it.

  python Tools/fw_update.py --port COM3 build/release/rfid_controller-v0.11.0.tlfw
  python Tools/fw_update.py --tcp 192.168.1.50 image.tlfw
  python Tools/fw_update.py --port COM3 --status

Steps: FW_BEGIN, FW_DATA in 128-byte chunks, FW_END (the reader checks
the SHA-256), FW_APPLY (the reader resets; the bootloader checks the
signature and copies the image), then FW_STATUS until the bootloader's
verdict is in. Works over the host serial port, USB CDC or TCP 5000.

Over USB CDC the COM port disappears during the reset; the tool waits
for it to come back. The text link stays claimed by this tool until it
sends RELEASE at the end.

Requires pyserial for --port:  python -m pip install pyserial
"""

import argparse
import os
import socket
import struct
import sys
import time

CHUNK = 128
REPLY_TIMEOUT_S = 10.0      # a 128 KB flash sector erase takes up to ~2 s
RETRIES = 3
INSTALL_TIMEOUT_S = 90.0


class Link:
    def __init__(self, args):
        self.args = args
        self.buffer = b""
        self.conn = None
        self.open()

    def open(self):
        if self.args.tcp:
            self.conn = socket.create_connection((self.args.tcp, self.args.tcp_port), timeout=5)
            self.conn.settimeout(0.1)
        else:
            import serial
            self.conn = serial.Serial(self.args.port, self.args.baud, timeout=0.1)
        self.buffer = b""

    def close(self):
        try:
            self.conn.close()
        except OSError:
            pass

    def write(self, text):
        data = (text + "\r").encode("ascii")
        if self.args.tcp:
            self.conn.sendall(data)
        else:
            self.conn.write(data)

    def read_some(self):
        if self.args.tcp:
            try:
                data = self.conn.recv(4096)
            except socket.timeout:
                return b""
            if not data:
                raise ConnectionError("connection closed")
            return data
        # read(n) waits for n bytes or the port timeout, so ask only for what
        # has already arrived (at least one byte). Reading a fixed 4096 here
        # would spend the full timeout on every chunk and make the transfer
        # several times slower than the link.
        return self.conn.read(max(1, self.conn.in_waiting))

    def reply(self, prefixes, timeout_s=REPLY_TIMEOUT_S):
        """Next line starting with one of prefixes; other lines (tags,
        heartbeats) are skipped. None on timeout."""
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                line, self.buffer = self.buffer.split(b"\n", 1)
                text = line.decode("ascii", "replace").strip()
                if text.startswith(prefixes):
                    return text
            self.buffer += self.read_some()
        return None

    def command(self, text, prefixes, timeout_s=REPLY_TIMEOUT_S):
        self.write(text)
        return self.reply(prefixes, timeout_s)


def status(link):
    return link.command("FW_STATUS", ("FW,",))


def parse_status(line):
    fields = {}
    for part in line.split(",")[1:]:
        if "=" in part:
            k, v = part.split("=", 1)
            fields[k] = v
    return fields


def send_image(link, image):
    reply = link.command(f"FW_BEGIN,{len(image)}", ("OK,FW_BEGIN", "ERROR,FW_BEGIN"))
    if reply is None or not reply.startswith("OK"):
        raise SystemExit(f"FW_BEGIN failed: {reply}")

    offset = 0
    started = time.monotonic()
    last_shown = -1
    while offset < len(image):
        chunk = image[offset:offset + CHUNK]
        for _attempt in range(RETRIES):
            reply = link.command(f"FW_DATA,{offset},{chunk.hex().upper()}",
                                 ("OK,FW_DATA", "ERROR,FW_DATA"))
            if reply is not None:
                break
        if reply is None:
            raise SystemExit(f"no reply to FW_DATA at offset {offset}")

        parts = reply.split(",")
        if reply.startswith("OK"):
            offset = int(parts[2])
        elif parts[2] == "OFFSET":
            offset = int(parts[3])          # resume where the reader is
        else:
            raise SystemExit(f"FW_DATA failed: {reply}")

        percent = offset * 100 // len(image)
        if percent // 10 != last_shown:
            last_shown = percent // 10
            print(f"  {percent:3d}%  {offset}/{len(image)} bytes")

    print(f"  sent in {time.monotonic() - started:.1f} s")

    reply = link.command("FW_END", ("OK,FW_END", "ERROR,FW_END"), timeout_s=30)
    if reply is None or not reply.startswith("OK"):
        raise SystemExit(f"FW_END failed: {reply}")
    print(f"reader accepted {reply.split(',', 2)[2]} (SHA-256 ok)")


def wait_for_install(link):
    """After FW_APPLY: reconnect and poll FW_STATUS until the bootloader
    has recorded a verdict."""
    link.close()
    deadline = time.monotonic() + INSTALL_TIMEOUT_S
    time.sleep(2.0)
    while time.monotonic() < deadline:
        try:
            link.open()
            line = link.command("FW_STATUS", ("FW,",), timeout_s=3)
            if line:
                fields = parse_status(line)
                if fields.get("LAST") != "PENDING":
                    return line
            link.close()
        except OSError:
            pass        # port or socket not back yet (SerialException is an OSError)
        time.sleep(1.0)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--port", help="serial or USB CDC port, e.g. COM3")
    target.add_argument("--tcp", help="reader IP address")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--tcp-port", type=int, default=5000)
    parser.add_argument("--status", action="store_true", help="only print FW_STATUS")
    parser.add_argument("--no-apply", action="store_true",
                        help="stop after FW_END; nothing is installed")
    parser.add_argument("image", nargs="?")
    args = parser.parse_args()

    link = Link(args)
    try:
        line = status(link)
        if line is None:
            raise SystemExit("no reply to FW_STATUS (firmware without update support?)")
        print(line)
        if args.status:
            return 0
        if not args.image:
            raise SystemExit("no image given")
        if parse_status(line).get("BOOT") != "1":
            raise SystemExit("the reader has no bootloader: flash it once with `make flash`")

        with open(args.image, "rb") as f:
            image = f.read()
        if len(image) < 256 or struct.unpack_from("<I", image, 0)[0] != 0x57464C54:
            raise SystemExit(f"{args.image} is not a signed image (.tlfw)")
        version = image[0x20:0x40].split(b"\0", 1)[0].decode("ascii", "replace")
        print(f"sending {os.path.basename(args.image)}: {version}, {len(image)} bytes")

        send_image(link, image)

        if args.no_apply:
            link.command("RELEASE", ("OK,RELEASE",), timeout_s=2)
            print("not applied (--no-apply)")
            return 0

        reply = link.command("FW_APPLY", ("OK,FW_APPLY", "ERROR,FW_APPLY"))
        if reply is None or not reply.startswith("OK"):
            raise SystemExit(f"FW_APPLY failed: {reply}")
        print("reader is restarting into the bootloader...")

        line = wait_for_install(link)
        if line is None:
            raise SystemExit("the reader did not come back within "
                             f"{INSTALL_TIMEOUT_S:.0f} s")
        print(line)
        fields = parse_status(line)
        link.command("RELEASE", ("OK,RELEASE",), timeout_s=2)
        if fields.get("LAST") == "INSTALLED":
            print(f"installed {fields.get('VERSION', '')}")
            return 0
        print(f"NOT installed: the bootloader reported {fields.get('LAST')}")
        return 1
    finally:
        link.close()


if __name__ == "__main__":
    sys.exit(main())
