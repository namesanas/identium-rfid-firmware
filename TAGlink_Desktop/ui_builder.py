from PySide6.QtCore import Qt, QSize, QMargins
from PySide6.QtGui import QFont, QIcon, QPixmap, QPainter, QColor, QBrush, QPen
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QFormLayout, QFrame,
    QLabel, QPushButton, QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox,
    QCheckBox, QTableWidget, QTableWidgetItem, QPlainTextEdit, QTabWidget,
    QHeaderView, QStackedWidget, QStatusBar, QSizePolicy, QScrollArea,
    QSplitter, QToolButton, QMenu
)
from PySide6.QtCharts import QChart, QChartView, QLineSeries, QValueAxis

import theme

# Segoe Fluent Icons / Segoe MDL2 Assets glyphs (both ship with Windows):
# list, tag, settings, diagnostic, document, toolbox.
RAIL_ICONS = tuple(chr(code) for code in (0xE8FD, 0xE8EC, 0xE713, 0xE9D9, 0xE7C3, 0xE90F))


def _button(text, handler, object_name=None):
    button = QPushButton(text)
    if object_name:
        button.setObjectName(object_name)
    button.clicked.connect(handler)
    button.setMinimumHeight(32)
    return button


def _field(widget, maximum=360):
    widget.setMaximumWidth(maximum)
    return widget


def _table(table, headers, stretch_column=None):
    table.setColumnCount(len(headers))
    table.setHorizontalHeaderLabels(headers)
    table.setAlternatingRowColors(True)
    table.setSelectionBehavior(QTableWidget.SelectRows)
    table.setSelectionMode(QTableWidget.SingleSelection)
    table.verticalHeader().setVisible(False)
    table.verticalHeader().setDefaultSectionSize(30)
    table.setShowGrid(False)
    header = table.horizontalHeader()
    header.setDefaultAlignment(Qt.AlignLeft | Qt.AlignVCenter)
    header.setHighlightSections(False)
    for column in range(len(headers)):
        header.setSectionResizeMode(column, QHeaderView.ResizeToContents)
    if stretch_column is not None:
        header.setSectionResizeMode(stretch_column, QHeaderView.Stretch)
    return table


def _separator():
    line = QFrame()
    line.setObjectName("BarSeparator")
    line.setFrameShape(QFrame.NoFrame)
    line.setFixedWidth(1)
    return line


def _glyph_icon(glyph, color, size=22):
    pixmap = QPixmap(size * 2, size * 2)
    pixmap.fill(Qt.transparent)
    painter = QPainter(pixmap)
    font = QFont("Segoe Fluent Icons")
    if not font.exactMatch():
        font = QFont("Segoe MDL2 Assets")
    font.setPixelSize(int(size * 1.4))
    painter.setFont(font)
    painter.setPen(QColor(color))
    painter.drawText(pixmap.rect(), Qt.AlignCenter, glyph)
    painter.end()
    pixmap.setDevicePixelRatio(2.0)
    return QIcon(pixmap)


def refresh_theme(window, dark, theme_name=None):
    """Theme parts style sheets cannot reach: rail icons and the chart."""
    theme_name = theme.normalize(theme_name or getattr(window, 'theme_name', theme.DEFAULT_THEME))
    c = theme.colors(dark, theme_name)
    for button, glyph in zip(window.nav_buttons, RAIL_ICONS):
        button.setIcon(_glyph_icon(glyph, c['text_strong'] if button.isChecked() else c['muted']))
    chart = window.vswr_chart
    chart.setBackgroundBrush(QBrush(QColor(c['surface'])))
    chart.setBackgroundPen(QPen(Qt.NoPen))
    chart.setPlotAreaBackgroundVisible(False)
    chart.legend().setLabelColor(QColor(c['muted']))
    for axis in (window.vswr_x_axis, window.vswr_y_axis):
        axis.setLabelsColor(QColor(c['muted']))
        axis.setTitleBrush(QBrush(QColor(c['muted'])))
        axis.setGridLineColor(QColor(c['border']))
        axis.setLinePenColor(QColor(c['border_strong']))
    window.vswr_series.setPen(QPen(QColor(c['accent_border']), 2.2))
    threshold_pen = QPen(QColor(c['danger_text']), 1.4)
    threshold_pen.setStyle(Qt.DashLine)
    window.vswr_threshold_series.setPen(threshold_pen)
    # Sun = switch to light, moon = switch to dark.
    window.theme_btn.setIcon(_glyph_icon(chr(0xE706) if dark else chr(0xE708), c['text'], 18))
    window.theme_btn.setToolTip("Switch to light theme" if dark else "Switch to dark theme")
    window.theme_choice_btn.setText(f"Theme: {theme.label(theme_name)}")
    window.theme_choice_btn.setToolTip(f"Color theme: {theme.label(theme_name)}")
    for name, action in window.theme_actions.items():
        action.setChecked(name == theme_name)


def set_vswr_x_range(window, low, high):
    """Frequency axis with ticks on round kHz values."""
    if high <= low:
        high = low + 1
    axis = window.vswr_x_axis
    axis.setRange(low, high)
    interval = next((step for step in (250, 500, 1000, 2000, 2500, 5000, 10000, 20000, 25000, 50000, 100000)
                     if (high - low) / step <= 6), 200000)
    axis.setTickType(QValueAxis.TicksDynamic)
    axis.setTickAnchor(-(-low // interval) * interval)
    axis.setTickInterval(interval)


def _spacer():
    spacer = QWidget()
    spacer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
    return spacer


def _card(window, title=None):
    frame = window.card()
    layout = QVBoxLayout(frame)
    layout.setContentsMargins(16, 14, 16, 14)
    layout.setSpacing(10)
    if title:
        layout.addWidget(window.sec(title))
    return frame, layout


def build_ui(window):
    root = QWidget()
    root.setObjectName("AppRoot")
    outer = QVBoxLayout(root)
    outer.setContentsMargins(16, 12, 16, 12)
    outer.setSpacing(12)

    # Compact single-row header.
    top = QFrame()
    top.setObjectName("TopBar")
    top_layout = QHBoxLayout(top)
    top_layout.setContentsMargins(14, 10, 14, 10)
    top_layout.setSpacing(6)
    brand = QLabel("TAGlink")
    brand.setObjectName("Title")
    top_layout.addWidget(brand)
    top_layout.addSpacing(8)
    window.port = QComboBox()
    window.port.setMinimumWidth(110)
    window.port.setToolTip("Reader connection: USB / serial COM port or Ethernet")
    window.port.setMaximumWidth(170)
    top_layout.addWidget(window.port)
    window.baud = QComboBox()
    window.baud.setMinimumWidth(88)
    window.baud.setMaximumWidth(92)
    for value in (115200, 57600, 38400, 19200, 9600):
        window.baud.addItem(str(value), value)
    top_layout.addWidget(window.baud)
    window.connect = _button("Connect", window.toggle, "Accent")
    window.connect.setMinimumWidth(window.connect.fontMetrics().horizontalAdvance("Disconnect") + 28)
    top_layout.addWidget(window.connect)
    window.conn = QLabel("● Disconnected")
    window.conn.setObjectName("Connection")
    window.conn.setMinimumWidth(window.conn.fontMetrics().horizontalAdvance("● Disconnected") + 8)
    top_layout.addWidget(window.conn)
    top_layout.addWidget(_separator())

    window.multi = _button("▶  Start", window.multi_poll, "Primary")
    window.multi.setToolTip("Start continuous inventory (Multi Poll)")
    window.stop = _button("■  Stop", window.stop_inv, "Danger")
    window.stop.setToolTip("Stop inventory")
    window.single = _button("Single", window.single_poll)
    window.single.setToolTip("One-shot read (Single Poll)")
    window.sync = _button("Sync", window.sync_poll)
    window.sync.setToolTip("Synchronous inventory (Sync Poll)")
    for button in (window.multi, window.stop, window.single, window.sync):
        button.setMinimumWidth(max(60, button.sizeHint().width()))
        top_layout.addWidget(button)
    top_layout.addWidget(_separator())

    window.kv = []
    for label, value in (
        ("Reader", "Offline"), ("Inventory", "IDLE"), ("Reads", "0"),
        ("Unique", "0"), ("Rate", "0.0/s"), ("Temp", "--")
    ):
        chip = QFrame()
        chip.setObjectName("MetricChip")
        chip_layout = QVBoxLayout(chip)
        chip_layout.setContentsMargins(6, 2, 6, 2)
        chip_layout.setSpacing(0)
        value_label = QLabel(value)
        value_label.setObjectName("MetricValue")
        caption = QLabel(label)
        caption.setObjectName("MetricCaption")
        chip_layout.addWidget(value_label)
        chip_layout.addWidget(caption)
        chip.setMinimumWidth(48)
        chip.setMaximumWidth(76)
        top_layout.addWidget(chip)
        window.kv.append(value_label)
        if label == "Reader":
            chip.hide()   # the connection label already shows this

    mode_chip = QFrame(); mode_chip.setObjectName("MetricChip")
    mode_layout = QVBoxLayout(mode_chip); mode_layout.setContentsMargins(6, 2, 6, 2); mode_layout.setSpacing(0)
    window.work_mode_value = QLabel("--"); window.work_mode_value.setObjectName("MetricValue")
    mode_caption = QLabel("Mode"); mode_caption.setObjectName("MetricCaption")
    mode_layout.addWidget(window.work_mode_value); mode_layout.addWidget(mode_caption)
    mode_chip.setToolTip("Reader work mode: Passive answers this tool only; Active runs the configured autonomous inventory and upload")
    top_layout.addWidget(mode_chip)
    window.return_passive = _button("Return to passive", window.return_to_passive, "Accent")
    window.return_passive.setToolTip("Stop the reader's autonomous inventory and switch it to passive mode")
    window.return_passive.hide()   # shown while the reader is in active mode
    top_layout.addWidget(window.return_passive)

    top_layout.addWidget(_spacer())
    window.theme_choice_btn = _button("Theme", lambda: window.theme_menu.popup(window.theme_choice_btn.mapToGlobal(window.theme_choice_btn.rect().bottomLeft())))
    window.theme_choice_btn.setMinimumWidth(94)
    window.theme_choice_btn.setMaximumWidth(112)
    window.theme_menu = QMenu(window.theme_choice_btn)
    window.theme_actions = {}
    for name, label in theme.THEME_LABELS.items():
        action = window.theme_menu.addAction(label)
        action.setCheckable(True)
        action.triggered.connect(lambda checked=False, selected=name: window.set_color_theme(selected))
        window.theme_actions[name] = action
    top_layout.addWidget(window.theme_choice_btn)
    window.theme_btn = _button("", lambda: window.set_theme(not window.dark))
    window.theme_btn.setObjectName("IconButton")
    window.theme_btn.setFixedSize(36, 32)
    window.theme_btn.setIconSize(QSize(18, 18))
    top_layout.addWidget(window.theme_btn)
    outer.addWidget(top)

    body = QHBoxLayout()
    body.setSpacing(12)
    rail = QFrame()
    rail.setObjectName("NavRail")
    rail.setFixedWidth(90)
    rail_layout = QVBoxLayout(rail)
    rail_layout.setContentsMargins(6, 8, 6, 8)
    rail_layout.setSpacing(4)

    window.nav_buttons = []
    nav_items = [
        ("◉", "Inventory", "Inventory and reader control"),
        ("✎", "Tags", "Tag read, write, lock and kill"),
        ("⚙", "Settings", "Reader, RF, network and peripherals"),
        ("⌁", "Diagnostics", "VSWR and reader information"),
        ("≡", "Log", "Event log, raw serial and command console"),
        ("✦", "Tools", "Read-rate tuner, stock check and batch encoding"),
    ]
    for index, (icon, label, tooltip) in enumerate(nav_items):
        button = QToolButton()
        button.setObjectName("RailButton")
        button.setCheckable(True)
        button.setToolTip(tooltip)
        button.setText(label)
        button.setIconSize(QSize(22, 22))
        button.setToolButtonStyle(Qt.ToolButtonTextUnderIcon)
        button.setFixedSize(76, 60)
        button.clicked.connect(lambda checked=False, page=index: window.select_feature(page))
        rail_layout.addWidget(button)
        window.nav_buttons.append(button)
    window.rfid_nav_button = window.nav_buttons[0]
    # Unread-error dot on the Log button.
    window.log_badge = QLabel(window.nav_buttons[4])
    window.log_badge.setObjectName("LogBadge")
    window.log_badge.setFixedSize(10, 10)
    window.log_badge.move(54, 8)
    window.log_badge.setToolTip("New errors in the log")
    window.log_badge.hide()
    rail_layout.addStretch()
    body.addWidget(rail)

    window.stack = QStackedWidget()
    window.content_layout = QVBoxLayout()
    window.content_layout.setContentsMargins(0, 0, 0, 0)
    window.content_layout.addWidget(window.stack)
    body.addLayout(window.content_layout, 1)
    outer.addLayout(body, 1)
    window.setCentralWidget(root)
    window.setStatusBar(QStatusBar())

    # Inventory workspace.
    inventory_page = QWidget()
    inventory_layout = QVBoxLayout(inventory_page)
    inventory_layout.setContentsMargins(0, 0, 0, 0)
    inventory_layout.setSpacing(12)
    inventory_header = QHBoxLayout()
    inventory_title = QLabel("Inventory")
    inventory_title.setObjectName("PageTitle")
    inventory_header.addWidget(inventory_title)
    inventory_header.addWidget(QLabel("Live tag reads"))
    inventory_header.addWidget(_spacer())
    inventory_header.addWidget(QLabel("Filter"))
    window.filter_edit = QLineEdit()
    window.filter_edit.setPlaceholderText("EPC contains…")
    window.filter_edit.setMinimumWidth(180)
    window.filter_edit.setMaximumWidth(300)
    window.filter_edit.textChanged.connect(window.apply_filter)
    inventory_header.addWidget(window.filter_edit)
    window.filter_exact_cb = QCheckBox("Exact")
    window.filter_exact_cb.toggled.connect(window.apply_filter)
    inventory_header.addWidget(window.filter_exact_cb)
    inventory_header.addWidget(_button("Clear", window.clear_inv))
    inventory_header.addWidget(_button("Export CSV", window.export_csv))
    inventory_header.addWidget(_button("Refresh", window.request_info))
    inventory_layout.addLayout(inventory_header)
    table_card, table_layout = _card(window)
    table_bar = QHBoxLayout()
    window.hint = QLabel("Waiting for TAG frames…")
    window.hint.setObjectName("Sub")
    table_bar.addWidget(window.hint)
    table_bar.addWidget(_spacer())
    table_layout.addLayout(table_bar)
    window.table = QTableWidget(0, 6)
    window.table.setObjectName("InventoryTable")
    _table(window.table, ["Time", "EPC", "RSSI (dBm)", "Antenna", "Frequency (kHz)", "Reads"], 1)
    window.table.itemSelectionChanged.connect(window.use_selected_epc)
    window.table.cellDoubleClicked.connect(lambda row, column: window.select_feature(1))
    table_layout.addWidget(window.table, 1)
    inventory_layout.addWidget(table_card, 1)

    analytics_toggle = QToolButton()
    analytics_toggle.setObjectName("Disclosure")
    analytics_toggle.setText("▸  Analytics")
    analytics_toggle.setCheckable(True)
    analytics_panel = QFrame()
    analytics_panel.setObjectName("AnalyticsPanel")
    analytics_panel.hide()
    analytics_layout = QHBoxLayout(analytics_panel)
    analytics_layout.setContentsMargins(12, 10, 12, 10)
    analytics_metrics = QGridLayout()
    analytics_metrics.setHorizontalSpacing(18)
    for row, label in enumerate(("Total reads", "Unique tags", "Read rate", "Elapsed")):
        analytics_metrics.addWidget(QLabel(label), row, 0)
    window.analytics_total = QLabel("0")
    window.analytics_unique = QLabel("0")
    window.analytics_rate = QLabel("0.0/s")
    window.analytics_elapsed = QLabel("0 s")
    for row, widget in enumerate((window.analytics_total, window.analytics_unique, window.analytics_rate, window.analytics_elapsed)):
        widget.setObjectName("Value")
        analytics_metrics.addWidget(widget, row, 1)
    analytics_layout.addLayout(analytics_metrics)
    window.antenna_stats_table = QTableWidget(0, 2)
    _table(window.antenna_stats_table, ["Antenna", "Reads"], 1)
    window.frequency_stats_table = QTableWidget(0, 2)
    _table(window.frequency_stats_table, ["Frequency (kHz)", "Reads"], 1)
    analytics_layout.addWidget(window.antenna_stats_table, 1)
    analytics_layout.addWidget(window.frequency_stats_table, 1)
    analytics_toggle.toggled.connect(lambda checked: (analytics_panel.setVisible(checked), analytics_toggle.setText("▾  Analytics" if checked else "▸  Analytics")))
    inventory_layout.addWidget(analytics_toggle)
    inventory_layout.addWidget(analytics_panel)
    window.stack.addWidget(inventory_page)

    # Tag operations workspace with one shared selected-tag header.
    tag_page = QWidget()
    tag_layout = QVBoxLayout(tag_page)
    tag_layout.setContentsMargins(0, 0, 0, 0)
    tag_layout.setSpacing(12)
    selected_card, selected_layout = _card(window, "Selected tag")
    selected_row = QHBoxLayout()
    selected_row.addWidget(QLabel("EPC"))
    window.selected_epc_display = QLineEdit()
    window.selected_epc_display.setReadOnly(True)
    window.selected_epc_display.setPlaceholderText("Select a row in Inventory")
    selected_row.addWidget(window.selected_epc_display, 1)
    selected_row.addWidget(_button("Use selected row", window.use_selected_epc))
    selected_layout.addLayout(selected_row)
    tag_layout.addWidget(selected_card)
    window.tag_tabs = QTabWidget()
    tag_layout.addWidget(window.tag_tabs, 1)

    # Backend target widgets remain real widgets because main.py reads them.
    window.read_target_epc = QLineEdit()
    window.target_epc = QLineEdit()
    window.lock_target_epc = QLineEdit()
    window.kill_target_epc = QLineEdit()
    window.epc_edit_old = QLineEdit()
    for hidden in (window.read_target_epc, window.target_epc, window.lock_target_epc, window.kill_target_epc, window.epc_edit_old):
        hidden.hide()

    read_tab = QWidget()
    read_layout = QFormLayout(read_tab)
    read_layout.setContentsMargins(16, 16, 16, 16)
    read_layout.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow)
    window.read_bank = QComboBox()
    for label, value in (("TID (2)", 2), ("USER (3)", 3), ("EPC (1)", 1), ("RESERVED (0)", 0)):
        window.read_bank.addItem(label, value)
    window.read_address = QSpinBox(); window.read_address.setRange(0, 2147483647)
    window.read_words = QSpinBox(); window.read_words.setRange(1, 96); window.read_words.setValue(2)
    window.read_result = QLineEdit(); window.read_result.setReadOnly(True); window.read_result.setPlaceholderText("Read data appears here")
    read_layout.addRow("Memory bank", _field(window.read_bank))
    read_layout.addRow("Start word", _field(window.read_address))
    read_layout.addRow("Word count", _field(window.read_words))
    read_actions = QHBoxLayout()
    window.read_use_selected = QPushButton("Use selected EPC"); window.read_use_selected.hide()
    window.read_tag_btn = _button("Read tag", window.read_tag, "Primary")
    read_actions.addWidget(window.read_tag_btn); read_actions.addStretch()
    read_layout.addRow("", read_actions)
    read_layout.addRow("Last data", _field(window.read_result))
    window.tag_tabs.addTab(read_tab, "Read")

    write_tab = QWidget()
    write_layout = QFormLayout(write_tab)
    write_layout.setContentsMargins(16, 16, 16, 16)
    write_layout.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow)
    window.write_operation = QComboBox()
    window.write_operation.addItem("Tag data", "TAG_DATA")
    window.write_operation.addItem("Edit EPC", "EPC_EDIT")
    window.write_operation.currentIndexChanged.connect(window.update_write_mode)
    write_layout.addRow("Operation", _field(window.write_operation))
    window.write_target_label = QLabel("Target EPC"); window.write_target_label.hide()
    window.write_bank_label = QLabel("Memory bank"); window.write_bank_label.hide()
    window.write_address_label = QLabel("Start word"); window.write_address_label.hide()
    window.write_data_label = QLabel("Write data"); window.write_data_label.hide()
    window.write_access_password_label = QLabel("Access password"); window.write_access_password_label.hide()
    window.epc_edit_old_label = QLabel("Current EPC"); window.epc_edit_old_label.hide()
    window.epc_edit_new_label = QLabel("New EPC")
    window.epc_edit_new = QLineEdit(); window.epc_edit_new.setPlaceholderText("Hex EPC, even number of digits")
    window.write_bank = QComboBox()
    for label, value in (("USER (3)", 3), ("EPC (1)", 1), ("TID (2)", 2), ("RESERVED (0)", 0)):
        window.write_bank.addItem(label, value)
    window.write_address = QSpinBox(); window.write_address.setRange(0, 2147483647)
    window.write_data = QLineEdit(); window.write_data.setPlaceholderText("Hex data, max 64 bytes")
    window.write_access_password = QLineEdit("00000000"); window.write_access_password.setEchoMode(QLineEdit.Password); window.write_access_password.setMaxLength(8)
    write_layout.addRow(window.epc_edit_new_label, _field(window.epc_edit_new))
    write_layout.addRow("Memory bank", _field(window.write_bank))
    write_layout.addRow("Start word", _field(window.write_address))
    write_layout.addRow("Write data", _field(window.write_data))
    write_layout.addRow("Access password", _field(window.write_access_password))
    write_actions = QHBoxLayout()
    window.use_selected = QPushButton("Use selected EPC"); window.use_selected.hide()
    window.write_tag_btn = _button("Write", window.write_selected_operation, "Primary")
    write_actions.addWidget(window.write_tag_btn); write_actions.addStretch()
    write_layout.addRow("", write_actions)
    window.tag_tabs.addTab(write_tab, "Write")

    lock_tab = QWidget()
    lock_form = QFormLayout(lock_tab); lock_form.setContentsMargins(16, 16, 16, 16)
    window.lock_access_password = QLineEdit("00000000"); window.lock_access_password.setEchoMode(QLineEdit.Password); window.lock_access_password.setMaxLength(8)
    window.lock_mask = QLineEdit("0000"); window.lock_mask.setMaxLength(4)
    window.lock_action = QLineEdit("0000"); window.lock_action.setMaxLength(4)
    lock_form.addRow("Access password", _field(window.lock_access_password))
    lock_form.addRow("Lock mask (hex)", _field(window.lock_mask))
    lock_form.addRow("Lock action (hex)", _field(window.lock_action))
    lock_actions = QHBoxLayout()
    window.lock_use_selected = QPushButton("Use selected EPC"); window.lock_use_selected.hide()
    window.lock_tag_btn = _button("Lock tag", window.lock_tag, "Primary")
    lock_actions.addWidget(window.lock_tag_btn); lock_actions.addStretch()
    lock_form.addRow("", lock_actions)
    window.tag_tabs.addTab(lock_tab, "Lock")

    kill_tab = QWidget()
    kill_form = QFormLayout(kill_tab); kill_form.setContentsMargins(16, 16, 16, 16)
    window.kill_password = QLineEdit("00000000"); window.kill_password.setEchoMode(QLineEdit.Password); window.kill_password.setMaxLength(8)
    kill_form.addRow("Kill password", _field(window.kill_password))
    kill_help = QLabel("Kill is irreversible on a properly configured tag."); kill_help.setObjectName("WarningText")
    kill_form.addRow("", kill_help)
    kill_actions = QHBoxLayout()
    window.kill_use_selected = QPushButton("Use selected EPC"); window.kill_use_selected.hide()
    window.kill_tag_btn = _button("Kill tag", window.kill_tag, "Danger")
    kill_actions.addWidget(window.kill_tag_btn); kill_actions.addStretch()
    kill_form.addRow("", kill_actions)
    window.tag_tabs.addTab(kill_tab, "Kill")
    window.stack.addWidget(tag_page)

    # Settings workspace: responsive two-column cards.
    settings_page = QWidget()
    settings_scroll = QScrollArea(); settings_scroll.setWidgetResizable(True); settings_scroll.setFrameShape(QFrame.NoFrame)
    settings_content = QWidget(); settings_content.setObjectName("ScrollContent"); settings_grid = QGridLayout(settings_content); settings_grid.setContentsMargins(0, 0, 0, 0); settings_grid.setSpacing(12); settings_scroll.setWidget(settings_content)
    settings_page_layout = QVBoxLayout(settings_page); settings_page_layout.setContentsMargins(0, 0, 0, 0); settings_page_layout.addWidget(settings_scroll)

    reader_card = QFrame(); reader_card.setObjectName("Card"); reader_form = QFormLayout(reader_card); reader_form.setContentsMargins(16, 14, 16, 14); reader_form.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow); reader_form.addRow(window.sec("Reader"))
    window.work_mode_passive = QPushButton("Passive"); window.work_mode_active = QPushButton("Active")
    window.work_mode_passive.setToolTip("Reader answers host commands only (this GUI, Reader Manager)")
    window.work_mode_active.setToolTip("Reader runs the configured autonomous inventory and upload (HTTP / TCP)")
    work_mode_row = QHBoxLayout(); work_mode_row.setSpacing(0)
    for button, mode, side in ((window.work_mode_passive, "PASSIVE", "SegmentLeft"), (window.work_mode_active, "ACTIVE", "SegmentRight")):
        button.setObjectName(side); button.setCheckable(True); button.setMinimumWidth(96); button.setMinimumHeight(32)
        button.clicked.connect(lambda checked=False, m=mode: window.set_work_mode(m))
        work_mode_row.addWidget(button)
    work_mode_row.addStretch()
    reader_form.addRow("Work mode", work_mode_row)
    window.region = QLineEdit("01"); window.region.setMaxLength(2)
    window.tx = QSpinBox(); window.tx.setRange(1, 255); window.tx.setValue(1)
    window.rx = QSpinBox(); window.rx.setRange(1, 255); window.rx.setValue(1)
    window.power = QDoubleSpinBox(); window.power.setRange(0.0, 33.0); window.power.setDecimals(2); window.power.setSingleStep(0.25); window.power.setValue(30.0); window.power.setSuffix(" dBm")
    window.session = QSpinBox(); window.session.setRange(0, 3)
    reader_form.addRow("Region (hex)", _field(window.region, 150)); reader_form.addRow("TX antenna", _field(window.tx, 150)); reader_form.addRow("RX antenna", _field(window.rx, 150)); reader_form.addRow("Power", _field(window.power, 180)); reader_form.addRow("Session", _field(window.session, 150))
    reader_actions = QHBoxLayout()
    for label, handler in (("Set power", lambda: window.enqueue(f"SET_POWER,{int(round(window.power.value() * 100.0))}")), ("Set antenna", lambda: window.enqueue(f"SET_ANTENNA,{window.tx.value()},{window.rx.value()}")), ("Set region", window.set_region), ("Set session", lambda: window.enqueue(f"SET_SESSION,{window.session.value()}"))):
        reader_actions.addWidget(_button(label, handler))
    reader_form.addRow("", reader_actions)

    rf_card = QFrame(); rf_card.setObjectName("Card"); rf_form = QFormLayout(rf_card); rf_form.setContentsMargins(16, 14, 16, 14); rf_form.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow); rf_form.addRow(window.sec("RF & Gen2"))
    window.antenna_count = QSpinBox(); window.antenna_count.setRange(1, 64); window.antenna_count.setValue(1); window.antenna_count.setReadOnly(True)
    window.inventory_antennas = QLineEdit("1")
    window.antenna_selectors = []
    antenna_picker = QHBoxLayout()
    for number in range(1, 9):
        selector = QCheckBox(f"A{number}")
        selector.setChecked(number == 1)
        window.antenna_selectors.append(selector)
        antenna_picker.addWidget(selector)
    antenna_picker.addStretch()
    def sync_inventory_antennas():
        selected = [str(index + 1) for index, selector in enumerate(window.antenna_selectors) if selector.isChecked() and selector.isEnabled()]
        window.inventory_antennas.setText(','.join(selected))
    def update_antenna_picker(limit):
        for index, selector in enumerate(window.antenna_selectors, 1):
            selector.setEnabled(index <= limit)
            if index > limit:
                selector.setChecked(False)
        sync_inventory_antennas()
    for selector in window.antenna_selectors:
        selector.toggled.connect(sync_inventory_antennas)
    window.antenna_count.valueChanged.connect(update_antenna_picker)
    update_antenna_picker(window.antenna_count.value())
    window.gen2_target = QComboBox(); window.gen2_target.addItems(["A", "B", "A/B"])
    window.gen2_q = QSpinBox(); window.gen2_q.setRange(-1, 15); window.gen2_q.setValue(-1); window.gen2_q.setSpecialValueText("Auto")
    # RF profile = module gen2tagEncoding (Reader Manager's Profile list); 0 leaves the module as it is.
    window.rf_profile = QComboBox()
    for label, value in (("Module default", 0), ("RF mode 1", 101), ("RF mode 3", 103), ("RF mode 5", 105), ("RF mode 7", 107), ("RF mode 11 (reading count)", 111), ("RF mode 12", 112), ("RF mode 13 (reading distance)", 113), ("RF mode 15", 115), ("RF mode 103", 203), ("RF mode 120", 220), ("RF mode 345", 45)):
        window.rf_profile.addItem(label, value)
    window.fast_mode = QCheckBox("Fast mode"); window.fast_mode.setToolTip("Session 0 + Target A/B: tags are reported continuously instead of about once per second.")
    window.fast_mode.toggled.connect(lambda checked: checked and (window.session.setValue(0), window.gen2_target.setCurrentText("A/B")))
    # Hopping is not implemented in the firmware; SET_GEN2 always sends 0.
    window.frequency_hop = QCheckBox("Frequency hopping"); window.frequency_hop.setVisible(False)
    rf_form.addRow("Detected antennas", _field(window.antenna_count, 150)); rf_form.addRow("Inventory antennas", _field(window.inventory_antennas)); rf_form.addRow("Select antennas", antenna_picker); rf_form.addRow("Gen2 target", _field(window.gen2_target)); rf_form.addRow("Gen2 Q", _field(window.gen2_q, 150)); rf_form.addRow("RF profile", _field(window.rf_profile))
    mode_row = QHBoxLayout(); mode_row.addWidget(window.fast_mode); mode_row.addWidget(window.frequency_hop); mode_row.addStretch(); rf_form.addRow("Reader mode", mode_row)
    rf_actions = QHBoxLayout(); window.rf_refresh = _button("Discover capabilities", window.request_capabilities); window.rf_apply = _button("Apply RF settings", window.apply_rf_settings, "Primary"); rf_actions.addWidget(window.rf_refresh); rf_actions.addWidget(window.rf_apply); rf_form.addRow("", rf_actions)

    network_card = QFrame(); network_card.setObjectName("Card"); network_form = QFormLayout(network_card); network_form.setContentsMargins(16, 14, 16, 14); network_form.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow); network_form.addRow(window.sec("Network & HTTP"))
    window.net_ip = QLineEdit("192.168.150.50"); window.net_mask = QLineEdit("255.255.255.0"); window.net_gateway = QLineEdit("192.168.150.1"); window.net_dns = QLineEdit("8.8.8.8")
    window.net_config_port = QSpinBox(); window.net_config_port.setRange(1, 65535); window.net_config_port.setValue(8080)
    for label, widget in (("Reader IP", window.net_ip), ("Netmask", window.net_mask), ("Gateway", window.net_gateway), ("DNS", window.net_dns), ("Config port", window.net_config_port)):
        network_form.addRow(label, _field(widget))
    window.http_url = QLineEdit(); window.http_url.setPlaceholderText("http://host:port/path")
    window.http_timeout = QSpinBox(); window.http_timeout.setRange(0, 65535); window.http_timeout.setValue(10); window.http_timeout.setSuffix(" s")
    window.http_active = QCheckBox("Reader uploads autonomously"); window.http_start_boot = QCheckBox("Start inventory at boot")
    network_form.addRow("HTTP URL", _field(window.http_url)); network_form.addRow("HTTP timeout", _field(window.http_timeout)); window.http_active.hide(); network_form.addRow("", window.http_start_boot)
    network_buttons = QHBoxLayout(); network_buttons.addWidget(_button("Discover readers", window.discover_readers)); apply_http = _button("Apply HTTP", window.apply_http_config, "Primary"); apply_http.setToolTip("Save the HTTP upload settings for active mode. The work mode is not changed (see Settings > Reader)."); network_buttons.addWidget(apply_http); network_form.addRow("", network_buttons)
    window.discovery_table = QTableWidget(0, 6); _table(window.discovery_table, ["IP", "MAC", "Port", "Board", "Firmware", "Mode"], 5); window.discovery_table.itemSelectionChanged.connect(window.use_discovered_reader)
    window.discovery_table.setMinimumHeight(110); window.discovery_table.setToolTip("Readers found by Discover readers; select one to use its IP")
    network_form.addRow(window.discovery_table)

    wifi_card = QFrame(); wifi_card.setObjectName("Card"); wifi_form = QFormLayout(wifi_card); wifi_form.setContentsMargins(16, 14, 16, 14); wifi_form.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow); wifi_form.addRow(window.sec("WiFi"))
    window.wifi_status = QLabel("Not read yet"); window.wifi_status.setObjectName("Sub"); window.wifi_status.setWordWrap(True); wifi_form.addRow("Status", window.wifi_status)
    window.wifi_host = QLineEdit(); window.wifi_host.setPlaceholderText("Reader WiFi IP, e.g. 192.168.1.17"); window.wifi_host.setToolTip("Address used when the connection is set to WiFi. Filled by Discover readers and by the reader's WiFi status.")
    wifi_form.addRow("Reader WiFi IP", _field(window.wifi_host))
    window.wifi_enable = QCheckBox("WiFi enabled")
    window.wifi_mode = QComboBox(); window.wifi_mode.addItem("Join a network (station)", "STA"); window.wifi_mode.addItem("Reader's own access point", "AP")
    window.wifi_ssid = QLineEdit(); window.wifi_ssid.setPlaceholderText("Network name (2.4 GHz)")
    window.wifi_password = QLineEdit(); window.wifi_password.setEchoMode(QLineEdit.Password); window.wifi_password.setPlaceholderText("Leave empty to keep the stored password")
    window.wifi_open = QCheckBox("Open network (no password)")
    wifi_form.addRow("", window.wifi_enable); wifi_form.addRow("Mode", _field(window.wifi_mode)); wifi_form.addRow("Network name", _field(window.wifi_ssid)); wifi_form.addRow("Password", _field(window.wifi_password)); wifi_form.addRow("", window.wifi_open)
    window.upload_interface = QComboBox(); window.upload_interface.addItem("Auto (Ethernet first)", "AUTO"); window.upload_interface.addItem("Ethernet", "ETH"); window.upload_interface.addItem("WiFi", "WIFI"); window.upload_interface.setToolTip("Interface used for active-mode uploads (TCP / HTTP)")
    wifi_form.addRow("Upload over", _field(window.upload_interface))
    wifi_buttons = QHBoxLayout(); window.wifi_refresh = _button("Refresh", lambda: window.request("GET_WIFI")); window.wifi_apply = _button("Apply WiFi", window.apply_wifi_settings, "Primary"); window.upload_if_apply = _button("Apply upload interface", window.apply_upload_interface)
    wifi_buttons.addWidget(window.wifi_refresh); wifi_buttons.addWidget(window.wifi_apply); wifi_buttons.addWidget(window.upload_if_apply); wifi_form.addRow("", wifi_buttons)

    peripherals_card = QFrame(); peripherals_card.setObjectName("Card"); peripherals_grid = QGridLayout(peripherals_card); peripherals_grid.setContentsMargins(16, 14, 16, 14); peripherals_grid.addWidget(window.sec("Peripherals"), 0, 0, 1, 4)
    window.gpi_labels = []
    for index in range(4):
        label = QLabel(f"GPI {index + 1}: –"); label.setObjectName("GpiChip"); label.setAlignment(Qt.AlignCenter); window.gpi_labels.append(label); peripherals_grid.addWidget(label, 1, index)
    window.gpo_state = []; window.gpo_duration = []; window.gpo_buttons = []
    for index in range(4):
        peripherals_grid.addWidget(QLabel(f"GPO {index + 1}"), 2 + index, 0)
        state = QComboBox(); state.addItem("Low", 0); state.addItem("High", 1)
        duration = QSpinBox(); duration.setRange(0, 86400); duration.setSuffix(" s")
        button = _button("Set", lambda checked=False, n=index + 1: window.set_gpo(n))
        window.gpo_state.append(state); window.gpo_duration.append(duration); window.gpo_buttons.append(button)
        peripherals_grid.addWidget(state, 2 + index, 1); peripherals_grid.addWidget(duration, 2 + index, 2); peripherals_grid.addWidget(button, 2 + index, 3)
    peripherals_grid.addWidget(QLabel("Buzzer"), 6, 0)
    window.buzzer_duration = QSpinBox(); window.buzzer_duration.setRange(50, 10000); window.buzzer_duration.setValue(250); window.buzzer_duration.setSuffix(" ms")
    window.buzzer_tone = QSpinBox(); window.buzzer_tone.setRange(100, 10000); window.buzzer_tone.setValue(2000); window.buzzer_tone.setSuffix(" Hz")
    window.buzzer_button = _button("Sound buzzer", window.sound_buzzer); peripherals_grid.addWidget(window.buzzer_duration, 6, 1); peripherals_grid.addWidget(window.buzzer_tone, 6, 2); peripherals_grid.addWidget(window.buzzer_button, 6, 3)
    peripherals_grid.addWidget(_button("Refresh GPI", lambda: window.request("GET_GPI")), 7, 0, 1, 4)
    peripherals_grid.setVerticalSpacing(10)
    peripherals_grid.setRowStretch(8, 1)   # keep rows packed at the top of the card

    interface_card = QFrame(); interface_card.setObjectName("Card"); interface_form = QFormLayout(interface_card); interface_form.setContentsMargins(16, 14, 16, 14); interface_form.addRow(window.sec("Interface"))
    window.dedupe = QCheckBox("One row per EPC (always)"); window.dedupe.setChecked(True); window.dedupe.setEnabled(False)
    window.auto = QCheckBox("Auto-scroll inventory"); window.auto.setChecked(True); window.clear_start = QCheckBox("Clear inventory on Start"); window.reconnect = QCheckBox("Auto reconnect"); window.reconnect.setChecked(True)
    interface_form.addRow("", window.dedupe); interface_form.addRow("", window.auto); interface_form.addRow("", window.clear_start); interface_form.addRow("", window.reconnect)
    settings_grid.addWidget(reader_card, 0, 0); settings_grid.addWidget(rf_card, 0, 1); settings_grid.addWidget(network_card, 1, 0); settings_grid.addWidget(peripherals_card, 1, 1); settings_grid.addWidget(wifi_card, 2, 0); settings_grid.addWidget(interface_card, 2, 1); settings_grid.setColumnStretch(0, 1); settings_grid.setColumnStretch(1, 1); settings_grid.setRowStretch(3, 1)
    window.stack.addWidget(settings_page)

    # Diagnostics workspace with a live QtCharts plot.
    diagnostics_page = QWidget(); diagnostics_layout = QVBoxLayout(diagnostics_page); diagnostics_layout.setContentsMargins(0, 0, 0, 0); diagnostics_layout.setSpacing(12)
    diag_header = QHBoxLayout(); diag_title = QLabel("Diagnostics"); diag_title.setObjectName("PageTitle"); diag_header.addWidget(diag_title); diag_header.addWidget(QLabel("Antenna health and module information")); diag_header.addWidget(_spacer())
    window.support_report_btn = _button("Save support report", window.save_support_report, "Primary"); window.support_report_btn.setToolTip("Save one text file with the reader's settings and health, this PC's details and the logs, to send to Identium support")
    diag_header.addWidget(window.support_report_btn); diagnostics_layout.addLayout(diag_header)
    # The page scrolls on short screens rather than squeezing the VSWR chart.
    diag_scroll = QScrollArea(); diag_scroll.setWidgetResizable(True); diag_scroll.setFrameShape(QFrame.NoFrame)
    diag_content = QWidget(); diag_content.setObjectName("ScrollContent"); diag_content_layout = QVBoxLayout(diag_content); diag_content_layout.setContentsMargins(0, 0, 0, 0); diag_content_layout.setSpacing(12); diag_scroll.setWidget(diag_content)
    vswr_card = QFrame(); vswr_card.setObjectName("Card"); vswr_layout = QVBoxLayout(vswr_card); vswr_layout.setContentsMargins(16, 14, 16, 14); vswr_layout.setSpacing(10); vswr_layout.addWidget(window.sec("VSWR sweep"))
    window.vswr_antenna = QSpinBox(); window.vswr_antenna.setRange(1, 64); window.vswr_antenna.setValue(1)
    window.vswr_start = QSpinBox(); window.vswr_start.setRange(100000, 1500000); window.vswr_start.setValue(865000); window.vswr_start.setSuffix(" kHz")
    window.vswr_stop = QSpinBox(); window.vswr_stop.setRange(100000, 1500000); window.vswr_stop.setValue(870000); window.vswr_stop.setSuffix(" kHz")
    window.vswr_step = QSpinBox(); window.vswr_step.setRange(1, 100000); window.vswr_step.setValue(250); window.vswr_step.setSuffix(" kHz")
    sweep_row = QHBoxLayout()
    for label, widget in (("Antenna", window.vswr_antenna), ("Start", window.vswr_start), ("Stop", window.vswr_stop), ("Step", window.vswr_step)):
        sweep_row.addWidget(QLabel(label)); sweep_row.addWidget(widget)
    sweep_row.addStretch(); vswr_layout.addLayout(sweep_row)
    vswr_actions = QHBoxLayout(); window.vswr_scan_button = _button("Start scan", window.start_vswr_scan, "Primary"); window.vswr_stop_button = _button("Stop scan", window.stop_vswr_scan, "Danger"); window.vswr_export_button = _button("Export CSV", window.export_vswr); vswr_actions.addWidget(window.vswr_scan_button); vswr_actions.addWidget(window.vswr_stop_button); vswr_actions.addWidget(window.vswr_export_button); vswr_actions.addStretch(); window.vswr_summary = QLabel("No VSWR samples"); window.vswr_summary.setObjectName("Sub"); vswr_actions.addWidget(window.vswr_summary); vswr_layout.addLayout(vswr_actions)
    window.vswr_chart = QChart(); window.vswr_chart.legend().setVisible(True); window.vswr_chart.legend().setAlignment(Qt.AlignTop); window.vswr_chart.setMargins(QMargins(4, 4, 8, 4)); window.vswr_chart.setBackgroundRoundness(0); window.vswr_series = QLineSeries(); window.vswr_series.setName("VSWR"); window.vswr_threshold_series = QLineSeries(); window.vswr_threshold_series.setName("Pass threshold 1.5"); vswr_chart_view = QChartView(window.vswr_chart); vswr_chart_view.setRenderHint(QPainter.Antialiasing); vswr_chart_view.setMinimumSize(420, 320)
    window.vswr_table = QTableWidget(0, 4); _table(window.vswr_table, ["Frequency (kHz)", "Return loss (dB)", "VSWR", "Status"], 3); window.vswr_table.setMinimumWidth(400)
    # Chart and results side by side so each keeps its full height.
    vswr_split = QSplitter(Qt.Horizontal); vswr_split.setChildrenCollapsible(False); vswr_split.addWidget(vswr_chart_view); vswr_split.addWidget(window.vswr_table); vswr_split.setStretchFactor(0, 3); vswr_split.setStretchFactor(1, 2); vswr_split.setSizes([3000, 2000])
    vswr_layout.addWidget(vswr_split, 1)
    diag_content_layout.addWidget(vswr_card, 1)

    info_card = QFrame(); info_card.setObjectName("Card"); info_layout = QVBoxLayout(info_card); info_layout.setContentsMargins(16, 14, 16, 14); info_layout.setSpacing(10); info_layout.addWidget(window.sec("Reader information"))
    info_grid = QGridLayout(); info_grid.setHorizontalSpacing(8); info_grid.setVerticalSpacing(8)
    for index, (label, command) in enumerate((("Version", "GET_VERSION"), ("Serial", "GET_SERIAL"), ("Temperature", "GET_TEMPERATURE"), ("Region", "GET_REGION"), ("Antenna", "GET_ANTENNA"), ("Power", "GET_POWER"), ("Protocol", "GET_PROTOCOL"), ("Session", "GET_SESSION"), ("Frequency", "GET_FREQUENCY"), ("Regions", "GET_REGIONS"))):
        info_grid.addWidget(_button(label, lambda checked=False, cmd=command: window.request(cmd)), index // 5, index % 5)
    info_layout.addLayout(info_grid)
    info_fields = QFormLayout(); info_fields.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow); window.module_model = QLineEdit("Not discovered"); window.module_model.setReadOnly(True); window.module_firmware = QLineEdit("Not discovered"); window.module_firmware.setReadOnly(True); info_fields.addRow("Module", window.module_model); info_fields.addRow("Firmware", window.module_firmware); info_layout.addLayout(info_fields); info_layout.addStretch()
    health_card = QFrame(); health_card.setObjectName("Card"); health_layout = QVBoxLayout(health_card); health_layout.setContentsMargins(16, 14, 16, 14); health_layout.setSpacing(10); health_layout.addWidget(window.sec("Reader health"))
    health_row = QHBoxLayout(); window.diag_refresh = _button("Refresh", lambda: window.request("GET_DIAG")); window.diag_clear = _button("Clear loop timing", lambda: window.request("CLEAR_DIAG")); window.diag_restart = _button("Restart reader", window.restart_reader, "Danger"); window.diag_restart.setToolTip("REBOOT: the reader resets itself; TAGlink reconnects when it is back"); health_row.addWidget(window.diag_refresh); health_row.addWidget(window.diag_clear); health_row.addStretch(); health_row.addWidget(window.diag_restart); health_layout.addLayout(health_row)
    window.diag_table = QTableWidget(0, 2); _table(window.diag_table, ["Item", "Value"], 1); window.diag_table.setMinimumHeight(180); window.diag_table.setToolTip("GET_DIAG: reset cause, last fault, main-loop timing, stack use, brown-out level")
    health_layout.addWidget(window.diag_table)

    diag_bottom = QHBoxLayout(); diag_bottom.setSpacing(12); diag_bottom.addWidget(info_card, 1); diag_bottom.addWidget(health_card, 1); diag_content_layout.addLayout(diag_bottom)
    diagnostics_layout.addWidget(diag_scroll, 1)
    window.stack.addWidget(diagnostics_page)

    # Log workspace.
    log_page = QWidget(); log_layout = QVBoxLayout(log_page); log_layout.setContentsMargins(0, 0, 0, 0); log_title = QLabel("Log"); log_title.setObjectName("PageTitle"); log_layout.addWidget(log_title)
    log_tabs = QTabWidget(); window.event = QPlainTextEdit(); window.event.setReadOnly(True); log_tabs.addTab(window.event, "Event Log"); window.raw = QPlainTextEdit(); window.raw.setReadOnly(True); log_tabs.addTab(window.raw, "Raw Serial")
    console = QWidget(); console_layout = QVBoxLayout(console); command_row = QHBoxLayout(); window.command = QLineEdit(); window.command.setPlaceholderText("GET_POWER / SET_REGION,01"); window.command.returnPressed.connect(window.manual); command_row.addWidget(window.command); command_row.addWidget(_button("Send", window.manual, "Primary")); console_layout.addLayout(command_row); window.responses = QPlainTextEdit(); window.responses.setReadOnly(True); console_layout.addWidget(window.responses); log_tabs.addTab(console, "Command Console")
    about = QLabel("TAGlink — reader control, tag operations, RF settings, antenna diagnostics and transport logs for the Identium RFID controller." + chr(10) * 2 + "Commands are sent one at a time; a reply (or a 5 s timeout) completes each one before the next is sent."); about.setObjectName("AboutText"); about.setWordWrap(True); about.setAlignment(Qt.AlignTop | Qt.AlignLeft); log_tabs.addTab(about, "About"); log_layout.addWidget(log_tabs, 1); window.stack.addWidget(log_page)

    # Tools workspace (tools_page.py).
    window.tools.build()

    # Chart axes are created once and then updated by Main.handle_vswr_point.
    window.vswr_chart.addSeries(window.vswr_series); window.vswr_chart.addSeries(window.vswr_threshold_series)
    x_axis = QValueAxis(); x_axis.setTitleText("Frequency (kHz)"); x_axis.setRange(800000, 1000000); x_axis.setLabelFormat("%.0f"); x_axis.setTickCount(6)
    y_axis = QValueAxis(); y_axis.setTitleText("VSWR"); y_axis.setRange(1.0, 3.0); y_axis.setTickCount(5); y_axis.setLabelFormat("%.2f")
    axis_font = QFont("Segoe UI", 8)
    for axis in (x_axis, y_axis):
        axis.setLabelsFont(axis_font); axis.setTitleFont(axis_font)
    window.vswr_chart.legend().setFont(axis_font)
    window.vswr_chart.addAxis(x_axis, Qt.AlignBottom); window.vswr_chart.addAxis(y_axis, Qt.AlignLeft); window.vswr_series.attachAxis(x_axis); window.vswr_series.attachAxis(y_axis); window.vswr_threshold_series.attachAxis(x_axis); window.vswr_threshold_series.attachAxis(y_axis)
    window.vswr_x_axis = x_axis; window.vswr_y_axis = y_axis
    window.vswr_series.setPointsVisible(True)
    window.select_feature(0)
