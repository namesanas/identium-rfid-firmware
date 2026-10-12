import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:share_plus/share_plus.dart';

import '../../protocol/command_builder.dart';
import '../../protocol/models.dart';
import '../../transport/reader_client.dart';
import '../../widgets/ui_helpers.dart';
import '../tags/tag_detail_screen.dart';
import '../../theme.dart';

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

  // The reader reports each tag once per inventory round (about 1 s), so a
  // 1 s window flickers between 0 and the tag count; average over 3 s.
  static const _rateWindow = Duration(seconds: 3);

  void _updateRate() {
    final cutoff = DateTime.now().subtract(_rateWindow);
    _readTimes.removeWhere((time) => time.isBefore(cutoff));
    final rate = (_readTimes.length / _rateWindow.inSeconds).round();
    if (mounted && rate != _readsPerSecond) setState(() => _readsPerSecond = rate);
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

  Widget _rssiBadge(BuildContext context, int rssi) {
    final theme = Theme.of(context);
    final strong = rssi >= -50, ok = rssi >= -65;
    final color = rssi == 0 ? theme.colorScheme.onSurfaceVariant : strong ? TaglinkColors.of(context).success : ok ? theme.colorScheme.primary : theme.colorScheme.error;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(color: color.withValues(alpha: 0.13), borderRadius: BorderRadius.circular(6)),
      child: Text(rssi == 0 ? '— dBm' : '$rssi dBm', style: TextStyle(fontSize: 12, fontWeight: FontWeight.w700, color: color)),
    );
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final visibleTags = _visibleTags; // filter and sort once per build, not once per row
    return Column(children: [
      Padding(padding: const EdgeInsets.fromLTRB(16, 12, 16, 4), child: Column(children: [
        ValueListenableBuilder<String>(valueListenable: widget.client.readerStatus, builder: (context, status, _) {
          final running = status == 'INVENTORY';
          final color = running ? TaglinkColors.of(context).success : status == 'IDLE' ? theme.colorScheme.onSurfaceVariant : theme.colorScheme.error;
          final label = running ? 'Reading tags' : status == 'IDLE' ? 'Ready' : status == 'UNKNOWN' ? 'Not synced' : status;
          return Row(children: [
            Expanded(child: FilledButton.icon(
              onPressed: _working || !widget.client.connected.value ? null : _startStop,
              icon: Icon(running ? Icons.stop_rounded : Icons.play_arrow_rounded, size: 26),
              label: Text(running ? 'Stop' : 'Start inventory', style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w700)),
              style: FilledButton.styleFrom(minimumSize: const Size(0, 54), backgroundColor: running ? theme.colorScheme.error : null),
            )),
            const SizedBox(width: 10),
            SizedBox(height: 54, child: OutlinedButton(
              onPressed: _working || running ? null : _singleRead,
              style: OutlinedButton.styleFrom(padding: const EdgeInsets.symmetric(horizontal: 14)),
              child: const Column(mainAxisAlignment: MainAxisAlignment.center, children: [Icon(Icons.filter_1, size: 20), Text('Single', style: TextStyle(fontSize: 11))]),
            )),
          ]).withStatus(context, label, color);
        }),
        const SizedBox(height: 12),
        Row(children: [
          Expanded(child: _MetricCard(label: 'Unique tags', value: '${_tags.length}', icon: Icons.sell_outlined)),
          const SizedBox(width: 8), Expanded(child: _MetricCard(label: 'Total reads', value: '$_totalReads', icon: Icons.repeat)),
          const SizedBox(width: 8), Expanded(child: _MetricCard(label: 'Reads / s', value: '$_readsPerSecond', icon: Icons.speed)),
        ]),
      ])),
      Padding(padding: const EdgeInsets.fromLTRB(16, 6, 8, 6), child: Row(children: [
        Expanded(child: TextField(controller: _filter, onChanged: (_) => setState(() {}), textCapitalization: TextCapitalization.characters, decoration: InputDecoration(hintText: 'Filter by EPC', prefixIcon: const Icon(Icons.search), suffixIcon: _filter.text.isEmpty ? null : IconButton(onPressed: () { _filter.clear(); setState(() {}); }, icon: const Icon(Icons.clear)), isDense: true))),
        PopupMenuButton<_SortMode>(tooltip: 'Sort', initialValue: _sort, onSelected: (value) => setState(() => _sort = value), itemBuilder: (_) => const [PopupMenuItem(value: _SortMode.count, child: Text('Most reads first')), PopupMenuItem(value: _SortMode.rssi, child: Text('Strongest signal first')), PopupMenuItem(value: _SortMode.epc, child: Text('By EPC'))], icon: const Icon(Icons.sort)),
        IconButton(tooltip: 'Export CSV', onPressed: _tags.isEmpty ? null : _exportCsv, icon: const Icon(Icons.ios_share)),
        IconButton(tooltip: 'Clear tags', onPressed: _tags.isEmpty ? null : _clear, icon: const Icon(Icons.delete_outline)),
      ])),
      Expanded(child: visibleTags.isEmpty
        ? Center(child: Padding(padding: const EdgeInsets.all(28), child: Column(mainAxisSize: MainAxisSize.min, children: [
            Icon(Icons.sensors, size: 52, color: theme.colorScheme.outline),
            const SizedBox(height: 12),
            Text(_filter.text.isEmpty ? 'No tags yet' : 'No tag matches “${_filter.text}”', style: theme.textTheme.titleMedium),
            const SizedBox(height: 4),
            if (_filter.text.isEmpty) Text('Start an inventory to read nearby tags.', textAlign: TextAlign.center, style: TextStyle(color: theme.colorScheme.onSurfaceVariant)),
          ])))
        : ListView.builder(padding: const EdgeInsets.fromLTRB(12, 0, 12, 16), itemCount: visibleTags.length, itemBuilder: (context, index) {
          final tag = visibleTags[index];
          final countLabel = tag.count > 9999 ? '9999+' : tag.count.toString();
          return Card(child: InkWell(
            borderRadius: BorderRadius.circular(12),
            onTap: () => Navigator.of(context).push(MaterialPageRoute<void>(builder: (_) => TagDetailScreen(client: widget.client, epc: tag.epc))),
            child: Padding(padding: const EdgeInsets.fromLTRB(14, 12, 8, 12), child: Row(children: [
              Expanded(child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                FittedBox(fit: BoxFit.scaleDown, alignment: Alignment.centerLeft, child: Text(tag.epc, maxLines: 1, style: const TextStyle(fontFamily: 'monospace', fontWeight: FontWeight.w700, fontSize: 15, letterSpacing: 0.2))),
                const SizedBox(height: 6),
                Row(children: [
                  _rssiBadge(context, tag.lastRssi),
                  const SizedBox(width: 10),
                  Text('Ant ${tag.antenna}  ·  ${timeLabel(tag.lastSeen)}', style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
                ]),
              ])),
              const SizedBox(width: 8),
              Column(children: [
                Text(countLabel, style: theme.textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w800, color: theme.colorScheme.primary)),
                Text('reads', style: theme.textTheme.labelSmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
              ]),
              Icon(Icons.chevron_right, color: theme.colorScheme.onSurfaceVariant),
            ])),
          ));
        })),
    ]);
  }
}

extension on Widget {
  /// The start/stop row with the reader's state shown above it.
  Widget withStatus(BuildContext context, String label, Color color) => Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
    Row(children: [
      Icon(Icons.circle, size: 9, color: color),
      const SizedBox(width: 7),
      Text(label, style: Theme.of(context).textTheme.labelLarge?.copyWith(color: color, fontWeight: FontWeight.w700)),
    ]),
    const SizedBox(height: 10),
    this,
  ]);
}

class _MetricCard extends StatelessWidget {
  const _MetricCard({required this.label, required this.value, required this.icon});
  final String label;
  final String value;
  final IconData icon;
  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Card(margin: EdgeInsets.zero, child: Padding(padding: const EdgeInsets.symmetric(vertical: 12, horizontal: 10), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Icon(icon, size: 18, color: theme.colorScheme.primary),
      const SizedBox(height: 6),
      FittedBox(fit: BoxFit.scaleDown, child: Text(value, style: theme.textTheme.headlineSmall?.copyWith(fontWeight: FontWeight.w800))),
      Text(label, maxLines: 1, overflow: TextOverflow.ellipsis, style: theme.textTheme.labelSmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
    ])));
  }
}
