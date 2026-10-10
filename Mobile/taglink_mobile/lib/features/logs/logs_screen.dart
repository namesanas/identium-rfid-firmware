import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../../protocol/models.dart';
import '../../transport/reader_client.dart';
import '../../widgets/ui_helpers.dart';

class LogsScreen extends StatefulWidget {
  const LogsScreen({super.key, required this.client});
  final ReaderClient client;
  @override
  State<LogsScreen> createState() => _LogsScreenState();
}

class _LogsScreenState extends State<LogsScreen> {
  late List<ProtocolLogEntry> _entries = widget.client.logs;
  StreamSubscription<ProtocolLogEntry>? _subscription;
  bool _txOnly = false;

  @override
  void initState() {
    super.initState();
    _subscription = widget.client.logStream.listen((entry) {
      if (!mounted) return;
      setState(() { _entries = <ProtocolLogEntry>[..._entries, entry]; if (_entries.length > 500) _entries.removeRange(0, _entries.length - 500); });
    });
  }

  Future<void> _copy() async {
    await Clipboard.setData(ClipboardData(text: _entries.map((e) => e.toString()).join('\n')));
    if (mounted) showMessage(context, 'Protocol log copied.');
  }

  void _clear() { widget.client.clearLogs(); setState(() => _entries = <ProtocolLogEntry>[]); }

  @override
  void dispose() { _subscription?.cancel(); super.dispose(); }

  @override
  Widget build(BuildContext context) {
    final shown = _txOnly ? _entries.where((e) => e.direction == 'TX').toList() : _entries;
    return Column(children: [
      Padding(padding: const EdgeInsets.fromLTRB(16, 8, 16, 0), child: Row(children: [Expanded(child: Text('Protocol log', style: Theme.of(context).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.bold))), Text('${_entries.length}/500', style: Theme.of(context).textTheme.labelMedium)])),
      Padding(padding: const EdgeInsets.symmetric(horizontal: 12), child: Row(children: [Expanded(child: SwitchListTile(dense: true, contentPadding: EdgeInsets.zero, title: const Text('Show sent commands only'), value: _txOnly, onChanged: (value) => setState(() => _txOnly = value))), IconButton(tooltip: 'Copy log', onPressed: _copy, icon: const Icon(Icons.copy)), IconButton(tooltip: 'Clear log', onPressed: _clear, icon: const Icon(Icons.delete_outline))])),
      const Divider(height: 1),
      Expanded(child: shown.isEmpty ? const Center(child: Text('No protocol traffic yet.')) : ListView.builder(itemCount: shown.length, itemBuilder: (context, index) {
        final entry = shown[index];
        final sent = entry.direction == 'TX';
        return Container(color: sent ? Theme.of(context).colorScheme.primary.withOpacity(0.045) : null, padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 7), child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
          SizedBox(width: 64, child: Text('${entry.time.hour.toString().padLeft(2, '0')}:${entry.time.minute.toString().padLeft(2, '0')}:${entry.time.second.toString().padLeft(2, '0')}.${entry.time.millisecond.toString().padLeft(3, '0')}', style: Theme.of(context).textTheme.labelSmall)),
          SizedBox(width: 28, child: Text(entry.direction, style: TextStyle(color: sent ? Theme.of(context).colorScheme.primary : Colors.teal, fontWeight: FontWeight.bold, fontSize: 11))),
          Expanded(child: SelectableText(entry.line, style: const TextStyle(fontFamily: 'monospace', fontSize: 12))),
        ]));
      })),
    ]);
  }
}
