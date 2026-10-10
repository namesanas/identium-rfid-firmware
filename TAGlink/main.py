import sys,csv,time,threading
from datetime import datetime
from PySide6.QtCore import Qt,QTimer,QSettings,Signal
from PySide6.QtGui import QAction,QFont
from PySide6.QtWidgets import QApplication,QMainWindow,QWidget,QVBoxLayout,QHBoxLayout,QGridLayout,QFormLayout,QFrame,QLabel,QPushButton,QComboBox,QLineEdit,QSpinBox,QDoubleSpinBox,QCheckBox,QTableWidget,QTableWidgetItem,QPlainTextEdit,QSplitter,QTabWidget,QFileDialog,QMessageBox,QHeaderView,QStatusBar,QScrollArea
from serial_manager import SerialManager, ETHERNET_OPTION, WIFI_OPTION, ETHERNET_HOST, ETHERNET_PORT
from protocol import parse_line
from vswr_transport import VSWRBridge
import theme
from ui_builder import build_ui, refresh_theme, set_vswr_x_range

INFO=["GET_VERSION","GET_SERIAL","GET_CAPABILITIES","GET_TEMPERATURE","GET_REGION","GET_ANTENNA","GET_POWER","GET_PROTOCOL","GET_SESSION","GET_FREQUENCY","GET_GPI","GET_NETWORK","GET_WORK_MODE","GET_WIFI"]

class Main(QMainWindow):
    ports_ready = Signal(list)
    discovery_ready_signal = Signal(list)
    vswr_sample_signal = Signal(dict)
    vswr_done_signal = Signal()
    vswr_error_signal = Signal(str)

    def __init__(self):
        super().__init__(); self.setWindowTitle("RFID Reader"); self.resize(1500,920); self.setMinimumSize(1200,760)
        self.settings=QSettings("SilionRFID","E310Reader"); self.dark=self.settings.value("dark",True,type=bool); self.theme_name=theme.normalize(self.settings.value("color_theme",theme.DEFAULT_THEME,type=str))
        self.connected=False; self.inventory=False; self.total=0; self.epcs={}; self.epc_row_map={}; self.selected_epc=''; self.epc_write_pending=''; self.last_read_data=''; self.poll_mode='MULTI'; self.single_poll_timeout_ms=3000; self.tag_filter=''; self.filter_exact=False; self.first_tag=None; self.pending=None; self.queue=[]; self.backlog=[]; self.pump_scheduled=False; self.info_scheduled=False
        self.module_capabilities={}; self.discovered_readers=[]; self.vswr_samples=[]; self.vswr_scanning=False; self.vswr_bridge=None; self.vswr_serial_mode=False; self.region_frequencies=[]; self.work_mode=''; self.work_mode_requested=''; self.info_deferred=False
        self.epc_first_seen={}; self.epc_last_seen={}; self.antenna_stats={}; self.frequency_stats={}; self.inventory_started_at=None
        self.connection_generation=0
        self.manual_disconnect=False
        self.release_pending=False
        self.wifi_form_dirty=False
        self.io=SerialManager()
        self.io.line_signal.connect(self.on_line)
        self.io.raw_signal.connect(self.on_raw)
        self.io.state_signal.connect(self.on_state)
        self.io.error_signal.connect(self.on_error)
        self.io.tx_signal.connect(self.on_tx)
        
        build_ui(self)
        # A WiFi status refresh must not overwrite what the user is typing.
        self.wifi_enable.toggled.connect(self.mark_wifi_dirty); self.wifi_mode.currentIndexChanged.connect(self.mark_wifi_dirty)
        self.wifi_ssid.textEdited.connect(self.mark_wifi_dirty); self.wifi_password.textEdited.connect(self.mark_wifi_dirty)
        self.wifi_open.toggled.connect(self.mark_wifi_dirty)
        self.set_controls(False)
        
        # Prevent diagnostic logs from endlessly growing and exhausting UI memory
        self.event.setMaximumBlockCount(1000)
        self.raw.setMaximumBlockCount(1000)
        self.responses.setMaximumBlockCount(1000)

        self.filter_timer = QTimer(self)
        self.filter_timer.setSingleShot(True)
        self.filter_timer.setInterval(300)
        self.filter_timer.timeout.connect(self.apply_filter_debounced)
        
        self.ports_ready.connect(self.on_ports_ready)
        self.discovery_ready_signal.connect(self.populate_discovery)
        self.vswr_sample_signal.connect(self.handle_vswr_point)
        self.vswr_done_signal.connect(self.on_vswr_done)
        self.vswr_error_signal.connect(self.on_vswr_error)

        self.restore(); self.apply_theme(); self.refresh_ports(); self.log("Application ready. Select USB, Ethernet or WiFi.")
        self.port_timer=QTimer(self); self.port_timer.timeout.connect(self.refresh_ports); self.port_timer.start(2500)
        self.status_timer=QTimer(self); self.status_timer.timeout.connect(self.status_poll); self.status_timer.start(15000)
        self.timeout_timer=QTimer(self); self.timeout_timer.timeout.connect(self.check_timeout); self.timeout_timer.start(100)

    def card(self): f=QFrame(); f.setObjectName("Card"); return f
    def sec(self,t):
        x=QLabel(t); x.setObjectName("Section"); x.setWordWrap(True); x.setMinimumWidth(0); x.setToolTip(t); return x

    def select_feature(self,index):
        self.stack.setCurrentIndex(index)
        for i,button in enumerate(self.nav_buttons):
            button.setChecked(i == index)
        if index == 4:
            self.set_log_badge(False)
        if hasattr(self,'vswr_x_axis'):
            refresh_theme(self,self.dark,self.theme_name)   # selected rail icon colour

    def set_log_badge(self, visible=True):
        if hasattr(self, 'log_badge'):
            self.log_badge.setVisible(bool(visible) and self.stack.currentIndex() != 4)

    def set_controls(self,on):
        controls=(self.region,self.tx,self.rx,self.power,self.session,self.command,self.filter_edit,self.filter_exact_cb,self.write_operation,self.target_epc,self.epc_edit_old,self.epc_edit_new,self.write_bank,self.write_address,self.write_data,self.write_access_password,self.use_selected,self.read_target_epc,self.read_bank,self.read_address,self.read_words,self.read_use_selected,self.lock_target_epc,self.lock_access_password,self.lock_mask,self.lock_action,self.lock_use_selected,self.kill_target_epc,self.kill_password,self.kill_use_selected,self.inventory_antennas,self.gen2_target,self.gen2_q,self.rf_profile,self.fast_mode,self.frequency_hop,self.rf_refresh,self.rf_apply,self.net_ip,self.net_mask,self.net_gateway,self.net_dns,self.net_config_port,self.http_url,self.http_timeout,self.http_active,self.http_start_boot,self.vswr_antenna,self.vswr_start,self.vswr_stop,self.vswr_step,self.vswr_scan_button,self.vswr_stop_button,self.vswr_export_button,self.buzzer_duration,self.buzzer_tone,self.buzzer_button)
        for w in controls:
            if w is not None:w.setEnabled(on)
        for w in getattr(self,'gpo_state',[]): w.setEnabled(on)
        for w in getattr(self,'gpo_duration',[]): w.setEnabled(on)
        for w in getattr(self,'gpo_buttons',[]): w.setEnabled(on)
        for name in ('wifi_enable','wifi_mode','wifi_ssid','wifi_password','wifi_open','upload_interface','wifi_refresh','wifi_apply','upload_if_apply','diag_refresh','diag_clear'):
            if hasattr(self,name): getattr(self,name).setEnabled(on)
        for w in (self.work_mode_passive,self.work_mode_active): w.setEnabled(on)
        self.return_passive.setVisible(on and self.work_mode == 'ACTIVE')
        self.write_tag_btn.setEnabled(on and not self.inventory)
        self.read_tag_btn.setEnabled(on and not self.inventory)
        self.lock_tag_btn.setEnabled(on and not self.inventory)
        self.kill_tag_btn.setEnabled(on and not self.inventory)
        self.single.setEnabled(on and not self.inventory)
        self.multi.setEnabled(on and not self.inventory)
        self.sync.setEnabled(on and not self.inventory)
        self.stop.setEnabled(on and self.inventory)
        if hasattr(self,'rfid_nav_button'): self.rfid_nav_button.setEnabled(True)

    def toggle(self):
        if self.connected:
            # Do a real firmware-level transport release before closing
            # the current USB/Ethernet connection.
            if self.release_pending:
                return

            self.manual_disconnect=True
            self.release_pending=True

            # Do not allow old GUI commands to be sent after RELEASE.
            self.pending=None
            self.queue=[]
            self.pump_scheduled=False
            self.info_scheduled=False

            self.log("> RELEASE")
            self.io.send("RELEASE")
            QTimer.singleShot(2000, self.force_disconnect)
        else:
            self.manual_disconnect=False
            self.release_pending=False

            p=self.port.currentData()
            if p:
                if p == WIFI_OPTION and not self.tcp_host():
                    self.statusBar().showMessage('Enter the reader WiFi IP (Settings > WiFi) or use Discover readers.',5000)
                    return
                self.io.connect(
                    p,
                    int(self.baud.currentData()),
                    self.tcp_host()
                )
    
    def force_disconnect(self):
        if self.release_pending:
            self.release_pending=False
            self.log("RELEASE timeout. Forcing disconnect.")
            self.io.disconnect()

    def on_state(self,on,detail):
        self.connection_generation += 1
        generation=self.connection_generation
        self.connected=on
        if on:
            self.connect.setText("Disconnect"); self.conn.setText("● Connected"); self.conn.setToolTip(detail); self.conn.setStyleSheet(f"color:{theme.colors(self.dark,self.theme_name)['success']};font-weight:700"); self.kv[0].setText("Online"); self.set_controls(True); self.log("Connected: "+detail)
            self.info_scheduled=True
            QTimer.singleShot(500, lambda g=generation: self.request_info_staged(g))
        else:
            self.connect.setText("Connect"); self.conn.setText("● Disconnected"); self.conn.setToolTip(""); self.conn.setStyleSheet(""); self.kv[0].setText("Offline"); self.apply_work_mode(''); self.pending=None; self.epc_write_pending=''; self.queue=[]; self.pump_scheduled=False; self.info_scheduled=False; self.set_controls(False); self.log("Disconnected")
            reconnect=self.reconnect.isChecked() and not self.manual_disconnect
            self.manual_disconnect=False
            if reconnect:
                QTimer.singleShot(1200, lambda g=generation: self.try_reconnect(g))
        self.statusBar().showMessage("Connected" if on else "Disconnected")
    def try_reconnect(self,generation=None):
        if generation is not None and generation != self.connection_generation:
            return
        if not self.connected and self.reconnect.isChecked() and self.port.currentData():self.io.connect(self.port.currentData(),int(self.baud.currentData()),self.tcp_host())
    
    def refresh_ports(self):
        def worker():
            ps=SerialManager.ports()
            self.ports_ready.emit(ps)
        threading.Thread(target=worker, daemon=True).start()

    def on_ports_ready(self, ps):
        old=self.port.currentData(); self.port.blockSignals(True); self.port.clear()
        for p in ps:
            self.port.addItem(str(p.device),p.device)
            self.port.setItemData(self.port.count()-1, f"{p.device} — {p.description}", Qt.ToolTipRole)
        if not ps:self.port.addItem("No VCP ports found",None)
        self.port.addItem("Ethernet",ETHERNET_OPTION)
        self.port.setItemData(self.port.count()-1, f"Ethernet — {self.ethernet_host()}:{ETHERNET_PORT}", Qt.ToolTipRole)
        self.port.addItem("WiFi",WIFI_OPTION)
        wifi_host = self.wifi_host.text().strip() if hasattr(self,'wifi_host') else ''
        self.port.setItemData(self.port.count()-1, f"WiFi — {wifi_host or 'set the reader WiFi IP'}:{ETHERNET_PORT}", Qt.ToolTipRole)

        target=old or self.settings.value("port","",type=str)
        if target:
            for i in range(self.port.count()):
                if self.port.itemData(i)==target:
                    self.port.setCurrentIndex(i)
                    break
        self.port.blockSignals(False)

    def ethernet_host(self):
        # Reader IP from the Network & HTTP page (filled by Discover readers
        # or GET_NETWORK); the old fixed address only as a fallback.
        host=self.net_ip.text().strip() if hasattr(self,'net_ip') else ''
        return host or ETHERNET_HOST

    def enqueue(self,cmd):
        if not self.connected:
            self.statusBar().showMessage("Connect to STM32 first.",3000)
            return
        self.queue.append(cmd)
        self.pump()

    def request(self,cmd):
        if self.inventory:
            self.statusBar().showMessage("Stop inventory before reading reader settings.",4000)
            return
        self.enqueue(cmd)

    def request_info(self):
        if self.connected and not self.inventory:
            # Avoid duplicating an existing startup/info batch.
            if any(cmd in self.queue for cmd in INFO) or self.pending:
                return
            self.queue.extend(INFO)
            self.pump()

    def request_info_staged(self,generation=None):
        self.info_scheduled=False
        if generation is not None and generation != self.connection_generation:
            return
        if self.connected and not self.inventory:
            # Never cancel an in-flight transaction from a previous callback.
            # Only append startup information when the transport is still the
            # same connection and the command path is idle.
            if self.pending or self.queue:
                return
            # GET_STATUS first: a reader in active mode is already
            # inventorying and would answer the settings queries with ERROR.
            self.queue.extend(['GET_STATUS']+INFO)
            self.pump()

    def pump(self):
        if self.pending or not self.queue or not self.connected or self.pump_scheduled:
            return

        # Keep a small inter-command gap for the STM32/E310 without adding
        # unnecessary GUI latency. Responses are still serialized by pending.
        self.pump_scheduled=True
        QTimer.singleShot(100, self._pump_now)

    def _pump_now(self):
        self.pump_scheduled=False
        if self.pending or not self.queue or not self.connected:
            return
        cmd=self.queue.pop(0)
        self.pending=[cmd,time.monotonic()]
        self.io.send(cmd)
    def on_tx(self,cmd):self.log("> "+cmd)
    def check_timeout(self):
        if self.pending:
            cmd=self.pending[0]
            # Allow slower reader-info transactions to complete before the GUI
            # declares a timeout. A late response must never be mistaken for
            # the response to the next queued command.
            timeout=5.0
            if cmd.startswith('SINGLE_POLL,'):
                try:
                    requested_ms=int(cmd.split(',',1)[1])
                    timeout=max(timeout, requested_ms/1000.0 + 1.0)
                except (ValueError, IndexError):
                    pass
            elif cmd.startswith('SYNC_POLL'):
                try:
                    parts=cmd.split(',',1)
                    requested_ms=int(parts[1]) if len(parts) == 2 else 1000
                    # SYNC_POLL can retry the RFID transaction up to three times.
                    # Keep the GUI alive for the firmware worst-case instead of
                    # releasing pending while the STM32 may still be executing.
                    timeout=max(timeout, 3.0 * (5.0 + requested_ms/1000.0) + 0.5)
                except (ValueError, IndexError):
                    timeout=max(timeout, 18.5)
            if time.monotonic()-self.pending[1]>timeout:
                self.log(f"TIMEOUT waiting for {cmd}")
                if cmd.startswith('WRITE_EPC,'):
                    self.epc_write_pending=''
                self.pending=None; self.pump()
    def status_poll(self):
        # Polled during inventory too, so a reader stopped elsewhere (work
        # mode, another tool) never leaves the GUI stuck at LIVE.
        if self.connected and not self.vswr_scanning and not self.pending and not self.queue:self.enqueue("GET_STATUS")

    def on_raw(self,data):
        if self.vswr_serial_mode and self.vswr_bridge:
            self.vswr_bridge.feed(data)
        self.raw.appendPlainText(data.decode('utf-8','replace'))

    def response_matches_pending(self,kind,data):
        if not self.pending:
            return False

        cmd=self.pending[0]
        base=cmd.split(',',1)[0].strip().upper()

        expected_kind={
            'GET_VERSION':'VERSION',
            'GET_SERIAL':'SERIAL',
            'GET_HARDWARE':'HARDWARE',
            'GET_CAPABILITIES':'CAPABILITIES',
            'GET_TEMPERATURE':'TEMP',
            'GET_REGION':'REGION',
            'GET_ANTENNA':'ANTENNA',
            'GET_POWER':'POWER',
            'GET_PROTOCOL':'PROTOCOL',
            'GET_SESSION':'SESSION',
            'GET_FREQUENCY':'FREQ',
            'GET_GPI':'GPI',
            'GET_NETWORK':'NETWORK',
            'GET_STATUS':'STATUS',
            'GET_WORK_MODE':'WORK_MODE',
            'GET_WIFI':'WIFI',
            'GET_DIAG':'DIAG',
        }.get(base)

        # All command-style operations complete with OK,<COMMAND>
        # or ERROR,<COMMAND>. READ_DATA is intermediate data and must
        # not release the pending transaction by itself.
        # The firmware answers a command it does not know with
        # ERROR,INVALID_COMMAND. Only one command is in flight, so that
        # reply belongs to it; without this the queue stalls for the full
        # timeout on every unsupported command.
        if kind=='ERROR':
            return data == base or data == 'INVALID_COMMAND'

        if expected_kind is not None:
            if base == 'GET_HARDWARE' and kind == 'VERSION':
                return True
            return kind == expected_kind

        if base == 'VSWR_SCAN':
            return (kind == 'OK' and data == base) or kind == 'VSWR_DONE'

        return kind=='OK' and data == base

    def on_line(self,line):
        self.responses.appendPlainText(line); kind,data=parse_line(line)

        if kind == 'ERROR':
            self.set_log_badge(True)

        # During a transport handoff, ignore everything except the firmware's
        # explicit RELEASE acknowledgement. This prevents stale responses or
        # TAG frames from the old connection from entering the new session.
        if self.release_pending:
            self.log("< "+line)
            if kind=='OK' and data=='RELEASE':
                self.release_pending=False
                self.log("Transport released.")
                self.io.disconnect()
            return

        # A stale/unsolicited response must never complete the current GUI
        # transaction. Previously any non-TAG line cleared pending, which
        # shifted the whole command queue when an old STATUS/TAG response
        # arrived before the requested response.
        matched=self.response_matches_pending(kind,data)
        if kind!='TAG' and self.pending and matched:
            self.pending=None
            self.pump()

        if kind=='TAG':
            self.handle_tag(data)
            return

        if kind in ('VSWR', 'VSWR_POINT'):
            self.handle_vswr_point(data)
            return

        self.log("< "+line)
        if kind=='OK':
            if data=='START':self.set_inventory(True)
            elif data=='STOP':self.set_inventory(False)
            elif data=='WRITE_TAG':
                self.log('Tag memory write successful.')
                self.statusBar().showMessage('Tag memory write successful.',5000)
            elif data=='WRITE_EPC':
                self.log('EPC write successful.')
                self.statusBar().showMessage('EPC write successful.',5000)
                new_epc=self.epc_write_pending
                self.epc_write_pending=''
                self.refresh_after_epc_write(new_epc)
            elif data=='LOCK_TAG':
                self.log('Tag lock successful.')
                self.statusBar().showMessage('Tag lock successful.',5000)
            elif data=='KILL_TAG':
                self.log('Tag kill successful.')
                self.statusBar().showMessage('Tag kill successful.',5000)
            elif data=='VSWR_SCAN':
                self.vswr_scanning=False
                self.vswr_scan_button.setEnabled(self.connected)
                self.vswr_stop_button.setEnabled(False)
            elif data=='SET_WIFI':
                self.statusBar().showMessage('WiFi settings saved. The reader is reconnecting to WiFi.',6000)
            elif data=='SET_UPLOAD_IF':
                self.statusBar().showMessage('Upload interface saved.',4000)
            elif data=='CLEAR_DIAG':
                self.request('GET_DIAG')
            elif data=='SET_HTTP':
                message='HTTP settings saved. They are used the next time the reader is in active mode.'
                self.log(message); self.statusBar().showMessage(message,6000)
            elif data=='SET_WORK_MODE':
                self.apply_work_mode(self.work_mode_requested)
                self.statusBar().showMessage(f'Reader is now in {self.work_mode_requested.lower()} mode.',5000)
                self.enqueue('GET_STATUS')   # active mode may start inventory, passive stops it
            elif data=='VSWR_STOP':
                self.vswr_scanning=False
                self.vswr_scan_button.setEnabled(self.connected)
                self.vswr_stop_button.setEnabled(False)
        elif kind=='STATUS':
            v=data.get('_csv',[]); running=bool(v and v[0].upper()=='INVENTORY')
            self.set_inventory(running)
            if running and any(cmd in self.queue for cmd in INFO if cmd!='GET_WORK_MODE'):
                # Settings queries fail while the module inventories; read
                # them once the reader is stopped.
                self.queue=[cmd for cmd in self.queue if cmd not in INFO or cmd=='GET_WORK_MODE']
                self.info_deferred=True
                self.statusBar().showMessage('Reader is running inventory. Press Stop, or Return to passive if it is in active mode.',8000)
        elif kind=='TEMP':
            v=data.get('_csv',[])
            if v:self.kv[5].setText(v[0]+" °C")
        elif kind=='VERSION':
            self.apply_capabilities(data)
            if data.get('HWVER'):
                self.module_model.setText(str(data['HWVER']))
            if data.get('FWVER'):
                self.module_firmware.setText(str(data['FWVER']))
        elif kind=='REGION':
            v=data.get('_csv',[])
            if v:self.region.setText(v[0])
        elif kind=='ANTENNA':self.apply(data,self.tx,'TX');self.apply(data,self.rx,'RX')
        elif kind=='POWER':
            if 'READ' in data:
                try:self.power.setValue(float(data['READ'])/100.0)
                except:pass
        elif kind=='SESSION':
            v=data.get('_csv',[])
            if v:
                try:self.session.setValue(int(v[0]))
                except:pass
        elif kind in ('HARDWARE','CAPABILITIES'):
            self.apply_capabilities(data)
        elif kind=='GPI':
            self.apply_gpi(data)
        elif kind=='GPO':
            self.log('GPO state updated.')
        elif kind=='NETWORK':
            self.apply_network(data)
        elif kind=='WIFI':
            self.apply_wifi(data)
        elif kind=='DIAG':
            self.apply_diag(data)
        elif kind=='WORK_MODE':
            values=data.get('_csv',[])
            if values:self.apply_work_mode(values[0].upper())
        elif kind=='FREQ':
            self.apply_frequency_table(data)
        elif kind=='HTTP':
            self.log('HTTP active-mode configuration updated.')
        elif kind=='VSWR_DONE':
            self.vswr_scanning=False
            self.vswr_scan_button.setEnabled(self.connected)
            self.vswr_stop_button.setEnabled(False)
        elif kind=='READ_DATA':
            bank=data.get('BANK','?'); addr=data.get('ADDR','?'); words=data.get('WORDS','?'); payload=data.get('DATA','')
            self.last_read_data=payload.upper()
            self.read_result.setText(self.last_read_data)
            self.hint.setText(f"Read bank {bank}, word {addr}, {words} word(s)")
            self.statusBar().showMessage("Tag memory read successful.",5000)
        elif kind=='ERROR':
            if data=='WRITE_EPC':
                self.epc_write_pending=''
            operation_messages={
                'WRITE_TAG':'Tag memory write failed',
                'WRITE_EPC':'EPC write failed',
                'LOCK_TAG':'Tag lock failed',
                'KILL_TAG':'Tag kill failed',
            }
            if data=='SET_WORK_MODE':
                self.apply_work_mode(self.work_mode)   # undo the optimistic switch
            if data in ('START','STOP','SET_WORK_MODE'):
                self.enqueue('GET_STATUS')   # resynchronise the inventory state
            if data=='INVALID_COMMAND':
                self.log('Command not supported by this reader firmware.')
                self.statusBar().showMessage('Command not supported by this reader firmware.',5000)
                return
            prefix=operation_messages.get(data,'STM32 error')
            self.log(f'{prefix}: STM32 ERROR,{data}')
            hint=' - reader is busy with inventory, press Stop first' if self.inventory and data not in ('START','STOP') else ''
            self.statusBar().showMessage(f'{prefix}: STM32 ERROR,{data}{hint}',6000)
    @staticmethod
    def apply(d,w,k):
        if k in d:
            try:w.setValue(int(d[k]))
            except:pass

    def request_capabilities(self):
        if not self.connected:
            self.statusBar().showMessage('Connect to a reader first.',3000)
            return
        self.queue.extend(['GET_VERSION','GET_CAPABILITIES','GET_ANTENNA','GET_GPI'])
        self.pump()

    def apply_capabilities(self, data):
        values = data if isinstance(data, dict) else {}
        model = values.get('MODEL') or values.get('MODULE') or values.get('MODULE_MODEL')
        firmware = values.get('FIRMWARE') or values.get('VERSION') or values.get('MODULE_VERSION')
        if model:
            self.module_model.setText(str(model))
        if firmware:
            self.module_firmware.setText(str(firmware))
        count = values.get('ANTENNAS') or values.get('ANTENNA_COUNT') or values.get('PORTS')
        try:
            if count is not None:
                count = max(1, min(64, int(count)))
                self.antenna_count.setValue(count)
                self.vswr_antenna.setRange(1, count)
                self.vswr_antenna.setValue(min(self.vswr_antenna.value(), count))
        except (TypeError, ValueError):
            pass
        self.module_capabilities.update(values)

    def apply_network(self, data):
        if not isinstance(data, dict):
            return
        pairs = ((self.net_ip,'IP'), (self.net_mask,'MASK'), (self.net_gateway,'GATEWAY'), (self.net_dns,'DNS'))
        for widget, key in pairs:
            if key in data:
                widget.setText(str(data[key]))
        if 'PORT' in data:
            try:self.net_config_port.setValue(int(data['PORT']))
            except (TypeError, ValueError):pass

    def apply_frequency_table(self, data):
        """Keep the VSWR sweep inside the reader's region: the module rejects
        frequencies outside its hop table (status 0x0105)."""
        try:
            frequencies = sorted(int(value) for value in data.get('_csv', []))
        except (TypeError, ValueError):
            return
        if not frequencies:
            return
        self.region_frequencies = frequencies
        low, high = frequencies[0], frequencies[-1]
        if low <= self.vswr_start.value() < self.vswr_stop.value() <= high:
            return
        step = 250
        while (high - low) // step + 1 > 78:  # one request holds at most 78 points
            step += 250
        self.vswr_start.setValue(low)
        self.vswr_stop.setValue(high)
        self.vswr_step.setValue(step)

    def apply_gpi(self, data):
        if not isinstance(data, dict):
            return
        for index, label in enumerate(self.gpi_labels, 1):
            key = f'GPI{index}'
            value = data.get(key, data.get(str(index)))
            if value is not None:
                label.setText(f'GPI {index}: {value}')
        self.log('GPI state refreshed.')

    def apply_rf_settings(self):
        if not self.connected:
            self.statusBar().showMessage('Connect to a reader first.',3000)
            return
        antennas = self.inventory_antennas.text().replace(' ','')
        try:
            antenna_values = [int(part) for part in antennas.split(',')]
        except (TypeError, ValueError):
            antenna_values = []
        if not antennas or not antenna_values or any(value < 1 or value > self.antenna_count.value() for value in antenna_values):
            QMessageBox.warning(self,'Invalid antennas','Enter a comma-separated list such as 1,2,3.')
            return
        # Apply commands implemented by the supplied firmware immediately.
        # The richer antenna-list and Gen2 settings remain ready in the UI
        # until newer firmware advertises their command capabilities.
        try:
            region_value = int(self.region.text().replace('0x', '').replace('0X', ''), 16)
        except ValueError:
            QMessageBox.warning(self, 'Invalid Region', 'Enter a hex byte such as 01.')
            return
        self.enqueue(f'SET_ANTENNA,{self.tx.value()},{self.rx.value()}')
        self.enqueue(f'SET_POWER,{int(round(self.power.value()*100.0))}')
        self.enqueue(f'SET_REGION,{region_value:02X}')
        self.enqueue(f'SET_SESSION,{self.session.value()}')
        if self.module_capabilities.get('SET_GEN2') in (1, '1', True, 'true', 'TRUE'):
            self.enqueue(f'SET_GEN2,{self.gen2_target.currentText()},{self.gen2_q.value()},{self.rf_profile.currentIndex()},{int(self.fast_mode.isChecked())},{int(self.frequency_hop.isChecked())}')
        else:
            self.log('Advanced Gen2 options retained locally; waiting for module capability SET_GEN2.')

    def set_gpo(self, number):
        state = int(self.gpo_state[number - 1].currentData())
        duration = self.gpo_duration[number - 1].value()
        self.enqueue(f'SET_GPO,{number},{state},{duration}')

    def sound_buzzer(self):
        self.enqueue(f'SET_BUZZER,{self.buzzer_duration.value()},{self.buzzer_tone.value()}')

    def apply_http_config(self):
        url = self.http_url.text().strip()
        if not url.lower().startswith('http://'):
            QMessageBox.warning(self,'Invalid HTTP endpoint','The current firmware accepts an http:// endpoint with an IPv4 host.')
            return
        if any(c in url for c in ('\r','\n',',','"','\\')):
            QMessageBox.warning(self,'Invalid HTTP endpoint','The endpoint cannot contain commas or line breaks.')
            return
        # HTTP settings only; the work mode has its own control (SET_WORK_MODE).
        self.enqueue(f'SET_HTTP,{url},{self.http_timeout.value()},{int(self.http_start_boot.isChecked())}')

    def set_work_mode(self, mode):
        if not self.connected:
            self.apply_work_mode(self.work_mode)
            self.statusBar().showMessage('Connect to a reader first.',3000)
            return
        if mode == self.work_mode:
            self.apply_work_mode(mode)
            return
        self.work_mode_requested = mode
        self.apply_work_mode(self.work_mode)   # stays until the reader confirms
        self.enqueue(f'SET_WORK_MODE,{mode}')

    def apply_work_mode(self, mode):
        self.work_mode = mode
        self.work_mode_value.setText({'ACTIVE':'Active','PASSIVE':'Passive'}.get(mode,'--'))
        self.work_mode_passive.setChecked(mode == 'PASSIVE')
        self.work_mode_active.setChecked(mode == 'ACTIVE')
        self.return_passive.setVisible(self.connected and mode == 'ACTIVE')

    def return_to_passive(self):
        """One-click recovery from active mode: stop whatever inventory is
        running, switch the reader to passive and re-read its state, ahead of
        anything still queued."""
        if not self.connected:
            self.statusBar().showMessage('Connect to a reader first.',3000)
            return
        self.work_mode_requested='PASSIVE'
        self.queue=['STOP','SET_WORK_MODE,PASSIVE','GET_STATUS']+INFO
        self.info_deferred=False
        self.pending=None
        self.statusBar().showMessage('Stopping inventory and switching the reader to passive...',4000)
        self.pump()

    # ------------------------------------------------------------ WiFi
    def tcp_host(self):
        """Address for the selected TCP link: the WiFi IP for WiFi, the
        Ethernet IP from Network & HTTP otherwise."""
        if self.port.currentData() == WIFI_OPTION:
            return self.wifi_host.text().strip() if hasattr(self,'wifi_host') else ''
        return self.ethernet_host()

    def apply_wifi(self, data):
        if not isinstance(data, dict):
            return
        state = {'CONNECTED':'connected', 'CONNECTING':'connecting', 'AP':'access point on',
                 'IDLE':'idle', 'DISABLED':'off', 'ERROR':'error'}.get(data.get('STATE',''), data.get('STATE','?').lower())
        ip = data.get('IP','0.0.0.0')
        parts = []
        if data.get('LINK') != 'ONLINE':
            parts.append('WiFi module not answering')
        else:
            parts.append(f"WiFi {state}")
            if data.get('SSID'): parts.append(f"SSID {data.get('SSID')}")
            if ip != '0.0.0.0': parts.append(ip)
            if data.get('STATE') == 'CONNECTED' and data.get('RSSI'): parts.append(f"{data.get('RSSI')} dBm")
            if data.get('HOST') == '1': parts.append('WiFi client connected')
        errors = data.get('ERRORS','0')
        if errors not in ('0',''): parts.append(f"link errors {errors}")
        self.wifi_status.setText('  ·  '.join(parts))
        if ip != '0.0.0.0':
            self.wifi_host.setText(ip)
        if not self.wifi_form_dirty:
            self.wifi_enable.setChecked(data.get('ENABLE') == '1')
            index = self.wifi_mode.findData(data.get('MODE','STA'))
            if index >= 0: self.wifi_mode.setCurrentIndex(index)
            self.wifi_ssid.setText(data.get('SSID',''))
            self.wifi_password.clear(); self.wifi_open.setChecked(False)
            index = self.upload_interface.findData(data.get('IF','AUTO'))
            if index >= 0: self.upload_interface.setCurrentIndex(index)
            self.wifi_form_dirty = False

    def mark_wifi_dirty(self, *args):
        self.wifi_form_dirty = True

    def apply_wifi_settings(self):
        if not self.connected:
            self.statusBar().showMessage('Connect to a reader first.',3000)
            return
        enable = 1 if self.wifi_enable.isChecked() else 0
        mode = self.wifi_mode.currentData()
        ssid = self.wifi_ssid.text().strip()
        if self.wifi_open.isChecked():
            password = ''
        else:
            password = self.wifi_password.text() or '****'
        if enable and not ssid:
            QMessageBox.warning(self,'Network name missing','Enter the WiFi network name.')
            return
        if len(ssid) > 32 or any(c in ssid for c in ',\r\n'):
            QMessageBox.warning(self,'Invalid network name','The network name can have at most 32 characters and no commas.')
            return
        if password not in ('', '****') and not (8 <= len(password) <= 64):
            QMessageBox.warning(self,'Invalid password','A WiFi password has 8 to 64 characters. Tick "Open network" for a network without a password.')
            return
        if any(c in password for c in '\r\n'):
            QMessageBox.warning(self,'Invalid password','The password cannot contain line breaks.')
            return
        if self.port.currentData() == WIFI_OPTION:
            answer = QMessageBox.question(self,'Connected over WiFi',
                'TAGlink is connected to the reader over WiFi. Changing the WiFi settings will drop this connection; reconnect afterwards (Discover readers finds the new address).\n\nApply the new settings?')
            if answer != QMessageBox.Yes:
                return
        self.wifi_form_dirty = False
        self.enqueue(f'SET_WIFI,{enable},{mode},{ssid},{password}')
        QTimer.singleShot(6000, lambda: self.connected and self.request('GET_WIFI'))

    def apply_upload_interface(self):
        if not self.connected:
            self.statusBar().showMessage('Connect to a reader first.',3000)
            return
        self.enqueue(f'SET_UPLOAD_IF,{self.upload_interface.currentData()}')

    def apply_diag(self, data):
        if not isinstance(data, dict):
            return
        names = {'RESET':'Last reset cause','UPTIME_MS':'Uptime (ms)','FAULTS':'Faults since power-on','LAST_FAULT':'Fault that ended the previous run',
                 'PC':'Fault PC','LR':'Fault LR','CFSR':'CFSR','HFSR':'HFSR','MMFAR':'MMFAR','BFAR':'BFAR','AT_MS':'Fault at uptime (ms)',
                 'LOOP_MAX_US':'Longest main-loop pass (µs)','PASSES':'Main-loop passes','STACK_USED':'Stack used (bytes)','STACK_SIZE':'Stack size (bytes)','BOR':'Brown-out level'}
        self.diag_table.setRowCount(0)
        for key, value in data.items():
            if key == '_csv':
                continue
            label = names.get(key)
            if label is None and key.endswith('_US'):
                label = f"Longest {key[:-3].lower()} task (µs)"
            row = self.diag_table.rowCount(); self.diag_table.insertRow(row)
            self.diag_table.setItem(row,0,QTableWidgetItem(label or key)); self.diag_table.setItem(row,1,QTableWidgetItem(str(value)))

    def discover_readers(self):
        self.statusBar().showMessage('Searching for readers on the local network...',3000)
        def worker():
            readers = SerialManager.discover()
            for reader in readers:
                reader['link'] = 'udp'
            # WiFi readers have no UDP discovery: scan for the text port.
            known = {reader.get('ip') for reader in readers}
            if self.connected and self.port.currentData() in (ETHERNET_OPTION, WIFI_OPTION):
                # The reader serves one TCP client: show the one we use.
                current = self.tcp_host()
                if current and current not in known:
                    readers.append({'ip': current, 'mac': '', 'port': ETHERNET_PORT, 'board': '', 'firmware': '', 'mode': 'Connected (this program)', 'link': 'tcp'})
                    known.add(current)
            readers += SerialManager.scan_tcp(skip=known)
            self.discovered_readers = readers
            self.discovery_ready_signal.emit(readers)
        threading.Thread(target=worker, daemon=True).start()

    def populate_discovery(self, readers):
        self.discovery_table.setRowCount(0)
        for reader in readers:
            row = self.discovery_table.rowCount()
            self.discovery_table.insertRow(row)
            mode = reader.get('mode','')
            if reader.get('link') == 'tcp' and mode == 'TCP':
                mode = 'TCP 5000 (WiFi or Ethernet)'
            elif reader.get('link') == 'tcp' and mode == 'Busy':
                mode = 'Busy: another link holds the reader'
            values = [reader.get('ip',''), reader.get('mac',''), str(reader.get('port','')), reader.get('board',''), reader.get('firmware',''), mode]
            for column, value in enumerate(values):
                item = QTableWidgetItem(str(value)); item.setData(Qt.UserRole, reader.get('link','udp'))
                self.discovery_table.setItem(row,column,item)
        self.statusBar().showMessage(f'{len(readers)} reader(s) discovered.',4000)

    def use_discovered_reader(self):
        rows = self.discovery_table.selectionModel().selectedRows()
        if not rows:
            return
        ip = self.discovery_table.item(rows[0].row(),0)
        if ip and ip.data(Qt.UserRole) == 'tcp':
            # Found by the TCP scan only: a WiFi address.
            self.wifi_host.setText(ip.text())
            self.port.setCurrentIndex(self.port.findData(WIFI_OPTION))
        elif ip:
            self.net_ip.setText(ip.text())
            self.port.setCurrentIndex(self.port.findData(ETHERNET_OPTION))

    def start_vswr_scan(self):
        if not self.connected:
            self.statusBar().showMessage('Connect to a reader first.',3000)
            return
        if self.inventory:
            self.statusBar().showMessage('Stop inventory before a VSWR scan.',4000)
            return
        if self.vswr_stop.value() <= self.vswr_start.value() or self.vswr_step.value() <= 0:
            QMessageBox.warning(self,'Invalid sweep','Stop frequency must be above start frequency and step must be positive.')
            return
        if self.region_frequencies and not (self.region_frequencies[0] <= self.vswr_start.value() and self.vswr_stop.value() <= self.region_frequencies[-1]):
            QMessageBox.warning(self,'Outside reader region',f'The reader region allows {self.region_frequencies[0]} to {self.region_frequencies[-1]} kHz. Adjust the sweep or change the region.')
            return
        if (self.vswr_stop.value() - self.vswr_start.value()) // self.vswr_step.value() + 1 > 78:
            QMessageBox.warning(self,'Too many points','One VSWR scan holds at most 78 frequencies. Increase the step.')
            return
        if self.port.currentData() == WIFI_OPTION:
            QMessageBox.information(self,'Not available over WiFi','A VSWR scan uses the module bridge, which the reader offers over USB, serial and Ethernet only. Connect over one of those to run a scan.')
            return
        if self.port.currentData() == ETHERNET_OPTION and not self.net_ip.text().strip():
            QMessageBox.warning(self,'No module address','Enter the reader IP on Network & HTTP, or select a discovered reader.')
            return
        self.vswr_samples.clear()
        self.vswr_table.setRowCount(0)
        self.vswr_series.clear()
        self.vswr_threshold_series.clear()
        set_vswr_x_range(self, self.vswr_start.value(), self.vswr_stop.value())
        self.vswr_y_axis.setRange(1.0, 3.0)
        self.vswr_summary.setText('Scanning...')
        self.vswr_scanning=True
        self.vswr_scan_button.setEnabled(False)
        self.vswr_stop_button.setEnabled(True)
        self.vswr_serial_mode = self.port.currentData() != ETHERNET_OPTION
        if self.vswr_serial_mode:
            self.io.set_binary_mode(True)
        self.vswr_bridge = VSWRBridge(
            self.net_ip.text().strip(),
            self.net_config_port.value(),
            self.vswr_antenna.value(),
            self.vswr_start.value(),
            self.vswr_stop.value(),
            self.vswr_step.value(),
            int(round(self.power.value() * 100.0)),
            self.region.text(),
            self.vswr_sample_signal.emit,
            self.vswr_done_signal.emit,
            self.vswr_error_signal.emit,
            self.io.send_bytes if self.vswr_serial_mode else None,
        )
        self.vswr_bridge.start()

    def stop_vswr_scan(self):
        if self.vswr_scanning and self.vswr_bridge:
            self.vswr_bridge.stop()

    def on_vswr_done(self):
        self.vswr_scanning=False
        self.vswr_scan_button.setEnabled(self.connected)
        self.vswr_stop_button.setEnabled(False)
        self.vswr_bridge=None
        if self.vswr_serial_mode:
            self.io.set_binary_mode(False)
            self.vswr_serial_mode=False
        if self.vswr_samples:
            self.statusBar().showMessage('VSWR scan completed.',4000)

    def on_vswr_error(self, message):
        self.vswr_scanning=False
        self.vswr_scan_button.setEnabled(self.connected)
        self.vswr_stop_button.setEnabled(False)
        self.vswr_bridge=None
        if self.vswr_serial_mode:
            self.io.set_binary_mode(False)
            self.vswr_serial_mode=False
        self.vswr_summary.setText(message)
        self.statusBar().showMessage(message,5000)
        self.log('VSWR: '+message)

    def handle_vswr_point(self, data):
        if not isinstance(data, dict):
            return
        def number(*keys):
            for key in keys:
                if key in data:
                    try:return float(data[key])
                    except (TypeError, ValueError):return None
            return None
        frequency = number('FREQ','FREQUENCY','KHZ')
        return_loss = number('RL','RETURN_LOSS','LOSS')
        vswr = number('VSWR','SWR')
        if vswr is None and return_loss is not None:
            reflection = 10.0 ** (-return_loss / 20.0)
            if reflection < 1.0:
                vswr = (1.0 + reflection) / (1.0 - reflection)
        if frequency is None or vswr is None:
            return
        self.vswr_samples.append((frequency, return_loss, vswr))
        row = self.vswr_table.rowCount()
        self.vswr_table.insertRow(row)
        values = [f'{frequency:.0f}', '' if return_loss is None else f'{return_loss:.2f}', f'{vswr:.3f}', 'PASS' if vswr <= 1.5 else 'CHECK']
        for column, value in enumerate(values):
            self.vswr_table.setItem(row,column,QTableWidgetItem(value))
        self.vswr_series.append(frequency, vswr)
        self.vswr_threshold_series.append(frequency, 1.5)
        frequencies = [sample[0] for sample in self.vswr_samples]
        set_vswr_x_range(self, min(frequencies), max(frequencies))
        self.vswr_y_axis.setRange(1.0, max(2.0, max(sample[2] for sample in self.vswr_samples) * 1.15))
        self.vswr_y_axis.applyNiceNumbers()
        peak = max(sample[2] for sample in self.vswr_samples)
        self.vswr_summary.setText(f'{len(self.vswr_samples)} samples - peak VSWR {peak:.3f}')

    def export_vswr(self):
        if not self.vswr_samples:
            self.statusBar().showMessage('No VSWR samples to export.',3000)
            return
        path,_=QFileDialog.getSaveFileName(self,'Export VSWR Scan','vswr_scan.csv','CSV (*.csv)')
        if not path:return
        with open(path,'w',newline='',encoding='utf-8') as stream:
            writer=csv.writer(stream); writer.writerow(['Frequency (kHz)','Return loss (dB)','VSWR'])
            writer.writerows(self.vswr_samples)

    def multi_poll(self):
        if self.clear_start.isChecked():
            self.clear_inv()
        self.poll_mode='MULTI'
        self.queue.clear(); self.pending=None
        self.enqueue("START")

    def sync_poll(self):
        self.enqueue("SYNC_POLL")

    def single_poll(self):
        if self.clear_start.isChecked():
            self.clear_inv()
        self.poll_mode='SINGLE'
        timeout_ms=self.single_poll_timeout_ms

        # Do not discard an in-flight transaction. That can make its eventual
        # response look like the response to SINGLE_POLL and desynchronize the
        # GUI command queue. Replace only commands that have not been sent yet.
        self.queue=[cmd for cmd in self.queue if not cmd.startswith(('START','STOP','SINGLE_POLL,'))]
        self.queue.append(f"SINGLE_POLL,{timeout_ms}")
        self.pump()

    def start_inv(self):
        # Backward-compatible alias for existing UI behavior.
        self.multi_poll()

    def stop_inv(self):
        # STOP is allowed during async inventory; bypass queued reader-info requests.
        self.queue.clear(); self.pending=None; self.enqueue("STOP")
    def set_inventory(self,on):
        self.inventory=on
        if not on and self.info_deferred and self.connected:
            self.info_deferred=False
            QTimer.singleShot(0, self.request_info)
        if on and self.inventory_started_at is None:
            self.inventory_started_at=time.monotonic()
        if not on:
            self.inventory_started_at=None
        self.kv[1].setText("LIVE" if on else "IDLE")
        self.single.setEnabled(self.connected and not on)
        self.multi.setEnabled(self.connected and not on)
        self.sync.setEnabled(self.connected and not on)
        self.stop.setEnabled(self.connected and on)
        self.write_tag_btn.setEnabled(self.connected and not on)
        self.read_tag_btn.setEnabled(self.connected and not on)
        self.lock_tag_btn.setEnabled(self.connected and not on)
        self.kill_tag_btn.setEnabled(self.connected and not on)
    def handle_tag(self,t):
        self.total+=1
        self.epcs[t.epc]=self.epcs.get(t.epc,0)+1
        now_wall=datetime.now()
        self.epc_first_seen.setdefault(t.epc,now_wall)
        self.epc_last_seen[t.epc]=now_wall
        self.antenna_stats[t.antenna]=self.antenna_stats.get(t.antenna,0)+1
        self.frequency_stats[t.frequency_khz]=self.frequency_stats.get(t.frequency_khz,0)+1
        self.kv[2].setText(str(self.total))
        self.kv[3].setText(str(len(self.epcs)))

        now=time.monotonic()
        self.first_tag=self.first_tag or now
        self.kv[4].setText(f"{self.total/max(now-self.first_tag,.001):.1f}/s")
        self.hint.setText(f"Last EPC: {t.epc} • RSSI {t.rssi} dBm")

        row=self.epc_row_map.get(t.epc)
        if row is None:
            row=self.table.rowCount()
            self.table.insertRow(row)
            self.epc_row_map[t.epc]=row

        values=[
            datetime.now().strftime('%H:%M:%S.%f')[:-3],
            t.epc,
            str(t.rssi),
            str(t.antenna),
            str(t.frequency_khz),
            str(self.epcs[t.epc])
        ]
        for c,v in enumerate(values):
            self.table.setItem(row,c,QTableWidgetItem(v))

        self.update_row_visibility(row, t.epc)

        if self.auto.isChecked():
            self.table.scrollToBottom()
        self.update_analytics()

    def update_analytics(self):
        if not hasattr(self,'analytics_total'):
            return
        elapsed=max(time.monotonic()-self.first_tag,.001) if self.first_tag else 0.0
        rate=(self.total/elapsed) if elapsed else 0.0
        self.analytics_total.setText(str(self.total))
        self.analytics_unique.setText(str(len(self.epcs)))
        self.analytics_rate.setText(f'{rate:.1f}/s')
        self.analytics_elapsed.setText(f'{elapsed:.1f} s')
        self.antenna_stats_table.setRowCount(0)
        for antenna, count in sorted(self.antenna_stats.items()):
            row=self.antenna_stats_table.rowCount(); self.antenna_stats_table.insertRow(row)
            self.antenna_stats_table.setItem(row,0,QTableWidgetItem(str(antenna)))
            self.antenna_stats_table.setItem(row,1,QTableWidgetItem(str(count)))
        self.frequency_stats_table.setRowCount(0)
        for frequency, count in sorted(self.frequency_stats.items()):
            row=self.frequency_stats_table.rowCount(); self.frequency_stats_table.insertRow(row)
            self.frequency_stats_table.setItem(row,0,QTableWidgetItem(str(frequency)))
            self.frequency_stats_table.setItem(row,1,QTableWidgetItem(str(count)))

    def update_row_visibility(self,row,epc):
        query=self.filter_edit.text().strip().upper()
        if not query:
            self.table.setRowHidden(row,False)
            return
        epc_u=epc.upper()
        match = (epc_u == query) if self.filter_exact_cb.isChecked() else (query in epc_u)
        self.table.setRowHidden(row,not match)

    def apply_filter(self,*_):
        self.filter_timer.start()

    def apply_filter_debounced(self):
        query=self.filter_edit.text().strip().upper()
        exact=self.filter_exact_cb.isChecked()
        for r in range(self.table.rowCount()):
            item=self.table.item(r,1)
            epc=item.text().upper() if item else ''
            match = True if not query else ((epc == query) if exact else (query in epc))
            self.table.setRowHidden(r,not match)

    def clear_filter(self):
        self.filter_edit.clear()
        self.filter_exact_cb.setChecked(False)
    def use_selected_epc(self):
        rows=self.table.selectionModel().selectedRows()
        if rows:
            item=self.table.item(rows[0].row(),1)
            if item:
                epc=item.text().strip().upper()
                self.selected_epc=epc
                self.selected_epc_display.setText(epc)
                self.target_epc.setText(epc)
                self.epc_edit_old.setText(epc)
                self.read_target_epc.setText(epc)
                self.lock_target_epc.setText(epc)
                self.kill_target_epc.setText(epc)

    def use_selected_read_epc(self):
        rows=self.table.selectionModel().selectedRows()
        if rows:
            item=self.table.item(rows[0].row(),1)
            if item:
                self.use_selected_epc()

    @staticmethod
    def normalize_hex(text):
        t=text.strip().replace(" ","").replace(":","").replace("0x","").replace("0X","")
        return t.upper()

    @staticmethod
    def validate_epc_value(text):
        """Return a canonical EPC or None when it is not a valid EPC value."""
        value=text
        if not value or len(value)%2 or any(c not in '0123456789abcdefABCDEF' for c in value):
            return None
        # EPC memory is addressed in 16-bit words and supports 1..31 words.
        if not 2 <= len(value)//2 <= 62:
            return None
        return value.upper()

    @staticmethod
    def validate_fixed_hex(text, digits):
        """Return an uppercase fixed-width hex value or None."""
        value=text.strip()
        if len(value)!=digits or any(c not in '0123456789abcdefABCDEF' for c in value):
            return None
        return value.upper()

    def update_write_mode(self, *_):
        epc_editing=self.write_operation.currentData()=='EPC_EDIT'
        for widget in (self.write_target_label,self.target_epc,self.write_bank_label,self.write_bank,self.write_address_label,self.write_address,self.write_data_label,self.write_data,self.write_access_password_label,self.write_access_password):
            widget.setVisible(not epc_editing)
        for widget in (self.epc_edit_old_label,self.epc_edit_old,self.epc_edit_new_label,self.epc_edit_new):
            widget.setVisible(epc_editing)
        self.write_tag_btn.setText('Write EPC' if epc_editing else 'Write Tag Data')

    def write_selected_operation(self):
        if self.write_operation.currentData()=='EPC_EDIT':
            self.write_epc()
        else:
            self.write_tag_data()

    def write_epc(self):
        if not self.connected:
            self.statusBar().showMessage("Connect to STM32 first.",3000); return
        if self.inventory:
            self.statusBar().showMessage("Stop inventory before writing a tag.",4000); return

        old_epc=self.validate_epc_value(self.selected_epc)
        if not old_epc:
            QMessageBox.warning(self,"No valid selected EPC","Select a tag row with a valid EPC first."); return

        new_epc=self.validate_epc_value(self.epc_edit_new.text())
        if not new_epc:
            QMessageBox.warning(self,"Invalid new EPC","EPC must contain hexadecimal characters only, have an even number of digits, and be 2 to 62 bytes long."); return

        answer=QMessageBox.question(
            self,
            "Confirm EPC write",
            f"Replace the selected EPC?\n\nOld EPC: {old_epc}\nNew EPC: {new_epc}",
            QMessageBox.Yes|QMessageBox.No,
            QMessageBox.No
        )
        if answer!=QMessageBox.Yes: return

        self.epc_edit_old.setText(old_epc)
        self.epc_write_pending=new_epc
        self.enqueue(f"WRITE_EPC,{old_epc},{new_epc}")

    def refresh_after_epc_write(self,new_epc):
        if not new_epc:
            return
        self.selected_epc=new_epc
        self.epc_edit_old.setText(new_epc)
        self.target_epc.setText(new_epc)
        self.read_target_epc.setText(new_epc)
        self.lock_target_epc.setText(new_epc)
        self.kill_target_epc.setText(new_epc)
        self.clear_inv()

    def read_tag(self):
        if not self.connected:
            self.statusBar().showMessage("Connect to STM32 first.",3000); return
        if self.inventory:
            self.statusBar().showMessage("Stop inventory before reading tag memory.",4000); return
        epc=self.normalize_hex(self.read_target_epc.text())
        if not epc:
            QMessageBox.warning(self,"No target EPC","Select a tag row first, or enter the EPC manually."); return
        if len(epc)%2 or len(epc)>124:
            QMessageBox.warning(self,"Invalid target EPC","Enter the selected EPC as hexadecimal."); return
        try:
            int(epc,16)
        except ValueError:
            QMessageBox.warning(self,"Invalid target EPC","EPC must contain hexadecimal digits only."); return
        bank=int(self.read_bank.currentData())
        address=self.read_address.value()
        words=self.read_words.value()
        self.selected_epc=epc
        self.epc_edit_old.setText(epc)
        self.read_target_epc.setText(epc)
        self.read_result.setText("Reading…")
        self.enqueue(f"READ_TAG,{epc},{bank},{address},{words}")

    def write_tag_data(self):
        if not self.connected:
            self.statusBar().showMessage("Connect to STM32 first.",3000); return
        if self.inventory:
            self.statusBar().showMessage("Stop inventory before writing a tag.",4000); return

        target=self.normalize_hex(self.target_epc.text())
        access_password=self.validate_fixed_hex(self.write_access_password.text(),8)
        data=self.normalize_hex(self.write_data.text())

        if not target:
            QMessageBox.warning(self,"No target EPC","Select a tag row first, or enter the EPC manually."); return
        if len(target)%2 or len(target)>124:
            QMessageBox.warning(self,"Invalid target EPC","Enter the current EPC as hexadecimal."); return
        try:int(target,16)
        except ValueError:
            QMessageBox.warning(self,"Invalid target EPC","EPC must contain hexadecimal digits only."); return

        if not access_password:
            QMessageBox.warning(self,"Invalid access password","Access Password must be exactly 8 hexadecimal characters (a 32-bit value)."); return

        if not data:
            QMessageBox.warning(self,"No write data","Enter hexadecimal data to write."); return
        if len(data)%2 or len(data)>128:
            QMessageBox.warning(self,"Invalid write data","Write data must contain an even number of hex digits, up to 64 bytes."); return
        try:int(data,16)
        except ValueError:
            QMessageBox.warning(self,"Invalid write data","Write data must contain hexadecimal digits only."); return

        bank=int(self.write_bank.currentData())
        address=self.write_address.value()
        bytes_len=len(data)//2
        answer=QMessageBox.question(
            self,
            "Confirm tag write",
            f"Write {bytes_len} byte(s)?\n\nBank: {bank}\nAddress: {address}\nData: {data}\n\nThe tag matching {target} will be written.",
            QMessageBox.Yes|QMessageBox.No,
            QMessageBox.No
        )
        if answer!=QMessageBox.Yes: return

        self.selected_epc=target
        self.target_epc.setText(target)
        self.enqueue(f"WRITE_TAG,{target},{bank},{address},{access_password},{data}")

    def lock_tag(self):
        if not self.connected:
            self.statusBar().showMessage("Connect to STM32 first.",3000); return
        if self.inventory:
            self.statusBar().showMessage("Stop inventory before locking a tag.",4000); return

        target=self.validate_epc_value(self.normalize_hex(self.lock_target_epc.text()))
        access_password=self.validate_fixed_hex(self.lock_access_password.text(),8)
        mask=self.validate_fixed_hex(self.lock_mask.text(),4)
        action=self.validate_fixed_hex(self.lock_action.text(),4)
        if not target:
            QMessageBox.warning(self,"Invalid target EPC","EPC must contain hexadecimal digits only, have an even number of digits, and be 2 to 62 bytes long."); return
        if not access_password:
            QMessageBox.warning(self,"Invalid access password","Access Password must be exactly 8 hexadecimal characters (a 32-bit value)."); return
        if not mask:
            QMessageBox.warning(self,"Invalid lock mask","Lock Mask must be exactly 4 hexadecimal characters."); return
        if not action:
            QMessageBox.warning(self,"Invalid lock action","Lock Action must be exactly 4 hexadecimal characters."); return

        answer=QMessageBox.question(
            self,
            "Confirm tag lock",
            f"Send the protocol-level lock command?\n\nTarget EPC: {target}\nLock Mask: {mask}\nLock Action: {action}",
            QMessageBox.Yes|QMessageBox.No,
            QMessageBox.No
        )
        if answer!=QMessageBox.Yes: return
        self.selected_epc=target
        self.lock_target_epc.setText(target)
        self.enqueue(f"LOCK_TAG,{target},{access_password},{mask},{action}")

    def kill_tag(self):
        if not self.connected:
            self.statusBar().showMessage("Connect to STM32 first.",3000); return
        if self.inventory:
            self.statusBar().showMessage("Stop inventory before killing a tag.",4000); return

        target=self.validate_epc_value(self.normalize_hex(self.kill_target_epc.text()))
        kill_password=self.validate_fixed_hex(self.kill_password.text(),8)
        if not target:
            QMessageBox.warning(self,"Invalid target EPC","EPC must contain hexadecimal digits only, have an even number of digits, and be 2 to 62 bytes long."); return
        if not kill_password:
            QMessageBox.warning(self,"Invalid kill password","Kill Password must be exactly 8 hexadecimal characters (a 32-bit value)."); return

        answer=QMessageBox.warning(
            self,
            "IRREVERSIBLE TAG KILL",
            f"This operation is destructive and irreversible on a properly configured tag.\n\n"
            f"Target EPC: {target}\n\nAre you sure you want to send the KILL command?",
            QMessageBox.Yes|QMessageBox.No,
            QMessageBox.No
        )
        if answer!=QMessageBox.Yes: return
        self.selected_epc=target
        self.kill_target_epc.setText(target)
        self.enqueue(f"KILL_TAG,{target},{kill_password}")

    def clear_lock_fields(self):
        self.lock_target_epc.clear()
        self.lock_access_password.setText('00000000')
        self.lock_mask.setText('0000')
        self.lock_action.setText('0000')

    def clear_kill_fields(self):
        self.kill_target_epc.clear()
        self.kill_password.setText('00000000')

    def find_row(self,epc):
        for r in range(self.table.rowCount()):
            if self.table.item(r,1) and self.table.item(r,1).text()==epc:return r
        return None
    def clear_inv(self):
        self.table.setRowCount(0)
        self.epcs={}
        self.epc_row_map={}
        self.epc_first_seen={}
        self.epc_last_seen={}
        self.antenna_stats={}
        self.frequency_stats={}
        self.total=0
        self.first_tag=None
        self.kv[2].setText('0')
        self.kv[3].setText('0')
        self.kv[4].setText('0.0/s')
        self.update_analytics()
        self.hint.setText('Waiting for TAG frames…')
    def export_csv(self):
        p,_=QFileDialog.getSaveFileName(self,'Export RFID Inventory','rfid_inventory.csv','CSV (*.csv)');
        if not p:return
        with open(p,'w',newline='',encoding='utf-8') as f:
            w=csv.writer(f);w.writerow(['Time','EPC','RSSI (dBm)','Antenna','Frequency (kHz)','Reads'])
            for r in range(self.table.rowCount()):w.writerow([self.table.item(r,c).text() if self.table.item(r,c) else '' for c in range(6)])
    def set_region(self):
        try:self.enqueue(f"SET_REGION,{int(self.region.text().replace('0x','').replace('0X',''),16):02X}")
        except:QMessageBox.warning(self,'Invalid Region','Enter a hex byte such as 01.')
    def manual(self):
        c=self.command.text().strip();
        if c:self.enqueue(c);self.command.clear()

    def log(self,msg):self.event.appendPlainText(f"[{datetime.now().strftime('%H:%M:%S.%f')[:-3]}] {msg}")
    def on_error(self,msg):self.log('ERROR: '+msg);self.set_log_badge(True);self.statusBar().showMessage(msg,5000)
    def restore(self):
        i=self.baud.findData(self.settings.value('baud',115200,type=int));
        if i>=0:self.baud.setCurrentIndex(i)
        for k,w in [('region',self.region),('tx',self.tx),('rx',self.rx),('session',self.session)]:
            v=self.settings.value(k)
            if v is not None:
                try:w.setText(str(v)) if isinstance(w,QLineEdit) else w.setValue(int(v))
                except:pass
        saved_power=self.settings.value('power')
        if saved_power is not None:
            try:
                power_value=float(saved_power)
                if power_value > 33.0:
                    power_value /= 100.0
                self.power.setValue(power_value)
            except:pass
        self.dedupe.setChecked(True);self.read_bank.setCurrentIndex(max(0,self.read_bank.findData(self.settings.value('read_bank',2,type=int))));self.read_address.setValue(self.settings.value('read_address',0,type=int));self.read_words.setValue(self.settings.value('read_words',2,type=int));self.auto.setChecked(self.settings.value('auto',True,type=bool));self.clear_start.setChecked(self.settings.value('clear_start',False,type=bool));self.reconnect.setChecked(self.settings.value('reconnect',True,type=bool));self.filter_edit.setText(self.settings.value('filter','',type=str));self.filter_exact_cb.setChecked(self.settings.value('filter_exact',False,type=bool));self.http_url.setText(self.settings.value('http_url','',type=str));self.http_timeout.setValue(self.settings.value('http_timeout',10,type=int));self.http_active.setChecked(self.settings.value('http_active',False,type=bool));self.http_start_boot.setChecked(self.settings.value('http_start_boot',False,type=bool));self.inventory_antennas.setText(self.settings.value('inventory_antennas','1',type=str));self.net_ip.setText(self.settings.value('net_ip',self.net_ip.text(),type=str));self.wifi_host.setText(self.settings.value('wifi_host','',type=str))
    def save(self):
        self.settings.setValue('dark',self.dark);self.settings.setValue('color_theme',self.theme_name);self.settings.setValue('port',self.port.currentData() or '');self.settings.setValue('baud',self.baud.currentData())
        for k,w in [('region',self.region),('tx',self.tx),('rx',self.rx),('power',self.power),('session',self.session)]:self.settings.setValue(k,w.text() if isinstance(w,QLineEdit) else w.value())
        self.settings.setValue('dedupe',True);self.settings.setValue('read_bank',self.read_bank.currentData());self.settings.setValue('read_address',self.read_address.value());self.settings.setValue('read_words',self.read_words.value());self.settings.setValue('auto',self.auto.isChecked());self.settings.setValue('clear_start',self.clear_start.isChecked());self.settings.setValue('reconnect',self.reconnect.isChecked());self.settings.setValue('filter',self.filter_edit.text());self.settings.setValue('filter_exact',self.filter_exact_cb.isChecked());self.settings.setValue('http_url',self.http_url.text());self.settings.setValue('http_timeout',self.http_timeout.value());self.settings.setValue('http_active',self.http_active.isChecked());self.settings.setValue('http_start_boot',self.http_start_boot.isChecked());self.settings.setValue('inventory_antennas',self.inventory_antennas.text());self.settings.setValue('net_ip',self.net_ip.text().strip());self.settings.setValue('wifi_host',self.wifi_host.text().strip())
    def apply_theme(self):
        self.theme_name=theme.normalize(self.theme_name)
        QApplication.instance().setStyleSheet(theme.stylesheet(self.dark,self.theme_name))
        refresh_theme(self,self.dark,self.theme_name)
        if self.connected:
            self.conn.setStyleSheet(f"color:{theme.colors(self.dark,self.theme_name)['success']};font-weight:700")
        self.save()
    def set_theme(self,d):self.dark=d;self.apply_theme();self.log('Theme changed to '+('Dark' if d else 'Light'))
    def set_color_theme(self,name):
        self.theme_name=theme.normalize(name);self.apply_theme();self.log('Color theme changed to '+theme.label(self.theme_name))
    def closeEvent(self,e):
        if self.vswr_bridge:
            self.vswr_bridge.stop()
        if self.vswr_serial_mode:
            self.io.set_binary_mode(False)
        self.save();self.io.close();e.accept()

app=QApplication(sys.argv);app.setStyle('Fusion');w=Main();w.show();sys.exit(app.exec())
