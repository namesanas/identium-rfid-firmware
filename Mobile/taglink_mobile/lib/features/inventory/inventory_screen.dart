import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:cross_file/cross_file.dart';
import 'package:flutter/material.dart';
import 'package:share_plus/share_plus.dart';

import '../../protocol/command_builder.dart';
import '../../protocol/models.dart';
import '../../transport/reader_client.dart';
import '../../widgets/ui_helpers.dart';
import '../tags/tag_detail_screen.dart';

enum _SortMode { count, rssi, epc }

class InventoryScreen extends StatefulWidget {
  const InventoryScreen({super.key, required this.client});
  final ReaderClient client;
  @override
  State<InventoryScreen> createState() => _InventoryScreenState();
}

class _InventoryScreenState extends State<InventoryScreen> {
  final Map<String, TagRecord> _tags = <String, TagRecord>{};
  final List<DateTime> _readTimes = <DateTime>[];
  final _filter = TextEditingController();
  StreamSubscription<List<ReaderTag>>? _tagSubscription;
  Timer? _rateTimer;
  _SortMode _sort = _SortMode.count;
  int _readsPerSecond = 0;
  int _totalReads = 0;
  bool _working = false;

  @override
  void initState() {
    super.initState();
    _tagSubscription = widget.client.tagBatches.listen(_onBatch);
    _rateTimer = Timer.periodic(const Duration(milliseconds: 500), (_) => _updateRate());
  }

  void _onBatch(List<ReaderTag> batch) {
    final now = DateTime.now();
    for (final read in batch) {
      final existing = _tags[read.epc];
      if (existing == null) {
        _tags[read.epc] = TagRecord(epc: read.epc, count: 1, lastRssi: read.rssi, antenna: read.antenna, firstSeen: now, lastSeen: now);
      } else {
        existing.record(read, now);
      }
      _totalReads++;
      _readTimes.add(now);
    }
    if (mounted) setState(() {});
  }

  void _updateRate() {
    final cutoff = DateTime.now().subtract(const Duration(seconds: 1));
    _readTimes.removeWhere((time) => time.isBefore(cutoff));
    if (mounted) setState(() => _readsPerSecond = _readTimes.length);
  }

  Future<void> _runCommand(String command, {String? success}) async {
    setState(() => _working = true);
    try {
      final result = await widget.client.send(command);
      if (result.startsWith('ERROR,')) throw StateError(result);
      if (success != null && mounted) showMessage(context, success);
    } catch (error) {
      if (mounted) showMessage(context, humanError(error));
    } finally {
      if (mounted) setState(() => _working = false);
    }
  }

  Future<void> _startStop() async {
    if (widget.client.readerStatus.value == 'INVENTORY') {
      await _runCommand(CommandBuilder.simple('STOP'), success: 'Inventory stopped.');
    } else {
      await _runCommand(CommandBuilder.simple('START'), success: 'Inventory started.');
    }
  }

  Future<void> _singleRead() async {
    await _runCommand(CommandBuilder.singlePoll(1000), success: 'Single read finished.');
  }

  void _clear() => setState(() { _tags.clear(); _readTimes.clear(); _totalReads = 0; _readsPerSecond = 0; });

  List<TagRecord> get _visibleTags {
    final search = _filter.text.trim().toUpperCase();
    final values = _tags.values.where((tag) => tag.epc.contains(search)).toList();
    switch (_sort) {
      case _SortMode.count:
        values.sort((a, b) => b.count.compareTo(a.count));
        break;
      case _SortMode.rssi:
        values.sort((a, b) => b.lastRssi.compareTo(a.lastRssi));
        break;
      case _SortMode.epc:
        values.sort((a, b) => a.epc.compareTo(b.epc));
        break;
    }
    return values;
  }

  String _csvCell(String value) {
    final escaped = value.replaceAll('"', '""');
    return '"$escaped"';
  }

  Future<void> _exportCsv() async {
    if (_tags.isEmpty) { showMessage(context, 'There are no tags to export.'); return; }
    final rows = <String>['EPC,Count,RSSI_dBm,Antenna,First_seen,Last_seen'];
    final sorted = _tags.values.toList()..sort((a, b) => a.epc.compareTo(b.epc));
    for (final tag in sorted) {
      rows.add([_csvCell(tag.epc), tag.count, tag.lastRssi, tag.antenna, _csvCell(tag.firstSeen.toIso8601String()), _csvCell(tag.lastSeen.toIso8601String())].join(','));
    }
    final joinedRows = rows.join('\r\n');
    final csv = '$joinedRows\r\n';
    try {
      await Share.shareXFiles(<XFile>[
        XFile.fromData(Uint8List.fromList(utf8.encode(csv)), mimeType: 'text/csv', name: 'taglink_tags.csv'),
      ], subject: 'TAGlink tag inventory CSV', text: 'TAGlink Mobile tag inventory (${_tags.length} unique EPCs)');
    } catch (error) {
      if (mounted) showMessage(context, 'CSV share failed: ${humanError(error)}');
    }
  }

  @override
  void dispose() {
    _tagSubscription?.cancel();
    _rateTimer?.cancel();
    _filter.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Column(children: [
      Padding(padding: const EdgeInsets.fromLTRB(16, 8, 16, 4), child: Column(children: [
        Padding(padding: const EdgeInsets.only(bottom: 8), child: Row(children: [
        const Text('Reader status'),
        const Spacer(),
        ValueListenableBuilder<String>(valueListenable: widget.client.readerStatus, builder: (context, status, _) {
          final color = status == 'INVENTORY' ? Colors.green : status == 'IDLE' ? Theme.of(context).colorScheme.outline : Theme.of(context).colorScheme.error;
          final label = status == 'UNKNOWN' ? 'NOT SYNCED' : status;
          return Chip(avatar: Icon(Icons.circle, size: 10, color: color), label: Text(label), visualDensity: VisualDensity.compact);
        }),
      ])),
      Row(children: [
          Expanded(child: ValueListenableBuilder<String>(valueListenable: widget.client.readerStatus, builder: (context, status, _) {
            final running = status == 'INVENTORY';
            return FilledButton.icon(onPressed: _working || !widget.client.connected.value ? null : _startStop, icon: Icon(running ? Icons.stop_circle : Icons.play_circle), label: Text(running ? 'Stop inventory' : 'Start inventory'), style: FilledButton.styleFrom(minimumSize: const Size(0, 56)));
          })),
          const SizedBox(width: 8),
          IconButton.filledTonal(tooltip: 'Single read (1 second)', onPressed: _working ? null : _singleRead, icon: const Icon(Icons.looks_one), iconSize: 28),
        ]),
        const SizedBox(height: 10),
        Row(children: [
          Expanded(child: _MetricCard(label: 'Unique tags', value: '${_tags.length}', icon: Icons.sell_outlined)),
          const SizedBox(width: 7), Expanded(child: _MetricCard(label: 'Total reads', value: '$_totalReads', icon: Icons.repeat)),
          const SizedBox(width: 7), Expanded(child: _MetricCard(label: 'Reads / sec', value: '$_readsPerSecond', icon: Icons.speed)),
        ]),
      ])),
      Padding(padding: const EdgeInsets.fromLTRB(16, 4, 16, 6), child: Row(children: [
        Expanded(child: TextField(controller: _filter, onChanged: (_) => setState(() {}), decoration: InputDecoration(hintText: 'Filter EPC substring', prefixIcon: const Icon(Icons.search), suffixIcon: _filter.text.isEmpty ? null : IconButton(onPressed: () { _filter.clear(); setState(() {}); }, icon: const Icon(Icons.clear)), isDense: true, border: const OutlineInputBorder()))),
        const SizedBox(width: 4),
        PopupMenuButton<_SortMode>(tooltip: 'Sort tags', initialValue: _sort, onSelected: (value) => setState(() => _sort = value), itemBuilder: (_) => const [PopupMenuItem(value: _SortMode.count, child: Text('Sort by read count')), PopupMenuItem(value: _SortMode.rssi, child: Text('Sort by RSSI')), PopupMenuItem(value: _SortMode.epc, child: Text('Sort by EPC'))], icon: const Icon(Icons.sort)),
        IconButton(tooltip: 'Export CSV', onPressed: _exportCsv, icon: const Icon(Icons.ios_share)),
        IconButton(tooltip: 'Clear tags', onPressed: _clear, icon: const Icon(Icons.delete_outline)),
      ])),
      Expanded(child: _visibleTags.isEmpty
        ? Center(child: Padding(padding: const EdgeInsets.all(28), child: Column(mainAxisSize: MainAxisSize.min, children: [Icon(Icons.radar, size: 56, color: Theme.of(context).colorScheme.outline), const SizedBox(height: 12), Text(_filter.text.isEmpty ? 'No tags captured yet' : 'No EPC matches this filter', style: Theme.of(context).textTheme.titleMedium), const SizedBox(height: 6), const Text('Start inventory or run a single read to discover nearby tags.', textAlign: TextAlign.center)])))
        : ListView.separated(padding: const EdgeInsets.fromLTRB(12, 0, 12, 16), itemCount: _visibleTags.length, separatorBuilder: (_, __) => const SizedBox(height: 2), itemBuilder: (context, index) {
          final tag = _visibleTags[index];
          final countLabel = tag.count > 999 ? '999+' : tag.count.toString();
          return Card(child: ListTile(
            leading: CircleAvatar(child: Text(countLabel, style: const TextStyle(fontSize: 11))),
            title: Text(tag.epc, style: const TextStyle(fontFamily: 'monospace', fontWeight: FontWeight.w600), maxLines: 2, overflow: TextOverflow.ellipsis),
            subtitle: Text('RSSI ${tag.lastRssi} dBm · Ant ${tag.antenna} · Last ${TimeOfDay.fromDateTime(tag.lastSeen).format(context)}'),
            trailing: const Icon(Icons.chevron_right),
            onTap: () => Navigator.of(context).push(MaterialPageRoute<void>(builder: (_) => TagDetailScreen(client: widget.client, epc: tag.epc))),
          ));
        })),
    ]);
  }
}

class _MetricCard extends StatelessWidget {
  const _MetricCard({required this.label, required this.value, required this.icon});
  final String label;
  final String value;
  final IconData icon;
  @override
  Widget build(BuildContext context) => Card(child: Padding(padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 7), child: Column(children: [Icon(icon, size: 20, color: Theme.of(context).colorScheme.primary), const SizedBox(height: 5), Text(value, style: Theme.of(context).textTheme.titleLarge?.copyWith(fontWeight: FontWeight.bold)), const SizedBox(height: 2), Text(label, textAlign: TextAlign.center, style: Theme.of(context).textTheme.labelSmall)])));
}
