"""MUBR workspace. IF9D's widgets and operations deliberately live elsewhere."""
import csv
import ipaddress
from datetime import datetime
from urllib.parse import urlsplit

from PySide6.QtCore import Qt, QSettings, QTimer, Signal, QSize
from PySide6.QtGui import QAction
from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QFrame, QHBoxLayout, QVBoxLayout,
    QFormLayout, QGridLayout, QLabel, QPushButton, QToolButton, QComboBox,
    QLineEdit, QSpinBox, QDoubleSpinBox, QCheckBox, QTableWidget,
    QTableWidgetItem, QHeaderView, QStackedWidget, QScrollArea, QPlainTextEdit,
    QFileDialog, QMessageBox, QMenu,
)

import theme
from ui_builder import _glyph_icon, RAIL_ICONS
from serial_manager import SerialManager, WIFI_OPTION
from ble_transport import BleTransport
from handheld_session import HandheldSession


def label(text, kind=None):
    widget = QLabel(text)
    widget.setWordWrap(True)
    if kind:
        widget.setObjectName(kind)
    return widget


def button(text, handler, kind=None):
    widget = QPushButton(text)
    widget.setMinimumHeight(32)
    if kind:
        widget.setObjectName(kind)
    widget.clicked.connect(handler)
    return widget


def card(title):
    widget = QFrame()
    widget.setObjectName("Card")
    layout = QVBoxLayout(widget)
    layout.setContentsMargins(16, 14, 16, 16)
    layout.addWidget(label(title, "Section"))
    return widget, layout


def combo(values):
    widget = QComboBox()
    widget.addItems(values)
    return widget


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
        self.log("MUBR ready. Select Wi-Fi TCP, USB or Bluetooth LE.")

    def _build(self):
        root = QWidget()
        root.setObjectName("AppRoot")
        self.setCentralWidget(root)
        outer = QVBoxLayout(root)
        outer.setContentsMargins(16, 12, 16, 12)
        outer.setSpacing(12)
        top = QFrame()
        top.setObjectName("TopBar")
        header = QHBoxLayout(top)
        header.setContentsMargins(14, 10, 14, 10)
        header.addWidget(label("TAGlink", "Title"))
        header.addWidget(label("MUBR · Handheld", "Sub"))
        header.addStretch()
        self.change_device = button("Change device", self.choose_device)
        header.addWidget(self.change_device)
        self.theme_button = button("Theme", self.open_theme)
        header.addWidget(self.theme_button)
        self.mode_button = button("Light mode", self.toggle_theme)
        header.addWidget(self.mode_button)
        outer.addWidget(top)
        connection, connection_layout = card("Reader connection")
        row = QHBoxLayout()
        self.transport_choice = combo(("Wi-Fi TCP", "USB", "Bluetooth LE"))
        self.transport_choice.setMinimumWidth(140)
        self.transport_choice.currentIndexChanged.connect(self.transport_selected)
        row.addWidget(self.transport_choice)
        self.host = QLineEdit()
        self.host.setPlaceholderText("Reader IP address")
        row.addWidget(self.host, 1)
        self.port = QComboBox()
        self.port.setMinimumWidth(210)
        row.addWidget(self.port, 1)
        self.baud = combo(("115200", "57600", "38400", "19200", "9600"))
        row.addWidget(self.baud)
        self.scan_button = button("Refresh ports", self.scan)
        row.addWidget(self.scan_button)
        self.connect_button = button("Connect", self.toggle_connection, "Accent")
        row.addWidget(self.connect_button)
        connection_layout.addLayout(row)
        self.connection_status = label("Disconnected · Wi-Fi uses TCP port 5000", "Caption")
        connection_layout.addWidget(self.connection_status)
        self.connection_error = label("", "WarningText")
        self.connection_error.hide()
        connection_layout.addWidget(self.connection_error)
        outer.addWidget(connection)
        body = QHBoxLayout()
        body.setSpacing(12)
        rail = QFrame()
        rail.setObjectName("NavRail")
        rail.setFixedWidth(96)
        nav_layout = QVBoxLayout(rail)
        nav_layout.setContentsMargins(6, 10, 6, 10)
        self.nav_buttons = []
        for index, title in enumerate(("Inventory", "Settings", "Diagnostics", "Log")):
            nav = QToolButton()
            nav.setObjectName("RailButton")
            nav.setText(title)
            nav.setToolButtonStyle(Qt.ToolButtonTextUnderIcon)
            nav.setIconSize(QSize(24, 24))
            nav.setCheckable(True)
            nav.setMinimumHeight(66)
            nav.setMinimumWidth(82)
            nav.clicked.connect(lambda checked=False, page=index: self.select_page(page))
            self.nav_buttons.append(nav)
            nav_layout.addWidget(nav)
        nav_layout.addStretch()
        body.addWidget(rail)
        self.stack = QStackedWidget()
        self.stack.addWidget(self._inventory_page())
        self.stack.addWidget(self._settings_page())
        self.stack.addWidget(self._diagnostics_page())
        self.stack.addWidget(self._log_page())
        body.addWidget(self.stack, 1)
        outer.addLayout(body, 1)
        self.select_page(0)

    def _inventory_page(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        header = QHBoxLayout()
        header.addWidget(label("Inventory", "PageTitle"))
        header.addStretch()
        self.start_button = button("Start", lambda: self.session.start(), "Primary")
        self.stop_button = button("Stop", self.session.stop, "Danger")
        self.single_button = button("Single poll", lambda: self.session.start(self.single_time.value()))
        self.single_time = QSpinBox()
        self.single_time.setRange(1, 3000)
        self.single_time.setValue(500)
        self.single_time.setSuffix(" ms")
        for widget in (self.start_button, self.stop_button, self.single_button, self.single_time):
            header.addWidget(widget)
        layout.addLayout(header)
        metrics, values = card("Live inventory")
        self.counts = label("IDLE · 0 reads · 0 unique tags", "Value")
        values.addWidget(self.counts)
        layout.addWidget(metrics)
        tools = QHBoxLayout()
        self.filter = QLineEdit()
        self.filter.setPlaceholderText("Filter EPC")
        self.filter.textChanged.connect(self.filter_rows)
        tools.addWidget(self.filter, 1)
        tools.addWidget(button("Clear", self.clear_tags))
        tools.addWidget(button("Export CSV", self.export_csv))
        layout.addLayout(tools)
        self.table = QTableWidget(0, 5)
        self.table.setObjectName("InventoryTable")
        self.table.setHorizontalHeaderLabels(("EPC", "RSSI (dBm)", "Reads", "Frequency (kHz)", "Last seen"))
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.setAlternatingRowColors(True)
        self.table.verticalHeader().setVisible(False)
        for column in range(5):
            self.table.horizontalHeader().setSectionResizeMode(column, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        layout.addWidget(self.table, 1)
        layout.addWidget(label("Frequency is unavailable when the reader reports 0. Physical trigger scanning follows the reader's saved scan settings.", "Caption"))
        return page

    def _settings_page(self):
        content = QWidget()
        content.setObjectName("ScrollContent")
        grid = QGridLayout(content)
        grid.setContentsMargins(0, 0, 8, 0)
        grid.setSpacing(12)
        title = QHBoxLayout()
        title.addWidget(label("MUBR settings", "PageTitle"))
        title.addStretch()
        self.refresh_button = button("Refresh from reader", self.session.refresh)
        title.addWidget(self.refresh_button)
        grid.addLayout(title, 0, 0, 1, 2)
        self.settings_widgets = []
        self.capability_buttons = {}

        rf, rf_layout = card("RF settings")
        rf_form = QFormLayout()
        self.power = QDoubleSpinBox()
        self.power.setRange(5, 26)
        self.power.setSingleStep(0.5)
        self.power.setDecimals(1)
        self.power.setSuffix(" dBm")
        self.power.setValue(20)
        self.session_value = QSpinBox()
        self.session_value.setRange(0, 3)
        self.target = combo(("A", "B"))
        self.q = QSpinBox()
        self.q.setRange(0, 15)
        self.q.setValue(4)
        self.region = QComboBox()
        self.region.addItem("Waiting for reader capabilities", "")
        for caption, widget in (("RF power", self.power), ("Session", self.session_value), ("Target", self.target), ("Q", self.q), ("Operating region", self.region)):
            rf_form.addRow(caption, widget)
        rf_layout.addLayout(rf_form)
        self.rf_apply = button("Apply RF settings", self.apply_rf, "Accent")
        rf_layout.addWidget(self.rf_apply)
        self.region_apply = button("Apply region", self.apply_region)
        rf_layout.addWidget(self.region_apply)
        self.region_note = label("Region choices come from this reader's supported RF plan.", "Caption")
        rf_layout.addWidget(self.region_note)
        grid.addWidget(rf, 1, 0)
        self.settings_widgets.append(rf)

        trigger, trigger_layout = card("Scan button")
        form = QFormLayout()
        self.scan_mode = combo(("HOLD", "TOGGLE", "TIMED"))
        self.scan_time = QSpinBox()
        self.scan_time.setRange(50, 3000)
        self.scan_time.setValue(1000)
        self.scan_time.setSuffix(" ms")
        self.delivery = combo(("AUTO", "USB", "WIFI", "BLE", "SERVER"))
        self.trigger_state = label("Not read yet", "Caption")
        form.addRow("Behavior", self.scan_mode)
        form.addRow("Timed scan", self.scan_time)
        form.addRow("Send tags to", self.delivery)
        form.addRow("Physical input", self.trigger_state)
        trigger_layout.addLayout(form)
        trigger_layout.addWidget(label("HOLD scans while pressed. AUTO uses an available connected destination.", "Caption"))
        self.scan_apply = button("Apply scan settings", self.apply_scan, "Accent")
        trigger_layout.addWidget(self.scan_apply)
        grid.addWidget(trigger, 1, 1)
        self.settings_widgets.append(trigger)
        self.capability_buttons[self.scan_apply] = "SCAN_MODE"

        wifi, wifi_layout = card("Wi-Fi")
        form = QFormLayout()
        self.wifi_enabled = QCheckBox("Enabled")
        self.wifi_mode = combo(("STA", "AP", "APSTA"))
        self.ssid = QLineEdit()
        self.ssid.setMaxLength(32)
        self.password = QLineEdit()
        self.password.setEchoMode(QLineEdit.Password)
        self.password.setPlaceholderText("Leave blank to keep the saved password")
        self.wifi_state = label("Not read yet", "Caption")
        form.addRow("Wi-Fi", self.wifi_enabled)
        form.addRow("Mode", self.wifi_mode)
        form.addRow("Network name", self.ssid)
        form.addRow("Password", self.password)
        wifi_layout.addLayout(form)
        wifi_layout.addWidget(self.wifi_state)
        self.wifi_apply = button("Save Wi-Fi settings", self.apply_wifi, "Accent")
        wifi_layout.addWidget(self.wifi_apply)
        wifi_layout.addWidget(label("STA joins your network; AP provides a direct reader network; APSTA enables both. Saving may close a Wi-Fi connection.", "Caption"))
        grid.addWidget(wifi, 2, 0)
        self.settings_widgets.append(wifi)
        self.capability_buttons[self.wifi_apply] = "WIFI"

        device, device_layout = card("Reader and Bluetooth")
        form = QFormLayout()
        self.name = QLineEdit()
        self.name.setMaxLength(31)
        self.ble_enabled = QCheckBox("Enabled")
        self.ble_state = label("Not read yet", "Caption")
        form.addRow("Reader name", self.name)
        form.addRow("Bluetooth LE", self.ble_enabled)
        device_layout.addLayout(form)
        device_layout.addWidget(self.ble_state)
        self.name_apply = button("Save reader name", self.apply_name)
        device_layout.addWidget(self.name_apply)
        self.ble_apply = button("Save Bluetooth setting", self.apply_ble)
        device_layout.addWidget(self.ble_apply)
        grid.addWidget(device, 2, 1)
        self.settings_widgets.append(device)
        self.capability_buttons[self.name_apply] = "PERSIST"
        self.capability_buttons[self.ble_apply] = "BLE"

        server, server_layout = card("Outbound TCP server")
        form = QFormLayout()
        self.server_enabled = QCheckBox("Enabled")
        self.server_host = QLineEdit()
        self.server_host.setPlaceholderText("Server IPv4 address")
        self.server_port = QSpinBox()
        self.server_port.setRange(1, 65535)
        self.server_port.setValue(5000)
        self.server_state = label("Not read yet", "Caption")
        form.addRow("Server delivery", self.server_enabled)
        form.addRow("Host", self.server_host)
        form.addRow("TCP port", self.server_port)
        server_layout.addLayout(form)
        server_layout.addWidget(self.server_state)
        self.server_apply = button("Save server settings", self.apply_server, "Accent")
        server_layout.addWidget(self.server_apply)
        grid.addWidget(server, 3, 0)
        self.settings_widgets.append(server)
        self.capability_buttons[self.server_apply] = "SERVER"

        self.http_card, http_layout = card("HTTP upload")
        http_form = QFormLayout()
        self.http_url = QLineEdit()
        self.http_url.setMaxLength(127)
        self.http_url.setPlaceholderText("http://IPv4:port/path")
        self.http_timeout = QSpinBox()
        self.http_timeout.setRange(1, 30)
        self.http_timeout.setValue(10)
        self.http_timeout.setSuffix(" s")
        self.http_mode = combo(("PASSIVE", "ACTIVE"))
        self.http_configured = False
        self.http_state = label("Not read yet", "Caption")
        http_form.addRow("URL", self.http_url)
        http_form.addRow("Timeout", self.http_timeout)
        http_form.addRow("Work mode", self.http_mode)
        http_layout.addLayout(http_form)
        http_layout.addWidget(label("Active uploads over Wi-Fi. The scan button still follows its saved behavior.", "Caption"))
        http_layout.addWidget(self.http_state)
        actions = QHBoxLayout()
        self.http_apply = button("Save endpoint", self.apply_http, "Accent")
        self.http_mode_apply = button("Apply work mode", self.apply_http_mode, "Accent")
        self.http_refresh = button("Refresh status", lambda: self.session.request("GET_HTTP", "GET_WORK_MODE"))
        for action in (self.http_apply, self.http_mode_apply, self.http_refresh):
            actions.addWidget(action)
        http_layout.addLayout(actions)
        grid.addWidget(self.http_card, 3, 1)
        self.settings_widgets.append(self.http_card)
        self.capability_buttons[self.http_apply] = "HTTP"
        self.capability_buttons[self.http_mode_apply] = "HTTP"
        self.capability_buttons[self.http_refresh] = "HTTP"
        grid.setColumnStretch(0, 1)
        grid.setColumnStretch(1, 1)
        grid.setRowStretch(4, 1)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.NoFrame)
        scroll.setWidget(content)
        return scroll

    def _diagnostics_page(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.setContentsMargins(0, 0, 0, 0)
        row = QHBoxLayout()
        row.addWidget(label("Reader diagnostics", "PageTitle"))
        row.addStretch()
        self.diagnostics_refresh = button("Refresh", lambda: self.session.request("GET_DIAG", "GET_BATTERY"))
        row.addWidget(self.diagnostics_refresh)
        layout.addLayout(row)
        self.identity_text = label("Connect to read the reader identity.", "Sub")
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
        layout.addWidget(label("Connection and reader log", "PageTitle"))
        self.event = QPlainTextEdit()
        self.event.setReadOnly(True)
        self.event.setMaximumBlockCount(1000)
        layout.addWidget(self.event, 1)
        layout.addWidget(button("Clear log", self.event.clear))
        return page

    def restore(self):
        self.host.setText(self.settings.value("mubr/host", "", type=str))
        index = self.transport_choice.findText(self.settings.value("mubr/transport", "Wi-Fi TCP", type=str))
        self.transport_choice.setCurrentIndex(max(0, index))
        self.transport_selected()

    def save(self):
        self.settings.setValue("dark", self.dark)
        self.settings.setValue("color_theme", self.theme_name)
        self.settings.setValue("mubr/transport", self.transport_choice.currentText())
        self.settings.setValue("mubr/host", self.host.text().strip())
        if self.transport_choice.currentText() == "USB":
            self.settings.setValue("mubr/port", self.port.currentData() or "")
        self.settings.sync()

    def transport_selected(self, *_):
        choice = self.transport_choice.currentText()
        self.host.setVisible(choice == "Wi-Fi TCP")
        self.port.setVisible(choice != "Wi-Fi TCP")
        self.baud.setVisible(choice == "USB")
        self.scan_button.setVisible(choice != "Wi-Fi TCP")
        self.scan_button.setText("Scan Bluetooth" if choice == "Bluetooth LE" else "Refresh ports")
        self.port.clear()
        if choice == "USB":
            self.refresh_ports()
        self.connection_status.setText("Disconnected · " + ("Accept the Windows pairing prompt when connecting." if choice == "Bluetooth LE" else "Wi-Fi uses TCP port 5000." if choice == "Wi-Fi TCP" else "USB serial connection."))

    def refresh_ports(self):
        selected = self.port.currentData() or self.settings.value("mubr/port", "", type=str)
        self.port.clear()
        for info in SerialManager.ports():
            self.port.addItem(f"{info.device} · {info.description}", info.device)
        index = self.port.findData(selected)
        if index >= 0:
            self.port.setCurrentIndex(index)

    def scan(self):
        if self.transport_choice.currentText() == "Bluetooth LE":
            self.ble.scan()
        else:
            self.refresh_ports()

    def ble_devices(self, devices):
        if self.transport_choice.currentText() != "Bluetooth LE":
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
        self.scan_button.setText("Scanning…" if scanning else "Scan Bluetooth")
        if not scanning and self.transport_choice.currentText() == "Bluetooth LE" and not self.port.count():
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
        choice = self.transport_choice.currentText()
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
        self.connection_status.setText(("Connected · " + detail) if connected else "Disconnected")
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
            self.http_url.setPlaceholderText("http://IPv4:port/path")
            self.http_state.setText("Not read yet")
            self.http_configured = False
        elif ready and not self.session.supported("GET_HTTP"):
            self.http_state.setText("Requires MUBR firmware with HTTP upload support.")
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
        self.mode_button.setText("Light mode" if self.dark else "Dark mode")
        self.theme_button.setText("Theme: " + theme.label(self.theme_name))
        self.update_icons()
        self.save()

    def toggle_theme(self):
        self.dark = not self.dark
        self.apply_theme()

    def open_theme(self):
        menu = QMenu(self.theme_button)
        for name, title in theme.THEME_LABELS.items():
            action = menu.addAction(title)
            action.setCheckable(True)
            action.setChecked(name == self.theme_name)
            action.triggered.connect(lambda checked=False, value=name: self.set_color_theme(value))
        menu.exec(self.theme_button.mapToGlobal(self.theme_button.rect().bottomLeft()))

    def set_color_theme(self, name):
        self.theme_name = theme.normalize(name)
        self.apply_theme()

    def log(self, text):
        if self.closed or not hasattr(self, "event"):
            return
        self.event.appendPlainText(f"[{datetime.now():%H:%M:%S}] {text}")
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
                        title = "India (restricted plan)" if code.upper() == "03" and data.get("RF_COUNTRY") == "IN" else "M100 region " + code.upper()
                        self.region.addItem(title + " · " + code.upper(), code.upper())
                self.region_note.setText("Inventory is available only on the region plan advertised by this reader.")
            elif kind == "POWER":
                self.power.setValue(int(data["READ"]) / 100)
            elif kind == "REGION":
                code = data["_csv"][0].upper()
                self.region.setCurrentIndex(self.region.findData(code))
            elif kind == "SESSION":
                self.session_value.setValue(int(data["_csv"][0]))
            elif kind == "GEN2":
                self.target.setCurrentText(data.get("TARGET", "A"))
                self.q.setValue(int(data.get("Q", "4")))
            elif kind == "SCAN_SETTINGS":
                self.scan_mode.setCurrentText(data.get("MODE", "HOLD"))
                self.scan_time.setValue(int(data.get("TIME_MS", "1000")))
                self.delivery.setCurrentText(data.get("DELIVERY", "AUTO"))
                gpio = data.get("GPIO", "-1")
                self.trigger_state.setText(("Disabled" if gpio == "-1" else "GPIO " + gpio) + (" · Pressed" if data.get("PRESSED") == "1" else " · Released"))
            elif kind == "WIFI":
                self.wifi_enabled.setChecked(data.get("ENABLE") == "1")
                self.wifi_mode.setCurrentText(data.get("MODE", "STA"))
                if not self.ssid.hasFocus():
                    self.ssid.setText(data.get("SSID", ""))
                self.wifi_state.setText(f"{data.get('STATE', '')} · {data.get('IP', '')} · {data.get('RSSI', '')} dBm")
            elif kind == "BLE":
                self.ble_enabled.setChecked(data.get("ENABLE") == "1")
                self.ble_state.setText(("Connected" if data.get("CONNECTED") == "1" else "Disconnected") + (" · Encrypted" if data.get("ENCRYPTED") == "1" else "") + " · MTU " + data.get("MTU", "23"))
            elif kind == "SERVER":
                self.server_enabled.setChecked(data.get("ENABLE") == "1")
                self.server_host.setText(data.get("HOST", ""))
                self.server_port.setValue(int(data.get("PORT", "5000")))
                self.server_state.setText("Connected" if data.get("CONNECTED") == "1" else "Disconnected")
            elif kind == "HTTP":
                self.http_configured = data.get("CONFIGURED") == "1"
                self.http_url.setPlaceholderText("Saved on reader" if self.http_configured else "http://IPv4:port/path")
                self.http_timeout.setValue(int(data.get("TIMEOUT", "10")))
                status = data.get("STATUS", "0")
                summary = "Wi-Fi station connected" if data.get("STA") == "1" else "Wi-Fi station disconnected"
                if status != "0":
                    summary += f" · HTTP {status}"
                self.http_state.setText(summary + f" · Uploaded {data.get('UPLOADED', '0')} · Failed {data.get('FAILED', '0')} · Dropped {data.get('DROPPED', '0')}")
            elif kind == "WORK_MODE":
                self.http_mode.setCurrentText(data["_csv"][0])
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
            self.log(f"RF power must be {minimum / 100:g}–{maximum / 100:g} dBm in {step / 100:g} dBm steps.")
            return
        self.session.request(f"SET_POWER,{power}", f"SET_SESSION,{self.session_value.value()}", f"SET_GEN2,{self.target.currentText()},{self.q.value()}", "GET_POWER", "GET_SESSION", "GET_GEN2")

    def apply_region(self):
        code = self.region.currentData()
        if code:
            self.session.request("SET_REGION," + code, "GET_REGION")

    def apply_scan(self):
        self.session.request("SET_SCAN_MODE," + self.scan_mode.currentText(), f"SET_SCAN_TIME,{self.scan_time.value()}", "SET_DELIVERY," + self.delivery.currentText(), "GET_SCAN_SETTINGS")

    def apply_wifi(self):
        ssid = self.ssid.text()
        if "," in ssid or not ssid:
            self.log("Enter a network name without commas.")
            return
        password = self.password.text() or "****"
        command = f"SET_WIFI,{int(self.wifi_enabled.isChecked())},{self.wifi_mode.currentText()},{ssid},{password}"
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
                self.log("Enter the server's IPv4 address.")
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
                self.log("Use an http:// URL with a numeric IPv4 address and an optional port and path.")
                return
        elif not self.http_configured:
            self.log("Enter the HTTP upload URL.")
            return
        self.session.request(f"SET_HTTP,{url or '****'},{self.http_timeout.value()},0", "GET_HTTP")

    def apply_http_mode(self):
        if self.http_mode.currentText() == "ACTIVE" and not self.http_configured:
            self.log("Save the HTTP endpoint before switching to Active.")
            return
        self.session.request("SET_WORK_MODE," + self.http_mode.currentText(), "GET_WORK_MODE", "GET_HTTP")

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
            self.counts.setText(f"{'SCANNING' if self.session.inventory else 'IDLE'} · {self.total:,} reads · {len(self.rows):,} unique tags" + suffix)

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
