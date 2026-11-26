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
RAIL_ICONS = tuple(chr(code) for code in (0xE8FD, 0xE8EC, 0xE713, 0xE9D9, 0xE7C3, 0xE90F, 0xE896))
BUTTON_HEIGHT = 34


def _button(text, handler, object_name=None):
    button = QPushButton(text)
    if object_name:
        button.setObjectName(object_name)
    button.clicked.connect(handler)
    button.setMinimumHeight(BUTTON_HEIGHT)
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
    table.verticalHeader().setDefaultSectionSize(32)
    table.setShowGrid(False)
    header = table.horizontalHeader()
    header.setDefaultAlignment(Qt.AlignLeft | Qt.AlignVCenter)
    header.setHighlightSections(False)
    for column in range(len(headers)):
        header.setSectionResizeMode(column, QHeaderView.ResizeToContents)
    if stretch_column is not None:
        header.setSectionResizeMode(stretch_column, QHeaderView.Stretch)
    return table


def build_workspace_shell(window, model, reader_type, toggle_mode):
    root = QWidget()
    root.setObjectName("AppRoot")
    window.setCentralWidget(root)
    outer = QVBoxLayout(root)
    outer.setContentsMargins(0, 0, 0, 0)
    outer.setSpacing(0)

    header = QFrame()
    header.setObjectName("WorkspaceHeader")
    header_layout = QVBoxLayout(header)
    header_layout.setContentsMargins(18, 10, 18, 8)
    header_layout.setSpacing(5)
    header_layout.addLayout(_workspace_title_row(window, model, reader_type, toggle_mode))
    connection_row = QHBoxLayout()
    connection_row.setSpacing(8)
    title = QLabel("Reader connection")
    title.setObjectName("ConnectionTitle")
    connection_row.addWidget(title)
    header_layout.addLayout(connection_row)
    outer.addWidget(header)
    return outer, header_layout, connection_row


def _workspace_title_row(window, model, reader_type, toggle_mode):
    row = QHBoxLayout()
    row.setSpacing(12)
    brand = QLabel("TAGlink")
    brand.setObjectName("Title")
    row.addWidget(brand)
    device = QLabel(f"{model} · {reader_type}")
    device.setObjectName("Sub")
    row.addWidget(device)
    row.addStretch()

    appearance = QPushButton("Appearance")
    appearance.setMinimumHeight(BUTTON_HEIGHT)
    menu = QMenu(appearance)
    window.appearance_mode = menu.addAction("", toggle_mode)
    menu.addSeparator()
    window.theme_actions = {}
    for name, title in theme.THEME_LABELS.items():
        action = menu.addAction(title)
        action.setCheckable(True)
        action.triggered.connect(lambda checked=False, color=name: window.set_color_theme(color))
        window.theme_actions[name] = action
    appearance.setMenu(menu)
    row.addWidget(appearance)
    window.change_device = _button("Change device", window.choose_device)
    row.addWidget(window.change_device)
    return row


def refresh_workspace_header(window):
    window.appearance_mode.setText("Use light mode" if window.dark else "Use dark mode")
    for name, action in window.theme_actions.items():
        action.setChecked(name == window.theme_name)


def build_workspace_body(window, outer):
    body = QHBoxLayout()
    body.setSpacing(0)
    rail = QFrame()
    rail.setObjectName("NavRail")
    rail.setFixedWidth(104)
    layout = QVBoxLayout(rail)
    layout.setContentsMargins(9, 12, 9, 12)
    layout.setSpacing(3)
    body.addWidget(rail)
    window.stack = QStackedWidget()
    content = QVBoxLayout()
    content.setContentsMargins(16, 12, 16, 12)
    content.addWidget(window.stack)
    body.addLayout(content, 1)
    outer.addLayout(body, 1)
    return layout


def workspace_nav_button(title, handler):
    button = QToolButton()
    button.setObjectName("RailButton")
    button.setText(title)
    button.setCheckable(True)
    button.setIconSize(QSize(23, 23))
    button.setToolButtonStyle(Qt.ToolButtonTextUnderIcon)
    button.setFixedSize(86, 52)
    button.clicked.connect(handler)
    return button


def page_panel(content):
    panel = QScrollArea()
    panel.setObjectName("PagePanel")
    panel.setWidgetResizable(True)
    panel.setFrameShape(QFrame.NoFrame)
    panel.viewport().setObjectName("PagePanelViewport")
    content.setObjectName("PagePanelContent")
    panel.setWidget(content)
    return panel


def section_divider(vertical=False):
    divider = QFrame()
    divider.setObjectName("SectionDivider")
    if vertical:
        divider.setFixedWidth(1)
    else:
        divider.setFixedHeight(1)
    return divider


def add_settings_sections(grid, left, right):
    grid.setContentsMargins(0, 0, 0, 0)
    grid.setSpacing(0)
    grid.addWidget(section_divider(vertical=True), 0, 1)
    for column, sections in ((0, left), (2, right)):
        stack = QVBoxLayout()
        stack.setSpacing(0)
        for index, section in enumerate(sections):
            section.setObjectName("SettingsSection")
            if index:
                stack.addWidget(section_divider())
            stack.addWidget(section)
        stack.addStretch()
        grid.addLayout(stack, 0, column)
        grid.setColumnStretch(column, 1)


def _glyph_icon(glyph, color, size=23):
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
    refresh_workspace_header(window)


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
    outer, connection_layout, top_layout = build_workspace_shell(
        window, "IF9D", "Fixed reader", lambda: window.set_theme(not window.dark))
    window.port = QComboBox()
    window.port.setMinimumWidth(110)
    window.port.setToolTip("Choose how the reader is connected to this computer.")
    window.port.setMaximumWidth(170)
    top_layout.addWidget(window.port)
    window.baud = QComboBox()
    window.baud.setMinimumWidth(88)
    window.baud.setMaximumWidth(92)
    window.baud.setToolTip("USB or serial speed. Use the speed set on your reader.")
    for value in (115200, 57600, 38400, 19200, 9600):
        window.baud.addItem(str(value), value)
    top_layout.addWidget(window.baud)
    window.connection_ip = QLineEdit()
    window.connection_ip.setPlaceholderText("Reader IP address")
    window.connection_ip.setAccessibleName("Reader IP address")
    top_layout.addWidget(window.connection_ip, 1)
    window.connect = _button("Connect", window.toggle, "Accent")
    window.connect.setMinimumWidth(window.connect.fontMetrics().horizontalAdvance("Disconnect") + 28)
    window.conn = QLabel("Disconnected")
    window.conn.setObjectName("Connection")
    connection_layout.addWidget(window.conn)
    top_layout.addStretch()

    window.multi = _button("Start scanning", window.multi_poll, "Primary")
    window.stop = _button("Stop scanning", window.stop_inv, "Danger")
    window.single = _button("Scan once", window.single_poll)
    window.single.setToolTip("Look for a tag once, then stop automatically.")
    window.sync = _button("Scan and wait", window.sync_poll)
    window.sync.setToolTip("Run one scan and wait for the reader to send all results.")
    window.return_passive = _button("Switch to app control", window.return_to_passive)
    window.return_passive.setToolTip("Stop automatic scanning and let TAGlink control the reader.")
    window.return_passive.hide()
    top_layout.addWidget(window.return_passive)
    top_layout.addWidget(window.connect)

    window.kv = []
    metrics = QHBoxLayout()
    metrics.setSpacing(8)
    for label, value in (
        ("Reader", "Offline"), ("Scanner", "Ready"), ("Reads", "0"),
        ("Unique tags", "0"), ("Read rate", "0.0/s"), ("Temperature", "–")
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
        chip.setMinimumWidth(80)
        metrics.addWidget(chip, 1)
        window.kv.append(value_label)
        if label == "Reader":
            chip.hide()   # the connection label already shows this

    mode_chip = QFrame(); mode_chip.setObjectName("MetricChip")
    mode_layout = QVBoxLayout(mode_chip); mode_layout.setContentsMargins(6, 2, 6, 2); mode_layout.setSpacing(0)
    window.work_mode_value = QLabel("--"); window.work_mode_value.setObjectName("MetricValue")
    mode_caption = QLabel("Mode"); mode_caption.setObjectName("MetricCaption")
    mode_layout.addWidget(window.work_mode_value); mode_layout.addWidget(mode_caption)
    mode_chip.setToolTip("App control waits for TAGlink. Automatic mode can scan and send tags on its own.")
    metrics.addWidget(mode_chip, 1)

    rail_layout = build_workspace_body(window, outer)

    window.nav_buttons = []
    nav_items = [
        ("Inventory", "Scan tags and see what the reader finds"),
        ("Tags", "Read, write, lock or permanently disable a tag"),
        ("Settings", "Change reader and connection settings"),
        ("Diagnostics", "Check the reader and its antenna"),
        ("Log", "View activity and troubleshooting details"),
        ("Tools", "Tune scanning, check stock, prepare batches or see saved tags"),
        ("Update", "Install reader software"),
    ]
    for index, (label, tooltip) in enumerate(nav_items):
        button = workspace_nav_button(label, lambda checked=False, page=index: window.select_feature(page))
        button.setToolTip(tooltip)
        window.nav_buttons.append(button)
    for index in (0, 2, 3, 4, 1, 5, 6):
        rail_layout.addWidget(window.nav_buttons[index])
    window.rfid_nav_button = window.nav_buttons[0]
    # Unread-error dot on the Log button.
    window.log_badge = QLabel(window.nav_buttons[4])
    window.log_badge.setObjectName("LogBadge")
    window.log_badge.setFixedSize(10, 10)
    window.log_badge.move(54, 8)
    window.log_badge.setToolTip("New errors in the log")
    window.log_badge.hide()
    rail_layout.addStretch()
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
    inventory_header.addWidget(_spacer())
    for button in (window.multi, window.stop, window.single, window.sync):
        inventory_header.addWidget(button)
    inventory_layout.addLayout(inventory_header)
    inventory_layout.addLayout(metrics)
    inventory_actions = QHBoxLayout()
    window.filter_edit = QLineEdit()
    window.filter_edit.setPlaceholderText("Filter by tag ID")
    window.filter_edit.textChanged.connect(window.apply_filter)
    inventory_actions.addWidget(window.filter_edit, 1)
    window.filter_exact_cb = QCheckBox("Exact match")
    window.filter_exact_cb.toggled.connect(window.apply_filter)
    inventory_actions.addWidget(window.filter_exact_cb)
    inventory_actions.addWidget(_button("Clear results", window.clear_inv))
    inventory_actions.addWidget(_button("Export CSV", window.export_csv))
    inventory_actions.addWidget(_button("Refresh reader", window.request_info))
    inventory_layout.addLayout(inventory_actions)
    table_card, table_layout = _card(window)
    table_bar = QHBoxLayout()
    window.hint = QLabel("Scanned tags will appear here.")
    window.hint.setObjectName("Sub")
    table_bar.addWidget(window.hint)
    table_bar.addWidget(_spacer())
    table_layout.addLayout(table_bar)
    window.table = QTableWidget(0, 6)
    window.table.setObjectName("InventoryTable")
    _table(window.table, ["Time", "Tag ID", "Signal (dBm)", "Antenna", "Frequency (kHz)", "Reads"], 1)
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
    selected_row.addWidget(QLabel("Tag ID"))
    window.selected_epc_display = QLineEdit()
    window.selected_epc_display.setReadOnly(True)
    window.selected_epc_display.setPlaceholderText("Select a row in Inventory")
    selected_row.addWidget(window.selected_epc_display, 1)
    selected_row.addWidget(_button("Use selected tag", window.use_selected_epc))
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
    for label, value in (("Factory ID (TID)", 2), ("User data", 3), ("Tag ID (EPC)", 1), ("Passwords (reserved)", 0)):
        window.read_bank.addItem(label, value)
    window.read_address = QSpinBox(); window.read_address.setRange(0, 2147483647)
    window.read_words = QSpinBox(); window.read_words.setRange(1, 96); window.read_words.setValue(2)
    window.read_result = QLineEdit(); window.read_result.setReadOnly(True); window.read_result.setPlaceholderText("Read data appears here")
    window.read_address.setToolTip("Position within the selected memory section. Each position holds two bytes.")
    window.read_words.setToolTip("Amount to read, in two-byte words.")
    read_layout.addRow("Memory section", _field(window.read_bank))
    read_layout.addRow("Start position", _field(window.read_address))
    read_layout.addRow("Amount to read (words)", _field(window.read_words))
    read_actions = QHBoxLayout()
    window.read_use_selected = QPushButton("Use selected EPC"); window.read_use_selected.hide()
    window.read_tag_btn = _button("Read tag", window.read_tag, "Primary")
    read_actions.addWidget(window.read_tag_btn); read_actions.addStretch()
    read_layout.addRow("", read_actions)
    read_layout.addRow("Result", _field(window.read_result))
    window.tag_tabs.addTab(read_tab, "Read")

    write_tab = QWidget()
    write_layout = QFormLayout(write_tab)
    write_layout.setContentsMargins(16, 16, 16, 16)
    write_layout.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow)
    window.write_operation = QComboBox()
    window.write_operation.addItem("Write tag data", "TAG_DATA")
    window.write_operation.addItem("Change tag ID", "EPC_EDIT")
    window.write_operation.currentIndexChanged.connect(window.update_write_mode)
    write_layout.addRow("What to change", _field(window.write_operation))
    window.write_target_label = QLabel("Target EPC"); window.write_target_label.hide()
    window.write_bank_label = QLabel("Memory bank"); window.write_bank_label.hide()
    window.write_address_label = QLabel("Start word"); window.write_address_label.hide()
    window.write_data_label = QLabel("Write data"); window.write_data_label.hide()
    window.write_access_password_label = QLabel("Access password"); window.write_access_password_label.hide()
    window.epc_edit_old_label = QLabel("Current EPC"); window.epc_edit_old_label.hide()
    window.epc_edit_new_label = QLabel("New tag ID")
    window.epc_edit_new = QLineEdit(); window.epc_edit_new.setPlaceholderText("Use 0–9 and A–F, in pairs")
    window.write_bank = QComboBox()
    for label, value in (("User data", 3), ("Tag ID (EPC)", 1), ("Factory ID (TID)", 2), ("Passwords (reserved)", 0)):
        window.write_bank.addItem(label, value)
    window.write_address = QSpinBox(); window.write_address.setRange(0, 2147483647)
    window.write_data = QLineEdit(); window.write_data.setPlaceholderText("Use 0–9 and A–F, up to 128 characters")
    window.write_access_password = QLineEdit("00000000"); window.write_access_password.setEchoMode(QLineEdit.Password); window.write_access_password.setMaxLength(8)
    write_layout.addRow(window.epc_edit_new_label, _field(window.epc_edit_new))
    window.write_address.setToolTip("Position within the selected memory section. Each position holds two bytes.")
    write_layout.addRow("Memory section", _field(window.write_bank))
    write_layout.addRow("Start position", _field(window.write_address))
    write_layout.addRow("Data to save", _field(window.write_data))
    write_layout.addRow("Access password", _field(window.write_access_password))
    write_actions = QHBoxLayout()
    window.use_selected = QPushButton("Use selected EPC"); window.use_selected.hide()
    window.write_tag_btn = _button("Write tag data", window.write_selected_operation, "Primary")
    write_actions.addWidget(window.write_tag_btn); write_actions.addStretch()
    write_layout.addRow("", write_actions)
    window.tag_tabs.addTab(write_tab, "Write")

    lock_tab = QWidget()
    lock_form = QFormLayout(lock_tab); lock_form.setContentsMargins(16, 16, 16, 16)
    window.lock_access_password = QLineEdit("00000000"); window.lock_access_password.setEchoMode(QLineEdit.Password); window.lock_access_password.setMaxLength(8)
    window.lock_mask = QLineEdit("0000"); window.lock_mask.setMaxLength(4)
    window.lock_action = QLineEdit("0000"); window.lock_action.setMaxLength(4)
    lock_form.addRow("Access password", _field(window.lock_access_password))
    window.lock_mask.setToolTip("Use the lock mask from your tag supplier to choose which areas to protect.")
    window.lock_action.setToolTip("Use the lock action from your tag supplier to choose the type of protection.")
    lock_form.addRow("Areas to protect (mask)", _field(window.lock_mask))
    lock_form.addRow("Protection code (action)", _field(window.lock_action))
    lock_actions = QHBoxLayout()
    window.lock_use_selected = QPushButton("Use selected EPC"); window.lock_use_selected.hide()
    window.lock_tag_btn = _button("Lock tag", window.lock_tag, "Primary")
    lock_actions.addWidget(window.lock_tag_btn); lock_actions.addStretch()
    lock_form.addRow("", lock_actions)
    window.tag_tabs.addTab(lock_tab, "Lock")

    kill_tab = QWidget()
    kill_form = QFormLayout(kill_tab); kill_form.setContentsMargins(16, 16, 16, 16)
    window.kill_password = QLineEdit("00000000"); window.kill_password.setEchoMode(QLineEdit.Password); window.kill_password.setMaxLength(8)
    kill_form.addRow("Disable password", _field(window.kill_password))
    kill_help = QLabel("Disabling a tag permanently stops it from responding to readers."); kill_help.setObjectName("WarningText")
    kill_form.addRow("", kill_help)
    kill_actions = QHBoxLayout()
    window.kill_use_selected = QPushButton("Use selected EPC"); window.kill_use_selected.hide()
    window.kill_tag_btn = _button("Disable tag", window.kill_tag, "Danger")
    kill_actions.addWidget(window.kill_tag_btn); kill_actions.addStretch()
    kill_form.addRow("", kill_actions)
    window.tag_tabs.addTab(kill_tab, "Disable")
    window.stack.addWidget(tag_page)

    # Settings workspace: responsive two-column cards.
    settings_page = QWidget()
    settings_content = QWidget()
    settings_grid = QGridLayout(settings_content)
    settings_page_layout = QVBoxLayout(settings_page)
    settings_page_layout.setContentsMargins(0, 0, 0, 0)
    settings_page_layout.setSpacing(10)

    reader_card = QFrame(); reader_card.setObjectName("Card"); reader_form = QFormLayout(reader_card); reader_form.setContentsMargins(16, 14, 16, 14); reader_form.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow); reader_form.addRow(window.sec("Reader"))
    window.work_mode_passive = QPushButton("App control"); window.work_mode_active = QPushButton("Automatic")
    window.work_mode_passive.setToolTip("Scan when TAGlink or another connected app asks the reader to scan.")
    window.work_mode_active.setToolTip("Scan and send tags using the settings saved on the reader.")
    work_mode_row = QHBoxLayout(); work_mode_row.setSpacing(0)
    for button, mode, side in ((window.work_mode_passive, "PASSIVE", "SegmentLeft"), (window.work_mode_active, "ACTIVE", "SegmentRight")):
        button.setObjectName(side); button.setCheckable(True); button.setMinimumWidth(96); button.setMinimumHeight(BUTTON_HEIGHT)
        button.clicked.connect(lambda checked=False, m=mode: window.set_work_mode(m))
        work_mode_row.addWidget(button)
    work_mode_row.addStretch()
    reader_form.addRow("Control mode", work_mode_row)
    window.region = QLineEdit("01"); window.region.setMaxLength(2)
    window.tx = QSpinBox(); window.tx.setRange(1, 255); window.tx.setValue(1)
    window.rx = QSpinBox(); window.rx.setRange(1, 255); window.rx.setValue(1)
    window.power = QDoubleSpinBox(); window.power.setRange(0.0, 33.0); window.power.setDecimals(2); window.power.setSingleStep(0.25); window.power.setValue(30.0); window.power.setSuffix(" dBm")
    window.session = QSpinBox(); window.session.setRange(0, 3)
    window.region.setToolTip("Enter the two-character region code supplied for your location.")
    window.session.setToolTip("Controls how long scanned tags wait before replying again.")
    reader_form.addRow("Region code", _field(window.region, 150)); reader_form.addRow("Sending antenna", _field(window.tx, 150)); reader_form.addRow("Receiving antenna", _field(window.rx, 150)); reader_form.addRow("Reading power", _field(window.power, 180)); reader_form.addRow("Scan session", _field(window.session, 150))
    reader_actions = QGridLayout()
    for index, (label, handler) in enumerate((("Save power", lambda: window.enqueue(f"SET_POWER,{int(round(window.power.value() * 100.0))}")), ("Save antennas", lambda: window.enqueue(f"SET_ANTENNA,{window.tx.value()},{window.rx.value()}")), ("Save region", window.set_region), ("Save session", lambda: window.enqueue(f"SET_SESSION,{window.session.value()}")))):
        reader_actions.addWidget(_button(label, handler), index // 2, index % 2)
    reader_form.addRow(reader_actions)

    rf_card = QFrame(); rf_card.setObjectName("Card"); rf_form = QFormLayout(rf_card); rf_form.setContentsMargins(16, 14, 16, 14); rf_form.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow); rf_form.addRow(window.sec("Advanced scanning"))
    window.antenna_count = QSpinBox(); window.antenna_count.setRange(1, 64); window.antenna_count.setValue(1); window.antenna_count.setReadOnly(True)
    window.inventory_antennas = QLineEdit("1")
    window.antenna_selectors = []
    antenna_picker = QGridLayout()
    antenna_picker.setHorizontalSpacing(8)
    for number in range(1, 9):
        selector = QCheckBox(f"A{number}")
        selector.setChecked(number == 1)
        window.antenna_selectors.append(selector)
        antenna_picker.addWidget(selector, (number - 1) // 4, (number - 1) % 4)
    antenna_picker.setColumnStretch(4, 1)
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
    # RF profile = module gen2tagEncoding (the modes of Reader Manager's Profile list the E310 supports); 0 leaves the module as it is.
    window.rf_profile = QComboBox()
    for label, value in (("Reader default", 0), ("Mode 3", 103), ("Mode 5", 105), ("Mode 7", 107), ("Mode 12", 112), ("Mode 13 (longer range)", 113)):
        window.rf_profile.addItem(label, value)
    window.fast_mode = QCheckBox("Report tags more often"); window.fast_mode.setToolTip("Uses scan session 0 and both tag groups to report repeated reads more often.")
    window.fast_mode.toggled.connect(lambda checked: checked and (window.session.setValue(0), window.gen2_target.setCurrentText("A/B")))
    # Hopping is not implemented in the firmware; SET_GEN2 always sends 0.
    window.frequency_hop = QCheckBox("Frequency hopping"); window.frequency_hop.setVisible(False)
    window.inventory_antennas.setToolTip("Enter antenna numbers separated by commas, or select them below.")
    window.gen2_target.setToolTip("Choose the tag group that the reader asks to reply.")
    window.gen2_q.setToolTip("Leave on Auto, or set a higher value when scanning many tags at once.")
    rf_form.addRow("Available antennas", _field(window.antenna_count, 150)); rf_form.addRow("Antennas to scan", _field(window.inventory_antennas)); rf_form.addRow("Pick antennas", antenna_picker); rf_form.addRow("Tag group", _field(window.gen2_target)); rf_form.addRow("Tag count setting (Q)", _field(window.gen2_q, 150)); rf_form.addRow("Reading profile", _field(window.rf_profile))
    mode_row = QHBoxLayout(); mode_row.addWidget(window.fast_mode); mode_row.addWidget(window.frequency_hop); mode_row.addStretch(); rf_form.addRow(mode_row)
    rf_actions = QHBoxLayout(); window.rf_refresh = _button("Check available options", window.request_capabilities); window.rf_apply = _button("Save scan settings", window.apply_rf_settings, "Primary"); rf_actions.addWidget(window.rf_refresh); rf_actions.addWidget(window.rf_apply); rf_form.addRow(rf_actions)

    network_card = QFrame(); network_card.setObjectName("Card"); network_form = QFormLayout(network_card); network_form.setContentsMargins(16, 14, 16, 14); network_form.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow); network_form.addRow(window.sec("Wired network"))
    window.net_ip = QLineEdit("192.168.150.50"); window.net_mask = QLineEdit("255.255.255.0"); window.net_gateway = QLineEdit("192.168.150.1"); window.net_dns = QLineEdit("8.8.8.8")
    window.net_config_port = QSpinBox(); window.net_config_port.setRange(1, 65535); window.net_config_port.setValue(8080)
    window.net_ip.setToolTip("Set the address TAGlink connects to. Use Change reader IP to save a new address on the reader.")
    window.net_ip.setObjectName("NetworkReadout")
    for field in (window.net_mask, window.net_gateway, window.net_dns):
        field.setReadOnly(True)
        field.setObjectName("NetworkReadout")
    window.net_config_port.setToolTip("Advanced. Port used to open the reader’s network settings and antenna checks.")
    for label, widget in (("Reader IP address", window.net_ip), ("Subnet mask", window.net_mask), ("Router address", window.net_gateway), ("Name server (DNS)", window.net_dns), ("Settings port", window.net_config_port)):
        network_form.addRow(label, _field(widget))
    network_buttons = QHBoxLayout()
    window.find_readers_button = _button("Find readers", window.discover_readers)
    network_buttons.addWidget(window.find_readers_button)
    window.edit_ip_button = _button("Change reader IP…", window.edit_reader_ip, "Primary")
    network_buttons.addWidget(window.edit_ip_button)
    network_form.addRow(network_buttons)
    window.discovery_table = QTableWidget(0, 6); _table(window.discovery_table, ["IP address", "Status", "Device address", "Port", "Board", "Software"]); window.discovery_table.itemSelectionChanged.connect(window.use_discovered_reader)
    window.discovery_table.setObjectName("DiscoveryTable")
    window.discovery_table.setMinimumHeight(110); window.discovery_table.setToolTip("Choose a reader from the list before connecting.")
    for column in range(6):
        window.discovery_table.horizontalHeader().setSectionResizeMode(column, QHeaderView.Interactive)
    for column, width in enumerate((112, 102, 130, 50, 55, 65)):
        window.discovery_table.setColumnWidth(column, width)
    window.discovery_table.hide()
    network_form.addRow(window.discovery_table)
    window.discovery_message = QLabel()
    window.discovery_message.setObjectName("Sub")
    window.discovery_message.setWordWrap(True)
    window.discovery_message.hide()
    network_form.addRow(window.discovery_message)

    upload_card = QFrame(); upload_card.setObjectName("Card"); upload_form = QFormLayout(upload_card); upload_form.setContentsMargins(16, 14, 16, 14); upload_form.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow); upload_form.addRow(window.sec("Send tags to a web address"))
    window.http_url = QLineEdit(); window.http_url.setPlaceholderText("http://192.168.1.20/upload")
    window.http_timeout = QSpinBox(); window.http_timeout.setRange(0, 65535); window.http_timeout.setValue(10); window.http_timeout.setSuffix(" s")
    window.http_active = QCheckBox(); window.http_active.hide()
    window.http_start_boot = QCheckBox("Scan when the reader starts")
    upload_form.addRow("Upload address", _field(window.http_url)); upload_form.addRow("Wait for reply", _field(window.http_timeout)); upload_form.addRow(window.http_start_boot)
    apply_http = _button("Save upload settings", window.apply_http_config, "Primary")
    apply_http.setToolTip("These settings are used when the reader is in Automatic mode.")
    upload_form.addRow(apply_http)

    wifi_card = QFrame(); wifi_card.setObjectName("Card"); wifi_form = QFormLayout(wifi_card); wifi_form.setContentsMargins(16, 14, 16, 14); wifi_form.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow); wifi_form.addRow(window.sec("Wi-Fi"))
    window.wifi_status = QLabel("Not read yet"); window.wifi_status.setObjectName("Sub"); window.wifi_status.setWordWrap(True); wifi_form.addRow("Status", window.wifi_status)
    window.wifi_host = QLineEdit(); window.wifi_host.setPlaceholderText("e.g. 192.168.1.17"); window.wifi_host.setToolTip("The address TAGlink uses when connecting over Wi-Fi.")
    wifi_form.addRow("Wi-Fi IP address", _field(window.wifi_host))
    window.wifi_enable = QCheckBox("Turn on Wi-Fi")
    window.wifi_mode = QComboBox(); window.wifi_mode.addItem("Join an existing network", "STA"); window.wifi_mode.addItem("Create a reader network", "AP")
    window.wifi_ssid = QLineEdit(); window.wifi_ssid.setPlaceholderText("Network name (2.4 GHz)")
    window.wifi_password = QLineEdit(); window.wifi_password.setEchoMode(QLineEdit.Password); window.wifi_password.setPlaceholderText("Leave empty to keep the stored password")
    window.wifi_open = QCheckBox("Open network (no password)")
    wifi_form.addRow(window.wifi_enable); wifi_form.addRow("Connection type", _field(window.wifi_mode)); wifi_form.addRow("Network name", _field(window.wifi_ssid)); wifi_form.addRow("Password", _field(window.wifi_password)); wifi_form.addRow(window.wifi_open)
    window.upload_interface = QComboBox(); window.upload_interface.addItem("Automatic (Ethernet first)", "AUTO"); window.upload_interface.addItem("Ethernet", "ETH"); window.upload_interface.addItem("Wi-Fi", "WIFI"); window.upload_interface.setToolTip("Choose the connection the reader uses to send scans automatically.")
    wifi_form.addRow("Send tags over", _field(window.upload_interface))
    wifi_buttons = QHBoxLayout(); window.wifi_refresh = _button("Refresh", lambda: window.request("GET_WIFI")); window.wifi_apply = _button("Save Wi-Fi", window.apply_wifi_settings, "Primary"); window.upload_if_apply = _button("Save upload connection", window.apply_upload_interface)
    wifi_buttons.addWidget(window.wifi_refresh); wifi_buttons.addWidget(window.wifi_apply); wifi_form.addRow(wifi_buttons)
    wifi_form.addRow(window.upload_if_apply)

    peripherals_card = QFrame(); peripherals_card.setObjectName("Card"); peripherals_grid = QGridLayout(peripherals_card); peripherals_grid.setContentsMargins(16, 14, 16, 14); peripherals_grid.addWidget(window.sec("Inputs, outputs and buzzer"), 0, 0, 1, 4)
    window.gpi_labels = []
    for index in range(4):
        label = QLabel(f"Input {index + 1}: –"); label.setObjectName("GpiChip"); label.setAlignment(Qt.AlignCenter); window.gpi_labels.append(label); peripherals_grid.addWidget(label, 1, index)
    window.gpo_state = []; window.gpo_duration = []; window.gpo_buttons = []
    for index in range(4):
        peripherals_grid.addWidget(QLabel(f"Output {index + 1}"), 2 + index, 0)
        state = QComboBox(); state.addItem("Low", 0); state.addItem("High", 1)
        duration = QSpinBox(); duration.setRange(0, 86400); duration.setSuffix(" s")
        duration.setToolTip("Time to hold the output. Use zero to set it without a timer.")
        button = _button("Set", lambda checked=False, n=index + 1: window.set_gpo(n))
        window.gpo_state.append(state); window.gpo_duration.append(duration); window.gpo_buttons.append(button)
        peripherals_grid.addWidget(state, 2 + index, 1); peripherals_grid.addWidget(duration, 2 + index, 2); peripherals_grid.addWidget(button, 2 + index, 3)
    buzzer_row = QHBoxLayout()
    buzzer_row.addWidget(QLabel("Buzzer"))
    window.buzzer_duration = QSpinBox(); window.buzzer_duration.setRange(50, 10000); window.buzzer_duration.setValue(250); window.buzzer_duration.setSuffix(" ms")
    window.buzzer_tone = QSpinBox(); window.buzzer_tone.setRange(100, 10000); window.buzzer_tone.setValue(2000); window.buzzer_tone.setSuffix(" Hz")
    window.buzzer_button = _button("Sound buzzer", window.sound_buzzer)
    for widget in (window.buzzer_duration, window.buzzer_tone, window.buzzer_button):
        buzzer_row.addWidget(widget)
    peripherals_grid.addLayout(buzzer_row, 6, 0, 1, 4)
    peripherals_grid.addWidget(_button("Refresh inputs", lambda: window.request("GET_GPI")), 7, 0, 1, 4)
    peripherals_grid.setVerticalSpacing(10)
    peripherals_grid.setRowStretch(8, 1)   # keep rows packed at the top of the card

    interface_card = QFrame(); interface_card.setObjectName("Card"); interface_form = QFormLayout(interface_card); interface_form.setContentsMargins(16, 14, 16, 14); interface_form.addRow(window.sec("App preferences"))
    window.dedupe = QCheckBox("One row per tag ID"); window.dedupe.setChecked(True); window.dedupe.hide()
    window.auto = QCheckBox("Scroll to the latest tag"); window.auto.setChecked(True); window.clear_start = QCheckBox("Clear results when scanning starts"); window.reconnect = QCheckBox("Reconnect automatically"); window.reconnect.setChecked(True)
    interface_form.addRow("", window.dedupe); interface_form.addRow("", window.auto); interface_form.addRow("", window.clear_start); interface_form.addRow("", window.reconnect)
    settings_header = QHBoxLayout()
    settings_title = QLabel("Settings"); settings_title.setObjectName("PageTitle")
    settings_header.addWidget(settings_title)
    settings_header.addStretch()
    settings_header.addWidget(_button("Refresh from reader", window.request_info))
    settings_page_layout.addLayout(settings_header)
    add_settings_sections(settings_grid, (network_card, wifi_card, upload_card, interface_card),
                          (reader_card, rf_card, peripherals_card))
    settings_page_layout.addWidget(page_panel(settings_content), 1)
    window.stack.addWidget(settings_page)

    # Diagnostics workspace with a live QtCharts plot.
    diagnostics_page = QWidget(); diagnostics_layout = QVBoxLayout(diagnostics_page); diagnostics_layout.setContentsMargins(0, 0, 0, 0); diagnostics_layout.setSpacing(12)
    diag_header = QHBoxLayout(); diag_title = QLabel("Diagnostics"); diag_title.setObjectName("PageTitle"); diag_header.addWidget(diag_title); diag_header.addWidget(_spacer())
    window.support_report_btn = _button("Save support report", window.save_support_report, "Primary"); window.support_report_btn.setToolTip("Save one text file with the reader's settings and health, this PC's details and the logs, to send to Identium support")
    diag_header.addWidget(window.support_report_btn); diagnostics_layout.addLayout(diag_header)
    # The page scrolls on short screens rather than squeezing the VSWR chart.
    diag_scroll = QScrollArea(); diag_scroll.setWidgetResizable(True); diag_scroll.setFrameShape(QFrame.NoFrame)
    diag_content = QWidget(); diag_content.setObjectName("ScrollContent"); diag_content_layout = QVBoxLayout(diag_content); diag_content_layout.setContentsMargins(0, 0, 0, 0); diag_content_layout.setSpacing(12); diag_scroll.setWidget(diag_content)
    vswr_card = QFrame(); vswr_card.setObjectName("Card"); vswr_layout = QVBoxLayout(vswr_card); vswr_layout.setContentsMargins(16, 14, 16, 14); vswr_layout.setSpacing(10); vswr_layout.addWidget(window.sec("Antenna check"))
    window.vswr_antenna = QSpinBox(); window.vswr_antenna.setRange(1, 64); window.vswr_antenna.setValue(1)
    window.vswr_start = QSpinBox(); window.vswr_start.setRange(100000, 1500000); window.vswr_start.setValue(865000); window.vswr_start.setSuffix(" kHz")
    window.vswr_stop = QSpinBox(); window.vswr_stop.setRange(100000, 1500000); window.vswr_stop.setValue(870000); window.vswr_stop.setSuffix(" kHz")
    window.vswr_step = QSpinBox(); window.vswr_step.setRange(1, 100000); window.vswr_step.setValue(250); window.vswr_step.setSuffix(" kHz")
    sweep_row = QHBoxLayout()
    for label, widget in (("Antenna", window.vswr_antenna), ("From", window.vswr_start), ("To", window.vswr_stop), ("Interval", window.vswr_step)):
        sweep_row.addWidget(QLabel(label)); sweep_row.addWidget(widget)
    sweep_row.addStretch(); vswr_layout.addLayout(sweep_row)
    vswr_actions = QHBoxLayout(); window.vswr_scan_button = _button("Start scan", window.start_vswr_scan, "Primary"); window.vswr_stop_button = _button("Stop scan", window.stop_vswr_scan, "Danger"); window.vswr_export_button = _button("Export CSV", window.export_vswr); vswr_actions.addWidget(window.vswr_scan_button); vswr_actions.addWidget(window.vswr_stop_button); vswr_actions.addWidget(window.vswr_export_button); vswr_actions.addStretch(); window.vswr_summary = QLabel("No antenna readings yet"); window.vswr_summary.setObjectName("Sub"); vswr_actions.addWidget(window.vswr_summary); vswr_layout.addLayout(vswr_actions)
    window.vswr_chart = QChart(); window.vswr_chart.legend().setVisible(True); window.vswr_chart.legend().setAlignment(Qt.AlignTop); window.vswr_chart.setMargins(QMargins(4, 4, 8, 4)); window.vswr_chart.setBackgroundRoundness(0); window.vswr_series = QLineSeries(); window.vswr_series.setName("VSWR"); window.vswr_threshold_series = QLineSeries(); window.vswr_threshold_series.setName("Pass threshold 1.5"); vswr_chart_view = QChartView(window.vswr_chart); vswr_chart_view.setRenderHint(QPainter.Antialiasing); vswr_chart_view.setMinimumSize(320, 290)
    window.vswr_table = QTableWidget(0, 4); _table(window.vswr_table, ["Frequency (kHz)", "Return loss (dB)", "VSWR", "Status"], 3); window.vswr_table.setMinimumWidth(355)
    # Chart and results side by side so each keeps its full height.
    vswr_split = QSplitter(Qt.Horizontal); vswr_split.setChildrenCollapsible(False); vswr_split.addWidget(vswr_chart_view); vswr_split.addWidget(window.vswr_table); vswr_split.setStretchFactor(0, 3); vswr_split.setStretchFactor(1, 2); vswr_split.setSizes([3000, 2000])
    vswr_layout.addWidget(vswr_split, 1)
    diag_content_layout.addWidget(vswr_card, 1)

    info_card = QFrame(); info_card.setObjectName("Card"); info_layout = QVBoxLayout(info_card); info_layout.setContentsMargins(16, 14, 16, 14); info_layout.setSpacing(10); info_layout.addWidget(window.sec("Reader information"))
    info_grid = QGridLayout(); info_grid.setHorizontalSpacing(8); info_grid.setVerticalSpacing(8)
    for index, (label, command) in enumerate((("Version", "GET_VERSION"), ("Serial", "GET_SERIAL"), ("Temperature", "GET_TEMPERATURE"), ("Region", "GET_REGION"), ("Antenna", "GET_ANTENNA"), ("Power", "GET_POWER"), ("Protocol", "GET_PROTOCOL"), ("Session", "GET_SESSION"), ("Frequency", "GET_FREQUENCY"), ("Regions", "GET_REGIONS"))):
        info_grid.addWidget(_button(label, lambda checked=False, cmd=command: window.request(cmd)), index // 4, index % 4)
    info_layout.addLayout(info_grid)
    info_fields = QFormLayout(); info_fields.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow); window.module_model = QLineEdit("Not discovered"); window.module_model.setReadOnly(True); window.module_firmware = QLineEdit("Not discovered"); window.module_firmware.setReadOnly(True); info_fields.addRow("Module", window.module_model); info_fields.addRow("Firmware", window.module_firmware); info_layout.addLayout(info_fields); info_layout.addStretch()
    health_card = QFrame(); health_card.setObjectName("Card"); health_layout = QVBoxLayout(health_card); health_layout.setContentsMargins(16, 14, 16, 14); health_layout.setSpacing(10); health_layout.addWidget(window.sec("Reader health"))
    health_row = QHBoxLayout(); window.diag_refresh = _button("Refresh", lambda: window.request("GET_DIAG")); window.diag_clear = _button("Reset timing stats", lambda: window.request("CLEAR_DIAG")); window.diag_restart = _button("Restart reader", window.restart_reader, "Danger"); window.diag_restart.setToolTip("Restart the reader and reconnect when it is ready."); health_row.addWidget(window.diag_refresh); health_row.addWidget(window.diag_clear); health_row.addStretch(); health_row.addWidget(window.diag_restart); health_layout.addLayout(health_row)
    window.diag_table = QTableWidget(0, 2); _table(window.diag_table, ["Item", "Value"], 1); window.diag_table.setMinimumHeight(180); window.diag_table.setToolTip("View restart information, faults, memory use, and timing details for support.")
    health_layout.addWidget(window.diag_table)

    diag_bottom = QHBoxLayout(); diag_bottom.setSpacing(12); diag_bottom.addWidget(info_card, 1); diag_bottom.addWidget(health_card, 1); diag_content_layout.addLayout(diag_bottom)
    diagnostics_layout.addWidget(diag_scroll, 1)
    window.stack.addWidget(diagnostics_page)

    # Log workspace.
    log_page = QWidget()
    log_layout = QVBoxLayout(log_page)
    log_layout.setContentsMargins(0, 0, 0, 0)
    log_header = QHBoxLayout()
    log_title = QLabel("Log")
    log_title.setObjectName("PageTitle")
    log_header.addWidget(log_title)
    log_header.addStretch()
    window.clear_log_button = _button("Clear log", window.clear_log)
    log_header.addWidget(window.clear_log_button)
    log_layout.addLayout(log_header)
    log_tabs = QTabWidget()
    window.event_log = QPlainTextEdit()
    window.event_log.setReadOnly(True)
    log_tabs.addTab(window.event_log, "Activity")
    window.raw = QPlainTextEdit()
    window.raw.setReadOnly(True)
    log_tabs.addTab(window.raw, "Connection details")
    console = QWidget(); console_layout = QVBoxLayout(console); command_row = QHBoxLayout(); window.command = QLineEdit(); window.command.setPlaceholderText("Enter a reader command"); window.command.returnPressed.connect(window.manual); command_row.addWidget(window.command); command_row.addWidget(_button("Send command", window.manual, "Primary")); console_layout.addLayout(command_row); window.responses = QPlainTextEdit(); window.responses.setReadOnly(True); console_layout.addWidget(window.responses); log_tabs.addTab(console, "Reader commands")
    about = QLabel("TAGlink connects to Identium RFID readers so you can scan tags, change settings, and check your equipment. The Log page includes connection details and reader commands for support."); about.setObjectName("AboutText"); about.setWordWrap(True); about.setAlignment(Qt.AlignTop | Qt.AlignLeft); log_tabs.addTab(about, "About"); log_layout.addWidget(log_tabs, 1); window.stack.addWidget(log_page)

    # Tools workspace (tools_page.py).
    window.tools.build()
    window.firmware.build()

    # Chart axes are created once and then updated by Main.handle_vswr_point.
    window.vswr_chart.addSeries(window.vswr_series); window.vswr_chart.addSeries(window.vswr_threshold_series)
    x_axis = QValueAxis(); x_axis.setTitleText("Frequency (kHz)"); x_axis.setRange(800000, 1000000); x_axis.setLabelFormat("%.0f"); x_axis.setTickCount(6)
    y_axis = QValueAxis(); y_axis.setTitleText("VSWR"); y_axis.setRange(1.0, 3.0); y_axis.setTickCount(5); y_axis.setLabelFormat("%.2f")
    axis_font = QFont("Segoe UI", 9)
    for axis in (x_axis, y_axis):
        axis.setLabelsFont(axis_font); axis.setTitleFont(axis_font)
    window.vswr_chart.legend().setFont(axis_font)
    window.vswr_chart.addAxis(x_axis, Qt.AlignBottom); window.vswr_chart.addAxis(y_axis, Qt.AlignLeft); window.vswr_series.attachAxis(x_axis); window.vswr_series.attachAxis(y_axis); window.vswr_threshold_series.attachAxis(x_axis); window.vswr_threshold_series.attachAxis(y_axis)
    window.vswr_x_axis = x_axis; window.vswr_y_axis = y_axis
    window.vswr_series.setPointsVisible(True)
    window.select_feature(0)
