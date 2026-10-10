import 'package:flutter_test/flutter_test.dart';
import 'package:taglink_mobile/protocol/line_framer.dart';

void main() {
  group('LineFramer', () {
    test('handles a line split across arbitrary chunks', () {
      final framer = LineFramer();
      expect(framer.add('STAT'), isEmpty);
      expect(framer.add('US,IDLE\r'), isEmpty);
      expect(framer.add('\n'), <String>['STATUS,IDLE']);
    });

    test('handles joined CRLF and LF lines and ignores empty lines', () {
      final framer = LineFramer();
      expect(framer.add('A,1\r\n\nB,2\nC,3\r\n'), <String>['A,1', 'B,2', 'C,3']);
    });

    test('retains only a bounded amount of malformed unterminated data', () {
      final framer = LineFramer();
      framer.add('x' * 70000);
      expect(framer.pendingText.length, lessThanOrEqualTo(65536));
    });
  });
}
