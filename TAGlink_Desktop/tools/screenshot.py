"""Render every workspace of the GUI to PNG files without a reader or a
visible window, for reviewing layout changes.

    python tools/screenshot.py [output_dir] [--light]

Writes <output_dir>/<width>x<height>_<page>.png for 1920x1080, 1500x920
and 1366x768 (default output_dir: screenshots/).
"""
import os
import sys

GUI_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
os.environ.setdefault('QT_QPA_FONTDIR', r'C:\Windows\Fonts')
sys.path.insert(0, GUI_DIR)
os.chdir(GUI_DIR)

from PySide6.QtWidgets import QApplication  # noqa: E402

out_dir = os.path.abspath(next((a for a in sys.argv[1:] if not a.startswith('--')), 'screenshots'))
os.makedirs(out_dir, exist_ok=True)

source = open(os.path.join(GUI_DIR, 'main.py'), encoding='utf-8').read()
marker = 'app=QApplication(sys.argv)'
source = source[:source.rindex(marker)]          # everything except the app start-up line

app = QApplication(sys.argv)
app.setStyle('Fusion')
namespace = {'__name__': 'gui_screenshot'}
exec(compile(source, 'main.py', 'exec'), namespace)
# Keep the user's saved GUI settings untouched: use a throwaway INI file.
from PySide6.QtCore import QSettings  # noqa: E402
import tempfile  # noqa: E402
_settings_file = os.path.join(tempfile.mkdtemp(prefix='taglink_settings_'), 'settings.ini')
namespace['QSettings'] = lambda *args, **kwargs: QSettings(_settings_file, QSettings.IniFormat)
window = namespace['Main']()
for timer in ('port_timer', 'status_timer'):
    getattr(window, timer).stop()
if '--light' in sys.argv:
    window.set_theme(False)

pages = window.stack.count()
for width, height in ((1920, 1080), (1500, 920), (1366, 768)):
    window.resize(width, height)
    window.show()
    for index in range(pages):
        window.select_feature(index)
        app.processEvents()
        window.grab().save(os.path.join(out_dir, f'{width}x{height}_{index:02d}.png'))

window.io.close()
print(f'{pages} pages x 3 sizes -> {out_dir}')
