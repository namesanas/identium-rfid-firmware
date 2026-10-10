#!/usr/bin/env python3
"""Integration tests for tools/reader_simulator.py; standard library only."""
import socket
import subprocess
import sys
import time
import unittest
from pathlib import Path

SIMULATOR = Path(__file__).with_name("reader_simulator.py")

class LineClient:
    def __init__(self, sock):
        self.sock = sock
        self.buffer = b""

    def send(self, command):
        self.sock.sendall((command + "\r\n").encode("ascii"))

    def line(self, timeout=2.0):
        self.sock.settimeout(timeout)
        while b"\n" not in self.buffer:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise EOFError("simulator closed the connection")
            self.buffer += chunk
        raw, self.buffer = self.buffer.split(b"\n", 1)
        return raw.rstrip(b"\r").decode("ascii")


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]

class SimulatorIntegrationTests(unittest.TestCase):
    def start_simulator(self, *args):
        port = free_port()
        proc = subprocess.Popen([sys.executable, str(SIMULATOR), "--host", "127.0.0.1", "--port", str(port), *args], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            try:
                sock = socket.create_connection(("127.0.0.1", port), timeout=0.15)
                return proc, port, sock
            except OSError:
                if proc.poll() is not None:
                    raise AssertionError("Simulator exited before listening")
                time.sleep(0.04)
        proc.terminate()
        raise AssertionError("Simulator did not listen within four seconds")

    def stop_simulator(self, proc, sock=None):
        if sock:
            try: sock.close()
            except OSError: pass
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill(); proc.wait(timeout=2)

    def test_documented_commands_and_inventory_stream(self):
        proc, port, sock = self.start_simulator()
        try:
            client = LineClient(sock)
            client.send("GET_STATUS")
            self.assertEqual(client.line(), "STATUS,IDLE")
            client.send("GET_VERSION")
            self.assertTrue(client.line().startswith("VERSION,"))
            client.send("GET_POWER")
            self.assertEqual(client.line(), "POWER,ANT=1,READ=2000,WRITE=2000")
            client.send("GET_WIFI")
            self.assertTrue(client.line().startswith("WIFI,LINK=ONLINE,"))
            client.send("NOT_A_COMMAND")
            self.assertEqual(client.line(), "ERROR,INVALID_COMMAND")
            client.send("START")
            self.assertEqual(client.line(), "OK,START")
            # Inventory tags are unsolicited and may precede the STOP reply.
            client.send("STOP")
            seen_stop = False
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                line = client.line(timeout=max(0.1, deadline-time.monotonic()))
                if line == "OK,STOP":
                    seen_stop = True
                    break
                self.assertTrue(line.startswith("TAG,"))
            self.assertTrue(seen_stop)
            client.send("RELEASE")
            self.assertEqual(client.line(), "OK,RELEASE")
        finally:
            self.stop_simulator(proc, sock)


    def test_every_documented_command_is_implemented(self):
        proc, port, sock = self.start_simulator()
        try:
            client = LineClient(sock)
            expected = {
                "GET_STATUS": "STATUS,",
                "GET_VERSION": "VERSION,",
                "GET_SERIAL": "SERIAL,",
                "GET_TEMPERATURE": "TEMP,",
                "GET_POWER": "POWER,",
                "SET_POWER,2050": "OK,SET_POWER",
                "GET_REGION": "REGION,",
                "GET_ANTENNA": "ANTENNA,",
                "GET_SESSION": "SESSION,",
                "SET_SESSION,2": "OK,SET_SESSION",
                "READ_TAG,E2004703CA4068230FC80111,1,0,2": "READ_DATA,",
                "WRITE_TAG,E2004703CA4068230FC80111,3,0,00000000,11223344": "OK,WRITE_TAG",
                "GET_GPI": "GPI,",
                "SET_GPO,1,1,0": "OK,SET_GPO",
                "GET_CAPABILITIES": "CAPABILITIES,",
                "GET_WIFI": "WIFI,",
                "SET_WIFI,1,STA,TestNetwork,password123": "OK,SET_WIFI",
                "GET_DIAG": "DIAG,",
            }
            for command, prefix in expected.items():
                client.send(command)
                reply = client.line()
                self.assertTrue(reply.startswith(prefix), f"{command}: unexpected {reply!r}")
            client.send("SINGLE_POLL,60")
            poll_lines = []
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                line = client.line(timeout=max(0.1, deadline-time.monotonic()))
                poll_lines.append(line)
                if line == "OK,SINGLE_POLL":
                    break
            self.assertIn("OK,SINGLE_POLL", poll_lines)
            self.assertTrue(any(line.startswith("TAG,") for line in poll_lines))
            client.send("WRITE_EPC,E2004703CA4068230FC80111,E2004703CA4068230FC80999")
            self.assertEqual(client.line(), "OK,WRITE_EPC")
            client.send("START")
            self.assertEqual(client.line(), "OK,START")
            client.send("STOP")
            deadline = time.monotonic() + 2
            stop_reply = None
            while time.monotonic() < deadline:
                line = client.line(timeout=max(0.1, deadline-time.monotonic()))
                if line == "OK,STOP":
                    stop_reply = line
                    break
            self.assertEqual(stop_reply, "OK,STOP")
            client.send("RELEASE")
            self.assertEqual(client.line(), "OK,RELEASE")
        finally:
            self.stop_simulator(proc, sock)

    def test_set_wifi_password_may_contain_commas(self):
        proc, port, sock = self.start_simulator()
        try:
            client = LineClient(sock)
            client.send("SET_WIFI,1,STA,TestNetwork,password,with,commas")
            self.assertEqual(client.line(), "OK,SET_WIFI")
        finally:
            self.stop_simulator(proc, sock)

    def test_busy_mode_sends_no_reply(self):
        proc, port, sock = self.start_simulator("--busy")
        try:
            client = LineClient(sock)
            sock.settimeout(0.3)
            client.send("GET_STATUS")
            with self.assertRaises(socket.timeout):
                client.line(timeout=0.3)
        finally:
            self.stop_simulator(proc, sock)

    def test_drop_after_one_command_closes_connection(self):
        proc, port, sock = self.start_simulator("--drop-after", "1")
        try:
            client = LineClient(sock)
            client.send("GET_VERSION")
            self.assertTrue(client.line().startswith("VERSION,"))
            sock.settimeout(1)
            self.assertEqual(sock.recv(1), b"")
        finally:
            self.stop_simulator(proc, sock)

    def test_latency_flag_delays_reply(self):
        proc, port, sock = self.start_simulator("--latency", "120")
        try:
            client = LineClient(sock)
            started = time.monotonic()
            client.send("GET_STATUS")
            self.assertEqual(client.line(), "STATUS,IDLE")
            self.assertGreaterEqual(time.monotonic() - started, 0.10)
        finally:
            self.stop_simulator(proc, sock)

if __name__ == "__main__":
    unittest.main(verbosity=2)
