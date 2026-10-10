import 'dart:async';
import 'package:flutter/material.dart';

import '../../protocol/command_builder.dart';
import '../../protocol/parser.dart';
import '../../transport/reader_client.dart';
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
      content: const Text('After applying, the reader may leave the current Wi-Fi network or use a different IP address. This TCP connection can drop. You will return to the connection screen and may need to join the reader’s new network manually.'),
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

  @override
  Widget build(BuildContext context) => RefreshIndicator(onRefresh: _refresh, child: ListView(padding: const EdgeInsets.fromLTRB(16, 8, 16, 24), children: [
    Row(children: [Expanded(child: Text('Reader Wi-Fi', style: Theme.of(context).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.bold))), IconButton(onPressed: _loading ? null : _refresh, icon: const Icon(Icons.refresh))]),
    if (_loading) const LinearProgressIndicator(),
    if (_message.isNotEmpty) Padding(padding: const EdgeInsets.symmetric(vertical: 6), child: Text(_message, style: const TextStyle(color: Colors.grey))),
    Card(child: Padding(padding: const EdgeInsets.all(16), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      sectionTitle('Current status'),
      if (_status.isEmpty) const Text('No Wi-Fi status available.'),
      for (final entry in _status.entries) infoTile(entry.key, entry.value),
    ]))),
    Card(child: Padding(padding: const EdgeInsets.all(16), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      sectionTitle('Configure network', subtitle: 'This may disconnect your phone from the reader. The password is not stored by this app.'),
      SwitchListTile(contentPadding: EdgeInsets.zero, title: const Text('Wi-Fi enabled'), value: _enabled, onChanged: _loading ? null : (value) => setState(() => _enabled = value)),
      const SizedBox(height: 8),
      DropdownButtonFormField<String>(value: _mode, decoration: const InputDecoration(labelText: 'Mode', border: OutlineInputBorder()), items: const [DropdownMenuItem(value: 'STA', child: Text('Station (join a Wi-Fi network)')), DropdownMenuItem(value: 'AP', child: Text('Access point (reader creates Wi-Fi)'))], onChanged: _loading ? null : (value) { if (value != null) setState(() => _mode = value); }),
      const SizedBox(height: 12),
      TextField(controller: _ssid, decoration: const InputDecoration(labelText: 'SSID (no commas)', border: OutlineInputBorder()), autocorrect: false, textCapitalization: TextCapitalization.none),
      const SizedBox(height: 12),
      TextField(controller: _password, focusNode: _passwordFocus, obscureText: _hidePassword, decoration: InputDecoration(labelText: 'Password', helperText: 'Empty = open network · **** = keep stored password', border: const OutlineInputBorder(), suffixIcon: IconButton(onPressed: () => setState(() => _hidePassword = !_hidePassword), icon: Icon(_hidePassword ? Icons.visibility : Icons.visibility_off))), autocorrect: false),
      const SizedBox(height: 14),
      SizedBox(width: double.infinity, child: FilledButton.icon(onPressed: _loading ? null : _apply, icon: const Icon(Icons.save), label: const Text('Apply Wi-Fi configuration'))),
    ]))),
  ]));
}
