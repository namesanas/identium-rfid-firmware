import 'package:flutter/material.dart';

void showMessage(BuildContext context, String message) {
  if (!context.mounted) return;
  ScaffoldMessenger.of(context)
    ..hideCurrentSnackBar()
    ..showSnackBar(SnackBar(content: Text(message), behavior: SnackBarBehavior.floating));
}

String humanError(Object error) {
  final text = error.toString();
  return text.startsWith('Exception: ') ? text.substring(11) : text;
}

Widget sectionTitle(String title, {String? subtitle}) => Padding(
  padding: const EdgeInsets.fromLTRB(0, 18, 0, 8),
  child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
    Text(title, style: const TextStyle(fontSize: 17, fontWeight: FontWeight.w700)),
    if (subtitle != null) Padding(padding: const EdgeInsets.only(top: 3), child: Text(subtitle, style: const TextStyle(color: Colors.grey))),
  ]),
);

Widget infoTile(String title, String value, {IconData? icon}) => ListTile(
  leading: icon == null ? null : Icon(icon),
  title: Text(title),
  subtitle: SelectableText(value.isEmpty ? '—' : value),
  dense: true,
  contentPadding: EdgeInsets.zero,
);
