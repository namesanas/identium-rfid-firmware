import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../../protocol/command_builder.dart';
import '../../protocol/models.dart';
import '../../transport/reader_client.dart';
import '../../theme.dart';
import '../../widgets/ui_helpers.dart';

/// "Metal detector" for one EPC: runs inventory, shows how strong the tag's
/// signal is and clicks/vibrates faster as it gets stronger.
class FindTagScreen extends StatefulWidget {
  const FindTagScreen({super.key, required this.client, required this.epc});
  final ReaderClient client;
  final String epc;
  @override
  State<FindTagScreen> createState() => _FindTagScreenState();
}

class _FindTagScreenState extends State<FindTagScreen> {
  static const _weakRssi = -80.0;     // shown as an empty gauge
  static const _strongRssi = -30.0;   // shown as a full gauge
  static const _lostAfter = Duration(milliseconds: 1500);

  StreamSubscription<List<ReaderTag>>? _subscription;
  Timer? _ticker;
  bool _startedHere = false;
  bool _sound = true;
  bool _vibrate = true;
  String _message = '';
  double? _smoothedRssi;
  int? _peakRssi;
  int _reads = 0;
  DateTime? _lastSeen;
  DateTime _nextBeep = DateTime.now();

  @override
  void initState() {
    super.initState();
    _subscription = widget.client.tagBatches.listen(_onBatch);
    _ticker = Timer.periodic(const Duration(milliseconds: 50), (_) => _tick());
    unawaited(_startInventory());
  }

  Future<void> _startInventory() async {
    if (widget.client.readerStatus.value == 'INVENTORY') return;
    try {
      final reply = await widget.client.send(CommandBuilder.simple('START'));
      if (reply.startsWith('ERROR,')) throw StateError(reply);
      _startedHere = true;
    } catch (error) {
      if (mounted) setState(() => _message = 'Could not start inventory: ${humanError(error)}');
    }
  }

  void _onBatch(List<ReaderTag> batch) {
    var matched = false;
    for (final tag in batch) {
      if (tag.epc != widget.epc) continue;
      matched = true;
      _reads++;
      // Smooth the jumpy per-read RSSI so the gauge follows movement, not noise.
      _smoothedRssi = _smoothedRssi == null ? tag.rssi.toDouble() : _smoothedRssi! * 0.65 + tag.rssi * 0.35;
      _peakRssi = _peakRssi == null ? tag.rssi : math.max(_peakRssi!, tag.rssi);
    }
    if (matched) {
      _lastSeen = DateTime.now();
      if (mounted) setState(() {});
    }
  }

  bool get _inRange => _lastSeen != null && DateTime.now().difference(_lastSeen!) < _lostAfter;

  double get _level {
    if (!_inRange || _smoothedRssi == null) return 0;
    return ((_smoothedRssi! - _weakRssi) / (_strongRssi - _weakRssi)).clamp(0.02, 1.0);
  }

  void _tick() {
    if (!mounted) return;
    final level = _level;
    final now = DateTime.now();
    if (level > 0 && !now.isBefore(_nextBeep)) {
      if (_sound) unawaited(SystemSound.play(SystemSoundType.click));
      if (_vibrate) unawaited(level > 0.75 ? HapticFeedback.heavyImpact() : HapticFeedback.lightImpact());
      // About 1 click per second when weak, up to ~11 per second when very close.
      _nextBeep = now.add(Duration(milliseconds: (1000 - 910 * level).round()));
    }
    // Redraw so "lost" and the last-seen time update without new reads.
    setState(() {});
  }

  String _distanceLabel(double level) {
    if (level <= 0) return _lastSeen == null ? 'Searching…' : 'Not in range';
    if (level > 0.8) return 'Very close';
    if (level > 0.55) return 'Close';
    if (level > 0.3) return 'Nearby';
    return 'Far';
  }

  @override
  void dispose() {
    _ticker?.cancel();
    unawaited(_subscription?.cancel());
    if (_startedHere && widget.client.connected.value) {
      unawaited(widget.client.send(CommandBuilder.simple('STOP')).catchError((Object _) => ''));
    }
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final colors = TaglinkColors.of(context);
    final level = _level;
    final gaugeColor = Color.lerp(theme.colorScheme.primary, colors.success, level) ?? colors.success;
    final seenText = _lastSeen == null ? 'Not seen yet' : 'Last seen ${timeLabel(_lastSeen!)}';
    return Scaffold(
      appBar: AppBar(titleSpacing: 4, title: const Text('Find tag', style: TextStyle(fontWeight: FontWeight.w800))),
      body: ListView(padding: const EdgeInsets.fromLTRB(16, 12, 16, 24), children: [
        Card(child: Padding(padding: const EdgeInsets.all(16), child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          Text('LOOKING FOR', style: theme.textTheme.labelMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant, letterSpacing: 1.1, fontWeight: FontWeight.w700)),
          const SizedBox(height: 6),
          FittedBox(fit: BoxFit.scaleDown, alignment: Alignment.centerLeft, child: SelectableText(widget.epc, style: theme.textTheme.titleLarge?.copyWith(fontFamily: 'monospace', fontWeight: FontWeight.w800))),
        ]))),
        const SizedBox(height: 24),
        Center(child: SizedBox(width: 250, height: 250, child: Stack(fit: StackFit.expand, children: [
          TweenAnimationBuilder<double>(
            tween: Tween<double>(end: level),
            duration: const Duration(milliseconds: 180),
            builder: (context, value, _) => CircularProgressIndicator(value: value, strokeWidth: 20, strokeCap: StrokeCap.round, color: gaugeColor, backgroundColor: theme.colorScheme.surfaceContainerHighest),
          ),
          Center(child: Column(mainAxisSize: MainAxisSize.min, children: [
            Text(level > 0 ? '${_smoothedRssi!.round()}' : '--', style: theme.textTheme.displayMedium?.copyWith(fontWeight: FontWeight.w800, color: level > 0 ? gaugeColor : theme.colorScheme.onSurfaceVariant)),
            Text('dBm', style: theme.textTheme.labelLarge?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
            const SizedBox(height: 8),
            Text(_distanceLabel(level), style: theme.textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700)),
          ])),
        ]))),
        const SizedBox(height: 20),
        Row(mainAxisAlignment: MainAxisAlignment.center, children: [
          Text(seenText, style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
          Text('  ·  $_reads reads  ·  peak ${_peakRssi ?? '--'} dBm', style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
        ]),
        if (_message.isNotEmpty) Padding(padding: const EdgeInsets.only(top: 12), child: Text(_message, textAlign: TextAlign.center, style: TextStyle(color: theme.colorScheme.error))),
        const SizedBox(height: 16),
        SectionCard(title: 'Feedback', icon: Icons.volume_up_outlined, child: Column(children: [
          SwitchListTile(contentPadding: EdgeInsets.zero, title: const Text('Click sound'), subtitle: const Text('Uses the phone\'s touch sound setting'), value: _sound, onChanged: (value) => setState(() => _sound = value)),
          SwitchListTile(contentPadding: EdgeInsets.zero, title: const Text('Vibrate'), value: _vibrate, onChanged: (value) => setState(() => _vibrate = value)),
        ])),
        Padding(padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 8), child: Text(
          'Move the antenna, or move items past it: the gauge fills and the clicks speed up as the tag gets closer. '
          'Lower the read power in Settings to narrow the search area.',
          style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onSurfaceVariant),
        )),
      ]),
    );
  }
}
