import 'dart:async';
import 'package:flutter/material.dart';

import '../../protocol/command_builder.dart';
import '../../protocol/parser.dart';
import '../../transport/reader_client.dart';
import '../../theme.dart';
import '../../widgets/ui_helpers.dart';
import 'find_tag_screen.dart';

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
  String _message = '';

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
      if (mounted) setState(() { _bankData[bank] = data; _message = 'Updated ${timeLabel(DateTime.now())}'; });
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

  IconData _bankIcon(int bank) => switch (bank) { 1 => Icons.sell_outlined, 2 => Icons.fingerprint, _ => Icons.notes };

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final failed = _message.contains('ERROR') || _message.contains('failed') || _message.contains('timed out');
    return Scaffold(
      appBar: AppBar(titleSpacing: 4, title: const Text('Tag', style: TextStyle(fontWeight: FontWeight.w800))),
      body: ListView(padding: const EdgeInsets.fromLTRB(16, 12, 16, 24), children: [
        Card(child: Padding(padding: const EdgeInsets.all(16), child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          Text('EPC', style: theme.textTheme.labelMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant, letterSpacing: 1.1, fontWeight: FontWeight.w700)),
          const SizedBox(height: 6),
          SelectableText(_epc, style: theme.textTheme.titleLarge?.copyWith(fontFamily: 'monospace', fontWeight: FontWeight.w800)),
          const SizedBox(height: 14),
          Row(children: [
            Expanded(child: OutlinedButton.icon(onPressed: _busy ? null : _changeEpc, icon: const Icon(Icons.edit_outlined, size: 18), label: const Text('Change EPC'))),
            const SizedBox(width: 10),
            Expanded(child: FilledButton.icon(onPressed: _busy ? null : _readAllBanks, icon: const Icon(Icons.refresh, size: 18), label: const Text('Read memory'))),
          ]),
          const SizedBox(height: 10),
          FilledButton.tonalIcon(
            onPressed: _busy ? null : () => Navigator.of(context).push(MaterialPageRoute<void>(builder: (_) => FindTagScreen(client: widget.client, epc: _epc))),
            icon: const Icon(Icons.radar, size: 18),
            label: const Text('Find this tag'),
          ),
        ]))),
        const SizedBox(height: 6),
        SizedBox(height: 2, child: _busy ? const LinearProgressIndicator() : null),
        Padding(padding: const EdgeInsets.symmetric(vertical: 8, horizontal: 4), child: Text(_message, style: theme.textTheme.bodySmall?.copyWith(color: failed ? theme.colorScheme.error : theme.colorScheme.onSurfaceVariant))),
        for (final bank in <int>[1, 2, 3]) SectionCard(
          title: '${_bankName(bank)} memory',
          subtitle: 'Bank $bank',
          icon: _bankIcon(bank),
          trailing: IconButton(onPressed: _busy ? null : () => _readBank(bank), icon: const Icon(Icons.refresh, size: 20), tooltip: 'Read ${_bankName(bank)} bank', visualDensity: VisualDensity.compact),
          child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(color: TaglinkColors.of(context).log, borderRadius: BorderRadius.circular(8), border: Border.all(color: theme.colorScheme.outlineVariant)),
              child: SelectableText(_bankData[bank] ?? 'Not read yet', style: TextStyle(fontFamily: 'monospace', height: 1.5, color: _bankData[bank] == null ? theme.colorScheme.onSurfaceVariant : null)),
            ),
            if (bank == 3) Padding(padding: const EdgeInsets.only(top: 12), child: Align(alignment: Alignment.centerRight, child: FilledButton.tonalIcon(onPressed: _busy ? null : _writeUserBank, icon: const Icon(Icons.edit_note), label: const Text('Write User memory')))),
          ]),
        ),
      ]),
    );
  }
}
