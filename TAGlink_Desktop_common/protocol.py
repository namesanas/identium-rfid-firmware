from dataclasses import dataclass

TAG_LOG_MAX_SEQUENCE = 0xFFFFFFFF
TAG_LOG_PAGE_SIZE = 20


@dataclass(frozen=True)
class TagLogStatus:
    state: str
    oldest: int
    next_sequence: int
    count: int
    capacity: int
    pending: int
    dropped: int

    def contains(self, sequence):
        return ((sequence - self.oldest) & TAG_LOG_MAX_SEQUENCE) < self.count


@dataclass(frozen=True)
class TagLogRecord:
    sequence: int
    timestamp: str
    clock: str
    antenna: int
    rssi: int
    reads: int
    epc: str
    truncated: bool


def parse_tag_log_number(value):
    if not value.isascii() or not value.isdecimal() or len(value) > 10:
        raise ValueError('invalid tag log number')
    number = int(value)
    if number > TAG_LOG_MAX_SEQUENCE:
        raise ValueError('tag log number outside uint32')
    return number


def _parse_tag_log(line):
    parts = line.split(',')
    if parts[1] == 'STATUS':
        try:
            fields = dict(part.split('=', 1) for part in parts[3:])
            keys = ('OLDEST', 'NEXT', 'COUNT', 'CAPACITY', 'PENDING', 'DROPPED')
            status = TagLogStatus(parts[2], *(parse_tag_log_number(fields[key]) for key in keys))
            span = (status.next_sequence - status.oldest) & TAG_LOG_MAX_SEQUENCE
            if status.state not in ('READY', 'CLEARING', 'ABSENT') or status.count != span:
                raise ValueError('invalid tag log status')
            if status.state != 'ABSENT' and (not status.capacity or span >= status.capacity * 2):
                raise ValueError('invalid tag log capacity')
        except (IndexError, KeyError, ValueError):
            return 'TAGLOG_STATUS', None
        return 'TAGLOG_STATUS', status

    try:
        _, seq, timestamp, clock, antenna, rssi, reads, epc, *flags = parts
        seconds, milliseconds = timestamp.split('.')
        parse_tag_log_number(seconds)
        if len(milliseconds) != 3:
            raise ValueError('invalid tag log timestamp')
        parse_tag_log_number(milliseconds)
        if clock not in ('E', 'U') or flags not in ([], ['TRUNC']):
            raise ValueError('invalid tag log flags')
        if len(epc) > 32 or len(epc) % 2 or any(c not in '0123456789ABCDEFabcdef' for c in epc):
            raise ValueError('invalid tag log EPC')
        record = TagLogRecord(parse_tag_log_number(seq), timestamp, clock,
                              parse_tag_log_number(antenna), int(rssi),
                              parse_tag_log_number(reads), epc.upper(), bool(flags))
    except ValueError:
        return 'TAGLOG_RECORD', None
    return 'TAGLOG_RECORD', record

@dataclass
class Tag:
    epc: str
    rssi: int
    antenna: int
    frequency_khz: int
    reader_time_ms: int

def parse_line(line: str):
    line = line.strip()
    if line.startswith('TAGLOG,'):
        return _parse_tag_log(line)
    if line.startswith('TAG,'):
        d = {}
        for part in line.split(',')[1:]:
            if '=' in part:
                k, v = part.split('=', 1)
                d[k.upper()] = v
        try:
            return 'TAG', Tag(d['EPC'].upper(), int(d['RSSI']), int(d['ANT']), int(d['FREQ']), int(d['TIME']))
        except (KeyError, ValueError):
            return 'MALFORMED', line
    head, sep, tail = line.partition(',')
    head = head.upper()
    if head in {'OK','ERROR'}:
        return head, tail
    if head in {
        'STATUS','VERSION','SERIAL','TEMP','REGION','ANTENNA','POWER',
        'PROTOCOL','SESSION','FREQ','REGIONS','READ_DATA','CAPABILITIES',
        'HARDWARE','GPI','GPO','HTTP','NETWORK','VSWR','VSWR_POINT',
        'VSWR_DONE','FIRMWARE','DISCOVERY','WORK_MODE','WIFI','DIAG','GEN2','FW',
        'SCAN_SETTINGS','BLE','SERVER','NAME','BATTERY','OTA','AP_CREDENTIALS'
    }:
        values={'_csv':[]}
        for p in [x.strip() for x in tail.split(',') if x.strip()]:
            if '=' in p:
                k,v=p.split('=',1); values[k.upper()] = v
            else: values['_csv'].append(p)
        return head, values
    return 'UNKNOWN', line
