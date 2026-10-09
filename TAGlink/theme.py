"""Theme palettes and the shared style-sheet template.

Each named theme contains a dark and light palette. COLORS is also used by
the chart and the navigation icons, which Qt style sheets cannot reach.

Arrows and the checkbox tick are small PNGs drawn at start-up (Qt style
sheets can only show them as images); call stylesheet(dark) after the
QApplication exists."""

import os
import tempfile

DEFAULT_THEME = 'brown'
THEME_LABELS = {'blue': 'Blue', 'brown': 'Brown', 'grey': 'Grey'}

COLORS = {
    # The original dark palette, retained as a selectable theme.
    'blue': {
        'dark': {
            'window': '#0F141B', 'surface': '#171F2A', 'surface_alt': '#1C2531', 'field': '#111821',
            'field_ro': '#151D27', 'border': '#2B3746', 'border_strong': '#3A495C',
            'text': '#E8EEF5', 'text_strong': '#F8FAFC', 'muted': '#93A2B4', 'faint': '#66768A',
            'accent': '#2D70D9', 'accent_border': '#4B8AF0', 'accent_soft': '#1E3F75',
            'on_accent': '#FFFFFF', 'danger': '#B84350', 'danger_border': '#D25A67',
            'danger_text': '#EC7C86', 'success': '#25B97F',
            'button': '#202A36', 'button_hover': '#2A3746', 'disabled_bg': '#1A212A',
            'disabled_text': '#5F6E80', 'disabled_border': '#27313D',
            'header': '#1A2430', 'alt_row': '#151E28', 'selection': '#244F98',
            'selection_text': '#FFFFFF', 'scroll': '#3B4A5D', 'log': '#0D131A',
        },
        'light': {
            'window': '#F3F6F9', 'surface': '#FFFFFF', 'surface_alt': '#F7F9FB', 'field': '#FFFFFF',
            'field_ro': '#F1F4F7', 'border': '#D6DEE7', 'border_strong': '#BCC8D5',
            'text': '#1C2937', 'text_strong': '#111C27', 'muted': '#5F6E80', 'faint': '#8C99A8',
            'accent': '#2563C9', 'accent_border': '#2563C9', 'accent_soft': '#DCE8FB',
            'on_accent': '#FFFFFF', 'danger': '#B53A47', 'danger_border': '#B53A47',
            'danger_text': '#B53A47', 'success': '#178A5C',
            'button': '#FFFFFF', 'button_hover': '#EEF3F8', 'disabled_bg': '#F1F4F7',
            'disabled_text': '#A3AFBC', 'disabled_border': '#DEE4EA',
            'header': '#EEF2F6', 'alt_row': '#F7F9FB', 'selection': '#CFE0FA',
            'selection_text': '#0F1D2B', 'scroll': '#BAC6D3', 'log': '#FFFFFF',
        },
    },
    'brown': {
        'dark': {
            'window': '#14110F', 'surface': '#1D1815', 'surface_alt': '#28211C', 'field': '#15120F',
            'field_ro': '#1A1512', 'border': '#3A2D25', 'border_strong': '#554136',
            'text': '#EDE5DE', 'text_strong': '#FFF8F1', 'muted': '#B9A79A', 'faint': '#8D7B6D',
            'accent': '#9A643A', 'accent_border': '#C18452', 'accent_soft': '#4A2E1E',
            'on_accent': '#FFF8F1', 'danger': '#B84F45', 'danger_border': '#D16C5F',
            'danger_text': '#EF9A8B', 'success': '#4EAB7C',
            'button': '#2A211C', 'button_hover': '#382B23', 'disabled_bg': '#211B17',
            'disabled_text': '#796A5E', 'disabled_border': '#362A22',
            'header': '#241C18', 'alt_row': '#211915', 'selection': '#6B452C',
            'selection_text': '#FFF8F1', 'scroll': '#69503F', 'log': '#100D0B',
        },
        'light': {
            'window': '#F5F1ED', 'surface': '#FFFCF9', 'surface_alt': '#FAF6F1', 'field': '#FFFDFC',
            'field_ro': '#F2ECE6', 'border': '#DED2C7', 'border_strong': '#C6B6A8',
            'text': '#352A22', 'text_strong': '#241A14', 'muted': '#75675D', 'faint': '#9B8D83',
            'accent': '#8A5734', 'accent_border': '#8A5734', 'accent_soft': '#F0E0D2',
            'on_accent': '#FFFFFF', 'danger': '#B54D45', 'danger_border': '#B54D45',
            'danger_text': '#B54D45', 'success': '#287B59',
            'button': '#FFFCF9', 'button_hover': '#F3EAE2', 'disabled_bg': '#F1EBE5',
            'disabled_text': '#B1A39A', 'disabled_border': '#E2D9D1',
            'header': '#F1EAE4', 'alt_row': '#FAF6F1', 'selection': '#E4CDBB',
            'selection_text': '#241A14', 'scroll': '#C6B6A8', 'log': '#FFFCF9',
        },
    },
    'grey': {
        'dark': {
            'window': '#121417', 'surface': '#1B1E22', 'surface_alt': '#24282D', 'field': '#15181B',
            'field_ro': '#1A1D20', 'border': '#353A40', 'border_strong': '#4B535B',
            'text': '#E8EBEE', 'text_strong': '#F8FAFC', 'muted': '#A5ADB5', 'faint': '#7E8790',
            'accent': '#717C88', 'accent_border': '#AEB8C2', 'accent_soft': '#343B43',
            'on_accent': '#F7F9FB', 'danger': '#A95757', 'danger_border': '#CE7777',
            'danger_text': '#EB9B9B', 'success': '#6EA88B',
            'button': '#272B30', 'button_hover': '#33383E', 'disabled_bg': '#202328',
            'disabled_text': '#69727C', 'disabled_border': '#30353B',
            'header': '#202429', 'alt_row': '#1E2125', 'selection': '#4A535D',
            'selection_text': '#F7F9FB', 'scroll': '#5B646D', 'log': '#101214',
        },
        'light': {
            'window': '#F1F3F5', 'surface': '#FFFFFF', 'surface_alt': '#F7F8F9', 'field': '#FFFFFF',
            'field_ro': '#EFF1F3', 'border': '#D9DEE3', 'border_strong': '#B9C1C9',
            'text': '#27313A', 'text_strong': '#1B232B', 'muted': '#68737D', 'faint': '#919CA6',
            'accent': '#5F6B76', 'accent_border': '#73808C', 'accent_soft': '#E3E8EC',
            'on_accent': '#FFFFFF', 'danger': '#AD5555', 'danger_border': '#AD5555',
            'danger_text': '#AD5555', 'success': '#3C805F',
            'button': '#FFFFFF', 'button_hover': '#EEF1F3', 'disabled_bg': '#EEF0F2',
            'disabled_text': '#A7B0B8', 'disabled_border': '#DDE1E5',
            'header': '#E9ECEF', 'alt_row': '#F7F8F9', 'selection': '#D6DDE2',
            'selection_text': '#1B232B', 'scroll': '#B9C1C9', 'log': '#FFFFFF',
        },
    },
}

_TEMPLATE = '''
QMainWindow,QWidget#AppRoot,QScrollArea,QWidget#ScrollContent,QStackedWidget{{background:{window};color:{text}}}
QWidget{{font-family:"Segoe UI";font-size:10pt;color:{text}}}
QLabel,QCheckBox,QRadioButton,QToolButton{{background:transparent}}
QToolTip{{background:{surface_alt};color:{text};border:1px solid {border_strong};padding:4px 6px}}
QMenu{{background:{surface};color:{text};border:1px solid {border}}}
QMenu::item:selected{{background:{accent_soft}}}

QFrame#TopBar,QFrame#Card,QFrame#NavRail,QFrame#AnalyticsPanel{{background:{surface};border:1px solid {border};border-radius:12px}}
QFrame#BarSeparator{{background:{border};max-width:1px;min-width:1px;margin:4px 6px}}
QFrame#MetricChip{{background:transparent}}

QLabel#Title{{font-size:14pt;font-weight:700;color:{text_strong}}}
QLabel#PageTitle{{font-size:13pt;font-weight:700;color:{text_strong}}}
QLabel#Section{{font-size:10.5pt;font-weight:600;color:{text_strong};padding:2px 0 4px 0}}
QLabel#Sub,QLabel#Caption,QLabel#FieldLabel{{color:{muted}}}
QLabel#MetricValue{{font-size:11pt;font-weight:700;color:{text_strong}}}
QLabel#MetricCaption{{font-size:8pt;color:{muted}}}
QLabel#Value{{font-size:11pt;font-weight:700;color:{text_strong}}}
QLabel#GpiChip{{background:{surface_alt};border:1px solid {border};border-radius:8px;padding:6px 10px;font-weight:600}}
QLabel#Connection{{font-weight:700;color:{danger_text}}}
QLabel#LogBadge{{background:{danger};color:#FFFFFF;border-radius:5px}}
QLabel#WarningText{{color:{danger_text}}}

QPushButton{{background:{button};color:{text};border:1px solid {border_strong};border-radius:8px;padding:6px 12px}}
QPushButton:hover{{background:{button_hover}}}
QPushButton:pressed{{background:{border}}}
QPushButton:disabled{{background:{disabled_bg};color:{disabled_text};border-color:{disabled_border}}}
QPushButton#Primary,QPushButton#Accent{{background:{accent};color:{on_accent};border-color:{accent_border};font-weight:600}}
QPushButton#Primary:hover,QPushButton#Accent:hover{{background:{accent_border}}}
QPushButton#Danger{{background:{danger};color:#FFFFFF;border-color:{danger_border};font-weight:600}}
QPushButton#Primary:disabled,QPushButton#Accent:disabled,QPushButton#Danger:disabled{{background:{disabled_bg};color:{disabled_text};border-color:{disabled_border};font-weight:400}}

QPushButton#IconButton{{padding:0}}
QPushButton#SegmentLeft,QPushButton#SegmentRight{{background:{field};color:{muted};border:1px solid {border_strong};padding:6px 14px}}
QPushButton#SegmentLeft{{border-top-right-radius:0;border-bottom-right-radius:0}}
QPushButton#SegmentRight{{border-top-left-radius:0;border-bottom-left-radius:0;border-left:0}}
QPushButton#SegmentLeft:hover,QPushButton#SegmentRight:hover{{background:{button_hover};color:{text}}}
QPushButton#SegmentLeft:checked,QPushButton#SegmentRight:checked{{background:{accent};color:{on_accent};border-color:{accent_border};font-weight:600}}
QPushButton#SegmentLeft:disabled,QPushButton#SegmentRight:disabled{{background:{disabled_bg};color:{disabled_text};border-color:{disabled_border}}}
QToolButton#RailButton{{color:{muted};border:1px solid transparent;border-radius:10px;padding:6px 0;font-size:8.5pt}}
QToolButton#RailButton:hover{{background:{button_hover};color:{text}}}
QToolButton#RailButton:checked{{background:{accent_soft};color:{text_strong};border-color:{accent_border};font-weight:600}}
QToolButton#Disclosure{{color:{muted};padding:4px 2px;font-weight:600}}
QToolButton#Disclosure:hover{{color:{text}}}

QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox{{background:{field};color:{text};border:1px solid {border_strong};border-radius:7px;padding:5px 7px;min-height:20px;selection-background-color:{accent};selection-color:{on_accent}}}
QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QDoubleSpinBox:focus{{border-color:{accent_border}}}
QLineEdit:read-only{{background:{field_ro};color:{muted}}}
QLineEdit:disabled,QComboBox:disabled,QSpinBox:disabled,QDoubleSpinBox:disabled{{background:{disabled_bg};color:{disabled_text};border-color:{disabled_border}}}
QComboBox::drop-down{{border:0;width:22px}}
QComboBox::down-arrow{{image:url({img_down});width:10px;height:10px;margin-right:6px}}
QComboBox::down-arrow:disabled{{image:url({img_down_disabled})}}
QComboBox QAbstractItemView{{background:{surface};color:{text};border:1px solid {border};selection-background-color:{accent_soft};selection-color:{text_strong};outline:0}}
QSpinBox::up-button,QDoubleSpinBox::up-button,QSpinBox::down-button,QDoubleSpinBox::down-button{{border:0;width:18px;background:transparent}}
QSpinBox::up-arrow,QDoubleSpinBox::up-arrow{{image:url({img_up});width:8px;height:8px}}
QSpinBox::down-arrow,QDoubleSpinBox::down-arrow{{image:url({img_down});width:8px;height:8px}}
QSpinBox::up-arrow:disabled,QDoubleSpinBox::up-arrow:disabled{{image:url({img_up_disabled})}}
QSpinBox::down-arrow:disabled,QDoubleSpinBox::down-arrow:disabled{{image:url({img_down_disabled})}}

QCheckBox{{spacing:7px}}
QCheckBox:disabled{{color:{disabled_text}}}
QCheckBox::indicator{{width:15px;height:15px;border:1px solid {border_strong};border-radius:4px;background:{field}}}
QCheckBox::indicator:hover{{border-color:{accent_border}}}
QCheckBox::indicator:checked{{background:{accent};border-color:{accent_border};image:url({img_check})}}
QCheckBox::indicator:disabled{{background:{disabled_bg};border-color:{disabled_border}}}
QCheckBox::indicator:checked:disabled{{background:{disabled_text};border-color:{disabled_text};image:url({img_check})}}

QTableWidget{{background:{field};alternate-background-color:{alt_row};gridline-color:{border};border:1px solid {border};border-radius:6px;color:{text};selection-background-color:{selection};selection-color:{selection_text};outline:0}}
QTableWidget::item{{padding:3px 6px}}
QTableWidget::item:selected{{background:{selection};color:{selection_text}}}
QTableWidget#InventoryTable{{font-family:"Consolas";font-size:10pt}}
QHeaderView::section{{background:{header};color:{muted};border:0;border-bottom:1px solid {border};padding:7px 6px;font-family:"Segoe UI";font-weight:600}}
QTableCornerButton::section{{background:{header};border:0}}

QPlainTextEdit{{background:{log};color:{text};border:1px solid {border};border-radius:8px;font-family:"Consolas";font-size:9.5pt;padding:4px}}
QTabWidget::pane{{background:{surface};border:1px solid {border};border-radius:8px;top:-1px}}
QTabBar::tab{{background:transparent;color:{muted};padding:8px 16px;border:0;border-bottom:2px solid transparent}}
QTabBar::tab:hover{{color:{text}}}
QTabBar::tab:selected{{color:{text_strong};border-bottom:2px solid {accent};font-weight:600}}
QLabel#AboutText{{padding:16px;color:{muted}}}

QGraphicsView{{background:{surface};border:0}}
QSplitter::handle{{background:transparent}}
QSplitter::handle:vertical{{height:8px}}
QScrollBar:vertical{{background:transparent;width:10px;margin:2px}}
QScrollBar::handle:vertical{{background:{scroll};border-radius:4px;min-height:30px}}
QScrollBar:horizontal{{background:transparent;height:10px;margin:2px}}
QScrollBar::handle:horizontal{{background:{scroll};border-radius:4px;min-width:30px}}
QScrollBar::add-line,QScrollBar::sub-line{{width:0;height:0}}
QScrollBar::add-page,QScrollBar::sub-page{{background:transparent}}
QStatusBar{{background:{window};color:{muted}}}
QStatusBar::item{{border:0}}
'''

def normalize(name):
    return name if name in COLORS else DEFAULT_THEME


def label(name):
    return THEME_LABELS.get(normalize(name), THEME_LABELS[DEFAULT_THEME])


def colors(dark, name=DEFAULT_THEME):
    return COLORS[normalize(name)]['dark' if dark else 'light']


def _draw_images(name, c):
    """Write the arrow/tick PNGs for one palette; returns {key: path}."""
    from PySide6.QtCore import QPointF, Qt
    from PySide6.QtGui import QColor, QImage, QPainter, QPainterPath, QPen, QPolygonF

    folder = os.path.join(tempfile.gettempdir(), 'taglink_theme')
    os.makedirs(folder, exist_ok=True)
    size = 32
    triangles = {
        'up': [(6, 22), (26, 22), (16, 10)],
        'down': [(6, 10), (26, 10), (16, 22)],
    }
    paths = {}

    def save(image, key):
        path = os.path.join(folder, f'{name}_{key}.png').replace(os.sep, '/')
        image.save(path)
        paths[key] = path

    for direction, points in triangles.items():
        for state, color in (('', c['muted']), ('_disabled', c['disabled_border'])):
            image = QImage(size, size, QImage.Format_ARGB32)
            image.fill(Qt.transparent)
            painter = QPainter(image)
            painter.setRenderHint(QPainter.Antialiasing)
            painter.setPen(Qt.NoPen)
            painter.setBrush(QColor(color))
            painter.drawPolygon(QPolygonF([QPointF(x, y) for x, y in points]))
            painter.end()
            save(image, f'img_{direction}{state}')

    image = QImage(size, size, QImage.Format_ARGB32)
    image.fill(Qt.transparent)
    painter = QPainter(image)
    painter.setRenderHint(QPainter.Antialiasing)
    pen = QPen(QColor('#FFFFFF'), 4.2)
    pen.setCapStyle(Qt.RoundCap)
    pen.setJoinStyle(Qt.RoundJoin)
    painter.setPen(pen)
    tick = QPainterPath(QPointF(7, 17))
    tick.lineTo(13.5, 23)
    tick.lineTo(25, 9)
    painter.drawPath(tick)
    painter.end()
    save(image, 'img_check')
    return paths


_cache = {}


def stylesheet(dark, theme_name=DEFAULT_THEME):
    theme_name = normalize(theme_name)
    mode = 'dark' if dark else 'light'
    cache_name = f'{theme_name}_{mode}'
    if cache_name not in _cache:
        palette = COLORS[theme_name][mode]
        values = dict(palette)
        values.update(_draw_images(cache_name, palette))
        _cache[cache_name] = _TEMPLATE.format(**values)
    return _cache[cache_name]
