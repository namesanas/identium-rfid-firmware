"""IF9D tools for the reader's persistent TAGLOG ring."""

import csv
import io
import time
from datetime import datetime, timezone

from PySide6.QtCore import QIODevice, QLocale, QObject, QSaveFile, QTimer
from PySide6.QtGui import QIntValidator
from PySide6.QtWidgets import (
    QFileDialog, QHBoxLayout, QLabel, QLineEdit, QMessageBox, QProgressBar,
    QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget,
)

from protocol import TAG_LOG_MAX_SEQUENCE, TAG_LOG_PAGE_SIZE, parse_tag_log_number
from ui_builder import _button, _card, _field, _table


MEMORY_CAPACITY = 64_000
CSV_HEADER = ('Sequence', 'EPC', 'Time (seconds)', 'Clock', 'Antenna',
              'RSSI (dBm)', 'Reads', 'EPC truncated')
FLASH_RETRIES = 8
CLEAR_TIMEOUT_S = 90


def _distance(start, end):
    return (end - start) & TAG_LOG_MAX_SEQUENCE


def _first_sequence(status):
    return (status.next_sequence - min(status.count, MEMORY_CAPACITY)) & TAG_LOG_MAX_SEQUENCE


def _clock(record):
    return 'UTC' if record.clock == 'E' else 'Reader uptime'


def _csv_record(record, origin):
    return (_distance(origin, record.sequence) + 1, record.epc, record.timestamp, _clock(record),
            record.antenna, record.rssi, record.reads, 'Yes' if record.truncated else '')


def _write_csv(file, rows):
    text = io.StringIO(newline='')
    csv.writer(text).writerows(rows)
    data = text.getvalue().encode('utf-8')
    if file.write(data) != len(data):
        raise OSError(file.errorString())


class TagMemoryController(QObject):
    def __init__(self, tools):
        super().__init__(tools)
        self.tools = tools
        self.w = tools.w
        self.status = None
        self.records = []
        self.page_start = self.page_end = None
        self.page_origin = None
        self.operation = ''
        self.command = ''
        self.cancelled = False
        self.export_file = None
        self.previous_poll_interval = None
        self.timer = QTimer(self)
        self.timer.setSingleShot(True)
        self.timer.timeout.connect(self._continue)

    @property
    def busy(self):
        return bool(self.operation)

    def build(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)
        layout.setContentsMargins(0, 12, 0, 0)
        card, content = _card(self.w, 'Reader tag memory')
        layout.addWidget(card)

        status_row = QHBoxLayout()
        self.summary = QLabel('Connect to an IF9D reader to check its tag memory.')
        self.summary.setWordWrap(True)
        status_row.addWidget(self.summary, 1)
        self.refresh_button = _button('Refresh memory', self.refresh)
        status_row.addWidget(self.refresh_button)
        content.addLayout(status_row)

        navigation = QHBoxLayout()
        self.navigation = {
            direction: _button(label, lambda checked=False, direction=direction: self.browse(direction))
            for direction, label in (
                ('first', 'First'), ('previous', 'Previous'), ('next', 'Next'), ('last', 'Last')
            )
        }
        for button in self.navigation.values():
            navigation.addWidget(button)
        navigation.addStretch()
        navigation.addWidget(QLabel('Record number'))
        self.sequence = QLineEdit()
        self.sequence.setPlaceholderText('e.g. 40012')
        self.sequence.setAccessibleName('Record number')
        self.sequence.setToolTip('Enter a record number from 1 to 64,000.')
        validator = QIntValidator(1, MEMORY_CAPACITY, self)
        locale = QLocale.c()
        locale.setNumberOptions(QLocale.RejectGroupSeparator)
        validator.setLocale(locale)
        self.sequence.setValidator(validator)
        self.sequence.returnPressed.connect(self.find_record)
        self.sequence.textChanged.connect(self.update_buttons)
        navigation.addWidget(_field(self.sequence, 120))
        self.find_button = _button('Find record', self.find_record, 'Primary')
        navigation.addWidget(self.find_button)
        content.addLayout(navigation)

        self.table = QTableWidget()
        _table(self.table, ['Record number', 'Tag ID', 'Time', 'Time source', 'Antenna',
                            'Signal (dBm)', 'Reads', 'ID shortened'], 1)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        content.addWidget(self.table, 1)

        actions = QHBoxLayout()
        self.export_page_button = _button('Export this page…', self.export_page)
        self.export_all_button = _button('Export all…', self.export_all)
        self.cancel_button = _button('Cancel export', self.cancel_export)
        self.clear_button = _button('Clear memory…', self.clear_memory, 'Danger')
        self.clear_button.setToolTip('Erase all saved tag records. Stop inventory before clearing.')
        for button in (self.export_page_button, self.export_all_button, self.cancel_button):
            actions.addWidget(button)
        actions.addStretch()
        actions.addWidget(self.clear_button)
        content.addLayout(actions)

        self.progress = QProgressBar()
        self.progress.hide()
        content.addWidget(self.progress)
        self.message = QLabel()
        self.message.setWordWrap(True)
        self.message.setObjectName('Sub')
        content.addWidget(self.message)
        self.update_buttons()
        return tab

    def _available(self):
        queued = self.w.queue + ([self.w.pending[0]] if self.w.pending else [])
        return (self.w.connected and not self.w.profile_check_pending and not self.w.profile_error
                and not self.w.release_pending and not self.w.vswr_scanning
                and not self.w.firmware.busy and not self.w.restart_requested
                and self.tools.busy is None and not self.tools.stock_running
                and not self.busy and not any(cmd.upper().startswith('TAGLOG_') for cmd in queued))

    def update_buttons(self):
        available = self._available()
        readable = bool(available and self.status and self.status.state == 'READY' and self.status.count)
        has_previous = (readable and self.page_start is not None
                        and self.page_start != _first_sequence(self.status))
        has_next = readable and self.page_end is not None and self.page_end != self.status.next_sequence
        self.refresh_button.setEnabled(available)
        self.navigation['first'].setEnabled(readable)
        self.navigation['last'].setEnabled(readable)
        self.navigation['previous'].setEnabled(has_previous)
        self.navigation['next'].setEnabled(has_next)
        self.sequence.setEnabled(readable)
        self.find_button.setEnabled(readable and self.sequence.hasAcceptableInput())
        self.export_page_button.setEnabled(not self.busy and bool(self.records))
        self.export_all_button.setEnabled(readable)
        self.cancel_button.setVisible(self.operation == 'export')
        self.cancel_button.setEnabled(self.operation == 'export' and not self.cancelled)
        self.clear_button.setEnabled(bool(available and self.status
            and self.status.state == 'READY' and (self.status.count or self.status.pending)
            and not self.w.inventory))

    def on_connection(self, connected):
        if not connected:
            self._finish('Connection closed.' if self.busy else '')
            self.status = None
            self._show_records([], None, None)
            self.summary.setText('Connect to an IF9D reader to check its tag memory.')
        self.update_buttons()

    def _begin(self, operation, message):
        self.operation = operation
        self.cancelled = False
        self.message.setText(message)
        self.tools.update_buttons()

    def _finish(self, message):
        self.timer.stop()
        if self.export_file is not None:
            self.export_file.cancelWriting()
            self.export_file = None
        if self.previous_poll_interval is not None:
            self.w.io.set_poll_interval(self.previous_poll_interval)
            self.previous_poll_interval = None
        self.operation = self.command = ''
        self.progress.hide()
        self.message.setText(message)
        self.tools.update_buttons()

    def _send(self, command):
        self.command = command
        self.w.enqueue(command)

    def refresh(self):
        if self._available():
            self._begin('status', 'Checking tag memory…')
            self._send('TAGLOG_STATUS')

    def browse(self, direction, sequence=None):
        if not self._available():
            return
        self.direction = direction
        self.requested_sequence = sequence
        self._begin('browse', 'Reading tag memory…')
        self._send('TAGLOG_STATUS')

    def find_record(self):
        if not self.find_button.isEnabled():
            return
        self.browse('find', int(self.sequence.text()))

    def _show_records(self, records, start, end, origin=None):
        self.records = records
        self.page_start, self.page_end = start, end
        self.page_origin = origin
        self.table.setRowCount(len(records))
        for row, record in enumerate(records):
            fields = list(_csv_record(record, origin))
            if record.clock == 'E':
                seconds, milliseconds = record.timestamp.split('.')
                stamp = datetime.fromtimestamp(int(seconds), timezone.utc)
                fields[2] = f'{stamp:%Y-%m-%d %H:%M:%S}.{milliseconds}'
            else:
                fields[2] = f'{record.timestamp} s'
            for col, value in enumerate(fields):
                self.table.setItem(row, col, QTableWidgetItem(str(value)))

    def _save_dialog(self, title):
        name = f'tag_memory_{datetime.now():%Y%m%d_%H%M%S}.csv'
        path, _ = QFileDialog.getSaveFileName(self.w, title, name, 'CSV (*.csv)')
        return path

    @staticmethod
    def _open_export(path):
        file = QSaveFile(path)
        if not file.open(QIODevice.WriteOnly):
            raise OSError(file.errorString())
        return file

    def export_page(self):
        if not self.records or self.busy:
            return
        records = self.records
        path = self._save_dialog('Export displayed tag records')
        if not path:
            return
        try:
            file = self._open_export(path)
            _write_csv(file, [CSV_HEADER, *(_csv_record(record, self.page_origin) for record in records)])
            if not file.commit():
                raise OSError(file.errorString())
        except OSError as error:
            self.message.setText(f'CSV export failed. {error}')
        else:
            self.message.setText(f'Exported {len(records):,} displayed records.')

    def export_all(self):
        if not self._available():
            return
        path = self._save_dialog('Export all tag memory')
        if path and self._available():
            self.export_path = path
            self._begin('export', 'Preparing the saved records…')
            self._send('TAGLOG_STATUS')

    def cancel_export(self):
        if self.operation == 'export':
            self.cancelled = True
            if self.command:
                self.message.setText('Cancelling export…')
                self.update_buttons()
            else:
                self._finish('Export cancelled.')

    def clear_memory(self):
        if not self.clear_button.isEnabled():
            return
        answer = QMessageBox.question(
            self.w, 'Clear reader tag memory',
            'Permanently erase all saved tag records from this reader? Choose Export all first to keep a copy.',
            QMessageBox.Yes | QMessageBox.Cancel, QMessageBox.Cancel,
        )
        if answer == QMessageBox.Yes and self._available() and not self.w.inventory:
            self._begin('clear', 'Checking inventory before clearing…')
            self._send('GET_STATUS')

    def on_timeout(self, command):
        if self.command == command:
            message = 'Export cancelled.' if self.cancelled else f'No reply to {command.split(",")[0]}.'
            if self.operation == 'clear':
                message += ' Refresh status to check the reader before trying again.'
            self._finish(message)

    def on_reply(self, command, kind, data):
        if not self.command or command != self.command:
            return
        if kind == 'TAGLOG_RECORD':
            self.batch.append(data)
            return
        self.command = ''
        if self.cancelled:
            self._finish('Export cancelled.')
        elif kind == 'ERROR':
            if command == 'TAGLOG_STATUS' and data == 'INVALID_COMMAND':
                self.status = None
                self._show_records([], None, None)
                self.summary.setText('This reader firmware does not support tag memory.')
                self._finish('')
            else:
                self._finish(f'The reader rejected {command.split(",")[0]}.')
        elif command == 'GET_STATUS':
            if data.get('_csv') != ['IDLE']:
                self._finish('Stop inventory before clearing tag memory.')
            else:
                self._send('TAGLOG_CLEAR')
        elif command == 'TAGLOG_CLEAR':
            self.clear_deadline = time.monotonic() + CLEAR_TIMEOUT_S
            self.saw_clearing = False
            self.clear_start = self.status
            self._show_records([], None, None)
            self.message.setText('Erasing tag memory…')
            self.timer.start(250)
        elif kind == 'TAGLOG_STATUS':
            self._receive_status(data)
        else:
            self._receive_page(data)

    def _receive_status(self, status):
        if status is None:
            self._finish('The reader returned an invalid memory status.')
            return
        self.status = status
        if status.state == 'ABSENT':
            self.summary.setText('This reader has no tag memory available.')
            self._show_records([], None, None)
            self._finish('')
            return
        count = min(status.count, MEMORY_CAPACITY)
        origin = _first_sequence(status)
        retained = f'Record numbers 1 to {count:,}' if count else 'No saved records'
        self.summary.setText(
            f'{retained}  •  Capacity {MEMORY_CAPACITY:,}\n'
            f'Waiting to save: {status.pending:,}  •  Could not save: {status.dropped:,}'
        )
        if self.operation == 'clear':
            self.saw_clearing |= status.state == 'CLEARING'
            reached_previous_end = self.clear_start.count and status.oldest == self.clear_start.next_sequence
            if status.state == 'READY' and (self.saw_clearing or not status.count or reached_previous_end):
                self._finish('Tag memory cleared.')
            elif time.monotonic() >= self.clear_deadline:
                self._finish('Erase completion could not be confirmed. Refresh status to check again.')
            else:
                self.timer.start(500)
        elif status.state == 'CLEARING':
            self._show_records([], None, None)
            self._finish('The reader is clearing memory. Refresh status when the erase finishes.')
        elif not status.count:
            self._show_records([], None, None)
            self._finish('Tag memory is empty.')
        elif self.operation == 'status':
            if self.page_start is not None and self.page_origin != origin:
                self._show_records([], None, None)
            self._finish('')
        elif self.operation == 'browse':
            self._begin_page(status)
        else:
            try:
                self.export_file = self._open_export(self.export_path)
                _write_csv(self.export_file, [CSV_HEADER])
            except OSError as error:
                self._finish(f'CSV export failed. {error}')
                return
            self.progress.setRange(0, count)
            self.progress.setValue(0)
            self.progress.show()
            self.previous_poll_interval = self.w.io.poll_interval
            self.w.io.set_poll_interval(0.002)
            self._begin_read(origin, count, origin)

    def _begin_page(self, status):
        origin = _first_sequence(status)
        available = min(status.count, MEMORY_CAPACITY)
        start = origin
        count = TAG_LOG_PAGE_SIZE
        if self.direction == 'find':
            number = self.requested_sequence
            if not 1 <= number <= available:
                self._show_records([], None, None)
                self._finish(f'Record {number:,} is outside the saved range of 1 to {available:,}.')
                return
            start, count = (origin + number - 1) & TAG_LOG_MAX_SEQUENCE, 1
        elif self.direction == 'last':
            start = (status.next_sequence - min(count, available)) & TAG_LOG_MAX_SEQUENCE
        elif (self.direction == 'previous' and self.page_start is not None
              and _distance(origin, self.page_start) < available):
            offset = _distance(origin, self.page_start)
            count = min(count, offset)
            start = (self.page_start - count) & TAG_LOG_MAX_SEQUENCE
        elif self.direction == 'next' and self.page_end is not None:
            if self.page_end == status.next_sequence:
                self._finish('You are at the last retained record.')
                return
            if _distance(origin, self.page_end) < available:
                start = self.page_end
        self._begin_read(start, min(count, _distance(start, status.next_sequence)), origin)

    def _begin_read(self, start, count, origin):
        if not count:
            self._finish('You are at the first retained record.')
            return
        self.start = self.cursor = start
        self.read_origin = origin
        self.total = self.remaining = count
        self.page_records = []
        self.exported = 0
        self.retries = 0
        self._read_next()

    def _read_next(self):
        self.batch = []
        self.request_size = min(TAG_LOG_PAGE_SIZE, self.remaining)
        self._send(f'TAGLOG_READ,{self.cursor},{self.request_size}')

    def _continue(self):
        if self.operation == 'clear':
            self._send('TAGLOG_STATUS')
        elif self.operation in ('browse', 'export'):
            self._read_next()

    def _receive_page(self, ack):
        try:
            prefix, next_sequence = ack.split(',NEXT=', 1)
            cursor = parse_tag_log_number(next_sequence)
            advanced = _distance(self.cursor, cursor)
            offsets = [_distance(self.cursor, record.sequence) for record in self.batch if record is not None]
            if (prefix != 'TAGLOG_READ' or advanced > self.request_size
                    or len(offsets) != len(self.batch) or offsets != sorted(set(offsets))
                    or any(offset >= advanced for offset in offsets)):
                raise ValueError('invalid memory page')
        except ValueError:
            self._finish('The reader returned an incomplete or invalid memory page. Try again.')
            return

        if self.operation == 'export':
            try:
                _write_csv(self.export_file, (_csv_record(record, self.read_origin) for record in self.batch))
            except OSError as error:
                self._finish(f'CSV export failed. {error}')
                return
            self.exported += len(self.batch)
            self.progress.setValue(self.total - self.remaining + advanced)
            self.message.setText(f'Exported {self.exported:,} records. Checked '
                                 f'{self.progress.value():,} of {self.total:,} record numbers.')
        else:
            self.page_records.extend(self.batch)

        self.cursor = cursor
        self.remaining -= advanced
        if not self.remaining:
            self._complete_read()
        elif advanced < self.request_size:
            self.retries = 0 if advanced else self.retries + 1
            if self.retries > FLASH_RETRIES:
                self._finish('Tag memory did not respond to repeated reads. Refresh status and try again.')
            else:
                self.timer.start(200)
        else:
            self.retries = 0
            self._read_next()

    def _complete_read(self):
        if self.operation == 'export':
            file, self.export_file = self.export_file, None
            if not file.commit():
                self._finish(f'CSV export failed. {file.errorString()}')
                return
            message = f'Exported {self.exported:,} records.'
            missing = self.total - self.exported
        else:
            self._show_records(self.page_records, self.start, self.cursor, self.read_origin)
            if self.direction == 'find' and not self.records:
                self._finish(f'Record {self.requested_sequence:,} is unavailable in tag memory.')
                return
            message = f'Showing {len(self.records):,} records.'
            missing = self.total - len(self.records)
        if missing:
            noun = 'record number' if missing == 1 else 'record numbers'
            message += f' {missing:,} {noun} had no saved data.'
        self._finish(message)
