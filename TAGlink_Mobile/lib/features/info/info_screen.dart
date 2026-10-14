import 'dart:async';
import 'package:flutter/material.dart';

import '../../protocol/parser.dart';
import '../../transport/reader_client.dart';
import '../../widgets/ui_helpers.dart';

class InfoScreen extends StatefulWidget {
  const InfoScreen({super.key, required this.client});
  final ReaderClient client;
  @override
  State<InfoScreen> createState() => _InfoScreenState();
}

class _InfoScreenState extends State<InfoScreen> {
  Map<String, String> _version = <String, String>{};
  Map<String, String> _serial = <String, String>{};
  String _temperature = '—';
  Map<String, String> _diagnostics = <String, String>{};
  String _updated = '';
  bool _loading = false;
  String _error = '';

  @override
  void initState() { super.initState(); unawaited(_refresh()); }

  Future<String> _get(String command) async {
    final line = await widget.client.send(command);
    if (line.startsWith('ERROR,')) throw StateError(line);
    return line;
  }

  Future<void> _refresh() async {
    setState(() { _loading = true; _error = ''; });
    try {
      final version = parseKeyValueReply(await _get('GET_VERSION'));
      final serial = parseKeyValueReply(await _get('GET_SERIAL'));
      final tempLine = parseLine(await _get('GET_TEMPERATURE'));
      final diag = parseKeyValueReply(await _get('GET_DIAG'));
      if (!mounted) return;
      setState(() {
        _version = version;
        _serial = serial;
        _temperature = tempLine.values.isNotEmpty ? '${tempLine.values.first} °C' : '—';
        _diagnostics = diag..remove('_type');
        _updated = timeLabel(DateTime.now());
      });
    } catch (error) { if (mounted) setState(() => _error = humanError(error)); }
    finally { if (mounted) setState(() => _loading = false); }
  }

  Future<void> _restart() async {
    final yes = await showDialog<bool>(context: context, builder: (ctx) => AlertDialog(
      title: const Text('Restart reader?'),
      content: const Text('A running inventory stops and the connection drops while the reader restarts. The app reconnects when the reader is back on Wi-Fi.'),
      actions: [TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')), FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Restart'))],
    ));
    if (yes != true || !mounted) return;
    setState(() { _loading = true; _error = ''; });
    try {
      await widget.client.restartReader();
    } catch (error) {
      if (!mounted) return;
      final text = error.toString();
      setState(() => _error = text.contains('INVALID_COMMAND') ? 'This reader firmware cannot be restarted from the app. Update the firmware.' : humanError(error));
    } finally { if (mounted) setState(() => _loading = false); }
  }

  Widget _stat(BuildContext context, IconData icon, String label, String value) {
    final theme = Theme.of(context);
    return Expanded(child: Card(child: Padding(padding: const EdgeInsets.symmetric(vertical: 14, horizontal: 12), child: Column(children: [
      Icon(icon, size: 22, color: theme.colorScheme.primary),
      const SizedBox(height: 6),
      Text(value, maxLines: 1, overflow: TextOverflow.ellipsis, style: theme.textTheme.titleLarge?.copyWith(fontWeight: FontWeight.w800)),
      Text(label, style: theme.textTheme.labelSmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
    ]))));
  }

  @override
  Widget build(BuildContext context) {
    final status = _error.isNotEmpty ? _error : _loading ? 'Reading reader information…' : _version.isEmpty ? '' : 'Updated $_updated';
    return RefreshIndicator(onRefresh: _refresh, child: ListView(padding: const EdgeInsets.fromLTRB(16, 0, 16, 24), children: [
      PageToolbar(status: status, loading: _loading, error: _error.isNotEmpty, onRefresh: _refresh),
      Row(children: [
        _stat(context, Icons.thermostat, 'Module temperature', _temperature),
        const SizedBox(width: 8),
        _stat(context, Icons.memory, 'Firmware', _version['FWVER'] ?? '—'),
      ]),
      const SizedBox(height: 4),
      SectionCard(title: 'Module', icon: Icons.developer_board, child: KeyValueList(labelled(_version, versionLabels, format: (k, v) => k == 'FWDATE' ? dateLabel(v) : v))),
      SectionCard(title: 'Identity', icon: Icons.qr_code_2, child: KeyValueList(labelled(_serial, serialLabels))),
      SectionCard(title: 'Health', icon: Icons.monitor_heart_outlined, child: KeyValueList(labelled(_diagnostics, diagLabels, format: (k, v) => k == 'UPTIME_MS' ? uptimeLabel(v) : v))),
      const SizedBox(height: 8),
      OutlinedButton.icon(
        onPressed: _loading ? null : _restart,
        style: OutlinedButton.styleFrom(minimumSize: const Size.fromHeight(48), foregroundColor: Theme.of(context).colorScheme.error),
        icon: const Icon(Icons.restart_alt),
        label: const Text('Restart reader'),
      ),
    ]));
  }
}
