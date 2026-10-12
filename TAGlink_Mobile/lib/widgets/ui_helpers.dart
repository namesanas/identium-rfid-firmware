import 'package:flutter/material.dart';

void showMessage(BuildContext context, String message) {
  if (!context.mounted) return;
  ScaffoldMessenger.of(context)
    ..hideCurrentSnackBar()
    ..showSnackBar(SnackBar(content: Text(message), behavior: SnackBarBehavior.floating));
}

String humanError(Object error) {
  final text = error.toString();
  if (text.startsWith('Exception: ')) return text.substring(11);
  if (text.startsWith('Bad state: ')) return text.substring(11);
  return text;
}

/// A titled card: icon + title (+ optional trailing action), then content.
class SectionCard extends StatelessWidget {
  const SectionCard({super.key, required this.title, required this.icon, required this.child, this.subtitle, this.trailing});
  final String title;
  final IconData icon;
  final String? subtitle;
  final Widget? trailing;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Card(child: Padding(padding: const EdgeInsets.fromLTRB(16, 14, 16, 16), child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      Row(children: [
        Container(
          padding: const EdgeInsets.all(7),
          decoration: BoxDecoration(color: theme.colorScheme.primaryContainer, borderRadius: BorderRadius.circular(9)),
          child: Icon(icon, size: 18, color: theme.colorScheme.primary),
        ),
        const SizedBox(width: 12),
        Expanded(child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(title, style: theme.textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700)),
          if (subtitle != null) Text(subtitle!, style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
        ])),
        if (trailing != null) trailing!,
      ]),
      const SizedBox(height: 12),
      child,
    ])));
  }
}

/// Label on the left, value on the right, one line per entry.
class KeyValueList extends StatelessWidget {
  const KeyValueList(this.entries, {super.key, this.empty = 'No data yet'});
  final List<MapEntry<String, String>> entries;
  final String empty;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    if (entries.isEmpty) return Text(empty, style: TextStyle(color: theme.colorScheme.onSurfaceVariant));
    return Column(children: [
      for (var i = 0; i < entries.length; i++) ...[
        if (i > 0) Divider(height: 1, color: theme.colorScheme.outlineVariant.withValues(alpha: 0.6)),
        Padding(padding: const EdgeInsets.symmetric(vertical: 9), child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Expanded(flex: 5, child: Text(entries[i].key, style: TextStyle(color: theme.colorScheme.onSurfaceVariant))),
          const SizedBox(width: 12),
          Expanded(flex: 4, child: SelectableText(entries[i].value.isEmpty ? '—' : entries[i].value, textAlign: TextAlign.right, style: const TextStyle(fontWeight: FontWeight.w600))),
        ])),
      ],
    ]);
  }
}

/// A slim row above a page's content: a status line and a refresh button.
class PageToolbar extends StatelessWidget {
  const PageToolbar({super.key, required this.status, required this.loading, required this.onRefresh, this.error = false});
  final String status;
  final bool loading;
  final bool error;
  final VoidCallback onRefresh;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Column(children: [
      Row(children: [
        Expanded(child: Text(status, maxLines: 2, overflow: TextOverflow.ellipsis, style: theme.textTheme.bodySmall?.copyWith(color: error ? theme.colorScheme.error : theme.colorScheme.onSurfaceVariant))),
        IconButton(tooltip: 'Refresh', onPressed: loading ? null : onRefresh, icon: const Icon(Icons.refresh)),
      ]),
      SizedBox(height: 2, child: loading ? const LinearProgressIndicator() : null),
      const SizedBox(height: 6),
    ]);
  }
}

String timeLabel(DateTime time) =>
    '${time.hour.toString().padLeft(2, '0')}:${time.minute.toString().padLeft(2, '0')}:${time.second.toString().padLeft(2, '0')}';

/// 'YYYYMMDD' -> 'YYYY-MM-DD'; anything else unchanged.
String dateLabel(String value) =>
    RegExp(r'^\d{8}$').hasMatch(value) ? '${value.substring(0, 4)}-${value.substring(4, 6)}-${value.substring(6)}' : value;

String onOff(String? value) => value == '1' ? 'On' : value == '0' ? 'Off' : (value ?? '');

/// Reader replies use terse keys; these are the labels shown for them
/// (the same wording as the TAGlink desktop app where it has one).
const versionLabels = <String, String>{
  'BTVER': 'Bootloader', 'HWVER': 'Hardware', 'FWVER': 'Firmware', 'FWDATE': 'Firmware date', 'PROTO': 'Protocols',
};

const serialLabels = <String, String>{'SN': 'Serial number', 'YEAR': 'Year code'};

const wifiLabels = <String, String>{
  'LINK': 'Coprocessor link', 'STATE': 'Connection', 'IP': 'IP address', 'RSSI': 'Signal', 'HOST': 'Host commands',
  'UPLOAD': 'Tag upload', 'ENABLE': 'Wi-Fi', 'MODE': 'Mode', 'SSID': 'Network (SSID)', 'IF': 'Upload interface',
  'ERRORS': 'Link errors', 'REJECTED': 'Rejected frames',
};

const diagLabels = <String, String>{
  'RESET': 'Last reset cause', 'UPTIME_MS': 'Uptime', 'FAULTS': 'Faults since power-on', 'LAST_FAULT': 'Previous run ended by',
  'PC': 'Fault PC', 'LR': 'Fault LR', 'CFSR': 'CFSR', 'HFSR': 'HFSR', 'MMFAR': 'MMFAR', 'BFAR': 'BFAR', 'AT_MS': 'Fault at uptime (ms)',
  'LOOP_MAX_US': 'Longest main-loop pass (µs)', 'PASSES': 'Main-loop passes', 'STACK_USED': 'Stack used (bytes)',
  'STACK_SIZE': 'Stack size (bytes)', 'BOR': 'Brown-out level',
};

String uptimeLabel(String ms) {
  final value = int.tryParse(ms);
  if (value == null) return ms;
  final s = value ~/ 1000;
  final h = s ~/ 3600, m = (s % 3600) ~/ 60, sec = s % 60;
  return h > 0 ? '${h}h ${m}m ${sec}s' : m > 0 ? '${m}m ${sec}s' : '${sec}s';
}

/// Turns a parsed reply into labelled rows, dropping internal keys.
List<MapEntry<String, String>> labelled(Map<String, String> values, Map<String, String> labels, {String Function(String key, String value)? format}) => [
  for (final e in values.entries)
    if (!e.key.startsWith('_'))
      MapEntry(labels[e.key] ?? (e.key.endsWith('_US') ? 'Longest ${e.key.substring(0, e.key.length - 3).toLowerCase()} task (µs)' : e.key),
          format == null ? e.value : format(e.key, e.value)),
];
