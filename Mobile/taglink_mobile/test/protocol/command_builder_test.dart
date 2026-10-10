import 'package:flutter_test/flutter_test.dart';
import 'package:taglink_mobile/protocol/command_builder.dart';

void main() {
  group('CommandBuilder', () {
    test('builds all standard safe commands', () {
      expect(CommandBuilder.simple('GET_STATUS'), 'GET_STATUS');
      expect(CommandBuilder.singlePoll(1000), 'SINGLE_POLL,1000');
      expect(CommandBuilder.setPower(2000), 'SET_POWER,2000');
      expect(CommandBuilder.setSession(3), 'SET_SESSION,3');
      expect(CommandBuilder.setGpo(channel: 2, value: true), 'SET_GPO,2,1,0');
      expect(CommandBuilder.setWifi(enabled: true, mode: 'STA', ssid: 'Lab WiFi', password: 'password,with,commas'), 'SET_WIFI,1,STA,Lab WiFi,password,with,commas');
    });

    test('rejects malformed values instead of emitting unsafe commands', () {
      expect(() => CommandBuilder.simple('CUSTOM\r\nSTOP'), throwsArgumentError);
      expect(() => CommandBuilder.singlePoll(0), throwsArgumentError);
      expect(() => CommandBuilder.setPower(2010), throwsArgumentError);
      expect(() => CommandBuilder.setSession(4), throwsArgumentError);
      expect(() => CommandBuilder.readTag(epc: 'NOTHEX', bank: 1, wordAddress: 0, wordCount: 1), throwsArgumentError);
      expect(() => CommandBuilder.writeTag(epc: 'E200', bank: 3, wordAddress: 0, accessPassword: 'ABC', dataHex: '12'), throwsArgumentError);
      expect(() => CommandBuilder.setWifi(enabled: true, mode: 'STA', ssid: 'bad,name', password: ''), throwsArgumentError);
      expect(() => CommandBuilder.setWifi(enabled: true, mode: 'AP', ssid: 'Reader', password: 'short'), throwsArgumentError);
      expect(() => CommandBuilder.setWifi(enabled: true, mode: 'STA', ssid: 'Café', password: 'password123'), throwsArgumentError);
    });
  });
}
