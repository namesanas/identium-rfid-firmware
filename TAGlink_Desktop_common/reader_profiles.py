"""MUBR's app-facing contract, separate from the existing IF9D command set."""

MUBR_PROFILE = "HANDHELD"
MUBR_PROTOCOL = "TAGLINK_TEXT_1"
BLE_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
BLE_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
BLE_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

# Each settings section can grow without inheriting the fixed-reader commands.
MUBR_INFO = (
    "GET_STATUS", "GET_SERIAL", "GET_NAME", "GET_POWER", "GET_REGION",
    "GET_SESSION", "GET_GEN2", "GET_SCAN_SETTINGS", "GET_WIFI", "GET_BLE",
    "GET_SERVER", "GET_BATTERY", "GET_DIAG", "GET_HTTP", "GET_WORK_MODE",
)
MUBR_READONLY = frozenset(MUBR_INFO + ("GET_VERSION", "GET_CAPABILITIES"))
MUBR_MUTATIONS = frozenset((
    "SET_POWER", "SET_REGION", "SET_SESSION", "SET_GEN2", "SET_SCAN_MODE",
    "SET_SCAN_TIME", "SET_DELIVERY", "SET_WIFI", "SET_BLE", "SET_SERVER", "SET_NAME",
    "SET_HTTP", "SET_WORK_MODE",
))
MUBR_COMMANDS = MUBR_READONLY | MUBR_MUTATIONS | {"START", "STOP", "SINGLE_POLL", "RELEASE"}


def is_mubr_identity(data):
    return data.get("PROFILE", "").upper() == MUBR_PROFILE and data.get("PROTO") == MUBR_PROTOCOL


def response_matches(command, kind, data):
    base = command.partition(",")[0]
    if kind == "ERROR":
        return data in (base, "INVALID_COMMAND")
    if base.startswith("GET_"):
        return kind == base[4:]
    return kind == "OK" and data == base


def command_bytes(command):
    """Reject line injection and firmware-overflowing commands before TX."""
    encoded = command.encode("ascii")
    if not encoded or len(encoded) + 1 > 256 or any(b < 32 or b > 126 for b in encoded):
        raise ValueError("Use printable text; commands must fit the reader's 256-byte line limit.")
    return encoded + b"\r\n"


class LineFramer:
    """MTU-independent CRLF assembly with a strict bound, reset per connection."""

    def __init__(self, limit=512):
        self.limit = limit
        self.buffer = bytearray()

    def clear(self):
        self.buffer.clear()

    def feed(self, data):
        lines = []
        for byte in data:
            if byte == 10:
                line = bytes(self.buffer).rstrip(b"\r")
                self.buffer.clear()
                if line:
                    lines.append(line.decode("ascii"))
            else:
                self.buffer.append(byte)
                if len(self.buffer) > self.limit:
                    self.buffer.clear()
                    raise ValueError("Reader reply exceeded the supported line length.")
        return lines
