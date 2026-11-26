"""Edit a fixed reader's wired address using its IPGET/IPSET settings interface."""

import socket
import threading
import time
from dataclasses import dataclass, replace
from ipaddress import IPv4Address, IPv4Network

from PySide6.QtCore import Qt, QTimer, Signal, Slot
from PySide6.QtWidgets import QDialog, QDialogButtonBox, QFormLayout, QLabel, QLineEdit, QVBoxLayout


READ_SETTINGS = b'IPGET'
SETTINGS_SIZE = 25
APPLY_DELAY = 1.8  # The reader applies saved settings 1.5 seconds after IPSET.


@dataclass(frozen=True)
class WiredSettings:
    address: IPv4Address
    mask: IPv4Address
    gateway: IPv4Address
    identity: bytes  # The existing hardware address and settings port travel with the request.

    @classmethod
    def from_reply(cls, reply):
        if len(reply) != SETTINGS_SIZE or not reply.startswith(READ_SETTINGS):
            raise ValueError('The reader sent an unexpected reply.')
        return cls(*(IPv4Address(reply[i:i + 4]) for i in (5, 9, 13)), reply[17:])

    @property
    def port(self):
        return int.from_bytes(self.identity[6:], 'big')

    def updated(self, address, mask, gateway):
        try:
            address = IPv4Address(address.strip())
            if address.is_unspecified or address.is_loopback or address.is_multicast or int(address) == 0xFFFFFFFF:
                raise ValueError
        except ValueError:
            raise ValueError('Enter a reader IP address like 192.168.1.35.') from None
        try:
            mask = IPv4Address(mask.strip())
            network = IPv4Network((address, str(mask)), strict=False)
            if network.netmask != mask:
                raise ValueError
        except ValueError:
            raise ValueError('Enter a subnet mask like 255.255.255.0.') from None
        if network.prefixlen < 31 and address in (network.network_address, network.broadcast_address):
            raise ValueError('Choose an IP address available for a device on this network.')
        try:
            gateway = IPv4Address(gateway.strip() or '0.0.0.0')
            if not gateway.is_unspecified and (gateway.is_multicast or gateway.is_loopback):
                raise ValueError
        except ValueError:
            raise ValueError('Enter the router address, or leave it blank if the reader has no router.') from None
        return replace(self, address=address, mask=mask, gateway=gateway)

    def save_frame(self):
        return b'IPSET' + self.address.packed + self.mask.packed + self.gateway.packed + self.identity


def read_wired_settings(connection):
    connection.sendall(READ_SETTINGS)
    reply = bytearray()
    while len(reply) < SETTINGS_SIZE:
        part = connection.recv(SETTINGS_SIZE - len(reply))
        if not part:
            raise ConnectionError('The reader closed the connection.')
        reply.extend(part)
    return WiredSettings.from_reply(bytes(reply))


def exchange_over_ethernet(host, port, previous, proposed):
    with socket.create_connection((host, port), timeout=1.5) as connection:
        current = read_wired_settings(connection)
        if proposed is None:
            return current, ''
        if current != previous:
            raise ValueError('The reader settings changed. Close this window and open it again.')
        connection.sendall(proposed.save_frame())
        # IPSET has no reply. Keep the connection open and read back the
        # settings so the reader can process the write before seeing a close.
        try:
            confirmed_save = read_wired_settings(connection) == proposed
        except (OSError, ValueError):
            confirmed_save = False

    time.sleep(APPLY_DELAY)
    for _ in range(2):
        try:
            with socket.create_connection((str(proposed.address), proposed.port), timeout=1.5) as connection:
                if read_wired_settings(connection) == proposed:
                    return proposed, ''
        except (OSError, ValueError):
            pass
        time.sleep(0.35)
    if confirmed_save:
        return proposed, 'The address was saved. Check that your computer can reach the new address before reconnecting.'
    return None, ''


class ReaderNetworkDialog(QDialog):
    result_ready = Signal(object, str)

    def __init__(self, parent, host, port, send_serial=None):
        super().__init__(parent)
        self.setWindowTitle('Change reader IP')
        self.setMinimumWidth(475)
        self.host, self.port, self.send_serial = host, port, send_serial
        self.original = self.proposed = self.saved = None
        self.saved_note = ''
        self.saving = False
        self.awaiting_serial = False
        self.buffer = bytearray()
        self.timeout = QTimer(self)
        self.timeout.setSingleShot(True)
        self.timeout.timeout.connect(self._serial_timeout)
        self.read_timer = QTimer(self)
        self.read_timer.setSingleShot(True)
        self.read_timer.timeout.connect(self.read_settings)
        self.cancelled = threading.Event()
        self.result_ready.connect(self._receive, Qt.QueuedConnection)

        layout = QVBoxLayout(self)
        intro = QLabel('These settings are stored on the reader. If you change its IP address, connect using the new address.')
        intro.setWordWrap(True)
        layout.addWidget(intro)
        form = QFormLayout()
        self.address, self.mask, self.gateway = QLineEdit(), QLineEdit(), QLineEdit()
        self.address.setPlaceholderText('e.g. 192.168.1.35')
        self.mask.setPlaceholderText('e.g. 255.255.255.0')
        self.gateway.setPlaceholderText('Optional')
        form.addRow('Reader IP address', self.address)
        form.addRow('Subnet mask', self.mask)
        form.addRow('Router address (gateway)', self.gateway)
        layout.addLayout(form)
        self.message = QLabel('Reading the current settings…')
        self.message.setWordWrap(True)
        layout.addWidget(self.message)
        self.buttons = QDialogButtonBox(QDialogButtonBox.Save | QDialogButtonBox.Cancel)
        self.buttons.button(QDialogButtonBox.Save).setText('Save to reader')
        self.buttons.accepted.connect(self.save_settings)
        self.buttons.rejected.connect(self.reject)
        layout.addWidget(self.buttons)
        self._set_fields_enabled(False)
        self.read_timer.start(0)

    def _set_fields_enabled(self, enabled):
        for field in (self.address, self.mask, self.gateway):
            field.setEnabled(enabled)
        self.buttons.button(QDialogButtonBox.Save).setEnabled(enabled)

    def _start_tcp_exchange(self):
        previous, proposed = self.original, self.proposed

        def worker():
            try:
                result, message = exchange_over_ethernet(self.host, self.port, previous, proposed)
            except OSError:
                result, message = None, 'Could not reach the reader’s network settings. Check the cable or try using USB.'
            except ValueError as error:
                result, message = None, str(error)
            if not self.cancelled.is_set():
                try:
                    self.result_ready.emit(result, message)
                except RuntimeError:
                    # The parent can close the dialog before the read finishes.
                    if not self.cancelled.is_set():
                        raise

        threading.Thread(target=worker, daemon=True).start()

    def read_settings(self):
        if self.send_serial is None:
            self._start_tcp_exchange()
        else:
            self.buffer.clear()
            self.awaiting_serial = True
            self.timeout.start(2500)
            self.send_serial(READ_SETTINGS)

    def feed(self, data):
        if not self.awaiting_serial:
            return
        self.buffer.extend(data)
        start = self.buffer.find(READ_SETTINGS)
        if start < 0:
            del self.buffer[:-(len(READ_SETTINGS) - 1)]
            return
        if len(self.buffer) - start < SETTINGS_SIZE:
            del self.buffer[:start]
            return
        self.awaiting_serial = False
        self.timeout.stop()
        self._receive(WiredSettings.from_reply(bytes(self.buffer[start:start + SETTINGS_SIZE])), '')

    def _serial_timeout(self):
        self.awaiting_serial = False
        self._receive(None, '' if self.saving else 'The reader did not send its network settings. Check the connection and try again.')

    def save_settings(self):
        if self.original is None or self.saving:
            return
        try:
            self.proposed = self.original.updated(self.address.text(), self.mask.text(), self.gateway.text())
        except ValueError as error:
            self.message.setText(str(error))
            return
        if self.proposed == self.original:
            self.accept()
            return
        self.saving = True
        self._set_fields_enabled(False)
        self.buttons.button(QDialogButtonBox.Cancel).setEnabled(False)
        self.message.setText('Saving and checking the reader’s new settings…')
        if self.send_serial is None:
            self._start_tcp_exchange()
        else:
            self.send_serial(self.proposed.save_frame())
            self.read_timer.start(round(APPLY_DELAY * 1000))

    @Slot(object, str)
    def _receive(self, settings, message):
        if self.saving:
            self.saving = False
            if settings == self.proposed:
                self.saved = settings
                self.saved_note = message
                self.accept()
                return
            self.buttons.button(QDialogButtonBox.Cancel).setText('Close')
            self.buttons.button(QDialogButtonBox.Cancel).setEnabled(True)
            self.message.setText(message or 'TAGlink could not confirm the new settings. Find the reader again or connect by USB to check the saved address.')
            return
        if settings is None:
            self.message.setText(message)
            return
        self.original = settings
        self.address.setText(str(settings.address))
        self.mask.setText(str(settings.mask))
        self.gateway.setText('' if settings.gateway.is_unspecified else str(settings.gateway))
        self.message.clear()
        self._set_fields_enabled(True)
        self.address.setFocus()
        self.address.selectAll()

    def reject(self):
        if not self.saving:
            self.cancelled.set()
            super().reject()
