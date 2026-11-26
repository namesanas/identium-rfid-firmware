"""Populate the GUI with protocol-shaped sample data and capture each workspace."""
import os
import sys

GUI_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
os.environ.setdefault('QT_QPA_FONTDIR', r'C:\Windows\Fonts')
sys.path.insert(0, GUI_DIR)
os.chdir(GUI_DIR)

from PySide6.QtWidgets import QApplication, QTableWidgetItem  # noqa: E402

out_dir = os.path.abspath(next((a for a in sys.argv[1:] if not a.startswith('--')), 'screenshots/after'))
os.makedirs(out_dir, exist_ok=True)

source = open(os.path.join(GUI_DIR, 'main.py'), encoding='utf-8').read()
marker = '\nif __name__ == "__main__":'
source = source[:source.index(marker)]

app = QApplication(sys.argv)
app.setStyle('Fusion')
namespace = {'__name__': 'gui_smoke_screenshot'}
exec(compile(source, 'main.py', 'exec'), namespace)
# Keep the user's saved GUI settings untouched: use a throwaway INI file.
from PySide6.QtCore import QSettings  # noqa: E402
import tempfile  # noqa: E402
_settings_file = os.path.join(tempfile.mkdtemp(prefix='taglink_settings_'), 'settings.ini')
namespace['QSettings'] = lambda *args, **kwargs: QSettings(_settings_file, QSettings.IniFormat)
window = namespace['Main']()
for timer in ('port_timer', 'status_timer'):
    getattr(window, timer).stop()
window.set_controls(False)
window.set_controls(True)
if '--light' in sys.argv:
    window.set_theme(False)

# Exercise the UI state transitions without opening a transport.
window.connected = True
window.connect.setText('Disconnect')
window.conn.setText('Connected')
window.conn.setStyleSheet('color:#18B477;font-weight:700')
window.kv[0].setText('Online')
window.set_controls(True)
for line in (
    'VERSION,MODEL=Silion E310,FIRMWARE=2.4.1,ANTENNAS=4',
    'STATUS,INVENTORY',
    'TAG,EPC=E2806894000050328410E5CD,RSSI=-38,ANT=1,FREQ=906250,TIME=17',
    'TAG,EPC=300833B2DDD9014000000001,RSSI=-52,ANT=2,FREQ=907750,TIME=18',
    'TAG,EPC=E2806894000050328410E5CD,RSSI=-41,ANT=1,FREQ=906250,TIME=19',
    'TEMP,42',
    'REGION,01',
    'POWER,ANT=1,READ=3000,WRITE=3000',
    'GPI,GPI1=0,GPI2=1,GPI3=0,GPI4=0',
    'NETWORK,IP=192.168.1.50,MASK=255.255.255.0,GATEWAY=192.168.1.1,DNS=0.0.0.0,PORT=8080,MAC=02:1D:62:9C:F3:7E',
    'FREQ,902750,915250,927250',
    'WORK_MODE,PASSIVE',
    'WIFI,LINK=ONLINE,STATE=CONNECTED,IP=192.168.1.17,RSSI=-32,HOST=1,UPLOAD=0,ENABLE=1,MODE=STA,SSID=OfficeNet,IF=AUTO,ERRORS=0,REJECTED=00/0',
    'DIAG,RESET=PIN,UPTIME_MS=55367,FAULTS=0,LAST_FAULT=NONE,LOOP_MAX_US=1410,PASSES=492514,ETH_US=54,WIFI_US=474,CFG_US=1351,PROTO_US=46,GPIO_US=15,RFID_US=5,HOST_US=866,STACK_USED=1564,STACK_SIZE=16352,BOR=2',
    'ERROR,INVALID_COMMAND',
):
    window.on_line(line)
for frequency, vswr in ((902750, 1.18), (915250, 1.42), (927250, 1.77)):
    window.handle_vswr_point({'FREQ': frequency, 'VSWR': vswr, 'RETURN_LOSS': 12.0 - vswr})

if window.table.rowCount():
    window.table.selectRow(0)
    window.use_selected_epc()
window.update_write_mode()
assert window.stack.count() == 7
assert window.table.columnCount() == 6
assert window.table.rowCount() == 2
assert window.selected_epc_display.text() == window.target_epc.text()
assert all(widget.text() == window.selected_epc for widget in (
    window.target_epc, window.epc_edit_old, window.read_target_epc,
    window.lock_target_epc, window.kill_target_epc
))
assert window.vswr_table.rowCount() == 3
assert window.discovery_table.isHidden()
window.populate_discovery([{'ip': '192.168.1.50', 'link': 'udp'}])
assert not window.discovery_table.isHidden() and window.discovery_table.rowCount() == 1
window.populate_discovery([])
assert window.discovery_table.isHidden() and 'No readers found' in window.discovery_message.text()
window.discovery_message.hide()
assert window.work_mode == 'PASSIVE' and window.work_mode_passive.isChecked() and window.work_mode_value.text() == 'App control'
window.set_work_mode('ACTIVE')                       # queued, not applied until the reader confirms
assert window.work_mode == 'PASSIVE' and window.queue[-1] == 'SET_WORK_MODE,ACTIVE' or window.pending
window.pending = ['SET_WORK_MODE,ACTIVE', 0]; window.on_line('OK,SET_WORK_MODE')
assert window.work_mode == 'ACTIVE' and window.work_mode_active.isChecked()
window.pending = ['GET_WORK_MODE', 0]; window.on_line('WORK_MODE,PASSIVE')
assert window.work_mode == 'PASSIVE'
assert window.vswr_series.count() == 3
# WiFi: status fills the card and the WiFi address; the form queues SET_WIFI.
assert window.wifi_host.text() == '192.168.1.17'
assert window.wifi_ssid.text() == 'OfficeNet' and window.wifi_enable.isChecked()
assert 'connected' in window.wifi_status.text() and '-32 dBm' in window.wifi_status.text()
window.on_ports_ready([])
assert window.port.findData(namespace['WIFI_OPTION']) >= 0
assert window.diag_table.rowCount() == 16
window.port.setCurrentIndex(window.port.findData(namespace['ETHERNET_OPTION']))
window.queue = []; window.pending = ['GET_STATUS', 0]
window.apply_wifi_settings()
assert window.queue[-1] == 'SET_WIFI,1,STA,OfficeNet,****', window.queue
window.wifi_ssid.setText('Bad,Name'); window.queue = []
namespace['QMessageBox'].warning = staticmethod(lambda *a, **k: None)
window.apply_wifi_settings(); assert window.queue == []
window.wifi_ssid.setText('OfficeNet')
window.upload_interface.setCurrentIndex(window.upload_interface.findData('WIFI')); window.apply_upload_interface()
assert window.queue[-1] == 'SET_UPLOAD_IF,WIFI'
window.port.setCurrentIndex(window.port.findData(namespace['WIFI_OPTION']))
assert window.tcp_host() == '192.168.1.17'
# Firmware page: the reader's status and a locally checked image.
window.on_line('FW,STATE=IDLE,RECEIVED=0,BOOT=1,LAST=INSTALLED,VERSION=v0.10.1')
assert window.firmware.boot_field.text() == 'Updates supported'
assert 'installed' in window.firmware.last_field.text()
assert window.firmware.staged_field.text() == 'v0.10.1'
assert window.firmware.boot_warning.isHidden()
window.on_line('FW,STATE=IDLE,RECEIVED=0,BOOT=NONE,LAST=NONE,VERSION=')
assert not window.firmware.boot_warning.isHidden()
assert not window.firmware.send_btn.isEnabled()        # no image chosen yet
import firmware_page
_image_dir = os.path.join(os.path.dirname(GUI_DIR), 'STM32_Firmware', 'build', 'release')
_images = [f for f in sorted(os.listdir(_image_dir)) if f.endswith('.tlfw')] if os.path.isdir(_image_dir) else []
if _images:
    with open(os.path.join(_image_dir, _images[-1]), 'rb') as handle:
        _info, _problem = firmware_page.describe_image(handle.read())
    assert _info is not None and not _problem, _problem
    assert _info['board'] in ('F429', 'F411') and _info['size'] > 0
    # A damaged image must be caught before anything is sent to the reader.
    _bad = bytearray(_info['bytes']); _bad[firmware_page.HEADER_SIZE + 64] ^= 0xFF
    assert firmware_page.describe_image(bytes(_bad))[0] is None
    assert firmware_page.describe_image(b'not an image')[0] is None
    window.firmware.image = _info
    window.firmware.file_label.setText(_images[-1])
    window.firmware.file_detail.setText(f"Version {_info['version']} for the {_info['board']} board, "
                                       f"{_info['size']:,} bytes — checksum ok.")
    window.firmware.file_detail.show()
    window.firmware.note(f"Chose {_info['version']} for {_info['board']}.")
    window.firmware.progress.setValue(58)
    window.firmware.status_label.setText('Ready to send.')
    assert window.firmware.step_log.isHidden()
    window.firmware.details_button.click()
    assert not window.firmware.step_log.isHidden()
    window.firmware.details_button.click()
    assert window.firmware.step_log.isHidden()
window.queue = []; window.pending = None
window.select_feature(4)
assert window.log_badge.isHidden()
window.select_feature(0)
window.on_line('ERROR,INVALID_COMMAND')
assert not window.log_badge.isHidden()
app.processEvents()

for width, height in ((1920, 1080), (1500, 920), (1366, 768), (960, 600)):
    window.resize(width, height)
    window.show()
    for index in range(window.stack.count()):
        window.select_feature(index)
        app.processEvents()
        window.grab().save(os.path.join(out_dir, f'{width}x{height}_{index:02d}.png'))

window.io.close()
print(f'{window.stack.count()} populated pages x 4 sizes -> {out_dir}')
