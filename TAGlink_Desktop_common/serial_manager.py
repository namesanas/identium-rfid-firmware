import queue, threading, socket, select, time
from serial import Serial, SerialException

ETHERNET_OPTION = "__ETHERNET__"
ETHERNET_HOST = "192.168.150.50"
ETHERNET_PORT = 5000
# The reader's WiFi host port (ESP32 coprocessor) uses the same text
# protocol and port as Ethernet; only the address differs.
WIFI_OPTION = "__WIFI__"
from serial.tools import list_ports
from PySide6.QtCore import QObject, Signal

class SerialManager(QObject):
    state_signal = Signal(bool, str)
    line_signal = Signal(str)
    raw_signal = Signal(bytes)
    error_signal = Signal(str)
    tx_signal = Signal(str)

    def __init__(self, max_line_bytes=None):
        super().__init__()
        self.q = queue.Queue()
        self.stop = threading.Event()
        self.ser = None
        self.transport = None
        self.binary_mode = False
        self.buf = bytearray()
        self.max_line_bytes = max_line_bytes
        # How long the worker waits for a new command before checking the
        # port again. 20 ms is fine for the GUI's one-command-at-a-time
        # traffic, but it dominates an operation made of hundreds of small
        # round trips (a firmware upload), so that can ask for less.
        self.poll_interval = 0.02
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    @staticmethod
    def ports():
        return list(list_ports.comports())

    @staticmethod
    def local_subnets():
        """/24 prefixes of this PC's IPv4 addresses (e.g. '192.168.1.')."""
        addresses = set()
        try:
            for address in socket.gethostbyname_ex(socket.gethostname())[2]:
                addresses.add(address)
        except OSError:
            pass
        try:
            probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            probe.connect(('8.8.8.8', 80))   # no packet is sent for UDP connect
            addresses.add(probe.getsockname()[0])
            probe.close()
        except OSError:
            pass
        return sorted({a.rsplit('.', 1)[0] + '.' for a in addresses
                       if not a.startswith(('127.', '169.254.'))})

    @staticmethod
    def scan_tcp(skip=(), port=ETHERNET_PORT, connect_timeout=0.8):
        """Find readers that answer text commands on TCP `port` in the local
        /24 networks (WiFi readers have no UDP discovery). Each open port
        gets GET_VERSION; a reader that answers is released again. A port
        that is open but silent belongs to a reader whose command interface
        is held by another link (or by this program): it is listed as busy."""
        from concurrent.futures import ThreadPoolExecutor

        def probe(ip):
            try:
                sock = socket.create_connection((ip, port), timeout=connect_timeout)
            except OSError:
                return None
            reader = {'ip': ip, 'mac': '', 'port': port, 'board': '',
                      'firmware': '', 'mode': 'Busy', 'link': 'tcp'}
            try:
                sock.settimeout(0.2)
                sock.sendall(b'GET_VERSION\r\n')
                buffer = b''
                deadline = time.monotonic() + 1.5
                while b'VERSION,' not in buffer and time.monotonic() < deadline:
                    try:
                        chunk = sock.recv(512)
                        if not chunk:
                            break
                        buffer += chunk
                    except socket.timeout:
                        pass
                for line in buffer.decode('ascii', 'replace').splitlines():
                    if line.startswith('VERSION,'):
                        fields = dict(part.split('=', 1) for part in line.split(',')[1:] if '=' in part)
                        reader['firmware'] = fields.get('FWVER', '')
                        reader['mode'] = 'TCP'
                        sock.sendall(b'RELEASE\r\n')
                        time.sleep(0.1)
                        break
            except OSError:
                pass
            finally:
                sock.close()
            return reader

        targets = [prefix + str(host) for prefix in SerialManager.local_subnets()
                   for host in range(1, 255)]
        targets = [ip for ip in targets if ip not in skip]
        with ThreadPoolExecutor(max_workers=64) as pool:
            return [r for r in pool.map(probe, targets) if r]

    @staticmethod
    def discover(timeout=0.75):
        """Discover ReaderManager-compatible readers on UDP port 15000."""
        request = bytes([0xEE, 0x06, 0x00, 0x00, 0x50, 0x01, 0, 0, 0, 0, 0, 0])
        readers = []
        seen = set()
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            sock.settimeout(timeout)
            sock.bind(('', 0))
            sock.sendto(request, ('255.255.255.255', 15000))
            while True:
                try:
                    payload, address = sock.recvfrom(256)
                except socket.timeout:
                    break
                if len(payload) < 47 or payload[4] != 0x50:
                    continue
                ip = '.'.join(str(value) for value in payload[17:21])
                mac = ':'.join(f'{value:02X}' for value in payload[10:16])
                key = (ip, mac)
                if key in seen:
                    continue
                seen.add(key)
                version = '.'.join(str(value) for value in payload[38:42])
                mode_byte = payload[46]
                readers.append({
                    'ip': ip,
                    'mac': mac,
                    'port': int.from_bytes(payload[33:35], 'big'),
                    'board': str(payload[35]),
                    'firmware': version,
                    'mode': 'Passive' if mode_byte == 1 else ('Boot' if mode_byte == 0 else 'Active'),
                    'source': address[0],
                })
        except OSError:
            return []
        finally:
            sock.close()
        return readers

    def connect(self, p, b, host=None):
        self.q.put(('connect', (p, b, host)))

    def disconnect(self):
        # Drop queued sends before disconnecting so a transport switch closes
        # the socket promptly instead of waiting behind stale commands.
        with self.q.mutex:
            self.q.queue.clear()
        self.q.put(('disconnect', None))

    def send(self, c):
        self.q.put(('send', c.rstrip('\r\n') + '\r\n'))

    def send_bytes(self, data):
        self.q.put(('send_bytes', bytes(data)))

    def set_poll_interval(self, seconds):
        """Worker wait between port reads. A single float assignment, so it
        needs no lock; the worker picks it up on its next loop."""
        self.poll_interval = min(0.02, max(0.002, float(seconds)))

    def set_binary_mode(self, enabled):
        self.q.put(('binary_mode', bool(enabled)))

    def close(self):
        self.stop.set()
        self.q.put(('shutdown', None))
        self.thread.join(2)

    def _close(self):
        was = self.ser is not None
        sock = self.ser if self.transport == 'ethernet' else None

        # For TCP, explicitly shut down both directions before close().
        # This makes the STM32/W5500 peer observe the disconnect promptly,
        # which is important when reconnecting to the same listening socket.
        if sock is not None:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except (OSError, socket.timeout):
                pass
            except Exception:
                pass

        try:
            if self.ser:
                self.ser.close()
        except Exception:
            pass
        self.ser = None
        self.transport = None
        self.buf.clear()
        self.binary_mode = False
        if was:
            self.state_signal.emit(False, '')

    def _open(self, p, b, host=None):
        self._close()

        if p in (ETHERNET_OPTION, WIFI_OPTION):
            self._open_ethernet(host or ETHERNET_HOST)
            return

        try:
            connection = Serial(
                port=None,
                baudrate=b,
                bytesize=8,
                parity='N',
                stopbits=1,
                timeout=.05,
                write_timeout=1
            )
            # The prototype's CH9102 bridge drives ESP32 boot/reset. Preserve
            # inactive lines before open; keep other serial devices' defaults.
            if SerialManager._uses_boot_reset_lines(p):
                connection.dtr = False
                connection.rts = False
            connection.port = p
            try:
                connection.open()
            except Exception:
                connection.close()
                raise
            if self.stop.is_set():
                connection.close()
                return
            self.ser = connection
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            self.transport = 'serial'
            self.buf.clear()
            self.state_signal.emit(True, f'{p} @ {b}')
        except Exception as e:
            self.ser = None
            self.transport = None
            self.binary_mode = False
            self.error_signal.emit(f'Open failed: {e}')
            self.state_signal.emit(False, '')

    @staticmethod
    def _uses_boot_reset_lines(port):
        try:
            return any(item.device == port and (item.vid, item.pid) == (0x1A86, 0x55D4)
                       for item in list_ports.comports())
        except OSError:
            return False

    def _open_ethernet(self, host):
        try:
            self.ser = socket.create_connection(
                (host, ETHERNET_PORT),
                timeout=5.0
            )
            if self.stop.is_set():
                self._close()
                return
            self.ser.setsockopt(
                socket.IPPROTO_TCP,
                socket.TCP_NODELAY,
                1
            )
            self.ser.settimeout(1.0)
            self.transport = 'ethernet'
            self.buf.clear()

            # The STM32 host interface currently sends no required banner.
            # Drain anything already waiting so the first GUI response cannot
            # inherit stale bytes from a previous socket state, but limit it
            # so it never spins infinitely on flooding connections.
            drain_iters = 0
            while drain_iters < 50:
                readable, _, _ = select.select([self.ser], [], [], 0)
                if not readable:
                    break
                data = self.ser.recv(4096)
                if not data:
                    raise ConnectionError('Ethernet peer closed the connection.')
                drain_iters += 1

            self.state_signal.emit(
                True,
                f'Ethernet {host}:{ETHERNET_PORT}'
            )
        except Exception as e:
            try:
                if self.ser:
                    self.ser.close()
            except Exception:
                pass
            self.ser = None
            self.transport = None
            self.error_signal.emit(f'Ethernet connect failed: {e}')
            self.state_signal.emit(False, '')

    def _send(self, t):
        if not self.ser:
            self.error_signal.emit('Not connected.')
            return

        if self.transport == 'serial' and not self.ser.is_open:
            self.error_signal.emit('Not connected.')
            return

        if self.transport == 'ethernet' and not self.ser.fileno() >= 0:
            self.error_signal.emit('Not connected.')
            return

        try:
            data = t.encode('ascii')

            if self.transport == 'ethernet':
                self.ser.sendall(data)
            else:
                self.ser.write(data)
                # Log pages are paced by their final reply. Polling the OS
                # transmit buffer adds a delay on every page on Windows.
                if not t.startswith('TAGLOG_READ,'):
                    self.ser.flush()

            self.tx_signal.emit(t.rstrip('\r\n'))
        except (OSError, socket.timeout) as e:
            self.error_signal.emit(f'TX failed: {e}')

    def _send_bytes(self, data):
        if not self.ser:
            self.error_signal.emit('Not connected.')
            return
        try:
            if self.transport == 'ethernet':
                self.ser.sendall(data)
            else:
                self.ser.write(data)
                self.ser.flush()
        except (OSError, socket.timeout) as e:
            self.error_signal.emit(f'Binary TX failed: {e}')
        except Exception as e:
            self.error_signal.emit(f'Binary TX failed: {e}')

    def _rx(self):
        if not self.ser:
            return

        if self.transport == 'serial' and not self.ser.is_open:
            return

        try:
            if self.transport == 'ethernet':
                readable, _, _ = select.select([self.ser], [], [], 0)
                if not readable:
                    return

                data = self.ser.recv(4096)
                if not data:
                    raise ConnectionError(
                        'Ethernet connection closed by the reader.'
                    )
            else:
                n = self.ser.in_waiting
                if not n:
                    return
                data = self.ser.read(n)
                if not data:
                    return

            self.raw_signal.emit(data)
            if getattr(self, 'binary_mode', False):
                return
            self.buf.extend(data)

            while b'\n' in self.buf:
                line_bytes, self.buf = self.buf.split(b'\n', 1)
                if self.max_line_bytes is not None and len(line_bytes) > self.max_line_bytes:
                    raise ConnectionError('Reader reply exceeded the supported line length.')
                line = line_bytes.decode('utf-8', 'replace').rstrip('\r')
                if line:
                    self.line_signal.emit(line)
            if self.max_line_bytes is not None and len(self.buf) > self.max_line_bytes:
                raise ConnectionError('Reader reply exceeded the supported line length.')

        except SerialException as e:
            self.error_signal.emit(f'Serial connection lost: {e}')
            self._close()
        except (ConnectionError, OSError, socket.timeout) as e:
            if self.transport == 'ethernet':
                self.error_signal.emit(f'Ethernet connection lost: {e}')
            else:
                self.error_signal.emit(f'RX failed: {e}')
            self._close()
        except Exception as e:
            self.error_signal.emit(f'RX failed: {e}')

    def _run(self):
        try:
            self._run_loop()
        finally:
            self._close()

    def _run_loop(self):
        while not self.stop.is_set():
            try:
                kind, payload = self.q.get(timeout=self.poll_interval)
                if kind == 'connect':
                    self._open(*payload)
                elif kind == 'disconnect':
                    self._close()
                elif kind == 'send':
                    self._send(payload)
                elif kind == 'send_bytes':
                    self._send_bytes(payload)
                elif kind == 'binary_mode':
                    self.binary_mode = payload
                    if not payload:
                        self.buf.clear()
                elif kind == 'shutdown':
                    self._close()
                    return
            except queue.Empty:
                pass
            except Exception as e:
                self.error_signal.emit(f'Worker: {e}')

            self._rx()

