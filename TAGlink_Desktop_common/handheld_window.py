"""MUBR workspace. IF9D's widgets and operations deliberately live elsewhere."""
import csv
import ipaddress
from datetime import datetime
from urllib.parse import urlsplit

from PySide6.QtCore import Qt, QSettings, QTimer, Signal
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QFrame, QHBoxLayout, QVBoxLayout,
    QFormLayout, QGridLayout, QLabel, QComboBox,
    QLineEdit, QSpinBox, QDoubleSpinBox, QCheckBox, QTableWidget,
    QTableWidgetItem, QPlainTextEdit,
    QFileDialog, QMessageBox,
)

import theme
from ui_builder import (
    _button as button, _glyph_icon, _table, RAIL_ICONS, add_settings_sections, build_workspace_body,
    build_workspace_shell, page_panel, refresh_workspace_header, workspace_nav_button,
)
from serial_manager import SerialManager, WIFI_OPTION
from ble_transport import BleTransport
from handheld_session import HandheldSession


def label(text, kind=None):
    widget = QLabel(text)
    widget.setWordWrap(True)
    if kind:
        widget.setObjectName(kind)
    return widget


def card(title):
    widget = QFrame()
    widget.setObjectName("Card")
    layout = QVBoxLayout(widget)
    layout.setContentsMargins(16, 14, 16, 16)
    layout.setAlignment(Qt.AlignTop)
    layout.addWidget(label(title, "Section"))
    return widget, layout


def combo(values):
    widget = QComboBox()
    for title, value in values:
        widget.addItem(title, value)
    return widget


def select_value(widget, value):
    index = widget.findData(value)
    if index >= 0:
        widget.setCurrentIndex(index)


class MubrWindow(QMainWindow):
    return_to_chooser = Signal()
    finished = Signal()

    def __init__(self, settings=None):
        super().__init__()
        self.setWindowTitle("TAGlink · MUBR")
        screen = QApplication.primaryScreen()
        area = screen.availableGeometry() if screen else None
        self.resize(min(1360, area.width() - 40) if area else 1360,
                    min(860, area.height() - 60) if area else 860)
        self.setMinimumSize(960, 600)
        self.settings = settings or QSettings("SilionRFID", "E310Reader")
        self.dark = self.settings.value("dark", True, type=bool)
        self.theme_name = theme.normalize(self.settings.value("color_theme", theme.DEFAULT_THEME, type=str))
        self.serial = SerialManager(max_line_bytes=512)
        self.ble = BleTransport(self)
        self.active = None
        self.connecting = False
        self.cancelling = False
        self.closing_window = False
        self.returning = False
        self.closed = False
        self.rows = {}
        self.total = 0
        self.pending_tags = {}
        self.session = HandheldSession(self)
        self.session.ready_signal.connect(self.update_controls)
        self.session.inventory_signal.connect(self.inventory_changed)
        self.session.reply_signal.connect(self.apply_reply)
        self.session.tag_signal.connect(self.receive_tag)
        self.session.message_signal.connect(self.log)
        self.session.failure_signal.connect(self.show_error)
        self.session.failure_signal.connect(lambda message:
            self.ble.diagnostics.record("MUBR session failure: " + message)
            if self.active is self.ble else None)
        self.session.disconnected_signal.connect(self.disconnected)
        for transport in (self.serial, self.ble):
            transport.state_signal.connect(lambda state, detail, source=transport: self.transport_state(source, state, detail))
            transport.line_signal.connect(lambda line, source=transport: self.session.on_line(line) if source is self.active and not self.closed else None)
            transport.error_signal.connect(self.transport_error)
        self.ble.devices_signal.connect(self.ble_devices)
        self.ble.scan_signal.connect(self.scan_state)
        self.ble.progress_signal.connect(self.ble_progress)
        self._build()
        self.restore()
        self.apply_theme()
        self.update_controls()
        self.tag_timer = QTimer(self)
        self.tag_timer.setInterval(100)
        self.tag_timer.timeout.connect(self.flush_tags)
        self.tag_timer.start()
        self.log("MUBR ready. Choose Wi-Fi, USB or Bluetooth to connect.")

    def _build(self):
        outer, connection_layout, row = build_workspace_shell(
            self, "MUBR", "Handheld reader", self.toggle_theme)
        self.transport_choice = combo((("Wi-Fi", "Wi-Fi TCP"), ("USB", "USB"), ("Bluetooth", "Bluetooth LE")))
        self.transport_choice.setMinimumWidth(110)
        self.transport_choice.currentIndexChanged.connect(self.transport_selected)
        row.addWidget(self.transport_choice)
        self.host = QLineEdit()
        self.host.setPlaceholderText("Reader IP address")
        row.addWidget(self.host, 1)
        self.port = QComboBox()
        self.port.setMinimumWidth(210)
        row.addWidget(self.port, 1)
        self.baud = QComboBox()
        self.baud.addItems(("115200", "57600", "38400", "19200", "9600"))
        self.baud.setToolTip("USB speed. Use the speed set on your reader.")
        row.addWidget(self.baud)
        self.scan_button = button("Refresh ports", self.scan)
        row.addWidget(self.scan_button)
        row.addStretch()
        self.connect_button = button("Connect", self.toggle_connection, "Accent")
        self.connect_button.setMinimumWidth(self.connect_button.fontMetrics().horizontalAdvance("Disconnect") + 28)
        row.addWidget(self.connect_button)
        self.connection_status = label("Disconnected", "Connection")
        connection_layout.addWidget(self.connection_status)
        self.connection_error = label("", "WarningText")
        self.connection_error.hide()
        connection_layout.addWidget(self.connection_error)
        nav_layout = build_workspace_body(self, outer)
        self.nav_buttons = []
        for index, title in enumerate(("Inventory", "Settings", "Diagnostics", "Log")):
            nav = workspace_nav_button(title, lambda checked=False, page=index: self.select_page(page))
            self.nav_buttons.append(nav)
            nav_layout.addWidget(nav)
        nav_layout.addStretch()
        self.stack.addWidget(self._inventory_page())
        self.stack.addWidget(self._settings_page())
        self.stack.addWidget(self._diagnostics_page())
        self.stack.addWidget(self._log_page())
        self.select_page(0)

    def _inventory_page(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(12)
        header = QHBoxLayout()
        header.addWidget(label("Inventory", "PageTitle"))
        header.addStretch()
        self.start_button = button("Start scanning", lambda: self.session.start(), "Primary")
        self.stop_button = button("Stop scanning", self.session.stop, "Danger")
        self.single_button = button("Scan once", lambda: self.session.start(self.single_time.value()))
        self.single_time = QSpinBox()
        self.single_time.setRange(1, 3000)
        self.single_time.setValue(500)
        self.single_time.setSuffix(" ms")
        self.single_time.setToolTip("How long to look for a tag when you choose Scan once.")
        for widget in (self.start_button, self.stop_button, self.single_button, self.single_time):
            header.addWidget(widget)
        layout.addLayout(header)
        metrics = QFrame()
        metrics.setObjectName("MetricChip")
        values = QHBoxLayout(metrics)
        self.counts = label("Ready  •  0 reads  •  0 unique tags", "Value")
        values.addWidget(self.counts)
        layout.addWidget(metrics)
        tools = QHBoxLayout()
        self.filter = QLineEdit()
        self.filter.setPlaceholderText("Filter by tag ID")
        self.filter.textChanged.connect(self.filter_rows)
        tools.addWidget(self.filter, 1)
        tools.addWidget(button("Clear results", self.clear_tags))
        tools.addWidget(button("Export CSV", self.export_csv))
        layout.addLayout(tools)
        self.table = QTableWidget(0, 5)
        self.table.setObjectName("InventoryTable")
        _table(self.table, ("Tag ID", "Signal (dBm)", "Reads", "Frequency (kHz)", "Last seen"), 0)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        layout.addWidget(self.table, 1)
        return page

    def _settings_page(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)
        content = QWidget()
        grid = QGridLayout(content)
        title = QHBoxLayout()
        title.addWidget(label("Settings", "PageTitle"))
        title.addStretch()
        self.refresh_button = button("Refresh from reader", self.session.refresh)
        title.addWidget(self.refresh_button)
        layout.addLayout(title)
        self.settings_widgets = []
        self.capability_buttons = {}

        rf, rf_layout = card("Tag scanning")
        rf_form = QFormLayout()
        self.power = QDoubleSpinBox()
        self.power.setRange(5, 26)
        self.power.setSingleStep(0.5)
        self.power.setDecimals(1)
        self.power.setSuffix(" dBm")
        self.power.setValue(20)
        self.session_value = QSpinBox()
        self.session_value.setRange(0, 3)
        self.target = combo((("Group A", "A"), ("Group B", "B")))
        self.q = QSpinBox()
        self.q.setRange(0, 15)
        self.q.setValue(4)
        self.region = QComboBox()
        self.region.addItem("Connect to see available regions", "")
        self.session_value.setToolTip("Advanced setting. Controls how long scanned tags wait before replying again.")
        self.target.setToolTip("Advanced setting. Choose which tag group the reader asks to reply.")
        self.q.setToolTip("Advanced setting. A higher value helps when scanning many tags at once.")
        for caption, widget in (("Reading power", self.power), ("Scan session", self.session_value), ("Tag group", self.target), ("Tag count setting (Q)", self.q), ("Operating region", self.region)):
            rf_form.addRow(caption, widget)
        rf_layout.addLayout(rf_form)
        self.rf_apply = button("Save scan settings", self.apply_rf, "Accent")
        rf_layout.addWidget(self.rf_apply)
        self.region_apply = button("Save region", self.apply_region)
        rf_layout.addWidget(self.region_apply)
        self.region_note = label("Choose the region where you use this reader.", "Caption")
        rf_layout.addWidget(self.region_note)
        self.settings_widgets.append(rf)

        trigger, trigger_layout = card("Scan button")
        form = QFormLayout()
        self.scan_mode = combo((("Scan while held", "HOLD"), ("Press to start or stop", "TOGGLE"),
                                ("Scan for a set time", "TIMED")))
        self.scan_time = QSpinBox()
        self.scan_time.setRange(50, 3000)
        self.scan_time.setValue(1000)
        self.scan_time.setSuffix(" ms")
        self.delivery = combo((("Automatic", "AUTO"), ("USB", "USB"), ("Wi-Fi", "WIFI"),
                               ("Bluetooth", "BLE"), ("Server", "SERVER")))
        self.trigger_state = label("Not read yet", "Caption")
        form.addRow("What the button does", self.scan_mode)
        form.addRow("Scan duration", self.scan_time)
        form.addRow("Send tags to", self.delivery)
        form.addRow("Button status", self.trigger_state)
        trigger_layout.addLayout(form)
        self.delivery.setToolTip("Automatic sends tags through an available connection.")
        self.scan_apply = button("Save button settings", self.apply_scan, "Accent")
        trigger_layout.addWidget(self.scan_apply)
        self.settings_widgets.append(trigger)
        self.capability_buttons[self.scan_apply] = "SCAN_MODE"

        wifi, wifi_layout = card("Wi-Fi")
        form = QFormLayout()
        self.wifi_enabled = QCheckBox("Turn on Wi-Fi")
        self.wifi_mode = combo((("Join an existing network", "STA"), ("Create a reader network", "AP"),
                                ("Join and create a network", "APSTA")))
        self.ssid = QLineEdit()
        self.ssid.setMaxLength(32)
        self.password = QLineEdit()
        self.password.setEchoMode(QLineEdit.Password)
        self.password.setPlaceholderText("Leave blank to keep the saved password")
        self.wifi_state = label("Not read yet", "Caption")
        form.addRow(self.wifi_enabled)
        form.addRow("Connection type", self.wifi_mode)
        form.addRow("Network name", self.ssid)
        form.addRow("Password", self.password)
        wifi_layout.addLayout(form)
        wifi_layout.addWidget(self.wifi_state)
        self.wifi_apply = button("Save Wi-Fi settings", self.apply_wifi, "Accent")
        wifi_layout.addWidget(self.wifi_apply)
        self.wifi_apply.setToolTip("Saving may disconnect TAGlink if you are using Wi-Fi.")
        self.settings_widgets.append(wifi)
        self.capability_buttons[self.wifi_apply] = "WIFI"

        device, device_layout = card("Reader and Bluetooth")
        form = QFormLayout()
        self.name = QLineEdit()
        self.name.setMaxLength(31)
        self.ble_enabled = QCheckBox("Turn on Bluetooth")
        self.ble_state = label("Not read yet", "Caption")
        form.addRow("Reader name", self.name)
        form.addRow(self.ble_enabled)
        device_layout.addLayout(form)
        device_layout.addWidget(self.ble_state)
        self.name_apply = button("Save reader name", self.apply_name)
        device_layout.addWidget(self.name_apply)
        self.ble_apply = button("Save Bluetooth setting", self.apply_ble)
        device_layout.addWidget(self.ble_apply)
        self.settings_widgets.append(device)
        self.capability_buttons[self.name_apply] = "PERSIST"
        self.capability_buttons[self.ble_apply] = "BLE"

        server, server_layout = card("Send tags to a server")
        form = QFormLayout()
        self.server_enabled = QCheckBox("Send tags automatically")
        self.server_host = QLineEdit()
        self.server_host.setPlaceholderText("e.g. 192.168.1.20")
        self.server_port = QSpinBox()
        self.server_port.setRange(1, 65535)
        self.server_port.setValue(5000)
        self.server_state = label("Not read yet", "Caption")
        form.addRow(self.server_enabled)
        form.addRow("Server IP address", self.server_host)
        form.addRow("Port", self.server_port)
        server_layout.addLayout(form)
        server_layout.addWidget(self.server_state)
        self.server_apply = button("Save server settings", self.apply_server, "Accent")
        server_layout.addWidget(self.server_apply)
        self.settings_widgets.append(server)
        self.capability_buttons[self.server_apply] = "SERVER"

        self.http_card, http_layout = card("Send tags to a web address")
        http_form = QFormLayout()
        self.http_url = QLineEdit()
        self.http_url.setMaxLength(127)
        self.http_url.setPlaceholderText("http://192.168.1.20/upload")
        self.http_timeout = QSpinBox()
        self.http_timeout.setRange(1, 30)
        self.http_timeout.setValue(10)
        self.http_timeout.setSuffix(" s")
        self.http_mode = combo((("App control", "PASSIVE"), ("Automatic upload", "ACTIVE")))
        self.http_configured = False
        self.http_state = label("Not read yet", "Caption")
        http_form.addRow("Upload address", self.http_url)
        http_form.addRow("Wait for reply", self.http_timeout)
        http_form.addRow("Upload mode", self.http_mode)
        http_layout.addLayout(http_form)
        self.http_mode.setToolTip("Automatic upload sends scans over Wi-Fi. The scan button keeps its saved behavior.")
        http_layout.addWidget(self.http_state)
        actions = QHBoxLayout()
        self.http_apply = button("Save address", self.apply_http, "Accent")
        self.http_mode_apply = button("Save mode", self.apply_http_mode)
        self.http_refresh = button("Refresh", lambda: self.session.request("GET_HTTP", "GET_WORK_MODE"))
        for action in (self.http_apply, self.http_mode_apply, self.http_refresh):
            actions.addWidget(action)
        http_layout.addLayout(actions)
        self.settings_widgets.append(self.http_card)
        self.capability_buttons[self.http_apply] = "HTTP"
        self.capability_buttons[self.http_mode_apply] = "HTTP"
        self.capability_buttons[self.http_refresh] = "HTTP"
        add_settings_sections(grid, (rf, wifi, server), (trigger, device, self.http_card))
        layout.addWidget(page_panel(content), 1)
        return page

    def _diagnostics_page(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        row = QHBoxLayout()
        row.addWidget(label("Diagnostics", "PageTitle"))
        row.addStretch()
        self.diagnostics_refresh = button("Refresh", lambda: self.session.request("GET_DIAG", "GET_BATTERY"))
        row.addWidget(self.diagnostics_refresh)
        layout.addLayout(row)
        self.identity_text = label("Connect to see reader details.", "Sub")
        layout.addWidget(self.identity_text)
        self.battery_text = label("Battery: not read yet", "Value")
        layout.addWidget(self.battery_text)
        self.diagnostics = QPlainTextEdit()
        self.diagnostics.setReadOnly(True)
        self.diagnostics.setMaximumBlockCount(100)
        layout.addWidget(self.diagnostics, 1)
        return page

    def _log_page(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        row = QHBoxLayout()
        row.addWidget(label("Log", "PageTitle"))
        row.addStretch()
        self.event_log = QPlainTextEdit()
        self.event_log.setReadOnly(True)
        self.event_log.setMaximumBlockCount(1000)
        self.clear_log_button = button("Clear log", self.event_log.clear)
        row.addWidget(self.clear_log_button)
        layout.addLayout(row)
        layout.addWidget(self.event_log, 1)
        return page

    def restore(self):
        self.host.setText(self.settings.value("mubr/host", "", type=str))
        index = self.transport_choice.findData(self.settings.value("mubr/transport", "Wi-Fi TCP", type=str))
        self.transport_choice.setCurrentIndex(max(0, index))
        self.transport_selected()

    def save(self):
        self.settings.setValue("dark", self.dark)
        self.settings.setValue("color_theme", self.theme_name)
        self.settings.setValue("mubr/transport", self.transport_choice.currentData())
        self.settings.setValue("mubr/host", self.host.text().strip())
        if self.transport_choice.currentData() == "USB":
            self.settings.setValue("mubr/port", self.port.currentData() or "")
        self.settings.sync()

    def transport_selected(self, *_):
        choice = self.transport_choice.currentData()
        self.host.setVisible(choice == "Wi-Fi TCP")
        self.port.setVisible(choice != "Wi-Fi TCP")
        self.baud.setVisible(choice == "USB")
        self.scan_button.setVisible(choice != "Wi-Fi TCP")
        self.scan_button.setText("Find readers" if choice == "Bluetooth LE" else "Refresh USB list")
        self.port.clear()
        if choice == "USB":
            self.refresh_ports()
        self.connection_status.setText("Disconnected")
        self.connect_button.setToolTip("Accept the Windows pairing prompt if it appears." if choice == "Bluetooth LE" else "Connect to the selected reader.")

    def refresh_ports(self):
        selected = self.port.currentData() or self.settings.value("mubr/port", "", type=str)
        self.port.clear()
        for info in SerialManager.ports():
            self.port.addItem(f"{info.device} · {info.description}", info.device)
        index = self.port.findData(selected)
        if index >= 0:
            self.port.setCurrentIndex(index)

    def scan(self):
        if self.transport_choice.currentData() == "Bluetooth LE":
            self.ble.scan()
        else:
            self.refresh_ports()

    def ble_devices(self, devices):
        if self.transport_choice.currentData() != "Bluetooth LE":
            return
        selected = self.port.currentData()
        self.port.clear()
        for key, description in devices:
            self.port.addItem(description, key)
        index = self.port.findData(selected)
        if index >= 0:
            self.port.setCurrentIndex(index)

    def scan_state(self, scanning):
        if self.closed:
            return
        self.scan_button.setEnabled(not scanning)
        self.scan_button.setText("Searching…" if scanning else "Find readers")
        if not scanning and self.transport_choice.currentData() == "Bluetooth LE" and not self.port.count():
            self.log("No MUBR Bluetooth readers found. Turn the reader and Bluetooth on, then scan again.")

    def ble_progress(self, message):
        self.log(message)
        if not self.closed and self.active is self.ble and self.connecting:
            self.connection_status.setText(message)

    def toggle_connection(self):
        if self.connecting:
            self.cancelling = True
            self.active.disconnect()
            self.connecting = False
            self.update_controls()
            return
        if self.session.connected:
            self.session.disconnect()
            return
        choice = self.transport_choice.currentData()
        if choice == "Wi-Fi TCP" and not self.host.text().strip():
            self.log("Enter the reader's IP address before connecting.")
            return
        if choice != "Wi-Fi TCP" and self.port.currentData() is None:
            self.log("Select a USB port or scan for a Bluetooth reader first.")
            return
        self.save()
        self.connection_error.hide()
        self.cancelling = False
        self.active = self.ble if choice == "Bluetooth LE" else self.serial
        self.session.bind(self.active)
        self.connecting = True
        self.connection_status.setText("Connecting…" + (" Accept the Windows pairing prompt." if choice == "Bluetooth LE" else ""))
        self.update_controls()
        if choice == "Bluetooth LE":
            self.active.connect(self.port.currentData())
        else:
            self.active.connect(WIFI_OPTION if choice == "Wi-Fi TCP" else self.port.currentData(), int(self.baud.currentText()), self.host.text().strip())

    def transport_state(self, source, connected, detail):
        if source is not self.active or self.closed:
            return
        if self.cancelling and connected:
            source.disconnect()
            return
        self.connecting = False
        self.connection_status.setText("Connected" if connected else "Disconnected")
        self.connection_status.setToolTip(detail if connected else "")
        self.show_connection_color(connected)
        self.session.on_state(connected, detail)
        self.update_controls()

    def disconnected(self):
        self.flush_tags()
        self.log("Disconnected. Inventory has stopped on this link.")
        if self.closing_window:
            QTimer.singleShot(0, self.close)

    def update_controls(self, *_):
        if not hasattr(self, "start_button"):
            return
        ready = self.session.ready and not self.session.closing
        busy = self.session.inventory
        pending_start = bool(self.session.pending and self.session.pending[0].partition(",")[0] in ("START", "SINGLE_POLL"))
        self.start_button.setEnabled(ready and not busy and not pending_start and self.session.supported("START"))
        self.single_button.setEnabled(ready and not busy and not pending_start and self.session.supported("SINGLE_POLL"))
        self.stop_button.setEnabled(ready and not self.session.stopping)
        self.single_time.setEnabled(ready and not busy)
        self.refresh_button.setEnabled(ready and not busy)
        self.diagnostics_refresh.setEnabled(ready)
        for widget in self.settings_widgets:
            widget.setEnabled(ready and not busy)
        self.http_card.setEnabled(ready and not busy and self.session.supported("GET_HTTP"))
        if not self.session.connected:
            self.http_url.clear()
            self.http_url.setPlaceholderText("http://192.168.1.20/upload")
            self.http_state.setText("Not read yet")
            self.http_configured = False
        elif ready and not self.session.supported("GET_HTTP"):
            self.http_state.setText("This version of the reader software does not support web uploads.")
        for widget, flag in self.capability_buttons.items():
            widget.setEnabled(ready and not busy and self.session.capabilities.get(flag) == "1")
        self.rf_apply.setEnabled(ready and not busy and all(self.session.capabilities.get(flag) == "1" for flag in ("SET_POWER", "SET_SESSION", "SET_GEN2")))
        self.region_apply.setEnabled(ready and not busy and self.session.capabilities.get("SET_REGION") == "1" and bool(self.region.currentData()))
        changing = self.connecting or self.session.connected
        for widget in (self.transport_choice, self.host, self.port, self.baud, self.scan_button):
            widget.setEnabled(not changing)
        self.connect_button.setText("Cancel" if self.connecting else "Disconnect" if self.session.connected else "Connect")
        self.connect_button.setEnabled(not self.session.closing)

    def inventory_changed(self, running):
        self.update_counts()
        self.update_controls()

    def select_page(self, index):
        self.stack.setCurrentIndex(index)
        for page, nav in enumerate(self.nav_buttons):
            nav.setChecked(index == page)
        if hasattr(self, "theme_name"):
            self.update_icons()

    def update_icons(self):
        palette = theme.colors(self.dark, self.theme_name)
        for nav, glyph in zip(self.nav_buttons, (RAIL_ICONS[0], RAIL_ICONS[2], RAIL_ICONS[3], RAIL_ICONS[4])):
            nav.setIcon(_glyph_icon(glyph, palette["text_strong"] if nav.isChecked() else palette["muted"]))

    def apply_theme(self):
        QApplication.instance().setStyleSheet(theme.stylesheet(self.dark, self.theme_name))
        refresh_workspace_header(self)
        self.show_connection_color(self.session.connected)
        self.update_icons()
        self.save()

    def show_connection_color(self, connected):
        color = theme.colors(self.dark, self.theme_name)['success' if connected else 'muted']
        self.connection_status.setStyleSheet(f'color:{color};font-weight:600')

    def toggle_theme(self):
        self.dark = not self.dark
        self.apply_theme()

    def set_color_theme(self, name):
        self.theme_name = theme.normalize(name)
        self.apply_theme()

    def log(self, text):
        if self.closed or not hasattr(self, "event"):
            return
        self.event_log.appendPlainText(f"[{datetime.now():%H:%M:%S}] {text}")
        if not text.startswith((">", "<")):
            self.statusBar().showMessage(text, 12000)
        QTimer.singleShot(0, self.update_controls)

    def show_error(self, message):
        if not self.closed:
            self.connection_error.setText(message)
            self.connection_error.show()
            self.statusBar().showMessage(message, 12000)

    def transport_error(self, message):
        self.log(message)
        self.show_error(message)

    def apply_reply(self, kind, data):
        try:
            if kind == "VERSION":
                self.identity_text.setText(f"MUBR · {data.get('HWVER', '')} · Firmware {data.get('FWVER', '')}")
            elif kind == "CAPABILITIES":
                self.power.setRange(int(data.get("MIN_POWER", "500")) / 100, int(data.get("MAX_POWER", "2600")) / 100)
                self.power.setSingleStep(int(data.get("POWER_STEP", "50")) / 100)
                self.single_time.setMaximum(min(3000, int(data.get("MAX_POLL_MS", "3000"))))
                self.region.clear()
                regions = data.get("SUPPORTED_REGIONS", "").replace(";", "|").split("|")
                for code in regions:
                    if len(code) == 2 and all(char in "0123456789ABCDEFabcdef" for char in code):
                        title = "India" if code.upper() == "03" and data.get("RF_COUNTRY") == "IN" else "Region " + code.upper()
                        self.region.addItem(title, code.upper())
            elif kind == "POWER":
                self.power.setValue(int(data["READ"]) / 100)
            elif kind == "REGION":
                code = data["_csv"][0].upper()
                self.region.setCurrentIndex(self.region.findData(code))
            elif kind == "SESSION":
                self.session_value.setValue(int(data["_csv"][0]))
            elif kind == "GEN2":
                select_value(self.target, data.get("TARGET", "A"))
                self.q.setValue(int(data.get("Q", "4")))
            elif kind == "SCAN_SETTINGS":
                select_value(self.scan_mode, data.get("MODE", "HOLD"))
                self.scan_time.setValue(int(data.get("TIME_MS", "1000")))
                select_value(self.delivery, data.get("DELIVERY", "AUTO"))
                gpio = data.get("GPIO", "-1")
                self.trigger_state.setText("Disabled" if gpio == "-1" else "Pressed" if data.get("PRESSED") == "1" else "Released")
                self.trigger_state.setToolTip("Reader input " + gpio if gpio != "-1" else "")
            elif kind == "WIFI":
                self.wifi_enabled.setChecked(data.get("ENABLE") == "1")
                select_value(self.wifi_mode, data.get("MODE", "STA"))
                if not self.ssid.hasFocus():
                    self.ssid.setText(data.get("SSID", ""))
                state = data.get("STATE", "").replace("_", " ").capitalize()
                address = data.get("IP", "")
                self.wifi_state.setText(f"{state}  •  {address}" if address and address != "0.0.0.0" else state)
                self.wifi_state.setToolTip(f"Wi-Fi signal: {data.get('RSSI', '–')} dBm")
            elif kind == "BLE":
                self.ble_enabled.setChecked(data.get("ENABLE") == "1")
                self.ble_state.setText(("Connected" if data.get("CONNECTED") == "1" else "Disconnected") + ("  •  Secure connection" if data.get("ENCRYPTED") == "1" else ""))
                self.ble_state.setToolTip("Bluetooth packet size (MTU): " + data.get("MTU", "23"))
            elif kind == "SERVER":
                self.server_enabled.setChecked(data.get("ENABLE") == "1")
                self.server_host.setText(data.get("HOST", ""))
                self.server_port.setValue(int(data.get("PORT", "5000")))
                self.server_state.setText("Connected" if data.get("CONNECTED") == "1" else "Disconnected")
            elif kind == "HTTP":
                self.http_configured = data.get("CONFIGURED") == "1"
                self.http_url.setPlaceholderText("Saved on reader" if self.http_configured else "http://192.168.1.20/upload")
                self.http_timeout.setValue(int(data.get("TIMEOUT", "10")))
                status = data.get("STATUS", "0")
                summary = "Wi-Fi connected" if data.get("STA") == "1" else "Wi-Fi disconnected"
                if status != "0":
                    summary += f" · HTTP {status}"
                self.http_state.setText(summary + f" · Uploaded {data.get('UPLOADED', '0')} · Failed {data.get('FAILED', '0')} · Dropped {data.get('DROPPED', '0')}")
            elif kind == "WORK_MODE":
                select_value(self.http_mode, data["_csv"][0])
            elif kind == "OK" and data == "SET_HTTP":
                self.http_url.clear()
            elif kind == "NAME":
                if not self.name.hasFocus():
                    self.name.setText(data.get("_csv", [""])[0])
            elif kind == "BATTERY":
                self.battery_text.setText("Battery: " + (f"{int(data['MV']) / 1000:.2f} V" if data.get("AVAILABLE") == "1" else "unavailable on this reader"))
            elif kind == "DIAG":
                self.diagnostics.setPlainText("\n".join(f"{key}: {value}" for key, value in data.items() if key != "_csv"))
        except (ValueError, KeyError, IndexError):
            self.log("The reader returned an invalid " + kind + " reply.")
        self.update_controls()

    def apply_rf(self):
        power = round(self.power.value() * 100)
        capabilities = self.session.capabilities
        minimum, maximum, step = (int(capabilities.get(key, default)) for key, default in (("MIN_POWER", "500"), ("MAX_POWER", "2600"), ("POWER_STEP", "50")))
        if step <= 0 or not minimum <= power <= maximum or (power - minimum) % step:
            self.log(f"Set reading power between {minimum / 100:g} and {maximum / 100:g} dBm, in steps of {step / 100:g}.")
            return
        self.session.request(f"SET_POWER,{power}", f"SET_SESSION,{self.session_value.value()}", f"SET_GEN2,{self.target.currentData()},{self.q.value()}", "GET_POWER", "GET_SESSION", "GET_GEN2")

    def apply_region(self):
        code = self.region.currentData()
        if code:
            self.session.request("SET_REGION," + code, "GET_REGION")

    def apply_scan(self):
        self.session.request("SET_SCAN_MODE," + self.scan_mode.currentData(), f"SET_SCAN_TIME,{self.scan_time.value()}", "SET_DELIVERY," + self.delivery.currentData(), "GET_SCAN_SETTINGS")

    def apply_wifi(self):
        ssid = self.ssid.text()
        if "," in ssid or not ssid:
            self.log("Enter a network name without commas.")
            return
        password = self.password.text() or "****"
        command = f"SET_WIFI,{int(self.wifi_enabled.isChecked())},{self.wifi_mode.currentData()},{ssid},{password}"
        if self.session.request(command):
            self.password.clear()

    def apply_name(self):
        name = self.name.text().strip()
        if not name or "," in name or "=" in name:
            self.log("Enter a reader name without commas or equals signs.")
            return
        self.session.request("SET_NAME," + name, "GET_NAME")

    def apply_ble(self):
        if self.active is self.ble and not self.ble_enabled.isChecked():
            if QMessageBox.question(self, "Disable Bluetooth", "Turn off Bluetooth on the reader? This Bluetooth connection will close.") != QMessageBox.Yes:
                return
        self.session.request(f"SET_BLE,{int(self.ble_enabled.isChecked())}")

    def apply_server(self):
        host = self.server_host.text().strip()
        if self.server_enabled.isChecked():
            try:
                ipaddress.IPv4Address(host)
            except ValueError:
                self.log("Enter the server’s IP address, for example 192.168.1.20.")
                return
        self.session.request(f"SET_SERVER,{int(self.server_enabled.isChecked())},{host or '0.0.0.0'},{self.server_port.value()}", "GET_SERVER")

    def apply_http(self):
        url = self.http_url.text().strip()
        if url:
            try:
                if not url.startswith("http://") or any(char in ' ,"\\#@' for char in url):
                    raise ValueError
                endpoint = urlsplit(url)
                ipaddress.IPv4Address(endpoint.hostname or "")
                if endpoint.netloc.endswith(":") or endpoint.port == 0 or endpoint.query and not endpoint.path:
                    raise ValueError
            except ValueError:
                self.log("Enter an address like http://192.168.1.20/upload. Add the port after the IP address if your server needs one.")
                return
        elif not self.http_configured:
            self.log("Enter the web address where you want to send tags.")
            return
        self.session.request(f"SET_HTTP,{url or '****'},{self.http_timeout.value()},0", "GET_HTTP")

    def apply_http_mode(self):
        if self.http_mode.currentData() == "ACTIVE" and not self.http_configured:
            self.log("Save the upload address before turning on automatic upload.")
            return
        self.session.request("SET_WORK_MODE," + self.http_mode.currentData(), "GET_WORK_MODE", "GET_HTTP")

    def receive_tag(self, tag):
        self.total += 1
        if tag.epc in self.pending_tags:
            _, count = self.pending_tags[tag.epc]
            self.pending_tags[tag.epc] = (tag, count + 1)
        elif len(self.rows) + len(self.pending_tags) < 5000 or tag.epc in self.rows:
            self.pending_tags[tag.epc] = (tag, 1)

    def flush_tags(self):
        tags, self.pending_tags = self.pending_tags, {}
        for epc, (tag, count) in tags.items():
            if epc not in self.rows:
                row = self.table.rowCount()
                self.table.insertRow(row)
                self.rows[epc] = [row, 0]
                for column in range(5):
                    self.table.setItem(row, column, QTableWidgetItem())
            row, reads = self.rows[epc]
            self.rows[epc][1] = reads + count
            values = (epc, str(tag.rssi), str(reads + count), str(tag.frequency_khz) if tag.frequency_khz else "Unavailable", datetime.now().strftime("%H:%M:%S"))
            for column, text in enumerate(values):
                self.table.item(row, column).setText(text)
            self.table.setRowHidden(row, self.filter.text().upper() not in epc)
        self.update_counts()

    def update_counts(self):
        if hasattr(self, "counts"):
            suffix = " · Table limit: 5,000 tags; export and clear to start a new batch" if len(self.rows) >= 5000 else ""
            self.counts.setText(f"{'Scanning' if self.session.inventory else 'Ready'}  •  {self.total:,} reads  •  {len(self.rows):,} unique tags" + suffix)

    def filter_rows(self):
        text = self.filter.text().upper()
        for epc, (row, _) in self.rows.items():
            self.table.setRowHidden(row, text not in epc)

    def clear_tags(self):
        self.pending_tags.clear()
        self.rows.clear()
        self.total = 0
        self.table.setRowCount(0)
        self.update_counts()

    def export_csv(self):
        self.flush_tags()
        path, _ = QFileDialog.getSaveFileName(self, "Export MUBR inventory", "mubr_inventory.csv", "CSV (*.csv)")
        if not path:
            return
        try:
            with open(path, "w", newline="", encoding="utf-8") as output:
                writer = csv.writer(output)
                writer.writerow(("EPC", "RSSI (dBm)", "Reads", "Frequency (kHz)", "Last seen"))
                for row in range(self.table.rowCount()):
                    writer.writerow(self.table.item(row, column).text() for column in range(5))
        except OSError as error:
            self.log("CSV export failed: " + str(error))

    def choose_device(self):
        self.returning = True
        self.close()

    def closeEvent(self, event):
        if self.session.connected and not self.closed:
            self.closing_window = True
            self.session.disconnect()
            event.ignore()
            return
        self.closed = True
        self.save()
        self.session.timer.stop()
        self.tag_timer.stop()
        self.ble.close()
        self.serial.close()
        event.accept()
        if self.returning:
            self.return_to_chooser.emit()
        else:
            self.finished.emit()
