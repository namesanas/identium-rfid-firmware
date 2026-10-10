import 'dart:async';
import 'package:flutter/material.dart';

import '../../protocol/command_builder.dart';
import '../../protocol/parser.dart';
import '../../transport/reader_client.dart';
import '../../widgets/ui_helpers.dart';

class TagDetailScreen extends StatefulWidget {
  const TagDetailScreen({super.key, required this.client, required this.epc});
  final ReaderClient client;
  final String epc;
  @override
  State<TagDetailScreen> createState() => _TagDetailScreenState();
}

class _TagDetailScreenState extends State<TagDetailScreen> {
  late String _epc = widget.epc;
  final Map<int, String> _bankData = <int, String>{};
  bool _busy = false;
  String _message = 'Read tag memory to inspect the EPC, TID and User banks.';

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => unawaited(_readAllBanks()));
  }

  Future<void> _readAllBanks() async {
    for (final bank in <int>[1, 2, 3]) {
      if (!mounted) return;
      await _readBank(bank);
    }
  }

  Future<void> _readBank(int bank) async {
    setState(() { _busy = true; _message = 'Reading ${_bankName(bank)} bank…'; });
    try {
      final words = bank == 1 ? 8 : 8;
      final reply = await widget.client.send(CommandBuilder.readTag(epc: _epc, bank: bank, wordAddress: 0, wordCount: words));
      if (reply.startsWith('ERROR,')) throw StateError(reply);
      final parsed = parseKeyValueReply(reply);
      final data = parsed['DATA'] ?? '(no DATA field)';
      if (mounted) setState(() { _bankData[bank] = data; _message = '${_bankName(bank)} bank read successfully.'; });
    } catch (error) {
      if (mounted) setState(() => _message = '${_bankName(bank)} bank: ${humanError(error)}');
    } finally {
      if (mounted) setState(() => _busy = false);
    }
  }

  String _bankName(int bank) => switch (bank) { 0 => 'Reserved', 1 => 'EPC', 2 => 'TID', _ => 'User' };

  Future<void> _writeUserBank() async {
    final password = TextEditingController(text: '00000000');
    final address = TextEditingController(text: '0');
    final data = TextEditingController();
    final accepted = await showDialog<bool>(context: context, builder: (dialogContext) => AlertDialog(
      title: const Text('Write User memory'),
      content: SingleChildScrollView(child: Column(mainAxisSize: MainAxisSize.min, children: [
        const Text('Writes hexadecimal words to bank 3. This changes physical tag memory and may be irreversible.'),
        const SizedBox(height: 12),
        TextField(controller: password, decoration: const InputDecoration(labelText: 'Access password (8 hex chars)'), autocorrect: false),
        TextField(controller: address, decoration: const InputDecoration(labelText: 'Word address'), keyboardType: TextInputType.number),
        TextField(controller: data, decoration: const InputDecoration(labelText: 'Data (hex, 4 chars per word)'), autocorrect: false, textCapitalization: TextCapitalization.characters),
      ])),
      actions: [TextButton(onPressed: () => Navigator.pop(dialogContext, false), child: const Text('Cancel')), FilledButton(onPressed: () => Navigator.pop(dialogContext, true), child: const Text('Write memory'))],
    ));
    if (accepted != true || !mounted) { password.dispose(); address.dispose(); data.dispose(); return; }
    try {
      final command = CommandBuilder.writeTag(epc: _epc, bank: 3, wordAddress: int.parse(address.text), accessPassword: password.text.trim(), dataHex: data.text.trim());
      final confirm = await showDialog<bool>(context: context, builder: (ctx) => AlertDialog(title: const Text('Confirm tag write'), content: Text('Write ${data.text.trim().length ~/ 4} word(s) to User bank at address ${address.text}?'), actions: [TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')), FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Confirm write'))]));
      if (confirm == true) {
        setState(() { _busy = true; _message = 'Writing User bank…'; });
        final reply = await widget.client.send(command);
        if (reply.startsWith('ERROR,')) throw StateError(reply);
        if (mounted) { showMessage(context, 'User bank write succeeded.'); await _readBank(3); }
      }
    } catch (error) {
      if (mounted) showMessage(context, 'Write failed: ${humanError(error)}');
    } finally {
      password.dispose(); address.dispose(); data.dispose();
      if (mounted) setState(() => _busy = false);
    }
  }

  Future<void> _changeEpc() async {
    final controller = TextEditingController(text: _epc);
    final next = await showDialog<String>(context: context, builder: (ctx) => AlertDialog(
      title: const Text('Change EPC'),
      content: TextField(controller: controller, autofocus: true, autocorrect: false, textCapitalization: TextCapitalization.characters, decoration: const InputDecoration(labelText: 'New EPC (hex; multiple of 4)')),
      actions: [TextButton(onPressed: () => Navigator.pop(ctx), child: const Text('Cancel')), FilledButton(onPressed: () => Navigator.pop(ctx, controller.text.trim()), child: const Text('Continue'))],
    ));
    if (next == null || !mounted) { controller.dispose(); return; }
    try {
      final cmd = CommandBuilder.writeEpc(oldEpc: _epc, newEpc: next);
      final confirmed = await showDialog<bool>(context: context, builder: (ctx) => AlertDialog(title: const Text('Confirm EPC change'), content: Text('Change tag EPC from\n$_epc\nto\n${next.toUpperCase()}?'), actions: [TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')), FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Write EPC'))]));
      if (confirmed == true) {
        setState(() => _busy = true);
        final reply = await widget.client.send(cmd);
        if (reply.startsWith('ERROR,')) throw StateError(reply);
        if (mounted) setState(() { _epc = next.toUpperCase(); _bankData.clear(); _message = 'EPC updated. Read banks again to verify.'; });
      }
    } catch (error) {
      if (mounted) showMessage(context, 'EPC change failed: ${humanError(error)}');
    } finally {
      controller.dispose();
      if (mounted) setState(() => _busy = false);
    }
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(title: const Text('Tag detail')),
    body: ListView(padding: const EdgeInsets.all(16), children: [
      Card(child: Padding(padding: const EdgeInsets.all(16), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text('EPC', style: Theme.of(context).textTheme.labelLarge),
        const SizedBox(height: 6),
        SelectableText(_epc, style: Theme.of(context).textTheme.titleMedium?.copyWith(fontFamily: 'monospace', fontWeight: FontWeight.bold)),
        const SizedBox(height: 12),
        Wrap(spacing: 8, runSpacing: 8, children: [OutlinedButton.icon(onPressed: _busy ? null : _changeEpc, icon: const Icon(Icons.edit), label: const Text('Change EPC')), OutlinedButton.icon(onPressed: _busy ? null : _readAllBanks, icon: const Icon(Icons.refresh), label: const Text('Read all banks'))]),
      ]))),
      if (_busy) const LinearProgressIndicator(),
      Padding(padding: const EdgeInsets.symmetric(vertical: 8), child: Text(_message, style: const TextStyle(color: Colors.grey))),
      for (final bank in <int>[1, 2, 3]) Card(child: Padding(padding: const EdgeInsets.all(16), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [Expanded(child: Text('${_bankName(bank)} bank · $bank', style: Theme.of(context).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700))), IconButton(onPressed: _busy ? null : () => _readBank(bank), icon: const Icon(Icons.refresh), tooltip: 'Read ${_bankName(bank)} bank')]),
        SelectableText(_bankData[bank] ?? 'Not read yet', style: const TextStyle(fontFamily: 'monospace', height: 1.5)),
        if (bank == 3) Align(alignment: Alignment.centerRight, child: FilledButton.tonalIcon(onPressed: _busy ? null : _writeUserBank, icon: const Icon(Icons.edit_note), label: const Text('Write User bank'))),
      ]))),
    ]),
  );
}
