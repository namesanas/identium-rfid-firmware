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
  Map<String, String> _wifi = <String, String>{};
  Map<String, String> _diagnostics = <String, String>{};
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
      final wifi = parseKeyValueReply(await _get('GET_WIFI'));
      final diag = parseKeyValueReply(await _get('GET_DIAG'));
      if (!mounted) return;
      setState(() {
        _version = version;
        _serial = serial;
        _temperature = tempLine.values.isNotEmpty ? '${tempLine.values.first} °C' : '—';
        _wifi = wifi;
        _diagnostics = diag..remove('_type');
      });
    } catch (error) { if (mounted) setState(() => _error = humanError(error)); }
    finally { if (mounted) setState(() => _loading = false); }
  }

  Widget _mapCard(String title, Map<String, String> values, IconData icon) => Card(child: Padding(padding: const EdgeInsets.all(16), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
    Row(children: [Icon(icon), const SizedBox(width: 8), Text(title, style: Theme.of(context).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.bold))]),
    const SizedBox(height: 8),
    if (values.isEmpty) const Text('No data yet'),
    for (final entry in values.entries) if (!entry.key.startsWith('_')) infoTile(entry.key, entry.value),
  ])));

  @override
  Widget build(BuildContext context) => RefreshIndicator(onRefresh: _refresh, child: ListView(padding: const EdgeInsets.fromLTRB(16, 8, 16, 24), children: [
    Row(children: [Expanded(child: Text('Reader information', style: Theme.of(context).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.bold))), IconButton(onPressed: _loading ? null : _refresh, icon: const Icon(Icons.refresh))]),
    if (_loading) const LinearProgressIndicator(),
    if (_error.isNotEmpty) Padding(padding: const EdgeInsets.all(8), child: Text(_error, style: TextStyle(color: Theme.of(context).colorScheme.error))),
    _mapCard('Firmware version', _version, Icons.memory),
    _mapCard('Serial number', _serial, Icons.qr_code_2),
    Card(child: Padding(padding: const EdgeInsets.all(16), child: Row(children: [const Icon(Icons.thermostat), const SizedBox(width: 10), const Expanded(child: Text('Module temperature')), Text(_temperature, style: Theme.of(context).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.bold))]))),
    _mapCard('Wi-Fi status', _wifi, Icons.wifi),
    _mapCard('Diagnostics · GET_DIAG', _diagnostics, Icons.health_and_safety_outlined),
  ]));
}
