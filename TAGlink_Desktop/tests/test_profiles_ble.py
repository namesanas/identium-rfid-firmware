"""Protocol/session and Bluetooth callback tests; no radio or serial port opens."""
import os
import sys
import unittest
import threading
from unittest.mock import Mock, patch
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from PySide6.QtCore import QCoreApplication
from reader_profiles import LineFramer, command_bytes
from handheld_session import HandheldSession
from ble_transport import BleTransport
from serial_manager import SerialManager, WIFI_OPTION

APP = QCoreApplication.instance() or QCoreApplication([])
VERSION = "VERSION,FWVER=0.7.1,HWVER=ESP32,PROFILE=HANDHELD,PROTO=TAGLINK_TEXT_1"
CAPS = "CAPABILITIES,PROFILE=HANDHELD,INVENTORY=1,START=1,STOP=1,SINGLE_POLL=1,READ_SETTINGS=1,SET_POWER=1,SET_SESSION=1,SET_GEN2=1,SET_REGION=1,SCAN_MODE=1,TRIGGER=1,PERSIST=1,WIFI=1,BLE=1,SERVER=1,MIN_POWER=500,MAX_POWER=2600,POWER_STEP=50,MAX_POLL_MS=3000,SUPPORTED_REGIONS=03,RF_COUNTRY=IN"


class FakeTransport:
    def __init__(self):
        self.sent = []
        self.disconnects = 0

    def send(self, text):
        self.sent.append(text)

    def disconnect(self):
        self.disconnects += 1


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.transport = FakeTransport()
        self.session = HandheldSession()
        self.session.timer.stop()
        self.session.bind(self.transport)
        self.messages = []
        self.session.message_signal.connect(self.messages.append)

    def ready(self, capabilities=CAPS):
        self.session.on_state(True, "test")
        self.session.on_line(VERSION)
        self.session.on_line(capabilities)
        self.session.queue.clear()
        self.session.pending = None
        return self.session

    def test_wrong_identity_never_sends_settings(self):
        self.session.on_state(True)
        self.session.on_line("VERSION,MODEL=IF9D,FWVER=2")
        self.assertEqual(self.transport.sent, ["GET_VERSION"])
        self.assertEqual(self.transport.disconnects, 1)
        self.assertFalse(self.session.ready)

    def test_capabilities_must_be_handheld_and_sane(self):
        for caps in ("CAPABILITIES,PROFILE=FIXED", CAPS.replace("POWER_STEP=50", "POWER_STEP=0"), CAPS.replace("MAX_POLL_MS=3000", "MAX_POLL_MS=bogus")):
            with self.subTest(caps=caps):
                self.session.on_state(True)
                self.session.on_line(VERSION)
                self.session.on_line(caps)
                self.assertFalse(self.session.ready)
        self.assertEqual(self.transport.disconnects, 3)

    def test_capabilities_remove_rf_reads(self):
        self.ready(CAPS.replace("READ_SETTINGS=1", "READ_SETTINGS=0"))
        for command in ("GET_POWER", "GET_REGION", "GET_SESSION", "GET_GEN2"):
            self.assertFalse(self.session.supported(command))
        self.assertNotIn("GET_POWER", self.transport.sent)

    def test_fixed_reader_commands_are_rejected_before_transport(self):
        self.ready()
        sent = len(self.transport.sent)
        for command in ("WRITE_TAG,1", "READ_TAG", "SYNC_POLL", "SET_TRIGGER,7", "SET_WORK_MODE,ACTIVE", "VSWR_SCAN"):
            self.assertFalse(self.session.request(command))
        self.assertEqual(len(self.transport.sent), sent)

    def test_stop_cancels_start_and_stale_reply_does_not_complete_stop(self):
        self.ready()
        self.assertTrue(self.session.start())
        self.assertFalse(self.session.start())
        self.session.stop()
        self.assertEqual(self.transport.sent[-2:], ["START", "STOP"])
        self.session.on_line("OK,START")
        self.assertEqual(self.session.pending[0], "STOP")
        self.assertFalse(self.session.inventory)
        self.session.on_line("OK,STOP")
        self.assertIsNone(self.session.pending)
        self.assertFalse(self.session.stopping)

    def test_disconnect_stops_then_releases_without_old_commands(self):
        self.ready()
        self.session.request("GET_POWER", "GET_REGION")
        self.session.disconnect()
        self.session.on_line("POWER,ANT=1,READ=2000")
        self.assertEqual(self.session.pending[0], "STOP")
        self.session.on_line("OK,STOP")
        self.session.on_line("OK,RELEASE")
        self.assertEqual(self.transport.sent[-2:], ["STOP", "RELEASE"])
        self.assertEqual(self.transport.disconnects, 1)
        self.assertFalse(self.session.queue)

    def test_timeout_closes_ambiguous_stream(self):
        self.ready()
        self.session.clock = lambda: 100
        self.session.request("START")
        self.session.clock = lambda: 109
        self.session.tick()
        self.assertEqual(self.transport.disconnects, 1)

    def test_password_is_not_logged(self):
        self.ready()
        self.session.request("SET_WIFI,1,STA,Local,secret-value")
        self.assertFalse(any("secret-value" in message for message in self.messages))
        self.assertEqual(self.transport.sent[-1], "SET_WIFI,1,STA,Local,secret-value")

    def test_http_commands_require_capability(self):
        self.ready()
        self.assertFalse(self.session.request("GET_HTTP"))
        self.assertFalse(self.session.request("SET_HTTP,http://192.0.2.10/tag,10,0"))
        self.ready(CAPS + ",HTTP=1")
        self.assertTrue(self.session.request("SET_HTTP,http://192.0.2.10/tag,10,0", "GET_HTTP"))
        self.session.on_line("OK,SET_HTTP")
        self.assertEqual(self.transport.sent[-1], "GET_HTTP")
        self.session.on_line("HTTP,CONFIGURED=1,TIMEOUT=10")
        self.assertIsNone(self.session.pending)
        self.assertFalse(any("192.0.2.10" in message for message in self.messages))

    def test_input_queue_and_line_bounds(self):
        self.ready()
        self.assertFalse(self.session.request("SET_NAME,evil\r\nSTART"))
        self.assertFalse(self.session.request("SET_NAME," + "A" * 256))
        self.assertFalse(self.session.request(*(["GET_STATUS"] * 49)))
        self.assertFalse(self.session.start(3001))
        with self.assertRaises(ValueError):
            command_bytes("START\nSTOP")


class FakeController:
    def __init__(self, mtu):
        self.size = mtu
        self.closed = False

    def mtu(self):
        return self.size

    def disconnectFromDevice(self):
        self.closed = True

    def deleteLater(self):
        pass

    def remoteName(self):
        return "Test MUBR"


class FakeService:
    def __init__(self):
        self.writes = []

    def writeCharacteristic(self, characteristic, data, mode):
        self.writes.append(bytes(data))

    def deleteLater(self):
        pass


class BleTests(unittest.TestCase):
    def setUp(self):
        self.ble = BleTransport(diagnostics=Mock(path=None, warning=None))
        self.ble.ready = True
        self.ble.controller = FakeController(23)
        self.ble.service = FakeService()
        self.ble.rx = object()
        self.ble.tx = object()

    def tearDown(self):
        self.ble.disconnect()

    def test_every_notification_split_and_multiple_lines(self):
        raw = b"TAG,EPC=DECAF0000001,RSSI=-54,ANT=1,FREQ=0,TIME=17\r\nOK,STOP\r\n"
        for split in range(len(raw) + 1):
            self.ble.framer.clear()
            lines = []
            self.ble.line_signal.connect(lines.append)
            self.ble._notification(self.ble.tx, raw[:split], self.ble.generation)
            self.ble._notification(self.ble.tx, raw[split:], self.ble.generation)
            self.assertEqual(lines, raw.decode().splitlines())
            self.ble.line_signal.disconnect(lines.append)

    def test_serialized_mtu23_writes_and_stale_completion(self):
        command = "SET_WIFI,1,STA,Local," + "p" * 63
        self.ble.send(command)
        self.assertEqual(len(self.ble.service.writes), 1)
        self.assertEqual(len(self.ble.service.writes[0]), 20)
        first = self.ble.in_flight
        self.ble._written(self.ble.rx, first, self.ble.generation - 1)
        self.assertEqual(self.ble.offset, 0)
        service = self.ble.service
        while self.ble.in_flight is not None:
            self.ble._written(self.ble.rx, self.ble.in_flight, self.ble.generation)
        self.assertEqual(b"".join(service.writes), command.encode() + b"\r\n")
        self.assertTrue(all(len(part) <= 20 for part in service.writes))
        self.assertFalse(self.ble.queue)
        self.ble._written(self.ble.rx, first, self.ble.generation)
        self.assertEqual(self.ble.offset, 0)

    def test_negotiated_mtu_and_safe_fallback(self):
        for mtu, maximum in ((247, 244), (-1, 20), (23, 20)):
            self.ble.controller.size = mtu
            self.ble.send("SET_NAME," + "A" * 31)
            while self.ble.in_flight is not None:
                self.ble._written(self.ble.rx, self.ble.in_flight, self.ble.generation)
            self.assertLessEqual(max(map(len, self.ble.service.writes)), maximum)
            self.ble.service.writes.clear()

    def test_partial_line_and_tx_clear_on_disconnect(self):
        self.ble.framer.feed(b"TAG,EPC=old")
        self.ble.send("GET_STATUS")
        generation = self.ble.generation
        old_rx = self.ble.rx
        self.ble.disconnect()
        self.assertFalse(self.ble.framer.buffer)
        self.assertFalse(self.ble.queue)
        self.assertIsNone(self.ble.in_flight)
        self.ble._written(old_rx, b"GET_STATUS\r\n", generation)
        self.assertFalse(self.ble.queue)

    def test_oversized_or_non_ascii_notification_closes(self):
        errors = []
        self.ble.error_signal.connect(errors.append)
        self.ble._notification(self.ble.tx, b"A" * 513, self.ble.generation)
        self.assertFalse(self.ble.ready)
        self.assertTrue(errors)

    def test_line_framer_bounds(self):
        framer = LineFramer()
        with self.assertRaises(ValueError):
            framer.feed(b"X" * 513)
        self.assertEqual(framer.feed(b"STATUS,IDLE\r\n"), ["STATUS,IDLE"])

    def test_subscribe_before_ready_and_old_callback_ignored(self):
        self.ble.ready = False
        self.ble.cccd = object()
        states = []
        self.ble.state_signal.connect(lambda state, text: states.append(state))
        self.ble._subscribed(self.ble.cccd, b"\x01\x00", self.ble.generation - 1)
        self.assertFalse(self.ble.ready)
        self.ble._subscribed(self.ble.cccd, b"\x01\x00", self.ble.generation)
        self.assertTrue(self.ble.ready)
        self.assertEqual(states, [True])


class CancelledOpenTests(unittest.TestCase):
    def test_closing_during_tcp_open_closes_late_socket_without_connect_signal(self):
        entered = threading.Event()
        resume = threading.Event()

        class Socket:
            closed = False

            def shutdown(self, direction):
                pass

            def close(self):
                self.closed = True

        late_socket = Socket()

        def connect(address, timeout):
            entered.set()
            self.assertTrue(resume.wait(2))
            return late_socket

        with patch("serial_manager.socket.create_connection", connect):
            manager = SerialManager()
            states = []
            manager.state_signal.connect(lambda state, detail: states.append(state))
            manager.connect(WIFI_OPTION, 115200, "127.0.0.1")
            self.assertTrue(entered.wait(1))
            closer = threading.Thread(target=manager.close)
            closer.start()
            self.assertTrue(manager.stop.wait(1))
            resume.set()
            closer.join(2)
            manager.thread.join(1)
            APP.processEvents()
            self.assertFalse(manager.thread.is_alive())
            self.assertTrue(late_socket.closed)
            self.assertNotIn(True, states)


if __name__ == "__main__":
    unittest.main(verbosity=2)
