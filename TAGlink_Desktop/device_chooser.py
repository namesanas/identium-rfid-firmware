"""Themed launch chooser and safe navigation between separate reader windows."""
from PySide6.QtCore import QObject, QSettings, Signal, Qt
from PySide6.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QHBoxLayout, QFrame, QLabel,
    QPushButton, QComboBox,
)
import theme


class DeviceChooser(QWidget):
    profile_selected = Signal(str)
    finished = Signal()

    def __init__(self, settings=None):
        super().__init__()
        self.settings = settings or QSettings("SilionRFID", "E310Reader")
        self.setObjectName("AppRoot")
        self.setWindowTitle("TAGlink · Choose your reader")
        self.resize(760, 460)
        self.setMinimumSize(600, 400)
        outer = QVBoxLayout(self)
        outer.setContentsMargins(28, 24, 28, 24)
        outer.setSpacing(18)
        header = QHBoxLayout()
        brand = QLabel("TAGlink")
        brand.setObjectName("Title")
        header.addWidget(brand)
        header.addStretch()
        self.palette = QComboBox()
        for name, title in theme.THEME_LABELS.items():
            self.palette.addItem(title, name)
        self.palette.currentIndexChanged.connect(self.change_palette)
        header.addWidget(self.palette)
        self.mode = QPushButton("Light mode")
        self.mode.clicked.connect(self.toggle_mode)
        header.addWidget(self.mode)
        outer.addLayout(header)
        title = QLabel("Which reader are you using?")
        title.setObjectName("PageTitle")
        outer.addWidget(title)
        hint = QLabel("Choose a device to open its workspace.")
        hint.setObjectName("Sub")
        outer.addWidget(hint)
        choices = QHBoxLayout()
        choices.setSpacing(16)
        self.profile_buttons = {}
        for code, subtitle, description in (
            ("IF9D", "Wall reader", "Inventory, tag tools, RF and network settings, antenna diagnostics and firmware tools."),
            ("MUBR", "Wi-Fi / BLE handheld", "Inventory, scan-button behavior, supported RF settings, wireless connections and reader diagnostics."),
        ):
            frame = QFrame()
            frame.setObjectName("Card")
            layout = QVBoxLayout(frame)
            layout.setContentsMargins(20, 18, 20, 18)
            heading = QLabel(code)
            heading.setObjectName("Title")
            layout.addWidget(heading)
            caption = QLabel(subtitle)
            caption.setObjectName("Section")
            layout.addWidget(caption)
            text = QLabel(description)
            text.setObjectName("Sub")
            text.setWordWrap(True)
            layout.addWidget(text, 1)
            choose = QPushButton("Open " + code)
            choose.setObjectName("Accent")
            choose.setMinimumHeight(38)
            choose.clicked.connect(lambda checked=False, profile=code: self.profile_selected.emit(profile))
            layout.addWidget(choose)
            self.profile_buttons[code] = choose
            choices.addWidget(frame, 1)
        outer.addLayout(choices, 1)
        footer = QLabel("You can change devices from the reader window.")
        footer.setObjectName("Caption")
        outer.addWidget(footer)
        self.reload_theme()

    def reload_theme(self):
        self.dark = self.settings.value("dark", True, type=bool)
        self.theme_name = theme.normalize(self.settings.value("color_theme", theme.DEFAULT_THEME, type=str))
        self.palette.blockSignals(True)
        self.palette.setCurrentIndex(self.palette.findData(self.theme_name))
        self.palette.blockSignals(False)
        self.apply_theme()

    def apply_theme(self):
        QApplication.instance().setStyleSheet(theme.stylesheet(self.dark, self.theme_name))
        self.mode.setText("Light mode" if self.dark else "Dark mode")

    def change_palette(self):
        self.theme_name = self.palette.currentData() or theme.DEFAULT_THEME
        self.settings.setValue("color_theme", self.theme_name)
        self.apply_theme()

    def toggle_mode(self):
        self.dark = not self.dark
        self.settings.setValue("dark", self.dark)
        self.apply_theme()

    def closeEvent(self, event):
        self.settings.sync()
        self.finished.emit()
        event.accept()


class ApplicationController(QObject):
    def __init__(self, app):
        super().__init__(app)
        self.app = app
        self.window = None
        # Keep the chooser alive, hidden, while a device window is open.
        self.chooser = DeviceChooser()
        self.chooser.profile_selected.connect(self.open_profile)
        self.chooser.finished.connect(app.quit)
        self.chooser.show()

    def open_profile(self, profile):
        if self.window is not None:
            return
        if profile == "MUBR":
            from handheld_window import MubrWindow
            self.window = MubrWindow()
        elif profile == "IF9D":
            from main import Main
            self.window = Main()
        else:
            return
        self.window.return_to_chooser.connect(self.show_chooser)
        self.window.finished.connect(self.app.quit)
        self.chooser.hide()
        self.window.show()

    def show_chooser(self):
        previous, self.window = self.window, None
        self.chooser.reload_theme()
        self.chooser.show()
        self.chooser.raise_()
        if previous is not None:
            previous.deleteLater()
