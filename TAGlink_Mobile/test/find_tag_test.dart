import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:taglink_mobile/features/tags/find_tag_screen.dart';
import 'package:taglink_mobile/theme.dart';
import 'package:taglink_mobile/transport/reader_client.dart';

const _wanted = 'E2004703CA4068230FC80111';
const _other = 'E2004703CA4068230FC80999';

void main() {
  testWidgets('starts inventory, follows the wanted tag and stops on close', (tester) async {
    final received = <String>[];
    late ServerSocket server;
    final client = ReaderClient();
    Timer? stream;
    await tester.runAsync(() async {
      server = await ServerSocket.bind(InternetAddress.loopbackIPv4, 0);
      server.listen((socket) {
        var pending = '';
        socket.listen((bytes) {
          pending += utf8.decode(bytes);
          while (pending.contains('\n')) {
            final line = pending.substring(0, pending.indexOf('\n')).replaceAll('\r', '');
            pending = pending.substring(pending.indexOf('\n') + 1);
            received.add(line);
            if (line == 'GET_STATUS') socket.add(utf8.encode('STATUS,IDLE\r\n'));
            if (line == 'START') {
              socket.add(utf8.encode('OK,START\r\n'));
              stream = Timer.periodic(const Duration(milliseconds: 40), (_) => socket.add(utf8.encode(
                'TAG,EPC=$_wanted,RSSI=-42,ANT=1,FREQ=927250,TIME=10\r\nTAG,EPC=$_other,RSSI=-35,ANT=1,FREQ=927250,TIME=10\r\n')));
            }
            if (line == 'STOP') { stream?.cancel(); socket.add(utf8.encode('OK,STOP\r\n')); }
            if (line == 'RELEASE') socket.add(utf8.encode('OK,RELEASE\r\n'));
          }
        });
      });
      await client.connect('127.0.0.1', port: server.port);
    });

    await tester.pumpWidget(MaterialApp(theme: taglinkTheme(Brightness.light), home: const SizedBox()));
    final navigator = tester.state<NavigatorState>(find.byType(Navigator));
    unawaited(navigator.push(MaterialPageRoute<void>(builder: (_) => FindTagScreen(client: client, epc: _wanted))));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));   // route transition
    await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 700)));
    await tester.pump(const Duration(milliseconds: 300));

    expect(received, contains('START'));
    // Only the wanted tag drives the gauge: -42 dBm, not the other tag's -35.
    expect(find.text('-42'), findsOneWidget);
    expect(find.text('Close'), findsOneWidget);

    navigator.pop();
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    await tester.pump(const Duration(milliseconds: 100));
    expect(find.byType(FindTagScreen), findsNothing);
    // Closing the screen queues STOP because the screen started the inventory.
    for (var i = 0; i < 10 && !received.contains('STOP'); i++) {
      await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 50)));
      await tester.pump(const Duration(milliseconds: 50));
    }
    expect(received, contains('STOP'));
    stream?.cancel();
    final closed = client.disconnect();
    var done = false;
    unawaited(closed.then((_) => done = true));
    for (var i = 0; i < 40 && !done; i++) {
      await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 50)));
      await tester.pump(const Duration(milliseconds: 100));
    }
    expect(done, isTrue);
    await tester.runAsync(() async { await client.dispose(); await server.close(); });
    await tester.pump();
  });
}
