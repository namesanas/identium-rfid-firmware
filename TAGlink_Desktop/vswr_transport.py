"""ReaderManager-compatible VSWR transport over the module bridge.

The firmware's configuration-port bridge forwards ModuleAPI binary frames to
the RFID module.  This module keeps that wire protocol isolated from the
normal line-oriented host transport used by the main GUI.
"""

import socket
import threading
import time


def _crc8_step(crc, value):
    """The ModuleAPI R2000 CRC step (named calcCRC8 in ModuleAPI.dll)."""
    for bit in range(7, -1, -1):
        old_msb = bool(crc & 0x8000)
        incoming = (value >> bit) & 0x01
        crc = ((crc << 1) & 0xFFFF) | incoming
        if old_msb:
            crc ^= 0x1021
    return crc


def _module_crc(frame):
    """Return the two CRC bytes used by ModuleAPI.GetSendCmd()."""
    crc = 0xFFFF
    if len(frame) < 5:
        return b'\x00\x00'
    crc = _crc8_step(crc, frame[1])
    crc = _crc8_step(crc, frame[2])
    # calcCRC() starts at byte 3 and loops while i < len - 5, so the
    # accessed byte is 3 + i and the final two placeholder bytes are omitted.
    for value in frame[3:len(frame) - 2]:
        crc = _crc8_step(crc, value)
    return crc.to_bytes(2, 'big')


def _module_region(region_text):
    """Module region code for the request.

    ModuleAPI converts its Region enum to the module's own region code
    (RegionToM5e: NA=0x01, EU=0x02, KR=0x03, IN=0x04, JP=0x05, PRC=0x06,
    EU2=0x07, EU3=0x08, ...), which is exactly what GET_REGION reports, so
    the reader's region is sent unchanged. It must match the frequencies:
    the module answers 0x0105 (invalid parameter) otherwise.
    """
    value = str(region_text).strip().replace('0x', '').replace('0X', '')
    try:
        return int(value, 16) & 0xFF
    except ValueError:
        return 0x01


def build_vswr_frame(antenna, frequencies_khz, power_centi_dbm=3000, region='08'):
    frequencies_khz = list(frequencies_khz)
    payload = bytearray()
    payload.extend(int(power_centi_dbm).to_bytes(2, 'big'))
    payload.append(int(antenna) & 0xFF)
    payload.append(_module_region(region))
    payload.append(len(frequencies_khz) & 0xFF)
    for frequency in frequencies_khz:
        payload.extend(int(frequency).to_bytes(3, 'big'))

    body = bytearray((0xAA,))
    body.extend(b'Moduletech')
    body.extend((0xAA, 0x4A))  # R2000 calibration: ReturnLossTest
    body.extend(payload)
    # ModuleAPI encodes the length as data length + 14 and appends a
    # four-byte trailer: additive sum, 0xBB, CRC high, CRC low.
    length = len(payload) + 14
    frame = bytearray((0xFF, length)) + body
    checksum = (0xAA + 0x4A + sum(payload)) & 0xFF
    frame.extend((checksum, 0xBB, 0x00, 0x00))
    crc = _module_crc(frame)
    frame[-2:] = crc
    return bytes(frame)


def _frames(buffer):
    """Split module replies: FF LEN CMD STATUS(2) DATA(LEN) CRC(2)."""
    while True:
        start = buffer.find(b'\xFF')
        if start < 0:
            buffer.clear()
            return
        if start:
            del buffer[:start]
        if len(buffer) < 2:
            return
        total = int(buffer[1]) + 7
        if len(buffer) < total:
            return
        frame = bytes(buffer[:total])
        del buffer[:total]
        yield frame


class ModuleError(Exception):
    pass


MODULE_STATUS_TEXT = {
    0x0100: 'invalid message length',
    0x0101: 'invalid command',
    0x0103: 'power too high',
    0x0104: 'frequency out of range',
    0x0105: 'invalid parameter (region, antenna, power or frequency)',
    0x0106: 'power too low',
}


def _signed_byte(value):
    return value - 256 if value > 127 else value


def parse_vswr_frame(frame, frequencies_khz=None):
    """Samples from a ReturnLossTest reply, [] if the frame is another reply.

    Reply data (ModuleAPI GetAntPortsVSWR): 'Moduletech' AA 4A, power(2),
    antenna(1), region(1), count(1), then count x [frequency(3) return
    loss(1)], the return loss in tenths of a dB. Raises ModuleError when
    the module rejected the request.
    """
    if len(frame) < 7 or len(frame) != int(frame[1]) + 7:
        return []
    if _module_crc(frame[:-2] + b'\x00\x00') != frame[-2:]:
        return []
    data = frame[5:-2]
    if frame[2] != 0xAA or not data.startswith(b'Moduletech\xAA\x4A'):
        return []
    status = (frame[3] << 8) | frame[4]
    if status == 0xAA4A:
        # Seen on hardware: every parameter valid, but nothing measurable
        # on the port (antenna cable not connected).
        raise ModuleError('VSWR scan failed (module status 0xAA4A): no antenna '
                          'detected on the selected port. Check the antenna and cable.')
    if status != 0:
        text = MODULE_STATUS_TEXT.get(status, 'module error')
        raise ModuleError(f'Module rejected the VSWR scan: 0x{status:04X} {text}.')
    body = data[12:]
    if len(body) < 5:
        return []
    count = body[4]
    samples = []
    for index in range(count):
        entry = body[5 + 4 * index:9 + 4 * index]
        if len(entry) < 4:
            break
        raw_return_loss = entry[3]
        samples.append({
            'FREQ': int.from_bytes(entry[:3], 'big'),
            'RETURN_LOSS': raw_return_loss / 10.0,
            'RETURN_LOSS_RAW': raw_return_loss,
        })
    return samples


class VSWRBridge:
    def __init__(self, host, port, antenna, start_khz, stop_khz, step_khz,
                 power_centi_dbm, region, on_sample, on_done, on_error,
                 send_bytes=None):
        self.host = host
        self.port = int(port)
        self.antenna = int(antenna)
        self.start_khz = int(start_khz)
        self.stop_khz = int(stop_khz)
        self.step_khz = int(step_khz)
        self.power_centi_dbm = int(power_centi_dbm)
        self.region = region
        self.on_sample = on_sample
        self.on_done = on_done
        self.on_error = on_error
        self.send_bytes = send_bytes
        self.stop_event = threading.Event()
        self.reply_event = threading.Event()
        self.reply_buffer = bytearray()
        self.reply_lock = threading.Lock()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self):
        self.thread.start()

    def stop(self):
        self.stop_event.set()
        self.reply_event.set()

    def feed(self, data):
        """Feed bytes received from the shared serial/USB transport."""
        if self.send_bytes is None or not data:
            return
        with self.reply_lock:
            self.reply_buffer.extend(data)
            self.reply_event.set()

    def _run(self):
        frequencies = list(range(self.start_khz, self.stop_khz + 1, self.step_khz))
        # The ModuleAPI length field is one byte and the ReturnLossTest
        # request uses 5 + 3*N payload bytes, leaving room for at most 78
        # explicit frequencies in one frame.
        if len(frequencies) > 78:
            self.on_error('VSWR sweep is limited to 78 frequency points per scan.')
            return
        try:
            frame = build_vswr_frame(
                self.antenna,
                frequencies,
                self.power_centi_dbm,
                self.region,
            )
            if self.send_bytes is not None:
                # USB/serial configuration transport is shared with the text
                # host interface.  The firmware switches it to the binary
                # module bridge when this frame begins with FF.
                self.send_bytes(frame)
                received = bytearray()
                deadline = time.monotonic() + 35.0
                while not self.stop_event.is_set() and time.monotonic() < deadline:
                    self.reply_event.wait(0.25)
                    self.reply_event.clear()
                    with self.reply_lock:
                        if self.reply_buffer:
                            received.extend(self.reply_buffer)
                            self.reply_buffer.clear()
                    for reply in _frames(received):
                        samples = parse_vswr_frame(reply, frequencies)
                        if samples:
                            for sample in samples:
                                self.on_sample(sample)
                            self.on_done()
                            return
                if self.stop_event.is_set():
                    self.on_done()
                else:
                    self.on_error('VSWR module response timed out or contained no frequency samples.')
                return

            with socket.create_connection((self.host, self.port), timeout=5.0) as sock:
                sock.settimeout(0.5)
                sock.sendall(frame)
                received = bytearray()
                idle_cycles = 0
                while not self.stop_event.is_set() and idle_cycles < 70:
                    try:
                        chunk = sock.recv(4096)
                    except socket.timeout:
                        idle_cycles += 1
                        continue
                    if not chunk:
                        break
                    idle_cycles = 0
                    received.extend(chunk)
                    for reply in _frames(received):
                        samples = parse_vswr_frame(reply, frequencies)
                        if samples:
                            for sample in samples:
                                self.on_sample(sample)
                            self.on_done()
                            return
            if self.stop_event.is_set():
                self.on_done()
            else:
                self.on_error('VSWR module response timed out or contained no frequency samples.')
        except ModuleError as exc:
            self.on_error(str(exc))
        except (OSError, ValueError) as exc:
            self.on_error(f'VSWR bridge failed: {exc}')
