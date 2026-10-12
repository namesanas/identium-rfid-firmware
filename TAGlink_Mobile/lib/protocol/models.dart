class ParsedLine {
  const ParsedLine({required this.key, required this.values, required this.fields, required this.raw});
  final String key;
  final List<String> values;
  final Map<String, String> fields;
  final String raw;

  String? operator [](String name) => fields[name];
}

class ReaderTag {
  const ReaderTag({required this.epc, required this.rssi, required this.antenna, required this.frequencyKHz, required this.readerTimeMs});
  final String epc;
  final int rssi;
  final int antenna;
  final int frequencyKHz;
  final int readerTimeMs;
}

class TagRecord {
  TagRecord({required this.epc, required this.count, required this.lastRssi, required this.antenna, required this.firstSeen, required this.lastSeen});
  final String epc;
  int count;
  int lastRssi;
  int antenna;
  DateTime firstSeen;
  DateTime lastSeen;

  void record(ReaderTag tag, DateTime now) {
    count++;
    lastRssi = tag.rssi;
    antenna = tag.antenna;
    lastSeen = now;
  }
}

class ReaderStatus {
  const ReaderStatus(this.state);
  final String state;
  bool get isInventory => state.toUpperCase() == 'INVENTORY';
}

class ReaderVersion {
  const ReaderVersion({this.btver = '', this.hwver = '', this.fwdate = '', this.fwver = '', this.proto = ''});
  final String btver;
  final String hwver;
  final String fwdate;
  final String fwver;
  final String proto;
}

class ReaderSerial {
  const ReaderSerial({this.year = '', this.serial = ''});
  final String year;
  final String serial;
}

class ReaderPower {
  const ReaderPower({required this.antenna, required this.readCentiDbm, required this.writeCentiDbm});
  final int antenna;
  final int readCentiDbm;
  final int writeCentiDbm;
}

class WifiStatus {
  const WifiStatus({required this.link, required this.state, required this.ip, required this.rssi, required this.host, required this.upload, required this.enabled, required this.mode, required this.ssid, required this.interfaceType, required this.errors, required this.rejected});
  final String link;
  final String state;
  final String ip;
  final int rssi;
  final bool host;
  final bool upload;
  final bool enabled;
  final String mode;
  final String ssid;
  final String interfaceType;
  final int errors;
  final String rejected;
}

class ProtocolLogEntry {
  const ProtocolLogEntry({required this.time, required this.direction, required this.line});
  final DateTime time;
  final String direction;
  final String line;
  @override
  String toString() => '${time.toIso8601String()} $direction $line';
}
