import queue, threading, socket, select
from serial import Serial, SerialException

ETHERNET_OPTION = "__ETHERNET__"
ETHERNET_HOST = "192.168.150.50"
ETHERNET_PORT = 5000
from serial.tools import list_ports
from PySide6.QtCore import QObject, Signal

class SerialManager(QObject):
    state_signal = Signal(bool, str)
    line_signal = Signal(str)
    raw_signal = Signal(bytes)
    error_signal = Signal(str)
    tx_signal = Signal(str)

    def __init__(self):
        super().__init__()
        self.q = queue.Queue()
        self.stop = threading.Event()
        self.ser = None
        self.transport = None
        self.binary_mode = False
        self.buf = bytearray()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    @staticmethod
    def ports():
        return list(list_ports.comports())

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

        if p == ETHERNET_OPTION:
            self._open_ethernet(host or ETHERNET_HOST)
            return

        try:
            self.ser = Serial(
                port=p,
                baudrate=b,
                bytesize=8,
                parity='N',
                stopbits=1,
                timeout=.05,
                write_timeout=1
            )
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

    def _open_ethernet(self, host):
        try:
            self.ser = socket.create_connection(
                (host, ETHERNET_PORT),
                timeout=5.0
            )
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
                line = line_bytes.decode('utf-8', 'replace').rstrip('\r')
                if line:
                    self.line_signal.emit(line)

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
        while not self.stop.is_set():
            try:
                kind, payload = self.q.get(timeout=.02)
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
