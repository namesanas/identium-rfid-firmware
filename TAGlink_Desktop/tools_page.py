"""Tools workspace: read-rate tuner, stock check and batch EPC encoding.

The tools drive the reader through Main's one-at-a-time command queue.
Main forwards every tag to on_tag() and every other reply to on_reply();
a tool sends its next command only after the reply to the previous one.
"""
import csv
import time
from datetime import datetime

from PySide6.QtCore import QObject, QTimer, Qt
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QFormLayout, QLabel, QSpinBox,
    QLineEdit, QCheckBox, QComboBox, QTabWidget, QTableWidget,
    QTableWidgetItem, QFileDialog, QMessageBox, QPlainTextEdit
)

from ui_builder import _button, _field, _table, _card, _spacer

STEP_TIMEOUT_MS = 8000        # a command without a reply fails the step
SCAN_MS = 1500                # batch encoding: how long to look for tags
TARGETS = {'A': 'A', 'B': 'B', 'AB': 'A/B'}


def _cmd_base(cmd):
    return cmd.split(',', 1)[0].strip().upper()


def _item(text):
    item = QTableWidgetItem(str(text))
    item.setFlags(item.flags() & ~Qt.ItemIsEditable)
    return item


class ToolsController(QObject):
    def __init__(self, window):
        super().__init__(window)
        self.w = window
        self.busy = None              # 'tuner' / 'batch' while a tool drives the reader
        self.steps = []               # commands still to send for the current step
        self.step_done = None
        self.step_failed = None
        self.waiting_for = ''
        self.collecting = False       # count tags for the tuner / batch scan
        self.stopping = False
        self.watchdog = QTimer(self); self.watchdog.setSingleShot(True); self.watchdog.timeout.connect(self._step_timeout)
        self.measure_timer = QTimer(self); self.measure_timer.setSingleShot(True); self.measure_timer.timeout.connect(self._end_collect)
        self.gen2_reply = None
        # Read-rate tuner
        self.tuner_presets = []; self.tuner_index = 0; self.tuner_results = []; self.tuner_original = None
        self.tuner_reads = 0; self.tuner_epcs = set(); self.tuner_rssi = 0; self.tuner_started = 0.0
        # Stock check
        self.expected = []; self.stock = {}; self.stock_rows = {}; self.stock_running = False
        # Batch encoding
        self.batch_number = 0; self.batch_seen = {}; self.batch_encoded = set(); self.batch_auto = False
        self.batch_old = ''; self.batch_new = ''; self.batch_count = 0
        self.batch_verified = ''; self.batch_locked = ''; self.lock_args = None
        self.collect_ms = 0; self.collect_done = None; self.step_failed_outer = None

    # ------------------------------------------------------------ page
    def build(self):
        w = self.w
        page = QWidget(); layout = QVBoxLayout(page); layout.setContentsMargins(0, 0, 0, 0); layout.setSpacing(12)
        header = QHBoxLayout(); title = QLabel("Tools"); title.setObjectName("PageTitle"); header.addWidget(title)
        header.addWidget(QLabel("Read-rate tuner, stock check and batch encoding")); header.addWidget(_spacer()); layout.addLayout(header)
        self.tabs = QTabWidget(); layout.addWidget(self.tabs, 1)
        self.tabs.addTab(self._build_tuner(), "Read-rate tuner")
        self.tabs.addTab(self._build_stock(), "Stock check")
        self.tabs.addTab(self._build_batch(), "Batch encode")
        w.stack.addWidget(page)
        self.update_buttons()

    def _build_tuner(self):
        tab = QWidget(); layout = QVBoxLayout(tab); layout.setContentsMargins(0, 12, 0, 0); layout.setSpacing(12)
        card, card_layout = _card(self.w, "Find the fastest reader mode")
        intro = QLabel("Put the tags you want to read in front of the antenna, then press Start. TAGlink runs each reader mode "
                       "for the time below and counts reads per second and unique tags. Your current settings are restored "
                       "at the end unless you apply the best mode.")
        intro.setWordWrap(True); intro.setObjectName("Sub"); card_layout.addWidget(intro)
        form = QFormLayout(); self.tuner_seconds = QSpinBox(); self.tuner_seconds.setRange(3, 60); self.tuner_seconds.setValue(8); self.tuner_seconds.setSuffix(" s per mode")
        form.addRow("Test time", _field(self.tuner_seconds, 180)); card_layout.addLayout(form)
        row = QHBoxLayout()
        self.tuner_start_btn = _button("Start tuning", self.start_tuner, "Primary")
        self.tuner_stop_btn = _button("Stop", self.stop_tool)
        self.tuner_apply_btn = _button("Apply best mode", self.apply_best)
        for button in (self.tuner_start_btn, self.tuner_stop_btn, self.tuner_apply_btn): row.addWidget(button)
        row.addWidget(_spacer()); card_layout.addLayout(row)
        self.tuner_status = QLabel("Idle."); self.tuner_status.setObjectName("Sub"); card_layout.addWidget(self.tuner_status)
        self.tuner_table = QTableWidget(0, 5); _table(self.tuner_table, ["Reader mode", "Reads/s", "Unique tags", "Avg RSSI (dBm)", "Result"], 0)
        card_layout.addWidget(self.tuner_table, 1)
        layout.addWidget(card, 1)
        return tab

    def _build_stock(self):
        tab = QWidget(); layout = QVBoxLayout(tab); layout.setContentsMargins(0, 12, 0, 0); layout.setSpacing(12)
        card, card_layout = _card(self.w, "Compare tags read with an expected list")
        row = QHBoxLayout()
        self.stock_import_btn = _button("Import list…", self.import_stock_list)
        self.stock_from_inv_btn = _button("Use current inventory", self.stock_from_inventory)
        self.stock_start_btn = _button("Start check", self.start_stock, "Primary")
        self.stock_stop_btn = _button("Stop", self.stop_stock)
        self.stock_reset_btn = _button("Reset", self.reset_stock)
        self.stock_export_btn = _button("Export result…", self.export_stock)
        for button in (self.stock_import_btn, self.stock_from_inv_btn, self.stock_start_btn, self.stock_stop_btn, self.stock_reset_btn, self.stock_export_btn):
            row.addWidget(button)
        row.addWidget(_spacer()); row.addWidget(QLabel("Show"))
        self.stock_show = QComboBox(); self.stock_show.addItems(["All", "Missing", "Found", "Unexpected"]); self.stock_show.currentIndexChanged.connect(self._filter_stock)
        row.addWidget(self.stock_show); card_layout.addLayout(row)
        self.stock_summary = QLabel("Import a CSV or text file with one EPC per line (or an EPC column)."); self.stock_summary.setObjectName("Value"); card_layout.addWidget(self.stock_summary)
        self.stock_table = QTableWidget(0, 5); _table(self.stock_table, ["EPC", "Status", "Reads", "RSSI (dBm)", "Last seen"], 0)
        self.stock_table.setSortingEnabled(False)
        card_layout.addWidget(self.stock_table, 1)
        layout.addWidget(card, 1)
        return tab

    def _build_batch(self):
        tab = QWidget(); layout = QHBoxLayout(tab); layout.setContentsMargins(0, 12, 0, 0); layout.setSpacing(12)
        card, card_layout = _card(self.w, "EPC sequence")
        form = QFormLayout()
        self.batch_prefix = QLineEdit("DECAF"); self.batch_prefix.setPlaceholderText("hex, e.g. DECAF")
        self.batch_start = QSpinBox(); self.batch_start.setRange(0, 2_000_000_000); self.batch_start.setValue(1)
        self.batch_step = QSpinBox(); self.batch_step.setRange(1, 1000); self.batch_step.setValue(1)
        self.batch_length = QSpinBox(); self.batch_length.setRange(4, 124); self.batch_length.setSingleStep(4); self.batch_length.setValue(24); self.batch_length.setSuffix(" hex digits")
        self.batch_format = QComboBox(); self.batch_format.addItems(["Decimal digits", "Hex"])
        self.batch_preview = QLabel(""); self.batch_preview.setObjectName("Value")
        for widget in (self.batch_prefix,):
            widget.textChanged.connect(self._update_batch_preview)
        for widget in (self.batch_start, self.batch_length):
            widget.valueChanged.connect(self._update_batch_preview)
        self.batch_format.currentIndexChanged.connect(self._update_batch_preview)
        form.addRow("Prefix", _field(self.batch_prefix, 240)); form.addRow("Start number", _field(self.batch_start, 200))
        form.addRow("Step", _field(self.batch_step, 120)); form.addRow("EPC length", _field(self.batch_length, 200))
        form.addRow("Number format", _field(self.batch_format, 200)); form.addRow("Next EPC", self.batch_preview)
        card_layout.addLayout(form)
        self.batch_verify = QCheckBox("Read the tag back after writing"); self.batch_verify.setChecked(True)
        self.batch_lock = QCheckBox("Lock after writing (uses the password, mask and action from Tags › Lock)")
        card_layout.addWidget(self.batch_verify); card_layout.addWidget(self.batch_lock)
        row = QHBoxLayout()
        self.batch_one_btn = _button("Encode one tag", lambda: self.start_batch(False), "Primary")
        self.batch_auto_btn = _button("Encode continuously", lambda: self.start_batch(True))
        self.batch_stop_btn = _button("Stop", self.stop_tool)
        for button in (self.batch_one_btn, self.batch_auto_btn, self.batch_stop_btn): row.addWidget(button)
        row.addWidget(_spacer()); card_layout.addLayout(row)
        self.batch_status = QLabel("Place one tag on the antenna, then press Encode."); self.batch_status.setObjectName("Sub"); self.batch_status.setWordWrap(True)
        card_layout.addWidget(self.batch_status); card_layout.addStretch()
        layout.addWidget(card, 2)
        result_card, result_layout = _card(self.w, "Encoded tags")
        self.batch_table = QTableWidget(0, 6); _table(self.batch_table, ["#", "Time", "Old EPC", "New EPC", "Verified", "Locked"], 3)
        result_layout.addWidget(self.batch_table, 1)
        result_row = QHBoxLayout(); result_row.addWidget(_spacer()); result_row.addWidget(_button("Export…", self.export_batch)); result_layout.addLayout(result_row)
        layout.addWidget(result_card, 3)
        self._update_batch_preview()
        return tab

    def update_buttons(self):
        if not hasattr(self, 'tuner_start_btn'):
            return
        connected = self.w.connected
        idle = connected and self.busy is None and not self.w.inventory
        self.tuner_start_btn.setEnabled(idle)
        self.tuner_stop_btn.setEnabled(self.busy == 'tuner')
        self.tuner_apply_btn.setEnabled(idle and any(r.get('rate') is not None for r in self.tuner_results))
        self.batch_one_btn.setEnabled(idle); self.batch_auto_btn.setEnabled(idle)
        self.batch_stop_btn.setEnabled(self.busy == 'batch')
        self.stock_start_btn.setEnabled(connected and bool(self.expected) and not self.stock_running and self.busy is None)
        self.stock_stop_btn.setEnabled(self.stock_running)

    # ------------------------------------------------------------ hooks from Main
    def on_connection(self, on):
        if not on:
            if self.busy:
                self._finish_tool('Connection closed.')
            self.stock_running = False
        self.update_buttons()

    def on_inventory(self, on):
        # Inventory stopped from the Inventory page also ends a stock check.
        if not on and self.stock_running and self.busy is None:
            self.stock_running = False
            self._update_stock_summary()
        self.update_buttons()

    def on_tag(self, tag):
        if self.stock_running:
            self._stock_tag(tag)
        if not self.collecting:
            return
        if self.busy == 'tuner':
            self.tuner_reads += 1; self.tuner_epcs.add(tag.epc); self.tuner_rssi += tag.rssi
        elif self.busy == 'batch':
            seen = self.batch_seen.setdefault(tag.epc, [0, tag.rssi]); seen[0] += 1; seen[1] = max(seen[1], tag.rssi)

    def on_reply(self, kind, data):
        if kind == 'GEN2':
            self.gen2_reply = data
        if not self.busy or not self.waiting_for:
            return
        base = self.waiting_for
        if kind == 'OK' and data == base:
            self.watchdog.stop(); self.waiting_for = ''
            if base == 'START' and self.collecting:
                self._begin_measure()
                return
            self._send_next()
        elif base == 'GET_GEN2' and kind == 'GEN2':
            self.watchdog.stop(); self.waiting_for = ''
            self._send_next()
        elif kind == 'ERROR' and data in (base, 'INVALID_COMMAND'):
            self.watchdog.stop(); self.waiting_for = ''
            self.collecting = False
            failed = self.step_failed; self.steps = []
            if failed:
                failed(f'ERROR,{data}')

    # ------------------------------------------------------------ command sequencer
    def _run(self, commands, done, failed):
        self.steps = list(commands); self.step_done = done; self.step_failed = failed
        self._send_next()

    def _send_next(self):
        if not self.busy:
            return
        if not self.steps:
            done = self.step_done
            if done:
                QTimer.singleShot(0, done)
            return
        cmd = self.steps.pop(0)
        self.waiting_for = _cmd_base(cmd)
        if self.waiting_for == 'STOP':
            self.w.stop_inv()
        else:
            self.w.enqueue(cmd)
        self.watchdog.start(STEP_TIMEOUT_MS)

    def _step_timeout(self):
        if not self.busy:
            return
        base = self.waiting_for; self.waiting_for = ''; self.collecting = False; self.steps = []
        if self.step_failed:
            self.step_failed(f'no reply to {base}')

    def _collect(self, ms, done):
        """START, gather tags for ms, STOP, then call done()."""
        self.collect_ms = ms; self.collect_done = done; self.collecting = True
        self._run(['START'], None, self._collect_failed)

    def _collect_failed(self, reason):
        if self.w.inventory:
            self.w.stop_inv()
        handler = self.step_failed_outer
        if handler:
            handler(reason)

    def _begin_measure(self):
        self.tuner_started = time.monotonic()
        self.measure_timer.start(self.collect_ms)

    def _end_collect(self):
        if not self.busy:
            return
        self.collecting = False
        self._run(['STOP'], self.collect_done, self.step_failed_outer)

    def stop_tool(self):
        if not self.busy:
            return
        self.measure_timer.stop(); self.watchdog.stop()
        self.collecting = False; self.steps = []; self.waiting_for = ''
        if self.w.inventory:
            self.w.stop_inv()
        if self.busy == 'tuner':
            self._restore_settings('Tuning stopped. Settings restored.')
        else:
            self._finish_tool('Stopped.')

    def _finish_tool(self, message):
        tool = self.busy
        self.busy = None; self.steps = []; self.waiting_for = ''; self.collecting = False
        self.measure_timer.stop(); self.watchdog.stop()
        if tool == 'tuner':
            self.tuner_status.setText(message)
        elif tool == 'batch':
            self.batch_status.setText(message); self.batch_auto = False
        self.w.log('Tools: ' + message)
        self.update_buttons()

    # ------------------------------------------------------------ read-rate tuner
    def start_tuner(self):
        if not self._ready():
            return
        if self.w.module_capabilities.get('SET_GEN2') not in (1, '1', True, 'true', 'TRUE'):
            QMessageBox.warning(self.w, 'Not supported', 'This reader firmware does not support SET_GEN2. Update the firmware first.')
            return
        self.busy = 'tuner'; self.tuner_results = []; self.tuner_index = 0; self.gen2_reply = None
        self.step_failed_outer = self._tuner_step_failed
        self.tuner_table.setRowCount(0)
        self.tuner_status.setText('Reading the current settings…')
        self.update_buttons()
        self._run(['GET_GEN2'], self._tuner_begin, lambda reason: self._tuner_begin())

    def _tuner_begin(self):
        reply = self.gen2_reply or {}
        target = TARGETS.get(str(reply.get('TARGET', 'A')).upper(), 'A')
        q_text = str(reply.get('Q', 'DYNAMIC')).upper()
        q = -1 if q_text == 'DYNAMIC' else int(q_text) if q_text.isdigit() else -1
        profile = int(reply['PROFILE']) if str(reply.get('PROFILE', '')).isdigit() else 0
        self.tuner_original = {'session': self.w.session.value(), 'target': target, 'q': q, 'profile': profile}
        # Standard first, so it is measured before an RF mode is changed.
        self.tuner_presets = [
            ('Standard (Session 0, Target A)', 0, 'A', -1, profile, 0),
            ('Fast mode (Session 0, Target A/B)', 0, 'A/B', -1, profile, 1),
            ('Fast: reading distance (RF mode 13)', 0, 'A/B', -1, 113, 1),
            ('Fast: reading count (RF mode 11, Q=2)', 0, 'A/B', 2, 111, 1),
        ]
        for preset in self.tuner_presets:
            row = self.tuner_table.rowCount(); self.tuner_table.insertRow(row)
            for column, text in enumerate((preset[0], '', '', '', 'Waiting')):
                self.tuner_table.setItem(row, column, _item(text))
        self._tuner_next()

    def _preset_commands(self, preset):
        _, session, target, q, profile, fast = preset
        return [f'SET_SESSION,{session}', f'SET_GEN2,{target},{q},{profile},{fast},0']

    def _tuner_next(self):
        if self.busy != 'tuner':
            return
        if self.tuner_index >= len(self.tuner_presets):
            self._tuner_finished(); return
        preset = self.tuner_presets[self.tuner_index]
        self.tuner_table.setItem(self.tuner_index, 4, _item('Testing…'))
        self.tuner_status.setText(f'Testing {preset[0]} ({self.tuner_index + 1} of {len(self.tuner_presets)})…')
        self.tuner_reads = 0; self.tuner_epcs = set(); self.tuner_rssi = 0
        self._run(self._preset_commands(preset), lambda: self._collect(self.tuner_seconds.value() * 1000, self._tuner_measured), self._tuner_step_failed)

    def _tuner_measured(self):
        elapsed = max(time.monotonic() - self.tuner_started, 0.001)
        rate = self.tuner_reads / elapsed
        avg = (self.tuner_rssi / self.tuner_reads) if self.tuner_reads else None
        self.tuner_results.append({'preset': self.tuner_presets[self.tuner_index], 'rate': rate, 'unique': len(self.tuner_epcs)})
        row = self.tuner_index
        self.tuner_table.setItem(row, 1, _item(f'{rate:.1f}'))
        self.tuner_table.setItem(row, 2, _item(len(self.tuner_epcs)))
        self.tuner_table.setItem(row, 3, _item(f'{avg:.1f}' if avg is not None else '-'))
        self.tuner_table.setItem(row, 4, _item('Done'))
        self.tuner_index += 1
        QTimer.singleShot(300, self._tuner_next)

    def _tuner_step_failed(self, reason):
        if self.busy != 'tuner':
            return
        self.tuner_results.append({'preset': self.tuner_presets[self.tuner_index] if self.tuner_index < len(self.tuner_presets) else None, 'rate': None, 'unique': 0})
        if self.tuner_index < len(self.tuner_presets):
            self.tuner_table.setItem(self.tuner_index, 4, _item(f'Failed: {reason}'))
        self.tuner_index += 1
        QTimer.singleShot(500, self._tuner_next)

    def _best_result(self):
        valid = [r for r in self.tuner_results if r.get('rate') is not None and r['preset']]
        if not valid:
            return None
        # Most unique tags first (a mode that misses tags is not faster), then reads/s.
        return max(valid, key=lambda r: (r['unique'], r['rate']))

    def _tuner_finished(self):
        best = self._best_result()
        if best:
            index = self.tuner_presets.index(best['preset'])
            self.tuner_table.setItem(index, 4, _item('★ Best'))
            self.tuner_table.selectRow(index)
            self._restore_settings(f"Best: {best['preset'][0]} — {best['rate']:.1f} reads/s, {best['unique']} tags. "
                                   "Your previous settings are back; press Apply best mode to use it.")
        else:
            self._restore_settings('No mode could be measured. Check that tags are in front of the antenna.')

    def _restore_settings(self, message):
        original = self.tuner_original
        if not original or not self.w.connected:
            self._finish_tool(message); return
        self.busy = 'tuner'
        commands = [f"SET_SESSION,{original['session']}",
                    f"SET_GEN2,{original['target']},{original['q']},{original['profile']},0,0"]
        self._run(commands, lambda: self._finish_tool(message), lambda reason: self._finish_tool(message + f' (Restoring settings failed: {reason}.)'))

    def apply_best(self):
        best = self._best_result()
        if not best or not self._ready():
            return
        preset = best['preset']
        self.busy = 'tuner'; self.tuner_original = None
        self.tuner_status.setText(f'Applying {preset[0]}…'); self.update_buttons()
        def applied():
            self.w.session.setValue(preset[1]); self.w.gen2_target.setCurrentText(preset[2]); self.w.gen2_q.setValue(preset[3])
            index = self.w.rf_profile.findData(preset[4]); self.w.rf_profile.setCurrentIndex(max(index, 0)); self.w.fast_mode.setChecked(bool(preset[5]))
            self._finish_tool(f'{preset[0]} applied and saved on the reader.')
        self._run(self._preset_commands(preset), applied, lambda reason: self._finish_tool(f'Applying failed: {reason}.'))

    # ------------------------------------------------------------ stock check
    def _set_expected(self, epcs, source):
        self.expected = epcs
        self.reset_stock()
        self.w.log(f'Stock check: {len(epcs)} expected EPCs from {source}.')

    def import_stock_list(self):
        path, _ = QFileDialog.getOpenFileName(self.w, 'Import expected EPCs', '', 'CSV or text (*.csv *.txt);;All files (*)')
        if not path:
            return
        epcs, skipped = [], 0
        try:
            with open(path, newline='', encoding='utf-8-sig') as handle:
                rows = list(csv.reader(handle))
        except (OSError, UnicodeDecodeError) as error:
            QMessageBox.warning(self.w, 'Import failed', str(error)); return
        column = 0
        if rows:
            header = [cell.strip().upper() for cell in rows[0]]
            if 'EPC' in header:
                column = header.index('EPC'); rows = rows[1:]
        for row in rows:
            if len(row) <= column or not row[column].strip():
                continue
            epc = self.w.validate_epc_value(self.w.normalize_hex(row[column]))
            if epc:
                if epc not in epcs:
                    epcs.append(epc)
            else:
                skipped += 1
        if not epcs:
            QMessageBox.warning(self.w, 'No EPCs', 'The file has no valid EPCs (hex, even length, one per line or in an EPC column).'); return
        self._set_expected(epcs, path)
        if skipped:
            self.w.statusBar().showMessage(f'{skipped} line(s) were not valid EPCs and were skipped.', 6000)

    def stock_from_inventory(self):
        epcs = list(self.w.epcs.keys())
        if not epcs:
            QMessageBox.information(self.w, 'Inventory is empty', 'Read some tags on the Inventory page first.'); return
        self._set_expected(epcs, 'the current inventory')

    def reset_stock(self):
        self.stock = {epc: {'status': 'Missing', 'reads': 0, 'rssi': '', 'seen': ''} for epc in self.expected}
        self.stock_rows = {}
        self.stock_table.setRowCount(0)
        for epc in self.expected:
            self._stock_row(epc)
        self._update_stock_summary(); self._filter_stock(); self.update_buttons()

    def _stock_row(self, epc):
        entry = self.stock[epc]
        row = self.stock_rows.get(epc)
        if row is None:
            row = self.stock_table.rowCount(); self.stock_table.insertRow(row); self.stock_rows[epc] = row
            self.stock_table.setItem(row, 0, _item(epc))
            for column in range(1, 5):
                self.stock_table.setItem(row, column, _item(''))
        for column, key in ((1, 'status'), (2, 'reads'), (3, 'rssi'), (4, 'seen')):
            self.stock_table.item(row, column).setText(str(entry[key]))
        return row

    def _stock_tag(self, tag):
        entry = self.stock.get(tag.epc)
        new_row = entry is None
        if new_row:
            entry = self.stock[tag.epc] = {'status': 'Unexpected', 'reads': 0, 'rssi': '', 'seen': ''}
        elif entry['status'] == 'Missing':
            entry['status'] = 'Found'
        entry['reads'] += 1; entry['rssi'] = tag.rssi; entry['seen'] = datetime.now().strftime('%H:%M:%S')
        row = self._stock_row(tag.epc)
        if new_row or entry['reads'] == 1:
            self._update_stock_summary()
            self._filter_row(row, entry['status'])

    def _update_stock_summary(self):
        found = sum(1 for e in self.stock.values() if e['status'] == 'Found')
        missing = sum(1 for e in self.stock.values() if e['status'] == 'Missing')
        unexpected = sum(1 for e in self.stock.values() if e['status'] == 'Unexpected')
        expected = len(self.expected)
        percent = (100.0 * found / expected) if expected else 0.0
        self.stock_summary.setText(f'Expected {expected}  ·  Found {found} ({percent:.0f}%)  ·  Missing {missing}  ·  Unexpected {unexpected}'
                                   + ('  ·  checking…' if self.stock_running else ''))

    def _filter_row(self, row, status):
        show = self.stock_show.currentText()
        self.stock_table.setRowHidden(row, show != 'All' and show != status)

    def _filter_stock(self, *_):
        for epc, row in self.stock_rows.items():
            self._filter_row(row, self.stock[epc]['status'])

    def start_stock(self):
        if not self.expected or not self.w.connected:
            return
        self.stock_running = True
        if not self.w.inventory:
            self.w.multi_poll()
        self._update_stock_summary(); self.update_buttons()

    def stop_stock(self):
        self.stock_running = False
        if self.w.inventory and self.busy is None:
            self.w.stop_inv()
        self._update_stock_summary(); self.update_buttons()

    def export_stock(self):
        if not self.stock:
            return
        path, _ = QFileDialog.getSaveFileName(self.w, 'Export stock check', f"stock_check_{datetime.now():%Y%m%d_%H%M%S}.csv", 'CSV (*.csv)')
        if not path:
            return
        order = {'Missing': 0, 'Unexpected': 1, 'Found': 2}
        with open(path, 'w', newline='', encoding='utf-8') as handle:
            writer = csv.writer(handle); writer.writerow(['EPC', 'Status', 'Reads', 'RSSI (dBm)', 'Last seen'])
            for epc, entry in sorted(self.stock.items(), key=lambda item: (order[item[1]['status']], item[0])):
                writer.writerow([epc, entry['status'], entry['reads'], entry['rssi'], entry['seen']])
        self.w.statusBar().showMessage(f'Stock check exported to {path}', 5000)

    # ------------------------------------------------------------ batch encoding
    def _batch_epc(self, number):
        prefix = self.w.normalize_hex(self.batch_prefix.text())
        length = self.batch_length.value()
        digits = length - len(prefix)
        if any(c not in '0123456789ABCDEF' for c in prefix) or length % 4 or digits < 1:
            return None
        text = str(number) if self.batch_format.currentIndex() == 0 else f'{number:X}'
        if len(text) > digits:
            return None
        return prefix + text.rjust(digits, '0')

    def _update_batch_preview(self, *_):
        epc = self._batch_epc(self.batch_start.value() if self.busy != 'batch' else self.batch_number)
        self.batch_preview.setText(epc or 'Invalid: use hex prefix, a length that is a multiple of 4, and a number that fits')

    def start_batch(self, auto):
        if not self._ready():
            return
        self.batch_number = self.batch_start.value()
        if not self._batch_epc(self.batch_number):
            QMessageBox.warning(self.w, 'Invalid sequence', self.batch_preview.text()); return
        if self.batch_lock.isChecked():
            self.lock_args = (self.w.validate_fixed_hex(self.w.lock_access_password.text(), 8),
                              self.w.validate_fixed_hex(self.w.lock_mask.text(), 4),
                              self.w.validate_fixed_hex(self.w.lock_action.text(), 4))
            if not all(self.lock_args):
                QMessageBox.warning(self.w, 'Lock settings missing', 'Fill in Access password (8 hex), Lock mask and Lock action (4 hex each) on Tags › Lock first.'); return
            answer = QMessageBox.question(self.w, 'Lock tags', 'Tags will be locked after writing. Depending on the mask and action this can be permanent. Continue?', QMessageBox.Yes | QMessageBox.No, QMessageBox.No)
            if answer != QMessageBox.Yes:
                return
        self.busy = 'batch'; self.batch_auto = auto
        self.step_failed_outer = self._batch_failed
        self.update_buttons()
        self._batch_scan()

    def _batch_scan(self):
        if self.busy != 'batch':
            return
        self.batch_seen = {}
        self.batch_status.setText(f'Looking for a tag to encode as {self._batch_epc(self.batch_number)}…')
        self._collect(SCAN_MS, self._batch_scanned)

    def _batch_scanned(self):
        if self.busy != 'batch':
            return
        # Tags encoded in this run stay in the field while the next one is placed; ignore them.
        candidates = [epc for epc in self.batch_seen if epc not in self.batch_encoded]
        if not candidates:
            if self.batch_auto:
                self.batch_status.setText(f'Waiting for the next tag ({self._batch_epc(self.batch_number)})… place it on the antenna.')
                QTimer.singleShot(500, self._batch_scan)
            else:
                self._finish_tool('No new tag found. Place one tag on the antenna and try again.')
            return
        if len(candidates) > 1:
            message = f'{len(candidates)} tags in the field ({", ".join(candidates[:3])}{"…" if len(candidates) > 3 else ""}). Keep only one new tag near the antenna.'
            if self.batch_auto:
                self.batch_status.setText(message); QTimer.singleShot(1000, self._batch_scan)
            else:
                self._finish_tool(message)
            return
        self.batch_old = candidates[0]; self.batch_new = self._batch_epc(self.batch_number)
        if self.batch_old == self.batch_new:
            self.batch_encoded.add(self.batch_old)
            if self.batch_auto:
                QTimer.singleShot(500, self._batch_scan)
            else:
                self._finish_tool(f'This tag already has EPC {self.batch_new}.')
            return
        self.batch_status.setText(f'Writing {self.batch_new} to tag {self.batch_old}…')
        self._run([f'WRITE_EPC,{self.batch_old},{self.batch_new}'], self._batch_written, self._batch_failed)

    def _batch_written(self):
        self.batch_verified = 'not checked'; self.batch_locked = 'no'
        if self.batch_verify.isChecked():
            self.batch_seen = {}
            self.batch_status.setText(f'Reading back {self.batch_new}…')
            self._collect(SCAN_MS, self._batch_check)
        else:
            self._batch_lock()

    def _batch_check(self):
        if self.batch_new not in self.batch_seen:
            self._batch_failed(f'{self.batch_new} was not seen after writing')
            return
        self.batch_verified = 'yes'
        self._batch_lock()

    def _batch_lock(self):
        if self.batch_lock.isChecked():
            password, mask, action = self.lock_args
            self.batch_status.setText(f'Locking {self.batch_new}…')
            def locked():
                self.batch_locked = 'yes'; self._batch_done()
            self._run([f'LOCK_TAG,{self.batch_new},{password},{mask},{action}'], locked, self._batch_failed)
        else:
            self._batch_done()

    def _batch_done(self):
        self.batch_count += 1
        self.batch_encoded.add(self.batch_new)
        row = self.batch_table.rowCount(); self.batch_table.insertRow(row)
        for column, text in enumerate((self.batch_count, datetime.now().strftime('%H:%M:%S'), self.batch_old, self.batch_new, self.batch_verified, self.batch_locked)):
            self.batch_table.setItem(row, column, _item(text))
        self.batch_table.scrollToBottom()
        self.batch_number += self.batch_step.value()
        self.batch_start.setValue(self.batch_number)   # the next run continues the sequence
        if not self._batch_epc(self.batch_number):
            self._finish_tool('Sequence finished: the next number does not fit in the EPC length.'); return
        if self.batch_auto:
            self.batch_status.setText(f'Encoded {self.batch_new}. Place the next tag.')
            QTimer.singleShot(500, self._batch_scan)
        else:
            self._finish_tool(f'Encoded {self.batch_new}. Next: {self._batch_epc(self.batch_number)}.')

    def _batch_failed(self, reason):
        if self.busy != 'batch':
            return
        if self.w.inventory:
            self.w.stop_inv()
        self._finish_tool(f'Failed: {reason}. The sequence number was not advanced.')

    def export_batch(self):
        if not self.batch_table.rowCount():
            return
        path, _ = QFileDialog.getSaveFileName(self.w, 'Export encoded tags', f"encoded_tags_{datetime.now():%Y%m%d_%H%M%S}.csv", 'CSV (*.csv)')
        if not path:
            return
        with open(path, 'w', newline='', encoding='utf-8') as handle:
            writer = csv.writer(handle); writer.writerow(['#', 'Time', 'Old EPC', 'New EPC', 'Verified', 'Locked'])
            for row in range(self.batch_table.rowCount()):
                writer.writerow([self.batch_table.item(row, column).text() for column in range(6)])
        self.w.statusBar().showMessage(f'Encoded tags exported to {path}', 5000)

    # ------------------------------------------------------------ shared
    def _ready(self):
        if not self.w.connected:
            self.w.statusBar().showMessage('Connect to a reader first.', 3000); return False
        if self.w.inventory or self.stock_running:
            self.w.statusBar().showMessage('Stop inventory first.', 4000); return False
        if self.busy:
            return False
        return True
