import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:taglink_mobile/app.dart';

void main() {
  // MaterialApp builds both themes, so a theme mistake fails in either mode.
  for (final dark in [false, true]) {
    testWidgets('app starts with the ${dark ? 'dark' : 'light'} theme', (tester) async {
      SharedPreferences.setMockInitialValues({'dark_theme': dark});
      await tester.pumpWidget(const TaglinkApp());
      await tester.pump();
      await tester.pump();
      expect(tester.takeException(), isNull);
      expect(find.byType(CircularProgressIndicator), findsNothing);
      final theme = Theme.of(tester.element(find.byType(Scaffold).first));
      expect(theme.brightness, dark ? Brightness.dark : Brightness.light);
    });
  }
}
