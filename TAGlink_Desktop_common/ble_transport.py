"""Qt Bluetooth LE central for MUBR; all callbacks run on the Qt GUI thread.

Uses the QtBluetooth module already supplied with PySide6. GATT writes use
responses and are serialized, including at MTU23. Pairing is delegated to the
OS; notification subscription and identity verification precede user commands.
"""
from collections import deque

from PySide6.QtCore import QObject, QTimer, Signal, QUuid, qVersion
from PySide6.QtBluetooth import (
    QBluetoothDeviceDiscoveryAgent, QBluetoothDeviceInfo, QBluetoothLocalDevice,
    QBluetoothUuid, QLowEnergyController, QLowEnergyService, QLowEnergyCharacteristic,
)

from reader_profiles import BLE_SERVICE, BLE_RX, BLE_TX, LineFramer, command_bytes
from ble_diagnostics import BleDiagnostics


class BleTransport(QObject):
    state_signal = Signal(bool, str)
    line_signal = Signal(str)
    error_signal = Signal(str)
    tx_signal = Signal(str)
    devices_signal = Signal(list)
    scan_signal = Signal(bool)
    progress_signal = Signal(str)

    def __init__(self, parent=None, diagnostics=None):
        super().__init__(parent)
        self.diagnostics = diagnostics if diagnostics is not None else BleDiagnostics()
        self.phase = "idle"
        self.agent = None
        self.devices = {}
        self.controller = None
        self.local = None
        self.service = None
        self.rx = None
        self.tx = None
        self.cccd = None
        self.ready = False
        self.generation = 0
        self.framer = LineFramer()
        self.queue = deque()
        self.offset = 0
        self.in_flight = None
        self.connect_timer = QTimer(self)
        self.connect_timer.setSingleShot(True)
        self.connect_timer.timeout.connect(self._connection_timeout)
        self.write_timer = QTimer(self)
        self.write_timer.setSingleShot(True)
        self.write_timer.timeout.connect(lambda: self._fail("Bluetooth write timed out; the connection was closed."))

    def _progress(self, phase, message):
        self.phase = phase
        self.diagnostics.record(f"BLE [{phase}] {message}")
        self.progress_signal.emit(message)

    def _connection_timeout(self):
        hint = ("Accept the Windows pairing prompt, or pair the reader in Windows Bluetooth settings and retry."
                if self.phase in ("pairing status", "pairing")
                else "Disconnect other Bluetooth clients and retry; see the app Log for the failed step.")
        self._fail("Bluetooth connection timed out. " + hint)

    def scan(self):
        if self.controller is not None or self.local is not None or self.connect_timer.isActive():
            self.error_signal.emit("Disconnect before scanning for another Bluetooth reader.")
            return
        if self.agent is None:
            self.agent = QBluetoothDeviceDiscoveryAgent(self)
            self.agent.setLowEnergyDiscoveryTimeout(6000)
            self.agent.deviceDiscovered.connect(self._found)
            self.agent.deviceUpdated.connect(lambda info, fields: self._found(info))
            self.agent.finished.connect(lambda: self.scan_signal.emit(False))
            self.agent.canceled.connect(lambda: self.scan_signal.emit(False))
            self.agent.errorOccurred.connect(lambda error: self._scan_error())
        if self.agent.isActive():
            return
        self.devices.clear()
        self.devices_signal.emit([])
        self.scan_signal.emit(True)
        self.agent.start(QBluetoothDeviceDiscoveryAgent.LowEnergyMethod)

    def _scan_error(self):
        self.scan_signal.emit(False)
        self.error_signal.emit("Bluetooth scan failed: " + self.agent.errorString())

    def _found(self, info):
        if not (info.coreConfigurations() & QBluetoothDeviceInfo.LowEnergyCoreConfiguration):
            return
        # The service UUID can appear only in a scan response. Keep named
        # TAGlink/MUBR advertisements too, then require the actual GATT service.
        name = info.name() or ""
        services = {uuid.toString().strip("{}").lower() for uuid in info.serviceUuids()}
        if BLE_SERVICE not in services and not name.upper().startswith(("TAGLINK", "MUBR")):
            return
        key = info.address().toString() or info.deviceUuid().toString()
        self.devices[key] = info
        self.devices_signal.emit([
            (key, f"{device.name() or 'MUBR'} · {key} · {device.rssi()} dBm")
            for key, device in sorted(self.devices.items())
        ])

    def connect(self, key):
        if key not in self.devices:
            self.error_signal.emit("Scan and select a Bluetooth reader first.")
            return
        info = self.devices[key]
        self.disconnect()
        if self.agent and self.agent.isActive():
            self.agent.stop()
        self.generation += 1
        generation = self.generation
        self.connect_timer.start(45000)
        self.diagnostics.record(f"BLE connection attempt: Qt {qVersion()}, reader {info.name()} {info.address().toString()}")
        if self.diagnostics.path is not None:
            self.progress_signal.emit("Bluetooth diagnostic log: " + str(self.diagnostics.path))
        if self.diagnostics.warning:
            self.progress_signal.emit(self.diagnostics.warning)
        self._progress("adapter", "Checking Bluetooth on this computer…")
        local = QBluetoothLocalDevice(self)
        # Qt's Windows constructor and pairingStatus() run nested event loops.
        # Cancel, timeout or a replacement connection may occur before return.
        if generation != self.generation:
            local.deleteLater()
            return
        self.local = local
        if not local.isValid():
            self._fail("No Bluetooth adapter is available. Turn Bluetooth on in Windows settings.")
            return
        self.diagnostics.record(f"BLE adapter: {local.name()}, state {local.hostMode()}")
        local.pairingFinished.connect(lambda address, state, g=generation:
            self._paired(info, state, g) if address == info.address() else None)
        local.errorOccurred.connect(lambda error, g=generation:
            self._fail("Bluetooth pairing failed: " + str(error) + ". Pair the reader in Windows Bluetooth settings and retry.")
            if g == self.generation else None)
        # Windows pairing is explicit, before accessing encrypted NUS attributes.
        self._progress("pairing status", "Checking whether the reader is paired…")
        pairing = local.pairingStatus(info.address())
        if generation != self.generation or self.local is not local:
            return
        self.diagnostics.record(f"BLE pairing status: {pairing}")
        if pairing == QBluetoothLocalDevice.Unpaired:
            self._progress("pairing", "Pairing the reader. Accept the Windows prompt if it appears.")
            local.requestPairing(info.address(), QBluetoothLocalDevice.Paired)
        else:
            self._create_controller(info, generation)

    def _paired(self, info, state, generation):
        if generation != self.generation:
            return
        if state == QBluetoothLocalDevice.Unpaired:
            self._fail("Bluetooth pairing was not accepted.")
        else:
            self._create_controller(info, generation)

    def _create_controller(self, info, generation):
        if generation != self.generation or self.controller is not None:
            return
        self._progress("connecting", "Connecting to the paired reader…")
        controller = QLowEnergyController.createCentral(info, self)
        if generation != self.generation:
            controller.disconnectFromDevice()
            controller.deleteLater()
            return
        self.controller = controller
        controller.connected.connect(lambda g=generation: self._connected(g))
        controller.disconnected.connect(lambda g=generation: self._lost(g))
        controller.errorOccurred.connect(lambda error, g=generation:
            self._fail("Bluetooth connection failed: " + str(error) + ": " + controller.errorString()) if g == self.generation else None)
        controller.discoveryFinished.connect(lambda g=generation: self._services(g))
        controller.connectToDevice()

    def _connected(self, generation):
        if generation != self.generation or self.controller is None:
            return
        self._progress("services", "Reader found. Setting up the Bluetooth connection…")
        self.controller.discoverServices()

    def _services(self, generation):
        if generation != self.generation or self.controller is None or self.service is not None:
            return
        service = self.controller.createServiceObject(QBluetoothUuid(QUuid(BLE_SERVICE)), self)
        if generation != self.generation:
            if service is not None:
                service.deleteLater()
            return
        if service is None:
            self._fail("This Bluetooth device does not provide the MUBR UART service.")
            return
        self.service = service
        self._progress("characteristics", "Setting up reader communication…")
        service.stateChanged.connect(lambda state, g=generation: self._details(state, g))
        service.characteristicChanged.connect(lambda characteristic, data, g=generation: self._notification(characteristic, data, g))
        service.descriptorWritten.connect(lambda descriptor, data, g=generation: self._subscribed(descriptor, data, g))
        service.characteristicWritten.connect(lambda characteristic, data, g=generation: self._written(characteristic, data, g))
        service.errorOccurred.connect(lambda error, g=generation:
            self._fail("Bluetooth GATT operation failed: " + str(error) + ". Check Windows pairing and reconnect.") if g == self.generation else None)
        service.discoverDetails(QLowEnergyService.SkipValueDiscovery)

    def _details(self, state, generation):
        if generation != self.generation or state != QLowEnergyService.RemoteServiceDiscovered or self.cccd is not None:
            return
        self.rx = self.service.characteristic(QBluetoothUuid(QUuid(BLE_RX)))
        self.tx = self.service.characteristic(QBluetoothUuid(QUuid(BLE_TX)))
        if not self.rx.isValid() or not self.tx.isValid():
            self._fail("MUBR Bluetooth characteristics are unavailable.")
            return
        if not (self.rx.properties() & QLowEnergyCharacteristic.Write) or not (self.tx.properties() & QLowEnergyCharacteristic.Notify):
            self._fail("MUBR Bluetooth write/notification properties are missing.")
            return
        self.cccd = self.tx.descriptor(QBluetoothUuid(QBluetoothUuid.DescriptorType.ClientCharacteristicConfiguration))
        if not self.cccd.isValid():
            self._fail("MUBR Bluetooth notification descriptor is missing.")
            return
        self._progress("notifications", "Setting up secure tag delivery…")
        self.service.writeDescriptor(self.cccd, b"\x01\x00")

    def _subscribed(self, descriptor, data, generation):
        if generation != self.generation or descriptor != self.cccd or bytes(data) != b"\x01\x00" or self.ready:
            return
        self.ready = True
        self.connect_timer.stop()
        mtu = max(23, self.controller.mtu())
        self._progress("ready", "Bluetooth connected. Checking the reader…")
        self.state_signal.emit(True, f"Bluetooth LE · {self.controller.remoteName()} · MTU {mtu}")

    def _notification(self, characteristic, data, generation):
        if generation != self.generation or not self.ready or characteristic != self.tx:
            return
        try:
            for line in self.framer.feed(bytes(data)):
                self.line_signal.emit(line)
        except (ValueError, UnicodeDecodeError) as error:
            self._fail(str(error))

    def send(self, command):
        try:
            data = command_bytes(command)
        except (ValueError, UnicodeEncodeError) as error:
            self.error_signal.emit(str(error))
            return
        if not self.ready:
            self.error_signal.emit("Bluetooth is not connected.")
            return
        if len(self.queue) >= 16:
            self._fail("Bluetooth command output is full; connection closed.")
            return
        self.queue.append((command, data))
        self._write_next()

    def _write_next(self):
        if not self.ready or self.in_flight is not None or not self.queue:
            return
        payload = max(20, min(512, self.controller.mtu() - 3))
        self.in_flight = self.queue[0][1][self.offset:self.offset + payload]
        self.write_timer.start(5000)
        self.service.writeCharacteristic(self.rx, self.in_flight, QLowEnergyService.WriteWithResponse)

    def _written(self, characteristic, data, generation):
        if generation != self.generation or characteristic != self.rx or self.in_flight is None or bytes(data) != self.in_flight:
            return
        self.write_timer.stop()
        self.offset += len(self.in_flight)
        self.in_flight = None
        if self.offset == len(self.queue[0][1]):
            command, _ = self.queue.popleft()
            self.offset = 0
            self.tx_signal.emit(command)
        self._write_next()

    def _lost(self, generation):
        if generation == self.generation:
            if not self.ready:
                self._fail("The reader disconnected before Bluetooth setup completed. Disconnect other BLE clients and check Windows pairing.")
                return
            self.diagnostics.record("BLE reader disconnected")
            self.disconnect()

    def _fail(self, message):
        generation = self.generation
        message += f" [step: {self.phase}]"
        self.diagnostics.record(message)
        self.error_signal.emit(message)
        if generation == self.generation:
            self.disconnect()

    def disconnect(self):
        was_active = self.ready or self.controller is not None or self.local is not None or self.connect_timer.isActive()
        if was_active:
            self.diagnostics.record(f"BLE closing [step: {self.phase}]")
        self.generation += 1
        self.phase = "idle"
        self.connect_timer.stop()
        self.write_timer.stop()
        self.ready = False
        self.framer.clear()
        self.queue.clear()
        self.offset = 0
        self.in_flight = None
        controller, self.controller = self.controller, None
        service, self.service = self.service, None
        local, self.local = self.local, None
        self.rx = self.tx = self.cccd = None
        if controller is not None:
            controller.disconnectFromDevice()
            controller.deleteLater()
        if service is not None:
            service.deleteLater()
        if local is not None:
            local.deleteLater()
        if was_active:
            self.state_signal.emit(False, "")

    def close(self):
        if self.agent and self.agent.isActive():
            self.agent.stop()
        self.disconnect()
