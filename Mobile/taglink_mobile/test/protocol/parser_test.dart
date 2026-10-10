import 'package:flutter_test/flutter_test.dart';
import 'package:taglink_mobile/protocol/parser.dart';

void main() {
  group('Protocol parser', () {
    test('parses tag lines and preserves fields', () {
      final tag = parseTag('TAG,EPC=E2004703CA4068230FC80111,RSSI=-39,ANT=1,FREQ=927250,TIME=1057');
      expect(tag, isNotNull);
      expect(tag!.epc, 'E2004703CA4068230FC80111');
      expect(tag.rssi, -39);
      expect(tag.antenna, 1);
      expect(tag.frequencyKHz, 927250);
      expect(tag.readerTimeMs, 1057);
      expect(parseTag('TAG,EPC=not-hex,RSSI=-40,ANT=1'), isNull);
    });

    test('parses all documented reply families without throwing', () {
      expect(parseStatus('STATUS,IDLE')!.state, 'IDLE');
      expect(parseStatus('STATUS,INVENTORY')!.isInventory, isTrue);
      final version = parseVersion('VERSION,BTVER=00000001,HWVER=00000002,FWDATE=20261001,FWVER=00000102,PROTO=00000001')!;
      expect(version.fwver, '00000102');
      expect(parseSerial('SERIAL,YEAR=2026,SN=ABC123')!.serial, 'ABC123');
      expect(parsePower('POWER,ANT=1,READ=2000,WRITE=1950')!.readCentiDbm, 2000);
      expect(parseWifi('WIFI,LINK=ONLINE,STATE=CONNECTED,IP=192.168.4.1,RSSI=-48,HOST=1,UPLOAD=0,ENABLE=1,MODE=STA,SSID=RFID-Network,IF=WIFI,ERRORS=0,REJECTED=00/0')!.ip, '192.168.4.1');
      expect(parseKeyValueReply('REGION,04')['_type'], 'REGION');
      for (final line in <String>[
        'TEMP,37', 'OK,START', 'OK,STOP', 'OK,SINGLE_POLL', 'REGION,04',
        'ANTENNA,TX=1,RX=1', 'SESSION,2', 'READ_DATA,BANK=3,ADDR=0,WORDS=2,DATA=11223344',
        'OK,WRITE_TAG', 'OK,WRITE_EPC', 'GPI,GPI1=0,GPI2=1,GPI3=0,GPI4=1',
        'OK,SET_GPO', 'CAPABILITIES,GPI=4,GPO=4', 'DIAG,RESET=POWER_ON,UPTIME_MS=10',
        'OK,RELEASE', 'ERROR,INVALID_COMMAND', 'MALFORMED',
      ]) {
        expect(() => parseKeyValueReply(line), returnsNormally, reason: line);
      }
    });

    test('malformed and unknown fields are safe', () {
      expect(parseLine('').key, isEmpty);
      expect(parseTag('TAG,EPC=,RSSI=x'), isNull);
      expect(intValue('bad', 17), 17);
      expect(boolValue('1'), isTrue);
      expect(boolValue('0'), isFalse);
      expect(parseKeyValueReply('WIFI,SSID=name,with=equals')['SSID'], 'name');
    });
  });
}
