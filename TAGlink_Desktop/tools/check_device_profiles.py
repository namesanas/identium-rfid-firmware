"""Offscreen chooser/MUBR interaction and screenshot checks, without hardware."""
import os
import sys
import csv
from pathlib import Path
from unittest.mock import patch

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QPA_FONTDIR", r"C:\Windows\Fonts")
GUI_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(GUI_DIR))

from PySide6.QtCore import QSettings
from PySide6.QtWidgets import QApplication
from device_chooser import DeviceChooser, ApplicationController
from handheld_window import MubrWindow
from protocol import parse_line
from reader_profiles import MUBR_INFO

app = QApplication([])
app.setStyle("Fusion")
app.setQuitOnLastWindowClosed(False)
output = GUI_DIR / "screenshots" / "device-profiles"
output.mkdir(parents=True, exist_ok=True)


class FakeTransport:
    def __init__(self):
        self.sent = []
        self.disconnects = 0

    def send(self, command):
        self.sent.append(command)

    def disconnect(self):
        self.disconnects += 1

    def close(self):
        pass


version = "VERSION,FWVER=0.7.1,HWVER=ESP32,PROFILE=HANDHELD,PROTO=TAGLINK_TEXT_1"
capabilities = "CAPABILITIES,PROFILE=HANDHELD,MODEL=M100,ANT=1,INVENTORY=1,START=1,STOP=1,SINGLE_POLL=1,READ_SETTINGS=1,SET_POWER=1,SET_REGION=1,SET_SESSION=1,SET_GEN2=1,SCAN_MODE=1,TRIGGER=1,PERSIST=1,WIFI=1,BLE=1,SERVER=1,MIN_POWER=500,MAX_POWER=2600,POWER_STEP=50,MAX_POLL_MS=3000,SUPPORTED_REGIONS=03,RF_COUNTRY=IN"
replies = dict(zip(MUBR_INFO, (
    "STATUS,IDLE", "SERIAL,YEAR=NA,SN=demo", "NAME,Reader MUBR", "POWER,ANT=1,READ=2000,WRITE=2000", "REGION,03",
    "SESSION,0", "GEN2,TARGET=A,Q=4,SESSION=0", "SCAN_SETTINGS,MODE=HOLD,TIME_MS=1000,GPIO=18,ACTIVE_LOW=1,DELIVERY=AUTO,PRESSED=0",
    "WIFI,ENABLE=1,MODE=STA,SSID=Example network,STATE=CONNECTED,IP=192.168.1.17,RSSI=-46", "BLE,ENABLE=1,CONNECTED=0,ENCRYPTED=0,MTU=23",
    "SERVER,ENABLE=0,HOST=0.0.0.0,PORT=5000,CONNECTED=0", "BATTERY,AVAILABLE=0", "DIAG,OWNER=NONE,FREE_HEAP=75504,MIN_HEAP=17816,REPORTS=657,BLE_ERRORS=1,WIFI_ERRORS=0,SETTINGS=1",
    "HTTP,CONFIGURED=0,TIMEOUT=10,READY=0,STA=1,STATUS=0,UPLOADED=0,FAILED=0,DROPPED=0", "WORK_MODE,PASSIVE",
)))

for dark in (True, False):
    mode = "dark" if dark else "light"
    settings = QSettings(str(output / ("ui-test-" + mode + ".ini")), QSettings.IniFormat)
    settings.clear()
    settings.setValue("dark", dark)
    settings.setValue("color_theme", "brown")
    chooser = DeviceChooser(settings)
    chosen = []
    chooser.profile_selected.connect(chosen.append)
    chooser.show()
    app.processEvents()
    assert set(chooser.profile_buttons) == {"IF9D", "MUBR"}
    chooser.profile_buttons["MUBR"].click()
    assert chosen == ["MUBR"]
    chooser.grab().save(str(output / ("chooser-" + mode + ".png")))
    chooser.close()

    window = MubrWindow(settings)
    fake = FakeTransport()
    window.session.bind(fake)
    window.session.on_state(True, "Example Wi-Fi TCP reader")
    window.session.on_line(version)
    window.session.on_line(capabilities + ",HTTP=1")
    while window.session.pending:
        window.session.on_line(replies[window.session.pending[0]])
    assert window.stack.count() == 4
    assert window.region.count() == 1 and window.region.currentData() == "03"
    assert window.scan_mode.currentText() == "HOLD"
    assert "unavailable" in window.battery_text.text()
    assert not hasattr(window, "write_tag_btn")
    assert not hasattr(window, "vswr_scan_button")
    assert window.power.maximum() == 26 and window.power.minimum() == 5

    window.http_url.setText("http://192.0.2.10:4612/rfid/sample")
    window.http_apply.click()
    assert fake.sent[-1] == "SET_HTTP,http://192.0.2.10:4612/rfid/sample,10,0"
    assert "/rfid/sample" not in window.event.toPlainText()
    window.session.on_line("OK,SET_HTTP")
    assert fake.sent[-1] == "GET_HTTP" and not window.http_url.text()
    window.session.on_line(replies["GET_HTTP"].replace("CONFIGURED=0", "CONFIGURED=1"))
    window.http_mode.setCurrentText("ACTIVE")
    window.http_mode_apply.click()
    assert fake.sent[-1] == "SET_WORK_MODE,ACTIVE"
    window.session.on_line("OK,SET_WORK_MODE")
    window.session.on_line("WORK_MODE,ACTIVE")
    window.session.on_line("HTTP,CONFIGURED=1,TIMEOUT=10,READY=1,STA=1,STATUS=204,UPLOADED=2,FAILED=0,DROPPED=0")
    assert "HTTP 204" in window.http_state.text() and "Uploaded 2" in window.http_state.text()

    # Typed off-step power must be caught before any mutation gets enqueued.
    before = list(fake.sent)
    window.power.setValue(20.1)
    window.apply_rf()
    assert fake.sent == before
    window.power.setValue(20)
    window.session.start()
    window.session.on_line("OK,START")
    for line in (
        "TAG,EPC=DECAF0000001,RSSI=-54,ANT=1,FREQ=0,TIME=17",
        "TAG,EPC=DECAF0000004,RSSI=-61,ANT=1,FREQ=0,TIME=18",
        "TAG,EPC=DECAF0000001,RSSI=-53,ANT=1,FREQ=0,TIME=19",
    ):
        window.session.on_line(line)
    window.flush_tags()
    assert window.table.rowCount() == 2
    assert window.table.item(0, 2).text() == "2"
    assert window.table.item(0, 3).text() == "Unavailable"
    with patch("handheld_window.QFileDialog.getSaveFileName", return_value=(str(output / ("inventory-" + mode + ".csv")), "CSV (*.csv)")):
        window.export_csv()
    with open(output / ("inventory-" + mode + ".csv"), newline="", encoding="utf-8") as exported:
        rows = list(csv.reader(exported))
    assert len(rows) == 3 and rows[0][0] == "EPC" and rows[1][3] == "Unavailable"
    window.session.stop()
    window.session.on_line("OK,STOP")
    window.connection_status.setText("Connected · Example Wi-Fi TCP reader · synthetic screenshot data")
    window.statusBar().clearMessage()
    window.show()
    for width, height in ((1200, 780), (960, 600)):
        window.resize(width, height)
        for index, name in enumerate(("inventory", "settings", "diagnostics", "log")):
            window.select_page(index)
            app.processEvents()
            window.grab().save(str(output / f"mubr-{mode}-{width}x{height}-{name}.png"))
    # Returning to the chooser while idle sends STOP, RELEASE and only then exits.
    returned = []
    window.return_to_chooser.connect(lambda: returned.append(True))
    window.choose_device()
    assert not returned and fake.sent[-1] == "STOP"
    window.session.on_line("OK,STOP")
    window.session.on_line("OK,RELEASE")
    window.session.on_state(False)
    app.processEvents()
    assert returned and fake.disconnects == 1

import main as fixed_app
navigation_settings = QSettings(str(output / "navigation-test.ini"), QSettings.IniFormat)
navigation_settings.clear()
with patch("device_chooser.QSettings", return_value=navigation_settings), patch("handheld_window.QSettings", return_value=navigation_settings), patch.object(fixed_app, "QSettings", return_value=navigation_settings), patch("serial_manager.SerialManager._open") as opened:
    # Preserve the legacy IF9D active-mode flow: VERSION may be unavailable
    # until inventory is stopped and the reader returns to passive mode.
    legacy = fixed_app.Main()
    legacy.io.close()
    legacy.io = FakeTransport()
    legacy.on_state(True, "simulated IF9D")
    legacy.request_info_staged(legacy.connection_generation)

    def legacy_reply(line):
        if legacy.pending is None:
            legacy._pump_now()
        legacy.on_line(line)

    legacy_reply("STATUS,INVENTORY")
    legacy_reply("WORK_MODE,ACTIVE")
    assert legacy.io.sent == ["GET_STATUS", "GET_WORK_MODE"]
    assert legacy.inventory and legacy.work_mode == "ACTIVE" and not legacy.return_passive.isHidden()
    legacy.return_to_passive()
    legacy_reply("OK,STOP")
    legacy_reply("OK,SET_WORK_MODE")
    legacy_reply("STATUS,IDLE")
    legacy_reply("VERSION,MODEL=IF9D,FWVER=2.4.1")
    assert legacy.work_mode == "PASSIVE" and not legacy.inventory and not legacy.profile_check_pending
    assert not legacy.io.disconnects
    legacy.connected = False
    legacy.close()

    wrong = fixed_app.Main()
    wrong.io.close()
    wrong.io = FakeTransport()
    wrong.on_state(True, "simulated MUBR selected as IF9D")
    wrong.request_info_staged(wrong.connection_generation)
    wrong._pump_now()
    wrong.on_line("STATUS,IDLE")
    wrong._pump_now()
    wrong.on_line(version)
    assert wrong.io.sent == ["GET_STATUS", "GET_VERSION"]
    assert wrong.io.disconnects == 1 and wrong.profile_error and not wrong.queue
    wrong.enqueue("START")
    assert wrong.io.sent == ["GET_STATUS", "GET_VERSION"]
    wrong.on_state(False, "")
    wrong.close()

    controller = ApplicationController(app)
    for profile in ("IF9D", "MUBR", "IF9D"):
        controller.open_profile(profile)
        app.processEvents()
        assert controller.window is not None and not controller.chooser.isVisible()
        window = controller.window
        if profile == "IF9D":
            assert window.stack.count() == 7 and not window.connected
        else:
            assert window.stack.count() == 4 and not window.session.connected
        window.choose_device()
        app.processEvents()
        assert controller.window is None and controller.chooser.isVisible()
    opened.assert_not_called()
    controller.chooser.close()

print("PASS: themed chooser, separate MUBR pages, supported RF settings, typed power validation, tag table, CSV shape, safe profile return, legacy IF9D active/passive recovery, wrong-profile rejection and repeated IF9D/MUBR navigation without opening a transport. Screenshots:", output)
