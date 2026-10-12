import 'dart:async';
import 'package:flutter/material.dart';

import '../../protocol/command_builder.dart';
import '../../protocol/parser.dart';
import '../../transport/reader_client.dart';
import '../../theme.dart';
import '../../widgets/ui_helpers.dart';

class WifiScreen extends StatefulWidget {
  const WifiScreen({super.key, required this.client});
  final ReaderClient client;
  @override
  State<WifiScreen> createState() => _WifiScreenState();
}

class _WifiScreenState extends State<WifiScreen> {
  final _ssid = TextEditingController();
  final _password = TextEditingController();
  final _passwordFocus = FocusNode();
  String _mode = 'STA';
  bool _enabled = true;
  bool _loading = false;
  bool _hidePassword = true;
  Map<String, String> _status = <String, String>{};
  String _message = '';

  @override
  void initState() { super.initState(); unawaited(_refresh()); }

  Future<void> _refresh() async {
    setState(() { _loading = true; _message = ''; });
    try {
      final reply = await widget.client.send(CommandBuilder.simple('GET_WIFI'));
      if (reply.startsWith('ERROR,')) throw StateError(reply);
      final wifi = parseWifi(reply);
      if (wifi == null) throw const FormatException('Reader returned an invalid WIFI reply.');
      if (!mounted) return;
      setState(() {
        _status = parseKeyValueReply(reply)..remove('_type');
        _enabled = wifi.enabled;
        _mode = wifi.mode == 'AP' ? 'AP' : 'STA';
        _ssid.text = wifi.ssid;
        // Password is intentionally never read back from the reader.
      });
    } catch (error) { if (mounted) setState(() => _message = humanError(error)); }
    finally { if (mounted) setState(() => _loading = false); }
  }

  Future<void> _apply() async {
    String command;
    try {
      command = CommandBuilder.setWifi(enabled: _enabled, mode: _mode, ssid: _ssid.text, password: _password.text);
    } catch (error) { showMessage(context, humanError(error)); return; }
    final confirm = await showDialog<bool>(context: context, builder: (ctx) => AlertDialog(
      title: const Text('Change reader Wi-Fi?'),
      content: const Text('The reader may leave this network or get a new IP address. The phone will disconnect and return to the connect screen.'),
      actions: [TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')), FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Apply Wi-Fi'))],
    ));
    if (confirm != true || !mounted) return;
    setState(() { _loading = true; _message = 'Sending Wi-Fi configuration…'; });
    try {
      final reply = await widget.client.send(command);
      if (reply.startsWith('ERROR,')) throw StateError(reply);
      if (reply != 'OK,SET_WIFI') throw FormatException('Unexpected reply: $reply');
      if (mounted) showMessage(context, 'Wi-Fi settings accepted. Returning to Connect.');
      // disconnect() makes a best-effort RELEASE before closing as required by the reader protocol.
      await widget.client.disconnect();
    } catch (error) {
      if (mounted) setState(() => _message = 'Wi-Fi update failed: ${humanError(error)}');
    } finally { if (mounted) setState(() => _loading = false); }
  }

  @override
  void dispose() { _ssid.dispose(); _password.dispose(); _passwordFocus.dispose(); super.dispose(); }

  String _formatStatus(String key, String value) => switch (key) {
    'RSSI' => value.isEmpty || value == '0' ? '—' : '$value dBm',
    'ENABLE' || 'HOST' || 'UPLOAD' => onOff(value),
    'STATE' || 'LINK' => value.isEmpty ? value : value[0] + value.substring(1).toLowerCase(),
    'MODE' => value == 'AP' ? 'Hotspot (AP)' : value == 'STA' ? 'Client (STA)' : value,
    _ => value,
  };

  Widget _summary(BuildContext context) {
    final theme = Theme.of(context);
    final connected = (_status['STATE'] ?? '').toUpperCase() == 'CONNECTED';
    final color = connected ? TaglinkColors.of(context).success : theme.colorScheme.onSurfaceVariant;
    final rssi = int.tryParse(_status['RSSI'] ?? '');
    final bars = rssi == null || rssi == 0 ? Icons.wifi_off : rssi > -60 ? Icons.wifi : rssi > -75 ? Icons.wifi_2_bar : Icons.wifi_1_bar;
    return Card(child: Padding(padding: const EdgeInsets.all(16), child: Row(children: [
      Container(
        width: 48, height: 48,
        decoration: BoxDecoration(color: color.withValues(alpha: 0.14), shape: BoxShape.circle),
        child: Icon(connected ? bars : Icons.wifi_off, color: color),
      ),
      const SizedBox(width: 14),
      Expanded(child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(_status.isEmpty ? 'Status unknown' : connected ? 'Connected to ${_status['SSID'] ?? ''}' : _formatStatus('STATE', _status['STATE'] ?? 'Not connected'),
            style: theme.textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700)),
        if (_status['IP'] != null && _status['IP']!.isNotEmpty) Text('${_status['IP']}${rssi != null && rssi != 0 ? '  ·  $rssi dBm' : ''}', style: TextStyle(color: theme.colorScheme.onSurfaceVariant)),
      ])),
    ])));
  }

  @override
  Widget build(BuildContext context) {
    final status = _message.isNotEmpty ? _message : _loading ? 'Reading Wi-Fi status…' : '';
    final shown = Map<String, String>.of(_status)..remove('SSID')..remove('IP')..remove('STATE')..remove('RSSI');
    return RefreshIndicator(onRefresh: _refresh, child: ListView(padding: const EdgeInsets.fromLTRB(16, 0, 16, 24), children: [
      PageToolbar(status: status, loading: _loading, error: _message.contains('failed') || _message.startsWith('ERROR'), onRefresh: _refresh),
      _summary(context),
      SectionCard(title: 'Details', icon: Icons.list_alt, child: KeyValueList(labelled(shown, wifiLabels, format: _formatStatus), empty: 'No Wi-Fi status available.')),
      SectionCard(title: 'Configure', icon: Icons.settings_outlined, subtitle: 'Applying may move the reader to another network.', child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        SwitchListTile(contentPadding: EdgeInsets.zero, title: const Text('Wi-Fi enabled'), value: _enabled, onChanged: _loading ? null : (value) => setState(() => _enabled = value)),
        const SizedBox(height: 8),
        SegmentedButton<String>(
          showSelectedIcon: false,
          segments: const [ButtonSegment(value: 'STA', icon: Icon(Icons.wifi, size: 18), label: Text('Join network')), ButtonSegment(value: 'AP', icon: Icon(Icons.wifi_tethering, size: 18), label: Text('Hotspot'))],
          selected: {_mode},
          onSelectionChanged: _loading ? null : (value) => setState(() => _mode = value.first),
        ),
        const SizedBox(height: 14),
        TextField(controller: _ssid, decoration: InputDecoration(labelText: _mode == 'AP' ? 'Hotspot name' : 'Network name (SSID)', prefixIcon: const Icon(Icons.wifi_password_outlined)), autocorrect: false, textCapitalization: TextCapitalization.none),
        const SizedBox(height: 12),
        TextField(controller: _password, focusNode: _passwordFocus, obscureText: _hidePassword, decoration: InputDecoration(labelText: 'Password', helperText: 'Leave empty for an open network · **** keeps the saved one', prefixIcon: const Icon(Icons.lock_outline), suffixIcon: IconButton(onPressed: () => setState(() => _hidePassword = !_hidePassword), icon: Icon(_hidePassword ? Icons.visibility_outlined : Icons.visibility_off_outlined))), autocorrect: false),
        const SizedBox(height: 16),
        FilledButton.icon(style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(48)), onPressed: _loading ? null : _apply, icon: const Icon(Icons.check), label: const Text('Apply')),
      ])),
    ]));
  }
}
