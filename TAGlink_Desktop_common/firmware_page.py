"""Firmware workspace: send a signed .tlfw image to the reader.

The reader keeps running its current firmware during the upload: the
image goes into a staging area, and only on the next reset does the
bootloader check the signature and install it. See
docs/design/0020-bootloader-and-signed-update.md.

Unlike the Tools page, the upload does not go through Main's command
queue. That queue adds a deliberate 100 ms gap between commands, which
for the ~720 chunks of a 90 KB image would add more than a minute. The
chunks are written straight to the transport instead, one at a time,
each sent only after the reply to the previous one. FW_STATUS still goes
through the normal queue.
"""
import hashlib
import struct
import time

from PySide6.QtCore import QObject, QTimer, Qt
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QFormLayout, QLabel,
    QProgressBar, QPlainTextEdit, QFileDialog, QMessageBox, QToolButton,
)

from ui_builder import _button, _spacer, page_panel, section_divider

MAGIC = 0x57464C54              # "TLFW"
HEADER_SIZE = 256
APP_BASE = 0x08004000
HEADER_SIGNED = struct.Struct("<IHHIII12s32s32s")   # through the SHA-256
BOARDS = {0x419: "F429", 0x431: "F411"}

REPLY_TIMEOUT_MS = 12000        # a staging sector erase can take a few seconds
CHUNK_RETRIES = 3
RESTART_WAIT_S = 45.0           # WiFi readers can take this long to come back
VERDICT_DELAY_MS = 2500         # after reconnecting, before asking FW_STATUS

STATE_TEXT = {
    'NONE': 'no image staged',
    'PENDING': 'staged, waiting for the next restart',
    'INSTALLED': 'installed',
    'BAD_HEADER': 'rejected: the header is not valid',
    'BAD_HASH': 'rejected: the image was damaged in transfer',
    'BAD_SIGNATURE': 'rejected: not signed with the Identium key',
}

# Why the bootloader turned an image down, as a sentence ending.
REJECT_REASON = {
    'BAD_SIGNATURE': 'it is not signed with the Identium key',
    'BAD_HASH': 'it was damaged on the way to the reader',
    'BAD_HEADER': 'its header is not valid for this reader',
}

FAIL_TEXT = {
    'NO_BOOTLOADER': 'This reader has no bootloader, so it cannot be updated over the link. '
                     'It needs one flashed over SWD once.',
    'NO_STAGING': 'This reader has no staging area for an update (an F411 without its SPI flash).',
    'SIZE': 'The reader refused the image size.',
    'INVENTORY': 'The reader is running an inventory. Stop it and try again.',
    'FORMAT': 'The reader could not read the command.',
    'HEADER': 'The reader refused the image header: it may be built for the other board.',
    'HASH': 'The image arrived damaged (checksum mismatch). Nothing was installed; try again.',
    'SEQUENCE': 'The reader lost track of the upload. Try again.',
    'FLASH': 'The reader could not write to its staging flash.',
}


def describe_image(data):
    """(info dict, error text) for a .tlfw file. Checks everything the
    reader checks except the signature, which only the bootloader can
    verify, so a wrong or damaged file is caught before the upload."""
    if len(data) < HEADER_SIZE:
        return None, 'This file is too short to be a firmware image.'
    magic, header_version, header_size, board, load, size, _pad, version, digest = \
        HEADER_SIGNED.unpack_from(data, 0)
    if magic != MAGIC:
        return None, 'This is not a TAGlink firmware image (.tlfw).'
    if header_version != 1 or header_size != HEADER_SIZE:
        return None, f'Unsupported image format (version {header_version}). Use a newer TAGlink.'
    if load != APP_BASE:
        return None, f'This image loads at 0x{load:08X}, not the application address 0x{APP_BASE:08X}.'
    body = data[HEADER_SIZE:HEADER_SIZE + size]
    if len(body) != size:
        return None, 'This image is truncated: it is shorter than its header says.'
    if hashlib.sha256(body).digest() != digest:
        return None, 'This image is damaged: its contents do not match its own checksum.'
    return {
        'board_id': board,
        'board': BOARDS.get(board, f'0x{board:X}'),
        'version': version.split(b'\x00', 1)[0].decode('ascii', 'replace'),
        'size': size,
        'bytes': data[:HEADER_SIZE + size],
    }, ''


class FirmwareController(QObject):
    def __init__(self, window):
        super().__init__(window)
        self.w = window
        self.image = None             # describe_image() result
        self.path = ''
        self.busy = False             # an upload is running
        self.step = ''                # command we are waiting for a reply to
        self.offset = 0
        self.chunk = 128
        self.retries = 0
        self.started = 0.0
        self.awaiting_verdict = False
        self.holding_result = False   # the status line shows an outcome, not a hint
        self.watchdog = QTimer(self)
        self.watchdog.setSingleShot(True)
        self.watchdog.timeout.connect(self._timeout)

    # ------------------------------------------------------------ page
    def build(self):
        w = self.w
        page = QWidget()
        page_layout = QVBoxLayout(page)
        page_layout.setContentsMargins(0, 0, 0, 0)
        page_layout.setSpacing(12)
        header = QHBoxLayout()
        title = QLabel("Reader software update")
        title.setObjectName("PageTitle")
        header.addWidget(title)
        header.addWidget(_spacer())
        self.refresh_btn = _button("Refresh", self.refresh_status)
        header.addWidget(self.refresh_btn)
        page_layout.addLayout(header)
        content = QWidget()
        layout = QVBoxLayout(content)
        layout.setContentsMargins(20, 14, 20, 14)
        layout.setSpacing(8)
        page_layout.addWidget(page_panel(content), 1)

        layout.addWidget(w.sec("Reader"))
        form = QFormLayout()
        form.setHorizontalSpacing(24)
        form.setVerticalSpacing(7)
        self.boot_field = QLabel("Not read yet")
        self.last_field = QLabel("Not read yet")
        self.staged_field = QLabel("Not read yet")
        for field in (self.boot_field, self.last_field, self.staged_field):
            field.setTextFormat(Qt.PlainText)
            field.setTextInteractionFlags(Qt.TextSelectableByMouse)
            field.setWordWrap(True)
        form.addRow("Update support", self.boot_field)
        form.addRow("Last update", self.last_field)
        form.addRow("Version waiting to install", self.staged_field)
        layout.addLayout(form)
        self.boot_warning = QLabel("")
        self.boot_warning.setObjectName("Sub")
        self.boot_warning.setWordWrap(True)
        self.boot_warning.hide()
        layout.addWidget(self.boot_warning)
        layout.addWidget(section_divider())

        layout.addWidget(w.sec("Update file"))
        intro = QLabel("Choose the .tlfw update file supplied by Identium.")
        intro.setWordWrap(True); intro.setObjectName("Sub")
        layout.addWidget(intro)
        pick_row = QHBoxLayout()
        self.choose_btn = _button("Choose file…", self.choose_file)
        pick_row.addWidget(self.choose_btn)
        self.file_label = QLabel("No file chosen")
        self.file_label.setObjectName("Value")
        self.file_label.setWordWrap(True)
        pick_row.addWidget(self.file_label, 1)
        layout.addLayout(pick_row)
        self.file_detail = QLabel("")
        self.file_detail.setObjectName("Sub")
        self.file_detail.setWordWrap(True)
        self.file_detail.hide()
        layout.addWidget(self.file_detail)
        layout.addWidget(section_divider())

        layout.addWidget(w.sec("Install the update"))
        warn = QLabel("The reader restarts to finish installing, and the connection briefly drops. "
                      "Keep the reader powered on until the update finishes.")
        warn.setWordWrap(True); warn.setObjectName("Sub")
        layout.addWidget(warn)
        button_row = QHBoxLayout()
        self.status_label = QLabel("Idle.")
        self.status_label.setObjectName("Value")
        self.status_label.setWordWrap(True)
        button_row.addWidget(self.status_label, 1)
        self.send_btn = _button("Install update", self.start_update, "Primary")
        self.abort_btn = _button("Cancel update", self.abort_update, "Danger")
        button_row.addWidget(self.send_btn)
        button_row.addWidget(self.abort_btn)
        layout.addLayout(button_row)
        self.progress_row = QWidget()
        progress_layout = QHBoxLayout(self.progress_row)
        progress_layout.setContentsMargins(0, 0, 0, 0)
        self.progress = QProgressBar()
        self.progress.setObjectName("UpdateProgress")
        self.progress.setAccessibleName("Update progress")
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        self.progress.setTextVisible(False)
        self.progress.setFixedHeight(8)
        self.progress_percent = QLabel("0%")
        self.progress_percent.setMinimumWidth(34)
        self.progress_percent.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        self.progress.valueChanged.connect(self._show_progress)
        progress_layout.addWidget(self.progress, 1)
        progress_layout.addWidget(self.progress_percent)
        self.progress_row.hide()
        layout.addWidget(self.progress_row)

        self.step_log = QPlainTextEdit()
        self.step_log.setObjectName("UpdateLog")
        self.step_log.setReadOnly(True)
        self.step_log.setMaximumBlockCount(200)
        self.step_log.setFixedHeight(120)
        self.step_log.hide()
        self.details_button = QToolButton()
        self.details_button.setObjectName("Disclosure")
        self.details_button.setText("▸  Update details")
        self.details_button.setCheckable(True)
        self.details_button.toggled.connect(self._toggle_details)
        self.details_button.hide()
        layout.addWidget(self.details_button, 0, Qt.AlignLeft)
        layout.addWidget(self.step_log)
        layout.addStretch()

        w.stack.addWidget(page)
        self.update_buttons()

    # ------------------------------------------------------------ helpers
    def note(self, text):
        self.step_log.appendPlainText(f"[{time.strftime('%H:%M:%S')}] {text}")
        self.details_button.show()

    def _toggle_details(self, expanded):
        self.details_button.setText("▾  Update details" if expanded else "▸  Update details")
        self.step_log.setVisible(expanded)

    def _show_progress(self, percent):
        self.progress_percent.setText(f"{percent}%")
        self.progress_row.show()

    def update_buttons(self):
        if not hasattr(self, 'send_btn'):
            return
        connected = self.w.connected
        self.refresh_btn.setEnabled(connected and not self.busy and not self.w.inventory)
        self.choose_btn.setEnabled(not self.busy)
        self.send_btn.setEnabled(connected and not self.busy and self.image is not None
                                 and not self.w.inventory and self.w.restart_deadline is None)
        self.abort_btn.setEnabled(self.busy)
        if self.busy or self.holding_result or not hasattr(self, 'status_label'):
            return
        if not connected:
            self.status_label.setText("Connect to the reader first.")
        elif self.w.inventory:
            self.status_label.setText("Stop the inventory before sending an update.")
        elif self.image is None:
            self.status_label.setText("Choose an update file.")
        else:
            self.status_label.setText("Ready to send.")

    # ------------------------------------------------------------ reader status
    def refresh_status(self):
        if self.w.connected and not self.busy:
            self.w.request('FW_STATUS')

    def apply_status(self, data):
        """FW,STATE=..,RECEIVED=..,BOOT=..,LAST=..,VERSION=.."""
        boot = data.get('BOOT', '')
        last = data.get('LAST', 'NONE')
        self.boot_field.setText("Updates supported" if boot == '1' else "Updates unavailable over this connection")
        self.last_field.setText(STATE_TEXT.get(last, last))
        self.staged_field.setText(data.get('VERSION', '') or '—')
        if boot != '1':
            self.boot_warning.setText(FAIL_TEXT['NO_BOOTLOADER'])
            self.boot_warning.show()
        else:
            self.boot_warning.hide()
        verdict = self.awaiting_verdict
        self.awaiting_verdict = False
        self.update_buttons()
        if verdict:
            # After update_buttons(), which would otherwise overwrite the
            # status line with the idle text.
            self._report_verdict(last, data.get('VERSION', ''))

    def _report_verdict(self, last, version):
        name = version or 'the image'
        self.holding_result = True
        if last == 'INSTALLED':
            self.note(f"The bootloader installed {name}.")
            self.status_label.setText(f"Update complete: the reader is running {name}.")
            self.progress.setValue(100)
            QMessageBox.information(self.w, 'Update complete',
                                    f'The reader is now running {name}.')
        elif last in REJECT_REASON:
            reason = REJECT_REASON[last]
            self.note(f"The bootloader refused {name}: {reason}.")
            self.status_label.setText(f"Not installed: {reason}.")
            QMessageBox.warning(self.w, 'Update refused',
                                f'The reader refused {name} because {reason}.\n\n'
                                'It is still running its previous firmware, so it keeps working. '
                                'Check that the file came from Identium and was not altered.')
        else:
            self.note(f"The reader reports LAST={last} after the restart.")
            self.status_label.setText(f"The reader reports {STATE_TEXT.get(last, last)}.")

    # ------------------------------------------------------------ file
    def choose_file(self):
        path, _ = QFileDialog.getOpenFileName(self.w, 'Choose a firmware image', '',
                                              'TAGlink firmware (*.tlfw);;All files (*)')
        if not path:
            return
        try:
            with open(path, 'rb') as handle:
                data = handle.read()
        except OSError as error:
            QMessageBox.warning(self.w, 'Cannot read the file', str(error))
            return
        image, problem = describe_image(data)
        if image is None:
            self.image = None; self.path = ''
            self.file_label.setText(path.rsplit('/', 1)[-1])
            self.file_detail.setText(problem)
            self.file_detail.show()
            QMessageBox.warning(self.w, 'Not a usable firmware image', problem)
            self.update_buttons()
            return
        self.image = image; self.path = path
        self.holding_result = False
        self.file_label.setText(path.replace('\\', '/').rsplit('/', 1)[-1])
        self.file_detail.setText(f"Version {image['version']} for the {image['board']} board, "
                                 f"{image['size']:,} bytes — checksum ok. "
                                 "The Identium signature is checked by the reader.")
        self.file_detail.show()
        self.note(f"Chose {image['version']} for {image['board']} ({image['size']:,} bytes).")
        self.update_buttons()

    # ------------------------------------------------------------ upload
    def start_update(self):
        if self.busy or self.image is None or not self.w.connected:
            return
        if self.w.wait_for_tag_memory():
            return
        if self.w.inventory:
            self.w.statusBar().showMessage('Stop the inventory before sending an update.', 4000)
            return
        answer = QMessageBox.question(
            self.w, 'Send firmware update',
            f"Send {self.image['version']} to the reader?\n\n"
            f"The upload takes a few seconds over USB and up to a minute over a serial link. "
            f"The reader then restarts once to install it.\n\n"
            f"Do not power the reader off until it reports back.")
        if answer != QMessageBox.Yes or not self.w.connected:
            return
        self.busy = True
        self.holding_result = False
        self.offset = 0
        self.chunk = 128
        self.retries = 0
        self.started = time.monotonic()
        self.progress.setValue(0)
        self._show_progress(0)
        self.step_log.clear()
        self.note(f"Sending {self.image['version']} ({self.image['size']:,} bytes).")
        self.status_label.setText("Starting…")
        self.update_buttons()
        # Nothing else may use the link while the chunks are in flight.
        self.w.queue = []
        self.w.pending = None
        # Every chunk is a round trip, so the transport's idle poll is what
        # limits the transfer, not the link.
        self.w.io.set_poll_interval(0.002)
        self._send(f"FW_BEGIN,{len(self.image['bytes'])}", 'FW_BEGIN')

    def _send(self, command, step):
        self.step = step
        self.w.io.send(command)
        self.watchdog.start(REPLY_TIMEOUT_MS)

    def _send_chunk(self):
        data = self.image['bytes']
        piece = data[self.offset:self.offset + self.chunk]
        if not piece:
            self._send("FW_END", 'FW_END')
            return
        self._send(f"FW_DATA,{self.offset},{piece.hex().upper()}", 'FW_DATA')

    def _progress(self, received):
        total = len(self.image['bytes'])
        self.progress.setValue(int(received * 100 / total) if total else 0)
        elapsed = time.monotonic() - self.started
        rate = received / elapsed if elapsed > 0.5 else 0
        remaining = f", about {int((total - received) / rate)} s left" if rate > 200 else ""
        self.status_label.setText(f"Uploading {received:,} of {total:,} bytes{remaining}")

    def abort_update(self):
        if not self.busy:
            return
        self.watchdog.stop()
        self.note("Aborted.")
        if self.w.connected:
            self.w.io.send("FW_ABORT")
        self._finish("Upload aborted. The reader is still running its current firmware.")

    def _finish(self, message):
        self.busy = False
        self.holding_result = True
        self.step = ''
        self.watchdog.stop()
        self.w.io.set_poll_interval(0.02)
        self.status_label.setText(message)
        self.update_buttons()
        self.w.pump()

    def _fail(self, message):
        self.note(message)
        self._finish(message)
        QMessageBox.warning(self.w, 'Update failed',
                            message + '\n\nThe reader is still running its current firmware.')

    def _timeout(self):
        if not self.busy:
            return
        if self.step == 'FW_DATA' and self.retries < CHUNK_RETRIES:
            self.retries += 1
            self.note(f"No reply to the chunk at {self.offset}; retry {self.retries}.")
            self._send_chunk()
            return
        self._fail(f"The reader stopped answering during {self.step or 'the update'}.")

    # ------------------------------------------------------------ hooks from Main
    def on_connection(self, on):
        if not on and self.busy and not self.awaiting_verdict:
            self.watchdog.stop()
            self._finish("The connection dropped during the upload. "
                         "The reader is still running its current firmware.")
        elif on and self.awaiting_verdict:
            self.note("Reader is back. Asking what the bootloader did…")
            QTimer.singleShot(VERDICT_DELAY_MS, self.refresh_status)
        self.update_buttons()

    def on_inventory(self, on):
        self.update_buttons()

    def quiet(self, text):
        """True for the per-chunk traffic, which must not flood the logs."""
        return self.busy and (text.startswith('FW_DATA') or text.startswith('OK,FW_DATA'))

    def on_reply(self, kind, data):
        if kind == 'FW':
            self.apply_status(data)
            return
        if not self.busy or not self.step:
            return
        parts = [p.strip() for p in str(data).split(',')]
        base = parts[0].upper() if parts else ''

        if kind == 'ERROR' and (base == self.step or base == 'INVALID_COMMAND'):
            self.watchdog.stop()
            if base == 'INVALID_COMMAND':
                self._fail("This reader firmware is too old to be updated over the link.")
                return
            reason = parts[1].upper() if len(parts) > 1 else ''
            # A rejected chunk carries the offset the reader expects next, so
            # a lost reply or a dropped chunk can be resumed instead of
            # restarting the whole upload.
            if self.step == 'FW_DATA' and reason == 'OFFSET' and len(parts) > 2 and self.retries < CHUNK_RETRIES:
                try:
                    expected = int(parts[2])
                except ValueError:
                    expected = -1
                if 0 <= expected <= len(self.image['bytes']):
                    self.retries += 1
                    self.note(f"The reader expects offset {expected}; resuming there.")
                    self.offset = expected
                    self._send_chunk()
                    return
            self._fail(FAIL_TEXT.get(reason, f"The reader refused {self.step} ({reason or 'no reason given'})."))
            return

        if kind != 'OK' or base != self.step:
            return
        self.watchdog.stop()
        self.retries = 0

        if self.step == 'FW_BEGIN':
            for part in parts[1:]:
                if part.upper().startswith('CHUNK='):
                    try:
                        self.chunk = max(16, int(part.split('=', 1)[1]))
                    except ValueError:
                        pass
            self.note(f"The reader is ready, {self.chunk} bytes per chunk.")
            self.offset = 0
            self._send_chunk()
        elif self.step == 'FW_DATA':
            try:
                received = int(parts[1])
            except (IndexError, ValueError):
                received = self.offset + self.chunk
            self.offset = received
            self._progress(received)
            self._send_chunk()
        elif self.step == 'FW_END':
            version = parts[1] if len(parts) > 1 else self.image['version']
            self.note(f"The reader accepted {version} and checked its contents.")
            self.status_label.setText("Image accepted. Installing…")
            self._send("FW_APPLY", 'FW_APPLY')
        elif self.step == 'FW_APPLY':
            self.note("Restarting into the bootloader to install it.")
            self.status_label.setText("The reader is restarting to install the update…")
            self.step = ''
            self.busy = False
            self.awaiting_verdict = True
            # Reuse Main's restart handling: it reconnects until the reader
            # is back, whatever the auto-reconnect setting says.
            self.w.io.set_poll_interval(0.02)
            self.w.restart_deadline = time.monotonic() + RESTART_WAIT_S
            self.w.manual_disconnect = True
            self.w.io.disconnect()
            self.update_buttons()
        elif self.step == 'FW_ABORT':
            self._finish("Upload aborted.")
