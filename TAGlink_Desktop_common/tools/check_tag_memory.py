"""Exercise the IF9D memory tab through Main's real command queue."""

import csv
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from PySide6.QtCore import QSettings, QTimer
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QApplication, QMessageBox

from main import Main
from protocol import TAG_LOG_MAX_SEQUENCE


class Reader:
    def __init__(self, window, oldest=39992, count=47):
        self.window = window
        self.oldest = oldest
        self.count = count
        self.stage = 'READY'
        self.sent = []
        self.busy_once = False
        self.missing = set()
        self.inventory = False
        self.clear_polls = None
        self.hold_reads = False
        self.held = None
        self.poll_interval = 0.02

    def set_poll_interval(self, seconds):
        self.poll_interval = seconds

    def send(self, command):
        self.sent.append(command)
        if command == 'GET_STATUS':
            lines = ['STATUS,INVENTORY' if self.inventory else 'STATUS,IDLE']
        elif command == 'TAGLOG_STATUS':
            if self.stage == 'UNSUPPORTED':
                lines = ['ERROR,INVALID_COMMAND']
            else:
                if self.clear_polls == 0:
                    self.oldest = (self.oldest + self.count) & TAG_LOG_MAX_SEQUENCE
                    self.count = 0
                    self.stage = 'READY'
                    self.clear_polls = None
                elif self.clear_polls is not None:
                    self.clear_polls -= 1
                end = (self.oldest + self.count) & TAG_LOG_MAX_SEQUENCE
                lines = [f'TAGLOG,STATUS,{self.stage},OLDEST={self.oldest},NEXT={end},'
                         f'COUNT={self.count},CAPACITY=65280,PENDING=0,DROPPED=2']
        elif command == 'TAGLOG_CLEAR':
            self.stage = 'CLEARING'
            self.clear_polls = 1
            lines = ['OK,TAGLOG_CLEAR']
        elif command.startswith('TAGLOG_READ,'):
            _, start, count = command.split(',')
            start, count = int(start), int(count)
            if self.busy_once:
                self.busy_once = False
                count = 1
            end = (start + count) & TAG_LOG_MAX_SEQUENCE
            sequences = [(start + offset) & TAG_LOG_MAX_SEQUENCE for offset in range(count)]
            lines = [f'TAGLOG,{seq},1760000000.123,E,1,-48,2,E2000012,TRUNC'
                     for seq in sequences if seq not in self.missing]
            lines.append(f'OK,TAGLOG_READ,NEXT={end}')
            if self.hold_reads:
                self.held = lines
                return
        else:
            lines = [f'OK,{command}']
        QTimer.singleShot(0, lambda: self.reply(lines))

    def reply(self, lines):
        for line in lines:
            self.window.on_line(line)

    def close(self):
        pass


class TagMemoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])
        cls.app.setQuitOnLastWindowClosed(False)

    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        preferences = QSettings(str(Path(folder.name) / 'settings.ini'), QSettings.IniFormat)
        with patch.object(Main, 'refresh_ports'), patch('main.QSettings', return_value=preferences):
            self.window = Main()
        self.window.io.close()
        self.reader = Reader(self.window)
        self.window.io = self.reader
        self.window.connected = True
        self.window.select_feature(5)
        self.memory = self.window.tools.memory

    def tearDown(self):
        self.window.connected = False
        self.window.tools.on_connection(False)
        with patch.object(self.window, 'save'):
            self.window.close()

    def wait_for(self, predicate):
        deadline = time.monotonic() + 4
        while not predicate() and time.monotonic() < deadline:
            QTest.qWait(10)
        self.assertTrue(predicate(), (self.reader.sent, self.memory.message.text()))

    def open_memory(self):
        self.window.tools.tabs.setCurrentWidget(self.window.tools.memory_tab)
        self.wait_for(lambda: not self.memory.busy)

    def test_find_page_and_export_preserve_sequence_order_across_retries(self):
        self.open_memory()
        self.memory.sequence.setText('21')
        self.memory.find_button.click()
        self.wait_for(lambda: not self.memory.busy)
        self.assertEqual([record.sequence for record in self.memory.records], [40012])
        self.assertEqual(self.memory.table.item(0, 0).text(), '21')
        self.assertEqual(self.memory.table.item(0, 7).text(), 'Yes')
        self.assertIn('TAGLOG_READ,40012,1', self.reader.sent)

        self.reader.busy_once = True
        self.memory.navigation['previous'].click()
        self.wait_for(lambda: not self.memory.busy)
        self.assertEqual([r.sequence for r in self.memory.records], list(range(39992, 40012)))
        self.assertIn('TAGLOG_READ,39993,19', self.reader.sent)
        self.memory.navigation['next'].click()
        self.wait_for(lambda: not self.memory.busy)
        self.assertEqual(self.memory.records[0].sequence, 40012)

        with tempfile.TemporaryDirectory() as folder:
            filename = Path(folder) / 'log.csv'
            self.reader.missing.add(40013)
            with patch.object(self.memory, '_save_dialog', return_value=str(filename)):
                self.memory.export_all_button.click()
            self.wait_for(lambda: not self.memory.busy)
            with filename.open(newline='', encoding='utf-8') as file:
                rows = list(csv.DictReader(file))
            self.assertEqual(len(rows), 46)
            self.assertEqual([int(row['Sequence']) for row in rows],
                             [number for number in range(1, 48) if number != 22])
            self.assertIn('1 record number had no saved data', self.memory.message.text())

    def test_wrapped_range_and_cancel_preserve_existing_csv(self):
        self.reader.oldest = TAG_LOG_MAX_SEQUENCE - 1
        self.reader.count = 4
        self.open_memory()
        self.memory.navigation['last'].click()
        self.wait_for(lambda: not self.memory.busy)
        self.assertEqual([r.sequence for r in self.memory.records],
                         [TAG_LOG_MAX_SEQUENCE - 1, TAG_LOG_MAX_SEQUENCE, 0, 1])
        self.assertEqual([self.memory.table.item(row, 0).text() for row in range(4)], ['1', '2', '3', '4'])
        with tempfile.TemporaryDirectory() as folder:
            filename = Path(folder) / 'log.csv'
            filename.write_bytes(b'previous contents')
            self.reader.hold_reads = True
            with patch.object(self.memory, '_save_dialog', return_value=str(filename)):
                self.memory.export_all_button.click()
            self.wait_for(lambda: self.reader.held is not None)
            self.memory.cancel_button.click()
            self.reader.reply(self.reader.held)
            self.assertFalse(self.memory.busy)
            self.assertEqual(filename.read_bytes(), b'previous contents')
            self.assertEqual(self.memory.message.text(), 'Export cancelled.')
            self.assertEqual(self.reader.poll_interval, 0.02)

    def test_full_memory_uses_only_latest_64000_and_stays_in_range(self):
        self.reader.oldest = 52_000
        self.reader.count = 65_307
        self.open_memory()
        self.assertIn('Capacity 64,000', self.memory.summary.text())
        self.memory.navigation['first'].click()
        self.wait_for(lambda: not self.memory.busy)
        self.assertIn('TAGLOG_READ,53307,20', self.reader.sent)
        self.assertEqual(self.memory.table.item(0, 0).text(), '1')
        self.assertFalse(self.memory.navigation['previous'].isEnabled())
        self.memory.navigation['last'].click()
        self.wait_for(lambda: not self.memory.busy)
        self.assertEqual(self.memory.table.item(19, 0).text(), '64000')
        self.assertFalse(self.memory.navigation['next'].isEnabled())
        self.memory.sequence.setText('64001')
        self.assertFalse(self.memory.find_button.isEnabled())
        self.memory.sequence.setText('64000')
        self.memory.find_button.click()
        self.wait_for(lambda: not self.memory.busy)
        self.assertIn('TAGLOG_READ,117306,1', self.reader.sent)
        self.assertEqual(self.memory.table.item(0, 0).text(), '64000')

    def test_clear_confirm_inventory_check_and_polling(self):
        self.open_memory()
        with patch('tag_memory.QMessageBox.question', return_value=QMessageBox.Cancel):
            self.memory.clear_button.click()
        self.assertNotIn('TAGLOG_CLEAR', self.reader.sent)

        self.reader.inventory = True
        with patch('tag_memory.QMessageBox.question', return_value=QMessageBox.Yes):
            self.memory.clear_button.click()
        self.wait_for(lambda: not self.memory.busy)
        self.assertNotIn('TAGLOG_CLEAR', self.reader.sent)
        self.assertFalse(self.memory.clear_button.isEnabled())

        self.reader.inventory = False
        self.window.set_inventory(False)
        with patch('tag_memory.QMessageBox.question', return_value=QMessageBox.Yes):
            self.memory.clear_button.click()
        self.wait_for(lambda: not self.memory.busy)
        start = self.reader.sent.index('TAGLOG_CLEAR')
        self.assertEqual(self.reader.sent[start - 1:start + 3],
                         ['GET_STATUS', 'TAGLOG_CLEAR', 'TAGLOG_STATUS', 'TAGLOG_STATUS'])
        self.assertEqual(self.memory.status.count, 0)
        self.assertEqual(self.memory.message.text(), 'Tag memory cleared.')

    def test_legacy_firmware_and_absent_flash_disable_storage_actions(self):
        self.reader.stage = 'UNSUPPORTED'
        self.open_memory()
        self.assertIn('does not support', self.memory.summary.text())
        self.assertFalse(self.memory.find_button.isEnabled())
        self.reader.stage = 'ABSENT'
        self.reader.count = 0
        self.memory.refresh_button.click()
        self.wait_for(lambda: not self.memory.busy)
        self.assertIn('no tag memory', self.memory.summary.text())
        self.assertFalse(self.memory.clear_button.isEnabled())


if __name__ == '__main__':
    unittest.main(verbosity=2)
