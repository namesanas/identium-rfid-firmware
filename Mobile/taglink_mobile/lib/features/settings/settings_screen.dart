import 'dart:async';
import 'package:flutter/material.dart';

import '../../protocol/command_builder.dart';
import '../../protocol/parser.dart';
import '../../transport/reader_client.dart';
import '../../widgets/ui_helpers.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key, required this.client});
  final ReaderClient client;
  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  int _power = 2000;
  int _session = 0;
  String _region = '—';
  String _antenna = '—';
  Map<String, String> _inputs = <String, String>{};
  final Map<int, bool> _outputs = <int, bool>{1: false, 2: false, 3: false, 4: false};
  bool _loading = false;
  String _status = '';

  @override
  void initState() { super.initState(); unawaited(_refresh()); }

  Future<String> _request(String command) async {
    final reply = await widget.client.send(command);
    if (reply.startsWith('ERROR,')) throw StateError(reply);
    return reply;
  }

  Future<void> _refresh() async {
    setState(() { _loading = true; _status = 'Refreshing reader settings…'; });
    try {
      final power = parsePower(await _request(CommandBuilder.simple('GET_POWER')));
      final session = parseLine(await _request(CommandBuilder.simple('GET_SESSION')));
      final region = parseLine(await _request(CommandBuilder.simple('GET_REGION')));
      final antenna = parseLine(await _request(CommandBuilder.simple('GET_ANTENNA')));
      final gpi = parseLine(await _request(CommandBuilder.simple('GET_GPI')));
      final caps = await _request(CommandBuilder.simple('GET_CAPABILITIES'));
      if (!mounted) return;
      setState(() {
        if (power != null) _power = power.readCentiDbm.clamp(500, 3000).toInt();
        _session = intValue(session.values.isNotEmpty ? session.values.first : null).clamp(0, 3).toInt();
        _region = region.values.isNotEmpty ? region.values.first : '—';
        _antenna = 'TX=${antenna['TX'] ?? '?'} · RX=${antenna['RX'] ?? '?'}';
        _inputs = Map<String, String>.from(gpi.fields);
        final cap = parseKeyValueReply(caps);
        final gpoCount = int.tryParse(cap['GPO'] ?? '') ?? 4;
        for (var i = gpoCount + 1; i <= 4; i++) { _outputs.remove(i); }
        _status = 'Settings refreshed.';
      });
    } catch (error) {
      if (mounted) setState(() => _status = humanError(error));
    } finally {
      if (mounted) setState(() => _loading = false);
    }
  }

  Future<void> _applyPower() async {
    setState(() => _loading = true);
    try {
      await _request(CommandBuilder.setPower(_power));
      if (mounted) showMessage(context, 'Read power set to ${(_power / 100).toStringAsFixed(2)} dBm.');
      await _refresh();
    } catch (error) { if (mounted) showMessage(context, humanError(error)); }
    finally { if (mounted) setState(() => _loading = false); }
  }

  Future<void> _setSession(int value) async {
    setState(() { _session = value; _loading = true; });
    try {
      await _request(CommandBuilder.setSession(value));
      if (mounted) showMessage(context, 'Session set to $value.');
    } catch (error) { if (mounted) showMessage(context, humanError(error)); await _refresh(); }
    finally { if (mounted) setState(() => _loading = false); }
  }

  Future<void> _setOutput(int channel, bool value) async {
    setState(() => _outputs[channel] = value);
    try {
      await _request(CommandBuilder.setGpo(channel: channel, value: value));
    } catch (error) {
      if (mounted) { setState(() => _outputs[channel] = !value); showMessage(context, humanError(error)); }
    }
  }

  @override
  Widget build(BuildContext context) => RefreshIndicator(onRefresh: _refresh, child: ListView(padding: const EdgeInsets.fromLTRB(16, 8, 16, 24), children: [
    Row(children: [Expanded(child: Text('Reader configuration', style: Theme.of(context).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.bold))), IconButton(onPressed: _loading ? null : _refresh, icon: const Icon(Icons.refresh))]),
    if (_loading) const LinearProgressIndicator(),
    if (_status.isNotEmpty) Padding(padding: const EdgeInsets.symmetric(vertical: 5), child: Text(_status, style: const TextStyle(color: Colors.grey))),
    Card(child: Padding(padding: const EdgeInsets.all(16), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      sectionTitle('Read power', subtitle: 'UI range 5.00–30.00 dBm, in 0.50 dB steps.'),
      Row(crossAxisAlignment: CrossAxisAlignment.center, children: [Expanded(child: Slider(value: _power.toDouble(), min: 500, max: 3000, divisions: 50, label: '${(_power / 100).toStringAsFixed(2)} dBm', onChanged: _loading ? null : (value) => setState(() => _power = value.round()))), Text('${(_power / 100).toStringAsFixed(2)} dBm', style: Theme.of(context).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.bold))]),
      Align(alignment: Alignment.centerRight, child: FilledButton(onPressed: _loading ? null : _applyPower, child: const Text('Apply power'))),
      const Divider(height: 24),
      Row(children: [const Expanded(child: Text('Session')), DropdownButton<int>(value: _session, items: const [DropdownMenuItem(value: 0, child: Text('S0')), DropdownMenuItem(value: 1, child: Text('S1')), DropdownMenuItem(value: 2, child: Text('S2')), DropdownMenuItem(value: 3, child: Text('S3'))], onChanged: _loading ? null : (value) { if (value != null) unawaited(_setSession(value)); })]),
      infoTile('Region (read-only)', _region),
      infoTile('Antenna configuration', _antenna),
    ]))),
    Card(child: Padding(padding: const EdgeInsets.all(16), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      sectionTitle('Digital inputs', subtitle: 'GPI status from GET_GPI.'),
      for (var i = 1; i <= 4; i++) ListTile(contentPadding: EdgeInsets.zero, leading: Icon(_inputs['GPI$i'] == '1' ? Icons.radio_button_checked : Icons.radio_button_unchecked, color: _inputs['GPI$i'] == '1' ? Colors.green : null), title: Text('GPI $i'), trailing: Text(_inputs['GPI$i'] == '1' ? 'HIGH' : _inputs.containsKey('GPI$i') ? 'LOW' : '—')),
      const Divider(),
      sectionTitle('Digital outputs', subtitle: 'Toggle stays in the selected state (duration = 0). Use care with connected equipment.'),
      for (final channel in _outputs.keys.toList()..sort()) SwitchListTile(contentPadding: EdgeInsets.zero, title: Text('GPO $channel'), subtitle: Text(_outputs[channel]! ? 'HIGH' : 'LOW'), value: _outputs[channel]!, onChanged: _loading ? null : (value) => _setOutput(channel, value)),
    ]))),
  ]));
}
