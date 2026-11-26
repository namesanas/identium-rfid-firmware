"""Bounded BLE lifecycle diagnostics; never log command/notification payloads."""
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
import sys


class _QuietFileHandler(RotatingFileHandler):
    def handleError(self, record):
        # A read-only/removed application directory must not break Bluetooth.
        pass


class BleDiagnostics:
    def __init__(self, directory=None):
        if directory is None:
            directory = (Path(sys.executable).resolve().parent / "logs"
                         if getattr(sys, "frozen", False)
                         else Path(__file__).resolve().parent / "build" / "ble-logs")
        self.path = Path(directory) / "mubr-ble.log"
        self.warning = None
        self.logger = logging.getLogger("taglink.mubr.ble." + str(self.path))
        self.logger.setLevel(logging.INFO)
        self.logger.propagate = False
        if not self.logger.handlers:
            try:
                self.path.parent.mkdir(parents=True, exist_ok=True)
                handler = _QuietFileHandler(self.path, maxBytes=131072,
                                            backupCount=2, encoding="utf-8")
                handler.setFormatter(logging.Formatter(
                    "%(asctime)s pid=%(process)d %(message)s"))
                self.logger.addHandler(handler)
            except OSError:
                self.logger.addHandler(logging.NullHandler())
                self.warning = "Bluetooth diagnostics could not be saved; use the app Log page."
                self.path = None

    def record(self, message):
        self.logger.info("%s", message.replace("\r", " ").replace("\n", " "))
