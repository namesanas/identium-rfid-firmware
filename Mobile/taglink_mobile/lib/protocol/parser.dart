import 'models.dart';

/// Parses KEY,VALUE and KEY,name=value protocol lines without throwing on bad input.
ParsedLine parseLine(String line) {
  final pieces = line.split(',');
  final key = pieces.isEmpty ? '' : pieces.first.trim();
  final values = <String>[];
  final fields = <String, String>{};
  for (final piece in pieces.skip(1)) {
    final item = piece.trim();
    final equals = item.indexOf('=');
    if (equals > 0) {
      fields[item.substring(0, equals).trim()] = item.substring(equals + 1).trim();
    } else {
      values.add(item);
    }
  }
  return ParsedLine(key: key, values: values, fields: fields, raw: line);
}

int intValue(String? value, [int fallback = 0]) => int.tryParse(value ?? '') ?? fallback;
bool boolValue(String? value) => value == '1' || value?.toLowerCase() == 'true';

ReaderTag? parseTag(String line) {
  final parsed = parseLine(line);
  if (parsed.key != 'TAG') return null;
  final epc = parsed['EPC']?.toUpperCase();
  if (epc == null || epc.isEmpty || !RegExp(r'^[0-9A-F]+$').hasMatch(epc)) return null;
  return ReaderTag(
    epc: epc,
    rssi: intValue(parsed['RSSI']),
    antenna: intValue(parsed['ANT'], 1),
    frequencyKHz: intValue(parsed['FREQ']),
    readerTimeMs: intValue(parsed['TIME']),
  );
}

ReaderStatus? parseStatus(String line) {
  final p = parseLine(line);
  if (p.key != 'STATUS' || p.values.isEmpty) return null;
  return ReaderStatus(p.values.first.toUpperCase());
}

ReaderVersion? parseVersion(String line) {
  final p = parseLine(line);
  if (p.key != 'VERSION') return null;
  return ReaderVersion(btver: p['BTVER'] ?? '', hwver: p['HWVER'] ?? '', fwdate: p['FWDATE'] ?? '', fwver: p['FWVER'] ?? '', proto: p['PROTO'] ?? '');
}

ReaderSerial? parseSerial(String line) {
  final p = parseLine(line);
  if (p.key != 'SERIAL') return null;
  return ReaderSerial(year: p['YEAR'] ?? '', serial: p['SN'] ?? '');
}

ReaderPower? parsePower(String line) {
  final p = parseLine(line);
  if (p.key != 'POWER') return null;
  return ReaderPower(antenna: intValue(p['ANT'], 1), readCentiDbm: intValue(p['READ']), writeCentiDbm: intValue(p['WRITE']));
}

WifiStatus? parseWifi(String line) {
  final p = parseLine(line);
  if (p.key != 'WIFI') return null;
  return WifiStatus(
    link: p['LINK'] ?? '', state: p['STATE'] ?? '', ip: p['IP'] ?? '', rssi: intValue(p['RSSI']),
    host: boolValue(p['HOST']), upload: boolValue(p['UPLOAD']), enabled: boolValue(p['ENABLE']),
    mode: p['MODE'] ?? '', ssid: p['SSID'] ?? '', interfaceType: p['IF'] ?? '',
    errors: intValue(p['ERRORS']), rejected: p['REJECTED'] ?? '',
  );
}

Map<String, String> parseKeyValueReply(String line) {
  final p = parseLine(line);
  return <String, String>{if (p.key.isNotEmpty) '_type': p.key, ...p.fields, if (p.values.isNotEmpty) '_values': p.values.join(',')};
}
