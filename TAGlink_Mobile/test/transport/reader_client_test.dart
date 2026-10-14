import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:taglink_mobile/transport/reader_client.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('serializes commands and routes interleaved TAG lines separately', () async {
    final server = await ServerSocket.bind(InternetAddress.loopbackIPv4, 0);
    final received = <String>[];
    final accepted = Completer<void>();
    server.listen((socket) {
      accepted.complete();
      final buffer = StringBuffer();
      socket.listen((bytes) {
        buffer.write(utf8.decode(bytes));
        var text = buffer.toString();
        while (text.contains('\n')) {
          final split = text.indexOf('\n');
          final line = text.substring(0, split).replaceAll('\r', '');
          text = text.substring(split + 1);
          buffer.clear(); buffer.write(text);
          received.add(line);
          if (line == 'GET_STATUS') {
            socket.add(utf8.encode('TAG,EPC=E2004703CA4068230FC80111,RSSI=-42,ANT=1,FREQ=927250,TIME=10\r\nSTA'));
            Timer(const Duration(milliseconds: 10), () => socket.add(utf8.encode('TUS,IDLE\r\n')));
          } else if (line == 'GET_VERSION') {
            socket.add(utf8.encode('VERSION,BTVER=00000001,HWVER=00000002,FWDATE=20261001,FWVER=00000102,PROTO=00000001\n'));
          } else if (line == 'GET_TEMPERATURE') {
            socket.add(utf8.encode('TEMP,37\r\n'));
          } else if (line == 'RELEASE') {
            socket.add(utf8.encode('OK,RELEASE\r\n'));
          }
        }
      });
    });

    final client = ReaderClient();
    final tags = <String>[];
    final sub = client.tagBatches.listen((batch) => tags.addAll(batch.map((t) => t.epc)));
    try {
      await client.connect('127.0.0.1', port: server.port);
      await accepted.future;
      final version = client.send('GET_VERSION');
      final temperature = client.send('GET_TEMPERATURE');
      expect(await version, startsWith('VERSION,'));
      expect(await temperature, 'TEMP,37');
      await Future<void>.delayed(const Duration(milliseconds: 250));
      expect(received.take(3), <String>['GET_STATUS', 'GET_VERSION', 'GET_TEMPERATURE']);
      expect(tags, contains('E2004703CA4068230FC80111'));
      expect(client.logs.length, greaterThanOrEqualTo(5));
    } finally {
      await sub.cancel();
      await client.disconnect();
      await server.close();
    }
  });

  test('times out a command before transmitting the next queued command', () async {
    final server = await ServerSocket.bind(InternetAddress.loopbackIPv4, 0);
    final received = <String>[];
    server.listen((socket) {
      var buffer = '';
      socket.listen((bytes) {
        buffer += utf8.decode(bytes);
        while (buffer.contains('\n')) {
          final index = buffer.indexOf('\n');
          final command = buffer.substring(0, index).replaceAll('\r', '');
          buffer = buffer.substring(index + 1);
          received.add(command);
          if (command == 'GET_STATUS') socket.add(utf8.encode('STATUS,IDLE\r\n'));
          if (command == 'GET_VERSION') socket.add(utf8.encode('VERSION,BTVER=00000001\r\n'));
        }
      });
    });
    final client = ReaderClient();
    try {
      await client.connect('127.0.0.1', port: server.port);
      final missing = client.send('GET_TEMPERATURE');
      final next = client.send('GET_VERSION');
      await expectLater(missing, throwsA(isA<TimeoutException>()));
      expect(await next, startsWith('VERSION,'));
      expect(received.indexOf('GET_TEMPERATURE'), lessThan(received.indexOf('GET_VERSION')));
    } finally {
      await client.disconnect();
      await server.close();
    }
  }, timeout: const Timeout(Duration(seconds: 12)));

  test('restartReader sends REBOOT and reconnects when the reader is back', () async {
    final received = <String>[];
    void serve(Socket socket) {
      var buffer = '';
      socket.listen((bytes) {
        buffer += utf8.decode(bytes);
        while (buffer.contains('\n')) {
          final index = buffer.indexOf('\n');
          final command = buffer.substring(0, index).replaceAll('\r', '');
          buffer = buffer.substring(index + 1);
          received.add(command);
          if (command == 'GET_STATUS') socket.add(utf8.encode('STATUS,IDLE\r\n'));
          if (command == 'RELEASE') socket.add(utf8.encode('OK,RELEASE\r\n'));
          if (command == 'REBOOT') {
            socket.add(utf8.encode('OK,REBOOT\r\n'));
            Timer(const Duration(milliseconds: 50), socket.destroy);
          }
        }
      }, onError: (_) {});
    }

    var server = await ServerSocket.bind(InternetAddress.loopbackIPv4, 0);
    final port = server.port;
    server.listen(serve);
    final client = ReaderClient();
    try {
      await client.connect('127.0.0.1', port: port);
      // The reader is down while it restarts: nothing listens on the port.
      await client.restartReader();
      await server.close();
      expect(client.connected.value, isFalse);
      expect(client.restarting.value, isTrue);
      await Future<void>.delayed(const Duration(seconds: 6));
      expect(client.connected.value, isFalse);
      server = await ServerSocket.bind(InternetAddress.loopbackIPv4, port);
      server.listen(serve);
      final deadline = DateTime.now().add(const Duration(seconds: 8));
      while (!client.connected.value && DateTime.now().isBefore(deadline)) {
        await Future<void>.delayed(const Duration(milliseconds: 100));
      }
      expect(client.connected.value, isTrue);
      expect(client.restarting.value, isFalse);
      expect(received.contains('RELEASE'), isFalse);
      expect(received.where((c) => c == 'REBOOT').length, 1);
    } finally {
      await client.disconnect();
      await server.close();
    }
  }, timeout: const Timeout(Duration(seconds: 30)));
}
