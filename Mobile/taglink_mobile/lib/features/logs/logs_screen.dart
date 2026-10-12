import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../../protocol/models.dart';
import '../../transport/reader_client.dart';
import '../../widgets/ui_helpers.dart';
import '../../theme.dart';

class LogsScreen extends StatefulWidget {
  const LogsScreen({super.key, required this.client});
  final ReaderClient client;
  @override
  State<LogsScreen> createState() => _LogsScreenState();
}

class _LogsScreenState extends State<LogsScreen> {
  late final List<ProtocolLogEntry> _entries = List<ProtocolLogEntry>.of(widget.client.logs);
  StreamSubscription<ProtocolLogEntry>? _subscription;
  Timer? _refresh;
  bool _txOnly = false;

  @override
  void initState() {
    super.initState();
    _subscription = widget.client.logStream.listen((entry) {
      _entries.add(entry);
      if (_entries.length > 500) _entries.removeRange(0, _entries.length - 500);
      // Inventory produces many lines a second: redraw at most every 200 ms.
      _refresh ??= Timer(const Duration(milliseconds: 200), () { _refresh = null; if (mounted) setState(() {}); });
    });
  }

  Future<void> _copy() async {
    await Clipboard.setData(ClipboardData(text: _entries.map((e) => e.toString()).join('\n')));
    if (mounted) showMessage(context, 'Protocol log copied.');
  }

  void _clear() { widget.client.clearLogs(); setState(_entries.clear); }

  @override
  void dispose() { _subscription?.cancel(); _refresh?.cancel(); super.dispose(); }

  Future<void> _copyLine(ProtocolLogEntry entry) async {
    await Clipboard.setData(ClipboardData(text: entry.line));
    if (mounted) showMessage(context, 'Line copied.');
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final sentColor = theme.colorScheme.primary;
    final receivedColor = TaglinkColors.of(context).success;
    final shown = _txOnly ? _entries.where((e) => e.direction == 'TX').toList() : _entries;
    return Column(children: [
      Padding(padding: const EdgeInsets.fromLTRB(16, 10, 8, 8), child: Row(children: [
        SegmentedButton<bool>(
          showSelectedIcon: false,
          style: const ButtonStyle(visualDensity: VisualDensity.compact),
          segments: const [ButtonSegment(value: false, label: Text('All')), ButtonSegment(value: true, label: Text('Sent'))],
          selected: {_txOnly},
          onSelectionChanged: (value) => setState(() => _txOnly = value.first),
        ),
        const SizedBox(width: 12),
        Text('${_entries.length} lines', style: theme.textTheme.labelMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
        const Spacer(),
        IconButton(tooltip: 'Copy log', onPressed: _entries.isEmpty ? null : _copy, icon: const Icon(Icons.copy_all_outlined)),
        IconButton(tooltip: 'Clear log', onPressed: _entries.isEmpty ? null : _clear, icon: const Icon(Icons.delete_outline)),
      ])),
      Divider(height: 1, color: theme.colorScheme.outlineVariant),
      Expanded(child: shown.isEmpty
        ? Center(child: Column(mainAxisSize: MainAxisSize.min, children: [
            Icon(Icons.terminal, size: 44, color: theme.colorScheme.outline),
            const SizedBox(height: 10),
            Text('No traffic yet', style: theme.textTheme.titleMedium),
          ]))
        // reverse: the list starts at the newest line and stays there as lines arrive.
        : Container(color: TaglinkColors.of(context).log, child: ListView.builder(reverse: true, padding: const EdgeInsets.symmetric(vertical: 6), itemCount: shown.length, itemBuilder: (context, index) {
            final entry = shown[shown.length - 1 - index];
            final sent = entry.direction == 'TX';
            return InkWell(onLongPress: () => _copyLine(entry), child: Padding(padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 5), child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
              SizedBox(width: 62, child: Text(timeLabel(entry.time), style: theme.textTheme.labelSmall?.copyWith(color: theme.colorScheme.onSurfaceVariant, fontFeatures: const [FontFeature.tabularFigures()]))),
              Padding(padding: const EdgeInsets.only(right: 8, top: 1), child: Icon(sent ? Icons.arrow_upward : Icons.arrow_downward, size: 13, color: sent ? sentColor : receivedColor)),
              Expanded(child: Text(entry.line, style: TextStyle(fontFamily: 'monospace', fontSize: 12, height: 1.35, color: sent ? sentColor : theme.colorScheme.onSurface, fontWeight: sent ? FontWeight.w700 : FontWeight.w400))),
            ])));
          }))),
    ]);
  }
}
