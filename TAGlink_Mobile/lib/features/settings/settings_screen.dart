import 'dart:async';
import 'package:flutter/material.dart';

import '../../protocol/command_builder.dart';
import '../../protocol/parser.dart';
import '../../transport/reader_client.dart';
import '../../widgets/ui_helpers.dart';
import '../../theme.dart';

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
    setState(() { _loading = true; _status = 'Reading settings…'; });
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
        _antenna = 'TX ${antenna['TX'] ?? '?'} · RX ${antenna['RX'] ?? '?'}';
        _inputs = Map<String, String>.from(gpi.fields);
        final cap = parseKeyValueReply(caps);
        final gpoCount = int.tryParse(cap['GPO'] ?? '') ?? 4;
        for (var i = gpoCount + 1; i <= 4; i++) { _outputs.remove(i); }
        _status = 'Updated ${timeLabel(DateTime.now())}';
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
      if (mounted) showMessage(context, 'Read power set to ${(_power / 100).toStringAsFixed(1)} dBm.');
      await _refresh();
    } catch (error) { if (mounted) showMessage(context, humanError(error)); }
    finally { if (mounted) setState(() => _loading = false); }
  }

  Future<void> _setSession(int value) async {
    setState(() { _session = value; _loading = true; });
    try {
      await _request(CommandBuilder.setSession(value));
      if (mounted) showMessage(context, 'Session set to S$value.');
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

  Widget _inputChip(BuildContext context, int i) {
    final theme = Theme.of(context);
    final known = _inputs.containsKey('GPI$i');
    final high = _inputs['GPI$i'] == '1';
    final color = high ? TaglinkColors.of(context).success : theme.colorScheme.onSurfaceVariant;
    return Expanded(child: Container(
      margin: const EdgeInsets.symmetric(horizontal: 3),
      padding: const EdgeInsets.symmetric(vertical: 10),
      decoration: BoxDecoration(
        color: high ? color.withValues(alpha: 0.14) : theme.colorScheme.surfaceContainerHigh,
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: high ? color : theme.colorScheme.outlineVariant),
      ),
      child: Column(children: [
        Text('GPI $i', style: theme.textTheme.labelMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
        const SizedBox(height: 2),
        Text(!known ? '—' : high ? 'HIGH' : 'LOW', style: TextStyle(fontWeight: FontWeight.w800, color: color)),
      ]),
    ));
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final dbm = (_power / 100).toStringAsFixed(1);
    final failed = _status.startsWith('ERROR') || _status.contains('timed out') || _status.contains('Not connected');
    return RefreshIndicator(onRefresh: _refresh, child: ListView(padding: const EdgeInsets.fromLTRB(16, 0, 16, 24), children: [
      PageToolbar(status: _status, loading: _loading, error: failed, onRefresh: _refresh),
      SectionCard(title: 'RF', icon: Icons.settings_input_antenna, child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Row(children: [
          Text('Read power', style: theme.textTheme.bodyLarge),
          const Spacer(),
          Text('$dbm dBm', style: theme.textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w800, color: theme.colorScheme.primary)),
        ]),
        Slider(padding: const EdgeInsets.symmetric(vertical: 8), value: _power.toDouble(), min: 500, max: 3000, divisions: 50, label: '$dbm dBm', onChanged: _loading ? null : (value) => setState(() => _power = value.round())),
        Row(children: [
          Text('5 dBm', style: theme.textTheme.labelSmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
          const Spacer(),
          Text('30 dBm', style: theme.textTheme.labelSmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
        ]),
        const SizedBox(height: 10),
        FilledButton(onPressed: _loading ? null : _applyPower, child: const Text('Apply power')),
        const SizedBox(height: 18),
        Text('Session', style: theme.textTheme.bodyLarge),
        const SizedBox(height: 8),
        SegmentedButton<int>(
          showSelectedIcon: false,
          segments: const [ButtonSegment(value: 0, label: Text('S0')), ButtonSegment(value: 1, label: Text('S1')), ButtonSegment(value: 2, label: Text('S2')), ButtonSegment(value: 3, label: Text('S3'))],
          selected: {_session},
          onSelectionChanged: _loading ? null : (value) => unawaited(_setSession(value.first)),
        ),
        const SizedBox(height: 10),
        KeyValueList([MapEntry('Region', _region), MapEntry('Antennas', _antenna)]),
      ])),
      SectionCard(title: 'Digital inputs', icon: Icons.input, trailing: IconButton(tooltip: 'Refresh inputs', visualDensity: VisualDensity.compact, onPressed: _loading ? null : _refresh, icon: const Icon(Icons.refresh, size: 20)),
        child: Row(children: [for (var i = 1; i <= 4; i++) _inputChip(context, i)])),
      SectionCard(title: 'Digital outputs', icon: Icons.output, subtitle: 'Outputs hold their state until changed.', child: Column(children: [
        for (final channel in _outputs.keys.toList()..sort()) SwitchListTile(
          contentPadding: EdgeInsets.zero, dense: true,
          title: Text('GPO $channel', style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 15)),
          subtitle: Text(_outputs[channel]! ? 'High' : 'Low'),
          value: _outputs[channel]!, onChanged: _loading ? null : (value) => _setOutput(channel, value)),
      ])),
    ]));
  }
}
