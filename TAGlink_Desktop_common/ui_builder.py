from PySide6.QtCore import Qt, QSize, QMargins, QObject, QEvent
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
# list, tag, settings, diagnostic, document, toolbox, download.
RAIL_ICONS = tuple(chr(code) for code in (0xE8FD, 0xE8EC, 0xE713, 0xE9D9, 0xE7C3, 0xE90F, 0xE896))
BUTTON_HEIGHT = 34
RAIL_WIDTH = 196
RAIL_BUTTON_HEIGHT = 38
RAIL_ICON = QSize(28, 18)        # 18 px glyph plus a gap before the label
CARD_MARGINS = (20, 16, 20, 18)
SETTINGS_BREAKPOINT = 900        # below this content width, settings cards stack in one column


# ------------------------------------------------------------------ small building blocks
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


def _spacer():
    spacer = QWidget()
    spacer.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
    return spacer


def _label(text, object_name=None, wrap=False):
    label = QLabel(text)
    if object_name:
        label.setObjectName(object_name)
    label.setWordWrap(wrap)
    return label


def _table(table, headers, stretch_column=None):
    table.setColumnCount(len(headers))
    table.setHorizontalHeaderLabels(headers)
    table.setAlternatingRowColors(False)
    table.setSelectionBehavior(QTableWidget.SelectRows)
    table.setSelectionMode(QTableWidget.SingleSelection)
    table.verticalHeader().setVisible(False)
    table.verticalHeader().setDefaultSectionSize(36)
    table.setShowGrid(False)
    table.setFocusPolicy(Qt.NoFocus)
    table.setWordWrap(False)
    header = table.horizontalHeader()
    header.setDefaultAlignment(Qt.AlignLeft | Qt.AlignVCenter)
    header.setHighlightSections(False)
    header.setMinimumSectionSize(64)
    for column in range(len(headers)):
        header.setSectionResizeMode(column, QHeaderView.ResizeToContents)
    if stretch_column is not None:
        header.setSectionResizeMode(stretch_column, QHeaderView.Stretch)
    return table


class _EmptyState(QObject):
    """A centred message over a table's viewport while the table has no rows."""

    def __init__(self, table, text):
        super().__init__(table)
        self.table = table
        self.label = QLabel(text, table.viewport())
        self.label.setObjectName("EmptyState")
        self.label.setAlignment(Qt.AlignCenter)
        self.label.setWordWrap(True)
        self.label.setAttribute(Qt.WA_TransparentForMouseEvents)
        model = table.model()
        for signal in (model.rowsInserted, model.rowsRemoved, model.modelReset):
            signal.connect(self.refresh)
        table.viewport().installEventFilter(self)
        self.refresh()

    def eventFilter(self, watched, event):
        if event.type() == QEvent.Resize:
            self.label.setGeometry(watched.rect().adjusted(24, 24, -24, -24))
        return False

    def refresh(self, *_):
        try:
            self.label.setVisible(self.table.rowCount() == 0)
        except RuntimeError:      # the model can outlive its table while a window closes
            pass


def empty_state(table, text):
    table._empty_state = _EmptyState(table, text)
    return table._empty_state.label


def _card(window, title=None, hint=None):
    frame = window.card() if window is not None else card_frame()
    layout = QVBoxLayout(frame)
    layout.setContentsMargins(*CARD_MARGINS)
    layout.setSpacing(12)
    if title:
        heading = QVBoxLayout()
        heading.setSpacing(3)
        heading.addWidget(window.sec(title) if window is not None else _label(title, "Section"))
        if hint:
            heading.addWidget(_label(hint, "CardHint", True))
        layout.addLayout(heading)
    return frame, layout


def card_frame():
    frame = QFrame()
    frame.setObjectName("Card")
    return frame


def form_layout(parent=None, label_width=150):
    form = QFormLayout(parent) if parent is not None else QFormLayout()
    form.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow)
    form.setLabelAlignment(Qt.AlignLeft | Qt.AlignVCenter)
    form.setFormAlignment(Qt.AlignLeft | Qt.AlignTop)
    form.setHorizontalSpacing(16)
    form.setVerticalSpacing(10)
    form._label_width = label_width
    return form


def settings_card(window, title):
    """Card holding a form, with the heading spanning both form columns."""
    frame = window.card()
    form = form_layout(frame)
    form.setContentsMargins(*CARD_MARGINS)
    heading = window.sec(title)
    heading.setContentsMargins(0, 0, 0, 4)
    form.addRow(heading)
    return frame, form


def action_row(*widgets, align='right'):
    row = QHBoxLayout()
    row.setSpacing(8)
    if align == 'right':
        row.addStretch(1)
    for widget in widgets:
        row.addWidget(widget)
    if align == 'left':
        row.addStretch(1)
    return row


def inline(*widgets, stretch_last=False):
    """Field plus a button on one form row."""
    holder = QWidget()
    row = QHBoxLayout(holder)
    row.setContentsMargins(0, 0, 0, 0)
    row.setSpacing(8)
    for widget in widgets:
        row.addWidget(widget)
    if not stretch_last:
        row.addStretch(1)
    return holder


def page_header(title, subtitle=None):
    row = QHBoxLayout()
    row.setSpacing(8)
    column = QVBoxLayout()
    column.setSpacing(2)
    column.addWidget(_label(title, "PageTitle"))
    if subtitle:
        column.addWidget(_label(subtitle, "PageSubtitle"))
    row.addLayout(column)
    row.addStretch(1)
    return row


def stat_tile(caption, value="–"):
    frame = QFrame()
    frame.setObjectName("StatTile")
    layout = QVBoxLayout(frame)
    layout.setContentsMargins(14, 10, 12, 12)
    layout.setSpacing(2)
    caption_label = _label(caption, "StatCaption")
    value_label = _label(value, "StatValue")
    layout.addWidget(caption_label)
    layout.addWidget(value_label)
    frame.setMinimumWidth(80)
    frame.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
    return frame, value_label


def page_panel(content):
    """Vertical scroll area for a page; pages draw their own cards inside it."""
    panel = QScrollArea()
    panel.setObjectName("PagePanel")
    panel.setWidgetResizable(True)
    panel.setFrameShape(QFrame.NoFrame)
    panel.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
    panel.viewport().setObjectName("ScrollViewport")
    content.setObjectName("ScrollContent")
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


class _ColumnFlow(QObject):
    """Two columns of cards that fold into one column on narrow windows."""

    def __init__(self, host, grid, left, right, measure=None):
        super().__init__(host)
        self.grid = grid
        self.left = list(left)
        self.right = list(right)
        self.columns = (QVBoxLayout(), QVBoxLayout())
        for index, column in enumerate(self.columns):
            column.setSpacing(16)
            grid.addLayout(column, 0, index)
        self.host = host
        self.viewport = None
        self.measure = measure
        self.single = None
        self.arrange(False)
        host.installEventFilter(self)

    def arrange(self, single):
        if single == self.single:
            return
        self.single = single
        for column in self.columns:
            while column.count():
                column.takeAt(0)
        left, right = self.columns
        for widget in self.left:
            left.addWidget(widget)
        for widget in self.right:
            (left if single else right).addWidget(widget)
        left.addStretch(1)
        right.addStretch(1)
        self.grid.setColumnStretch(0, 1)
        self.grid.setColumnStretch(1, 0 if single else 1)

    def eventFilter(self, watched, event):
        if event.type() in (QEvent.Resize, QEvent.Show):
            # Measure the scroll viewport: the content itself never shrinks below two columns.
            viewport = self.measure or self.host.parentWidget()
            if viewport is not None and viewport is not self.viewport:
                self.viewport = viewport
                viewport.installEventFilter(self)
            self.arrange((self.viewport or self.host).width() < SETTINGS_BREAKPOINT)
        return False


def add_settings_sections(grid, left, right, measure=None):
    grid.setContentsMargins(0, 0, 6, 8)
    grid.setHorizontalSpacing(16)
    grid.setVerticalSpacing(16)
    host = grid.parentWidget()
    host._column_flow = _ColumnFlow(host, grid, left, right, measure)


# ------------------------------------------------------------------ window shell
def build_workspace_shell(window, model, reader_type, toggle_mode):
    root = QWidget()
    root.setObjectName("AppRoot")
    window.setCentralWidget(root)
    outer = QVBoxLayout(root)
    outer.setContentsMargins(0, 0, 0, 0)
    outer.setSpacing(0)

    header = QFrame()
    header.setObjectName("AppHeader")
    header_layout = QVBoxLayout(header)
    header_layout.setContentsMargins(20, 12, 20, 12)
    header_layout.setSpacing(10)
    header_layout.addLayout(_workspace_title_row(window, model, reader_type, toggle_mode))

    panel = QFrame()
    panel.setObjectName("ConnectionPanel")
    panel_layout = QHBoxLayout(panel)
    panel_layout.setContentsMargins(8, 6, 6, 6)
    panel_layout.setSpacing(8)
    status = QVBoxLayout()
    status.setSpacing(2)
    status.setAlignment(Qt.AlignVCenter)
    panel_layout.addLayout(status)
    connection_row = QHBoxLayout()
    connection_row.setSpacing(8)
    panel_layout.addLayout(connection_row, 1)
    header_layout.addWidget(panel)
    outer.addWidget(header)
    return outer, status, connection_row


def _workspace_title_row(window, model, reader_type, toggle_mode):
    row = QHBoxLayout()
    row.setSpacing(10)
    row.addWidget(_label("TAGlink", "Brand"))
    device = _label(f"{model} · {reader_type}", "DeviceChip")
    device.setSizePolicy(QSizePolicy.Maximum, QSizePolicy.Fixed)
    row.addWidget(device, 0, Qt.AlignVCenter)
    row.addStretch()

    appearance = QPushButton("Appearance")
    appearance.setObjectName("Quiet")
    appearance.setProperty("menu", True)
    appearance.setMinimumHeight(BUTTON_HEIGHT)
    appearance.setStyleSheet("padding-right:30px")
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
    rail.setFixedWidth(RAIL_WIDTH)
    layout = QVBoxLayout(rail)
    layout.setContentsMargins(10, 14, 10, 14)
    layout.setSpacing(2)
    body.addWidget(rail)
    window.stack = QStackedWidget()
    content = QVBoxLayout()
    content.setContentsMargins(24, 20, 24, 16)
    content.addWidget(window.stack)
    body.addLayout(content, 1)
    outer.addLayout(body, 1)
    return layout


def workspace_nav_button(title, handler):
    button = QPushButton(title)
    button.setObjectName("RailButton")
    button.setCheckable(True)
    button.setIconSize(RAIL_ICON)
    button.setFixedHeight(RAIL_BUTTON_HEIGHT)
    button.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
    button.setCursor(Qt.PointingHandCursor)
    button.clicked.connect(handler)
    return button


def nav_divider():
    holder = QWidget()
    layout = QVBoxLayout(holder)
    layout.setContentsMargins(8, 8, 8, 8)
    line = QFrame()
    line.setObjectName("NavDivider")
    line.setFixedHeight(1)
    layout.addWidget(line)
    return holder


def _glyph_icon(glyph, color, size=18):
    """Glyph drawn at the left of a wider pixmap so the label keeps a gap."""
    width = RAIL_ICON.width() if size == RAIL_ICON.height() else size
    pixmap = QPixmap(width * 2, size * 2)
    pixmap.fill(Qt.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.TextAntialiasing)
    font = QFont("Segoe Fluent Icons")
    if not font.exactMatch():
        font = QFont("Segoe MDL2 Assets")
    font.setPixelSize(int(size * 2 * 0.9))
    painter.setFont(font)
    painter.setPen(QColor(color))
    painter.drawText(0, 0, size * 2, size * 2, Qt.AlignCenter, glyph)
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


def _page():
    page = QWidget()
    layout = QVBoxLayout(page)
    layout.setContentsMargins(0, 0, 0, 0)
    layout.setSpacing(16)
    return page, layout


def _tab_body(*widgets):
    """Tab content: cards below the tab bar, packed to the top."""
    body = QWidget()
    layout = QVBoxLayout(body)
    layout.setContentsMargins(0, 16, 0, 0)
    layout.setSpacing(16)
    for widget in widgets:
        layout.addWidget(widget)
    layout.addStretch(1)
    return body


# ------------------------------------------------------------------ IF9D workspace
def build_ui(window):
    outer, connection_layout, top_layout = build_workspace_shell(
        window, "IF9D", "Fixed reader", lambda: window.set_theme(not window.dark))
    window.conn = QLabel("Disconnected")
    window.conn.setObjectName("Connection")
    connection_layout.addWidget(window.conn)
    window.port = QComboBox()
    window.port.setMinimumWidth(120)
    window.port.setToolTip("Choose how the reader is connected to this computer.")
    window.port.setMaximumWidth(190)
    top_layout.addWidget(window.port)
    window.baud = QComboBox()
    window.baud.setMinimumWidth(96)
    window.baud.setMaximumWidth(110)
    window.baud.setToolTip("USB or serial speed. Use the speed set on your reader.")
    for value in (115200, 57600, 38400, 19200, 9600):
        window.baud.addItem(str(value), value)
    top_layout.addWidget(window.baud)
    window.connection_ip = QLineEdit()
    window.connection_ip.setPlaceholderText("Reader IP address")
    window.connection_ip.setAccessibleName("Reader IP address")
    window.connection_ip.setMinimumWidth(150)
    window.connection_ip.setMaximumWidth(240)
    top_layout.addWidget(window.connection_ip)
    top_layout.addStretch(1)
    window.connect = _button("Connect", window.toggle, "Accent")
    window.connect.setMinimumWidth(window.connect.fontMetrics().horizontalAdvance("Disconnect") + 40)

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
    metrics.setSpacing(12)
    for label, value in (
        ("Reader", "Offline"), ("Scanner", "Ready"), ("Reads", "0"),
        ("Unique tags", "0"), ("Read rate", "0.0/s"), ("Temperature", "–")
    ):
        tile, value_label = stat_tile(label, value)
        metrics.addWidget(tile, 1)
        window.kv.append(value_label)
        if label == "Reader":
            tile.hide()   # the connection label already shows this
    mode_tile, window.work_mode_value = stat_tile("Mode", "--")
    mode_tile.setToolTip("App control waits for TAGlink. Automatic mode can scan and send tags on its own.")
    metrics.addWidget(mode_tile, 1)

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
    window.page_subtitles = dict(nav_items)
    for index, (label, tooltip) in enumerate(nav_items):
        button = workspace_nav_button(label, lambda checked=False, page=index: window.select_feature(page))
        button.setToolTip(tooltip)
        window.nav_buttons.append(button)
    # Shared with the MUBR window: Inventory, Settings, Diagnostics and Log keep the same places.
    for index in (0, 2, 3, 4):
        rail_layout.addWidget(window.nav_buttons[index])
    rail_layout.addWidget(nav_divider())
    for index in (1, 5):
        rail_layout.addWidget(window.nav_buttons[index])
    rail_layout.addStretch()
    rail_layout.addWidget(window.nav_buttons[6])
    window.rfid_nav_button = window.nav_buttons[0]
    # Unread-error dot on the Log button.
    window.log_badge = QLabel(window.nav_buttons[4])
    window.log_badge.setObjectName("LogBadge")
    window.log_badge.setFixedSize(8, 8)
    window.log_badge.move(RAIL_WIDTH - 20 - 24, (RAIL_BUTTON_HEIGHT - 8) // 2)
    window.log_badge.setToolTip("New errors in the log")
    window.log_badge.hide()
    window.setStatusBar(QStatusBar())
    window.statusBar().setSizeGripEnabled(False)
    window.statusBar().setContentsMargins(14, 0, 0, 0)

    _build_inventory(window, metrics)
    _build_tags(window)
    _build_settings(window)
    _build_diagnostics(window)
    _build_log(window)

    # Tools workspace (tools_page.py) and the firmware page.
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


def _build_inventory(window, metrics):
    page, layout = _page()
    header = page_header("Inventory", window.page_subtitles["Inventory"])
    for button in (window.single, window.sync, window.multi, window.stop):
        header.addWidget(button)
    layout.addLayout(header)
    layout.addLayout(metrics)

    table_card, table_layout = _card(window)
    table_layout.setContentsMargins(16, 14, 16, 12)
    toolbar = QHBoxLayout()
    toolbar.setSpacing(8)
    window.filter_edit = QLineEdit()
    window.filter_edit.setPlaceholderText("Filter by tag ID")
    window.filter_edit.setClearButtonEnabled(True)
    window.filter_edit.setMaximumWidth(360)
    window.filter_edit.textChanged.connect(window.apply_filter)
    toolbar.addWidget(window.filter_edit, 1)
    window.filter_exact_cb = QCheckBox("Exact match")
    window.filter_exact_cb.toggled.connect(window.apply_filter)
    toolbar.addWidget(window.filter_exact_cb)
    toolbar.addStretch(1)
    toolbar.addWidget(_button("Refresh reader", window.request_info, "Quiet"))
    toolbar.addWidget(_button("Clear results", window.clear_inv, "Quiet"))
    toolbar.addWidget(_button("Export CSV", window.export_csv))
    table_layout.addLayout(toolbar)
    window.table = QTableWidget(0, 6)
    window.table.setObjectName("InventoryTable")
    _table(window.table, ["Time", "Tag ID", "Signal (dBm)", "Antenna", "Frequency (kHz)", "Reads"], 1)
    window.table.itemSelectionChanged.connect(window.use_selected_epc)
    window.table.cellDoubleClicked.connect(lambda row, column: window.select_feature(1))
    empty_state(window.table, "No tags yet. Choose Start scanning to read the tags in front of the antenna.")
    table_layout.addWidget(window.table, 1)
    window.hint = QLabel("Scanned tags will appear here.")
    window.hint.setObjectName("Caption")
    table_layout.addWidget(window.hint)
    layout.addWidget(table_card, 1)

    analytics_toggle = QToolButton()
    analytics_toggle.setObjectName("Disclosure")
    analytics_toggle.setText("▸  Analytics")
    analytics_toggle.setCheckable(True)
    analytics_panel = QFrame()
    analytics_panel.setObjectName("AnalyticsPanel")
    analytics_panel.hide()
    analytics_layout = QHBoxLayout(analytics_panel)
    analytics_layout.setContentsMargins(16, 14, 16, 14)
    analytics_layout.setSpacing(16)
    analytics_metrics = QGridLayout()
    analytics_metrics.setHorizontalSpacing(18)
    analytics_metrics.setVerticalSpacing(8)
    for row, label in enumerate(("Total reads", "Unique tags", "Read rate", "Elapsed")):
        analytics_metrics.addWidget(_label(label, "Caption"), row, 0)
    window.analytics_total = QLabel("0")
    window.analytics_unique = QLabel("0")
    window.analytics_rate = QLabel("0.0/s")
    window.analytics_elapsed = QLabel("0 s")
    for row, widget in enumerate((window.analytics_total, window.analytics_unique, window.analytics_rate, window.analytics_elapsed)):
        widget.setObjectName("Value")
        analytics_metrics.addWidget(widget, row, 1)
    analytics_metrics.setRowStretch(4, 1)
    analytics_layout.addLayout(analytics_metrics)
    window.antenna_stats_table = QTableWidget(0, 2)
    _table(window.antenna_stats_table, ["Antenna", "Reads"], 1)
    window.frequency_stats_table = QTableWidget(0, 2)
    _table(window.frequency_stats_table, ["Frequency (kHz)", "Reads"], 1)
    for table in (window.antenna_stats_table, window.frequency_stats_table):
        table.setMaximumHeight(180)
    analytics_layout.addWidget(window.antenna_stats_table, 1)
    analytics_layout.addWidget(window.frequency_stats_table, 1)
    analytics_toggle.toggled.connect(lambda checked: (analytics_panel.setVisible(checked), analytics_toggle.setText("▾  Analytics" if checked else "▸  Analytics")))
    layout.addWidget(analytics_toggle, 0, Qt.AlignLeft)
    layout.addWidget(analytics_panel)
    window.stack.addWidget(page)


def _tag_form(window):
    card, card_layout = _card(window)
    card.setMaximumWidth(960)
    form = form_layout(label_width=170)
    card_layout.addLayout(form)
    return card, form


def _build_tags(window):
    page, layout = _page()
    layout.addLayout(page_header("Tags", window.page_subtitles["Tags"]))
    selected_card, selected_layout = _card(window)
    selected_layout.setContentsMargins(20, 14, 16, 14)
    selected_row = QHBoxLayout()
    selected_row.setSpacing(12)
    selected_row.addWidget(window.sec("Selected tag"))
    window.selected_epc_display = QLineEdit()
    window.selected_epc_display.setReadOnly(True)
    window.selected_epc_display.setPlaceholderText("Select a row in Inventory")
    window.selected_epc_display.setFont(QFont("Consolas", 10))
    selected_row.addWidget(window.selected_epc_display, 1)
    selected_row.addWidget(_button("Use selected tag", window.use_selected_epc))
    selected_layout.addLayout(selected_row)
    layout.addWidget(selected_card)
    window.tag_tabs = QTabWidget()
    window.tag_tabs.setDocumentMode(True)
    layout.addWidget(window.tag_tabs, 1)

    # Backend target widgets remain real widgets because main.py reads them.
    window.read_target_epc = QLineEdit()
    window.target_epc = QLineEdit()
    window.lock_target_epc = QLineEdit()
    window.kill_target_epc = QLineEdit()
    window.epc_edit_old = QLineEdit()
    for hidden in (window.read_target_epc, window.target_epc, window.lock_target_epc, window.kill_target_epc, window.epc_edit_old):
        hidden.hide()

    read_card, read_layout = _tag_form(window)
    window.read_bank = QComboBox()
    for label, value in (("Factory ID (TID)", 2), ("User data", 3), ("Tag ID (EPC)", 1), ("Passwords (reserved)", 0)):
        window.read_bank.addItem(label, value)
    window.read_address = QSpinBox(); window.read_address.setRange(0, 2147483647)
    window.read_words = QSpinBox(); window.read_words.setRange(1, 96); window.read_words.setValue(2)
    window.read_result = QLineEdit(); window.read_result.setReadOnly(True); window.read_result.setPlaceholderText("Read data appears here")
    window.read_result.setFont(QFont("Consolas", 10))
    window.read_address.setToolTip("Position within the selected memory section. Each position holds two bytes.")
    window.read_words.setToolTip("Amount to read, in two-byte words.")
    read_layout.addRow("Memory section", _field(window.read_bank, 320))
    read_layout.addRow("Start position", _field(window.read_address, 160))
    read_layout.addRow("Amount to read (words)", _field(window.read_words, 160))
    window.read_use_selected = QPushButton("Use selected EPC"); window.read_use_selected.hide()
    window.read_tag_btn = _button("Read tag", window.read_tag, "Primary")
    read_layout.addRow("", inline(window.read_tag_btn))
    read_layout.addRow("Result", _field(window.read_result, 720))
    window.tag_tabs.addTab(_tab_body(read_card), "Read")

    write_card, write_layout = _tag_form(window)
    window.write_operation = QComboBox()
    window.write_operation.addItem("Write tag data", "TAG_DATA")
    window.write_operation.addItem("Change tag ID", "EPC_EDIT")
    window.write_operation.currentIndexChanged.connect(window.update_write_mode)
    write_layout.addRow("What to change", _field(window.write_operation, 320))
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
    write_layout.addRow(window.epc_edit_new_label, _field(window.epc_edit_new, 480))
    window.write_address.setToolTip("Position within the selected memory section. Each position holds two bytes.")
    write_layout.addRow("Memory section", _field(window.write_bank, 320))
    write_layout.addRow("Start position", _field(window.write_address, 160))
    write_layout.addRow("Data to save", _field(window.write_data, 480))
    write_layout.addRow("Access password", _field(window.write_access_password, 200))
    window.use_selected = QPushButton("Use selected EPC"); window.use_selected.hide()
    window.write_tag_btn = _button("Write tag data", window.write_selected_operation, "Primary")
    write_layout.addRow("", inline(window.write_tag_btn))
    window.tag_tabs.addTab(_tab_body(write_card), "Write")

    lock_card, lock_form = _tag_form(window)
    window.lock_access_password = QLineEdit("00000000"); window.lock_access_password.setEchoMode(QLineEdit.Password); window.lock_access_password.setMaxLength(8)
    window.lock_mask = QLineEdit("0000"); window.lock_mask.setMaxLength(4)
    window.lock_action = QLineEdit("0000"); window.lock_action.setMaxLength(4)
    lock_form.addRow("Access password", _field(window.lock_access_password, 200))
    window.lock_mask.setToolTip("Use the lock mask from your tag supplier to choose which areas to protect.")
    window.lock_action.setToolTip("Use the lock action from your tag supplier to choose the type of protection.")
    lock_form.addRow("Areas to protect (mask)", _field(window.lock_mask, 200))
    lock_form.addRow("Protection code (action)", _field(window.lock_action, 200))
    window.lock_use_selected = QPushButton("Use selected EPC"); window.lock_use_selected.hide()
    window.lock_tag_btn = _button("Lock tag", window.lock_tag, "Primary")
    lock_form.addRow("", inline(window.lock_tag_btn))
    window.tag_tabs.addTab(_tab_body(lock_card), "Lock")

    kill_card, kill_form = _tag_form(window)
    window.kill_password = QLineEdit("00000000"); window.kill_password.setEchoMode(QLineEdit.Password); window.kill_password.setMaxLength(8)
    kill_help = QLabel("Disabling a tag permanently stops it from responding to readers."); kill_help.setObjectName("WarningText"); kill_help.setWordWrap(True)
    kill_form.addRow("", kill_help)
    kill_form.addRow("Disable password", _field(window.kill_password, 200))
    window.kill_use_selected = QPushButton("Use selected EPC"); window.kill_use_selected.hide()
    window.kill_tag_btn = _button("Disable tag", window.kill_tag, "Danger")
    kill_form.addRow("", inline(window.kill_tag_btn))
    window.tag_tabs.addTab(_tab_body(kill_card), "Disable")
    window.stack.addWidget(page)


def _build_settings(window):
    page, layout = _page()
    header = page_header("Settings", window.page_subtitles["Settings"])
    header.addWidget(_button("Refresh from reader", window.request_info))
    layout.addLayout(header)
    settings_content = QWidget()
    settings_grid = QGridLayout(settings_content)

    # Reader: each value is saved with the button on its own row.
    reader_card, reader_form = settings_card(window, "Reader")
    window.work_mode_passive = QPushButton("App control"); window.work_mode_active = QPushButton("Automatic")
    window.work_mode_passive.setToolTip("Scan when TAGlink or another connected app asks the reader to scan.")
    window.work_mode_active.setToolTip("Scan and send tags using the settings saved on the reader.")
    work_mode_row = QHBoxLayout(); work_mode_row.setSpacing(0)
    for button, mode, side in ((window.work_mode_passive, "PASSIVE", "SegmentLeft"), (window.work_mode_active, "ACTIVE", "SegmentRight")):
        button.setObjectName(side); button.setCheckable(True); button.setMinimumWidth(104); button.setMinimumHeight(BUTTON_HEIGHT)
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
    for widget in (window.region, window.tx, window.rx, window.power, window.session):
        widget.setFixedWidth(140)
    save_power = _button("Save power", lambda: window.enqueue(f"SET_POWER,{int(round(window.power.value() * 100.0))}"))
    save_antennas = _button("Save antennas", lambda: window.enqueue(f"SET_ANTENNA,{window.tx.value()},{window.rx.value()}"))
    save_region = _button("Save region", window.set_region)
    save_session = _button("Save session", lambda: window.enqueue(f"SET_SESSION,{window.session.value()}"))
    reader_form.addRow("Region code", inline(window.region, save_region))
    reader_form.addRow("Sending antenna", inline(window.tx))
    reader_form.addRow("Receiving antenna", inline(window.rx, save_antennas))
    reader_form.addRow("Reading power", inline(window.power, save_power))
    reader_form.addRow("Scan session", inline(window.session, save_session))

    rf_card, rf_form = settings_card(window, "Advanced scanning")
    window.antenna_count = QSpinBox(); window.antenna_count.setRange(1, 64); window.antenna_count.setValue(1); window.antenna_count.setReadOnly(True)
    window.inventory_antennas = QLineEdit("1")
    window.antenna_selectors = []
    antenna_picker = QGridLayout()
    antenna_picker.setHorizontalSpacing(14)
    antenna_picker.setVerticalSpacing(8)
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
    rf_form.addRow("Available antennas", _field(window.antenna_count, 140)); rf_form.addRow("Antennas to scan", _field(window.inventory_antennas, 280)); rf_form.addRow("Pick antennas", antenna_picker); rf_form.addRow("Tag group", _field(window.gen2_target, 280)); rf_form.addRow("Tag count setting (Q)", _field(window.gen2_q, 140)); rf_form.addRow("Reading profile", _field(window.rf_profile, 280))
    mode_row = QHBoxLayout(); mode_row.addWidget(window.fast_mode); mode_row.addWidget(window.frequency_hop); mode_row.addStretch(); rf_form.addRow("", mode_row)
    window.rf_refresh = _button("Check available options", window.request_capabilities, "Quiet"); window.rf_apply = _button("Save scan settings", window.apply_rf_settings, "Primary")
    rf_form.addRow(action_row(window.rf_refresh, window.rf_apply))

    network_card, network_form = settings_card(window, "Wired network")
    window.net_ip = QLineEdit("192.168.150.50"); window.net_mask = QLineEdit("255.255.255.0"); window.net_gateway = QLineEdit("192.168.150.1"); window.net_dns = QLineEdit("8.8.8.8")
    window.net_config_port = QSpinBox(); window.net_config_port.setRange(1, 65535); window.net_config_port.setValue(8080)
    window.net_ip.setToolTip("Set the address TAGlink connects to. Use Change reader IP to save a new address on the reader.")
    window.net_ip.setObjectName("NetworkReadout")
    for field in (window.net_mask, window.net_gateway, window.net_dns):
        field.setReadOnly(True)
        field.setObjectName("NetworkReadout")
    window.net_config_port.setToolTip("Advanced. Port used to open the reader’s network settings and antenna checks.")
    for label, widget, width in (("Reader IP address", window.net_ip, 280), ("Subnet mask", window.net_mask, 280), ("Router address", window.net_gateway, 280), ("Name server (DNS)", window.net_dns, 280), ("Settings port", window.net_config_port, 140)):
        network_form.addRow(label, _field(widget, width))
    window.find_readers_button = _button("Find readers", window.discover_readers)
    window.edit_ip_button = _button("Change reader IP…", window.edit_reader_ip, "Primary")
    network_form.addRow(action_row(window.find_readers_button, window.edit_ip_button))
    window.discovery_table = QTableWidget(0, 6); _table(window.discovery_table, ["IP address", "Status", "Device address", "Port", "Board", "Software"]); window.discovery_table.itemSelectionChanged.connect(window.use_discovered_reader)
    window.discovery_table.setObjectName("DiscoveryTable")
    window.discovery_table.setMinimumHeight(120); window.discovery_table.setToolTip("Choose a reader from the list before connecting.")
    for column in range(6):
        window.discovery_table.horizontalHeader().setSectionResizeMode(column, QHeaderView.Interactive)
    for column, width in enumerate((112, 102, 130, 56, 64, 72)):
        window.discovery_table.setColumnWidth(column, width)
    window.discovery_table.hide()
    network_form.addRow(window.discovery_table)
    window.discovery_message = QLabel()
    window.discovery_message.setObjectName("StatusLine")
    window.discovery_message.setWordWrap(True)
    window.discovery_message.hide()
    network_form.addRow(window.discovery_message)

    upload_card, upload_form = settings_card(window, "Send tags to a web address")
    window.http_url = QLineEdit(); window.http_url.setPlaceholderText("http://192.168.1.20/upload")
    window.http_timeout = QSpinBox(); window.http_timeout.setRange(0, 65535); window.http_timeout.setValue(10); window.http_timeout.setSuffix(" s")
    window.http_active = QCheckBox(); window.http_active.hide()
    window.http_start_boot = QCheckBox("Scan when the reader starts")
    upload_form.addRow("Upload address", _field(window.http_url, 420)); upload_form.addRow("Wait for reply", _field(window.http_timeout, 140)); upload_form.addRow("", window.http_start_boot)
    apply_http = _button("Save upload settings", window.apply_http_config, "Primary")
    apply_http.setToolTip("These settings are used when the reader is in Automatic mode.")
    upload_form.addRow(action_row(apply_http))

    wifi_card, wifi_form = settings_card(window, "Wi-Fi")
    window.wifi_status = QLabel("Not read yet"); window.wifi_status.setObjectName("Sub"); window.wifi_status.setWordWrap(True); wifi_form.addRow("Status", window.wifi_status)
    window.wifi_host = QLineEdit(); window.wifi_host.setPlaceholderText("e.g. 192.168.1.17"); window.wifi_host.setToolTip("The address TAGlink uses when connecting over Wi-Fi.")
    wifi_form.addRow("Wi-Fi IP address", _field(window.wifi_host, 280))
    window.wifi_enable = QCheckBox("Turn on Wi-Fi")
    window.wifi_mode = QComboBox(); window.wifi_mode.addItem("Join an existing network", "STA"); window.wifi_mode.addItem("Create a reader network", "AP")
    window.wifi_ssid = QLineEdit(); window.wifi_ssid.setPlaceholderText("Network name (2.4 GHz)")
    window.wifi_password = QLineEdit(); window.wifi_password.setEchoMode(QLineEdit.Password); window.wifi_password.setPlaceholderText("Leave empty to keep the stored password")
    window.wifi_open = QCheckBox("Open network (no password)")
    wifi_form.addRow("", window.wifi_enable); wifi_form.addRow("Connection type", _field(window.wifi_mode, 280)); wifi_form.addRow("Network name", _field(window.wifi_ssid, 280)); wifi_form.addRow("Password", _field(window.wifi_password, 280)); wifi_form.addRow("", window.wifi_open)
    window.wifi_refresh = _button("Refresh", lambda: window.request("GET_WIFI"), "Quiet"); window.wifi_apply = _button("Save Wi-Fi", window.apply_wifi_settings, "Primary")
    wifi_form.addRow(action_row(window.wifi_refresh, window.wifi_apply))
    wifi_form.addRow(section_divider())
    window.upload_interface = QComboBox(); window.upload_interface.addItem("Automatic (Ethernet first)", "AUTO"); window.upload_interface.addItem("Ethernet", "ETH"); window.upload_interface.addItem("Wi-Fi", "WIFI"); window.upload_interface.setToolTip("Choose the connection the reader uses to send scans automatically.")
    window.upload_if_apply = _button("Save upload connection", window.apply_upload_interface)
    window.upload_interface.setMinimumWidth(180)
    wifi_form.addRow("Send tags over", inline(_field(window.upload_interface, 240), window.upload_if_apply))

    peripherals_card, peripherals_layout = _card(window, "Inputs, outputs and buzzer")
    inputs_head = QHBoxLayout()
    inputs_head.addWidget(_label("Inputs", "Caption"))
    inputs_head.addStretch()
    inputs_head.addWidget(_button("Refresh inputs", lambda: window.request("GET_GPI"), "Quiet"))
    peripherals_layout.addLayout(inputs_head)
    inputs_row = QHBoxLayout(); inputs_row.setSpacing(8)
    window.gpi_labels = []
    for index in range(4):
        label = QLabel(f"Input {index + 1}: –"); label.setObjectName("GpiChip"); label.setAlignment(Qt.AlignCenter); window.gpi_labels.append(label); inputs_row.addWidget(label, 1)
    peripherals_layout.addLayout(inputs_row)
    peripherals_layout.addWidget(section_divider())
    peripherals_layout.addWidget(_label("Outputs", "Caption"))
    outputs = QGridLayout(); outputs.setHorizontalSpacing(8); outputs.setVerticalSpacing(8)
    window.gpo_state = []; window.gpo_duration = []; window.gpo_buttons = []
    for index in range(4):
        outputs.addWidget(QLabel(f"Output {index + 1}"), index, 0)
        state = QComboBox(); state.addItem("Low", 0); state.addItem("High", 1)
        duration = QSpinBox(); duration.setRange(0, 86400); duration.setSuffix(" s")
        duration.setToolTip("Time to hold the output. Use zero to set it without a timer.")
        button = _button("Set", lambda checked=False, n=index + 1: window.set_gpo(n))
        button.setMinimumWidth(72)
        window.gpo_state.append(state); window.gpo_duration.append(duration); window.gpo_buttons.append(button)
        outputs.addWidget(state, index, 1); outputs.addWidget(duration, index, 2); outputs.addWidget(button, index, 3)
    outputs.setColumnStretch(1, 1); outputs.setColumnStretch(2, 1)
    peripherals_layout.addLayout(outputs)
    peripherals_layout.addWidget(section_divider())
    peripherals_layout.addWidget(_label("Buzzer", "Caption"))
    buzzer_row = QHBoxLayout(); buzzer_row.setSpacing(8)
    window.buzzer_duration = QSpinBox(); window.buzzer_duration.setRange(50, 10000); window.buzzer_duration.setValue(250); window.buzzer_duration.setSuffix(" ms")
    window.buzzer_tone = QSpinBox(); window.buzzer_tone.setRange(100, 10000); window.buzzer_tone.setValue(2000); window.buzzer_tone.setSuffix(" Hz")
    window.buzzer_button = _button("Sound buzzer", window.sound_buzzer)
    buzzer_row.addWidget(window.buzzer_duration, 1); buzzer_row.addWidget(window.buzzer_tone, 1); buzzer_row.addWidget(window.buzzer_button)
    peripherals_layout.addLayout(buzzer_row)

    interface_card, interface_layout = _card(window, "App preferences")
    window.dedupe = QCheckBox("One row per tag ID"); window.dedupe.setChecked(True); window.dedupe.hide()
    window.auto = QCheckBox("Scroll to the latest tag"); window.auto.setChecked(True); window.clear_start = QCheckBox("Clear results when scanning starts"); window.reconnect = QCheckBox("Reconnect automatically"); window.reconnect.setChecked(True)
    for checkbox in (window.dedupe, window.auto, window.clear_start, window.reconnect):
        interface_layout.addWidget(checkbox)

    add_settings_sections(settings_grid, (network_card, wifi_card, upload_card, interface_card),
                          (reader_card, rf_card, peripherals_card))
    layout.addWidget(page_panel(settings_content), 1)
    window.stack.addWidget(page)


def _build_diagnostics(window):
    page, layout = _page()
    header = page_header("Diagnostics", window.page_subtitles["Diagnostics"])
    window.support_report_btn = _button("Save support report", window.save_support_report, "Primary"); window.support_report_btn.setToolTip("Save one text file with the reader's settings and health, this PC's details and the logs, to send to Identium support")
    header.addWidget(window.support_report_btn)
    layout.addLayout(header)
    # The page scrolls on short screens rather than squeezing the VSWR chart.
    diag_content = QWidget(); diag_content_layout = QVBoxLayout(diag_content); diag_content_layout.setContentsMargins(0, 0, 6, 8); diag_content_layout.setSpacing(16)
    diag_scroll = page_panel(diag_content)

    vswr_card, vswr_layout = _card(window)
    vswr_head = QHBoxLayout(); vswr_head.setSpacing(8)
    vswr_title = QVBoxLayout(); vswr_title.setSpacing(3)
    vswr_title.addWidget(window.sec("Antenna check"))
    vswr_title.addWidget(_label("Measures how well each antenna is matched across the frequency band. Lower VSWR is better.", "CardHint", True))
    vswr_head.addLayout(vswr_title, 1)
    vswr_layout.addLayout(vswr_head)
    window.vswr_antenna = QSpinBox(); window.vswr_antenna.setRange(1, 64); window.vswr_antenna.setValue(1)
    window.vswr_start = QSpinBox(); window.vswr_start.setRange(100000, 1500000); window.vswr_start.setValue(865000); window.vswr_start.setSuffix(" kHz")
    window.vswr_stop = QSpinBox(); window.vswr_stop.setRange(100000, 1500000); window.vswr_stop.setValue(870000); window.vswr_stop.setSuffix(" kHz")
    window.vswr_step = QSpinBox(); window.vswr_step.setRange(1, 100000); window.vswr_step.setValue(250); window.vswr_step.setSuffix(" kHz")
    sweep_row = QHBoxLayout(); sweep_row.setSpacing(16)
    for label, widget, width in (("Antenna", window.vswr_antenna, 90), ("From", window.vswr_start, 140), ("To", window.vswr_stop, 140), ("Interval", window.vswr_step, 120)):
        group = QVBoxLayout(); group.setSpacing(4)
        group.addWidget(_label(label, "Caption")); widget.setFixedWidth(width); group.addWidget(widget)
        sweep_row.addLayout(group)
    sweep_row.addStretch(1)
    window.vswr_scan_button = _button("Start scan", window.start_vswr_scan, "Primary"); window.vswr_stop_button = _button("Stop scan", window.stop_vswr_scan, "Danger"); window.vswr_export_button = _button("Export CSV", window.export_vswr, "Quiet")
    for button in (window.vswr_export_button, window.vswr_stop_button, window.vswr_scan_button):
        vswr_head.addWidget(button, 0, Qt.AlignTop)
    vswr_layout.addLayout(sweep_row)
    window.vswr_summary = QLabel("No antenna readings yet"); window.vswr_summary.setObjectName("Caption")
    window.vswr_chart = QChart(); window.vswr_chart.legend().setVisible(True); window.vswr_chart.legend().setAlignment(Qt.AlignTop); window.vswr_chart.setMargins(QMargins(4, 4, 8, 4)); window.vswr_chart.setBackgroundRoundness(0); window.vswr_series = QLineSeries(); window.vswr_series.setName("VSWR"); window.vswr_threshold_series = QLineSeries(); window.vswr_threshold_series.setName("Pass threshold 1.5"); vswr_chart_view = QChartView(window.vswr_chart); vswr_chart_view.setRenderHint(QPainter.Antialiasing); vswr_chart_view.setMinimumSize(280, 290)
    window.vswr_table = QTableWidget(0, 4); _table(window.vswr_table, ["Frequency (kHz)", "Return loss (dB)", "VSWR", "Status"], 3); window.vswr_table.setMinimumWidth(290)
    empty_state(window.vswr_table, "Readings appear here during an antenna check.")
    # Chart and results side by side so each keeps its full height.
    vswr_split = QSplitter(Qt.Horizontal); vswr_split.setChildrenCollapsible(False); vswr_split.addWidget(vswr_chart_view); vswr_split.addWidget(window.vswr_table); vswr_split.setStretchFactor(0, 3); vswr_split.setStretchFactor(1, 2); vswr_split.setSizes([3000, 2000])
    vswr_layout.addWidget(vswr_split, 1)
    vswr_layout.addWidget(window.vswr_summary)
    diag_content_layout.addWidget(vswr_card, 1)

    info_card, info_layout = _card(window, "Reader information")
    info_fields = form_layout(); window.module_model = QLineEdit("Not discovered"); window.module_model.setReadOnly(True); window.module_firmware = QLineEdit("Not discovered"); window.module_firmware.setReadOnly(True); info_fields.addRow("Module", window.module_model); info_fields.addRow("Firmware", window.module_firmware); info_layout.addLayout(info_fields)
    info_grid = QGridLayout(); info_grid.setHorizontalSpacing(8); info_grid.setVerticalSpacing(8)
    for index, (label, command) in enumerate((("Version", "GET_VERSION"), ("Serial", "GET_SERIAL"), ("Temperature", "GET_TEMPERATURE"), ("Region", "GET_REGION"), ("Antenna", "GET_ANTENNA"), ("Power", "GET_POWER"), ("Protocol", "GET_PROTOCOL"), ("Session", "GET_SESSION"), ("Frequency", "GET_FREQUENCY"), ("Regions", "GET_REGIONS"))):
        info_grid.addWidget(_button(label, lambda checked=False, cmd=command: window.request(cmd)), index // 4, index % 4)
    info_layout.addLayout(info_grid); info_layout.addStretch()
    health_card, health_layout = _card(window, "Reader health")
    health_row = QHBoxLayout(); health_row.setSpacing(8); window.diag_refresh = _button("Refresh", lambda: window.request("GET_DIAG")); window.diag_clear = _button("Reset timing stats", lambda: window.request("CLEAR_DIAG"), "Quiet"); window.diag_restart = _button("Restart reader", window.restart_reader, "Danger"); window.diag_restart.setToolTip("Restart the reader and reconnect when it is ready."); health_row.addWidget(window.diag_refresh); health_row.addWidget(window.diag_clear); health_row.addStretch(); health_row.addWidget(window.diag_restart); health_layout.addLayout(health_row)
    window.diag_table = QTableWidget(0, 2); _table(window.diag_table, ["Item", "Value"], 1); window.diag_table.setMinimumHeight(200); window.diag_table.setToolTip("View restart information, faults, memory use, and timing details for support.")
    empty_state(window.diag_table, "Choose Refresh to read the reader's health.")
    health_layout.addWidget(window.diag_table)

    diag_bottom = QWidget(); bottom_grid = QGridLayout(diag_bottom)
    add_settings_sections(bottom_grid, (info_card,), (health_card,), measure=diag_scroll.viewport())
    bottom_grid.setContentsMargins(0, 0, 0, 0)
    diag_content_layout.addWidget(diag_bottom)
    layout.addWidget(diag_scroll, 1)
    window.stack.addWidget(page)


def _build_log(window):
    page, layout = _page()
    header = page_header("Log", window.page_subtitles["Log"])
    window.clear_log_button = _button("Clear log", window.clear_log)
    header.addWidget(window.clear_log_button)
    layout.addLayout(header)
    log_tabs = QTabWidget()
    log_tabs.setDocumentMode(True)
    window.event_log = QPlainTextEdit()
    window.event_log.setReadOnly(True)
    log_tabs.addTab(_tab_fill(window.event_log), "Activity")
    window.raw = QPlainTextEdit()
    window.raw.setReadOnly(True)
    log_tabs.addTab(_tab_fill(window.raw), "Connection details")
    console = QWidget(); console_layout = QVBoxLayout(console); console_layout.setContentsMargins(0, 16, 0, 0); console_layout.setSpacing(10)
    command_row = QHBoxLayout(); command_row.setSpacing(8); window.command = QLineEdit(); window.command.setPlaceholderText("Enter a reader command"); window.command.setFont(QFont("Consolas", 10)); window.command.returnPressed.connect(window.manual); command_row.addWidget(window.command); command_row.addWidget(_button("Send command", window.manual, "Primary")); console_layout.addLayout(command_row)
    window.responses = QPlainTextEdit(); window.responses.setReadOnly(True); console_layout.addWidget(window.responses); log_tabs.addTab(console, "Reader commands")
    about = QLabel("TAGlink connects to Identium RFID readers so you can scan tags, change settings, and check your equipment. The Log page includes connection details and reader commands for support."); about.setObjectName("AboutText"); about.setWordWrap(True); about.setAlignment(Qt.AlignTop | Qt.AlignLeft)
    log_tabs.addTab(_tab_body(about), "About")
    layout.addWidget(log_tabs, 1)
    window.stack.addWidget(page)


def _tab_fill(widget):
    body = QWidget()
    layout = QVBoxLayout(body)
    layout.setContentsMargins(0, 16, 0, 0)
    layout.addWidget(widget)
    return body
