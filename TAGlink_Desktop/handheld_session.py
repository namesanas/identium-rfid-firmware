"""Single-command MUBR session used by the handheld UI on every transport."""
from collections import deque
import time

from PySide6.QtCore import QObject, QTimer, Signal

from protocol import parse_line
from reader_profiles import (
    MUBR_INFO, MUBR_COMMANDS, MUBR_MUTATIONS, command_bytes,
    is_mubr_identity, response_matches,
)


class HandheldSession(QObject):
    ready_signal = Signal(bool)
    inventory_signal = Signal(bool)
    reply_signal = Signal(str, object)
    tag_signal = Signal(object)
    message_signal = Signal(str)
    failure_signal = Signal(str)
    disconnected_signal = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.transport = None
        self.connected = False
        self.verified = False
        self.ready = False
        self.inventory = False
        self.stopping = False
        self.closing = False
        self.pending = None
        self.queue = deque()
        self.capabilities = {}
        self.identity = {}
        self.last_poll = 0
        self.clock = time.monotonic
        self.timer = QTimer(self)
        self.timer.setInterval(100)
        self.timer.timeout.connect(self.tick)
        self.timer.start()

    def bind(self, transport):
        self.transport = transport

    def on_state(self, connected, detail=""):
        self.connected = connected
        self.verified = self.ready = False
        self.capabilities = {}
        self.identity = {}
        self.queue.clear()
        self.pending = None
        self.stopping = self.closing = False
        self._set_inventory(False)
        self.ready_signal.emit(False)
        if connected:
            self.message_signal.emit("Connected: " + detail + ". Checking MUBR identity…")
            self._send("GET_VERSION")
        else:
            self.disconnected_signal.emit()

    def _set_inventory(self, value):
        self.inventory = value
        self.inventory_signal.emit(value)

    def supported(self, command):
        base = command.partition(",")[0]
        flag = {
            "START": "START", "STOP": "STOP", "SINGLE_POLL": "SINGLE_POLL",
            "SET_POWER": "SET_POWER", "SET_REGION": "SET_REGION",
            "SET_SESSION": "SET_SESSION", "SET_GEN2": "SET_GEN2",
            "GET_SCAN_SETTINGS": "SCAN_MODE", "SET_SCAN_MODE": "SCAN_MODE",
            "SET_SCAN_TIME": "SCAN_MODE", "SET_DELIVERY": "TRIGGER",
            "GET_WIFI": "WIFI", "SET_WIFI": "WIFI", "GET_BLE": "BLE", "SET_BLE": "BLE",
            "GET_SERVER": "SERVER", "SET_SERVER": "SERVER", "GET_NAME": "PERSIST", "SET_NAME": "PERSIST",
            "GET_POWER": "READ_SETTINGS", "GET_REGION": "READ_SETTINGS",
            "GET_SESSION": "READ_SETTINGS", "GET_GEN2": "READ_SETTINGS",
            "GET_HTTP": "HTTP", "GET_WORK_MODE": "HTTP", "SET_WORK_MODE": "HTTP", "SET_HTTP": "HTTP",
        }.get(base)
        return base in MUBR_COMMANDS and (flag is None or self.capabilities.get(flag) == "1")

    def request(self, *commands):
        if not self.ready or self.closing:
            self.message_signal.emit("Connect to a verified MUBR reader first.")
            return False
        try:
            for command in commands:
                command_bytes(command)
                if not self.supported(command):
                    raise ValueError("This MUBR does not advertise support for " + command.partition(",")[0] + ".")
                if self.inventory and (command.partition(",")[0] in MUBR_MUTATIONS or command in ("GET_POWER", "GET_REGION", "GET_SESSION")):
                    raise ValueError("Stop inventory before changing or reading RF settings.")
            if len(self.queue) + len(commands) > 48:
                raise ValueError("Wait for the current reader commands to finish.")
        except (ValueError, UnicodeEncodeError) as error:
            self.message_signal.emit(str(error))
            return False
        self.queue.extend(commands)
        self._pump()
        return True

    def refresh(self):
        if not self.ready or self.inventory or self.pending or self.queue:
            self.message_signal.emit("Stop inventory and wait for the current command before refreshing.")
            return
        self.request(*(command for command in MUBR_INFO if self.supported(command)))

    def start(self, single_ms=None):
        if self.inventory or self.stopping or any(command.partition(",")[0] in ("START", "SINGLE_POLL") for command in self.queue) or (self.pending and self.pending[0].partition(",")[0] in ("START", "SINGLE_POLL")):
            return False
        if single_ms is None:
            return self.request("START")
        maximum = min(3000, int(self.capabilities.get("MAX_POLL_MS", "3000")))
        if not 1 <= single_ms <= maximum:
            self.message_signal.emit(f"Single poll must be between 1 and {maximum} ms.")
            return False
        return self.request(f"SINGLE_POLL,{single_ms}")

    def stop(self):
        if not self.ready or self.closing or self.stopping:
            return
        # STOP may cancel the asynchronous START/SINGLE_POLL startup before its
        # reply. A late reply for that cancelled command must not consume STOP.
        self.queue.clear()
        self.pending = None
        self.stopping = True
        self._send("STOP")

    def disconnect(self):
        if self.transport is None:
            return
        if not self.connected:
            self.transport.disconnect()  # also cancels a pending BLE pairing
            return
        if self.closing:
            return
        self.closing = True
        self.ready = False
        self.ready_signal.emit(False)
        self.queue.clear()
        self.pending = None
        self._send("STOP" if self.verified else "RELEASE")

    def _send(self, command):
        self.pending = (command, self.clock())
        # Credentials never enter the app's traffic log.
        shown = command
        if command.startswith("SET_WIFI,"):
            shown = "SET_WIFI,…,[password hidden]"
        elif command.startswith("SET_HTTP,"):
            shown = "SET_HTTP,[endpoint hidden]"
        self.message_signal.emit("> " + shown)
        self.transport.send(command)

    def _pump(self):
        if self.connected and self.pending is None and self.queue:
            self._send(self.queue.popleft())

    def on_line(self, line):
        if not self.connected:
            return
        kind, data = parse_line(line)
        if kind == "TAG":
            if self.ready and not self.closing:
                self.tag_signal.emit(data)
            return
        self.message_signal.emit("< " + line)
        command = self.pending[0] if self.pending else ""
        matched = bool(command) and response_matches(command, kind, data)
        if self.closing:
            if matched:
                self.pending = None
                if command == "STOP":
                    self._send("RELEASE")
                else:
                    self.transport.disconnect()
            return
        if not self.verified:
            if matched and kind == "VERSION" and is_mubr_identity(data):
                self.pending = None
                self.verified = True
                self.identity = data
                self.reply_signal.emit(kind, data)
                self._send("GET_CAPABILITIES")
            elif matched:
                self._reject("The connected reader is not MUBR. Choose IF9D for the wall reader.")
            return
        if not self.ready:
            if matched and kind == "CAPABILITIES" and data.get("PROFILE") == "HANDHELD":
                try:
                    minimum = int(data.get("MIN_POWER", "500"))
                    maximum = int(data.get("MAX_POWER", "2600"))
                    step = int(data.get("POWER_STEP", "50"))
                    poll_ms = int(data.get("MAX_POLL_MS", "3000"))
                    if not 500 <= minimum <= maximum <= 2600 or not 1 <= step <= 2600 or not 1 <= poll_ms <= 3000:
                        raise ValueError
                except ValueError:
                    self._reject("MUBR advertised invalid RF limits; reconnect after checking the reader firmware.")
                    return
                self.pending = None
                self.capabilities = data
                self.ready = True
                self.reply_signal.emit(kind, data)
                self.ready_signal.emit(True)
                self.last_poll = self.clock()
                self.refresh()
            elif matched:
                self._reject("MUBR capability discovery failed; no scan or setting was changed.")
            return
        if not matched:
            return  # stale acknowledgements never advance this session
        self.pending = None
        if kind == "ERROR":
            self.message_signal.emit("Reader rejected " + command.partition(",")[0] + ". Refresh to read its current settings.")
            self.queue.clear()
            if command == "STOP":
                self.stopping = False
            self.reply_signal.emit(kind, data)
            self._send("GET_STATUS") if command != "GET_STATUS" else None
            return
        if kind == "OK":
            if data == "START" and not self.stopping:
                self._set_inventory(True)
            elif data in ("STOP", "SINGLE_POLL", "RELEASE"):
                self.stopping = False
                self._set_inventory(False)
        elif kind == "STATUS":
            self._set_inventory(bool(data.get("_csv")) and data["_csv"][0] == "INVENTORY")
            if self.inventory:
                self.queue = deque(c for c in self.queue if c not in ("GET_POWER", "GET_REGION", "GET_SESSION"))
        self.reply_signal.emit(kind, data)
        self._pump()

    def _reject(self, message):
        self.message_signal.emit(message)
        self.failure_signal.emit(message)
        self.queue.clear()
        self.pending = None
        self.ready = False
        self.ready_signal.emit(False)
        self.transport.disconnect()

    def tick(self):
        if self.pending:
            command, started = self.pending
            timeout = 3 if self.closing else 8
            if command.startswith("SINGLE_POLL,"):
                timeout += int(command.partition(",")[2]) / 1000
            if self.clock() - started > timeout:
                message = "Timed out waiting for " + command.partition(",")[0] + "; reconnect to recover the command stream."
                self.message_signal.emit(message)
                self.failure_signal.emit(message)
                self.transport.disconnect()
                return
        if self.ready and not self.closing and self.pending is None and not self.queue and self.clock() - self.last_poll >= 10:
            self.last_poll = self.clock()
            self.request("GET_STATUS")
