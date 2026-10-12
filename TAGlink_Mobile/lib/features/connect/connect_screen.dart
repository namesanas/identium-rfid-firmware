import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../protocol/line_framer.dart';
import '../../transport/reader_client.dart';
import '../../widgets/ui_helpers.dart';

class SavedReader {
  const SavedReader({required this.host, required this.port});
  final String host;
  final int port;
  String get label => '$host:$port';
  String encode() => '$host|$port';
  static SavedReader? decode(String value) {
    final parts = value.split('|');
    if (parts.length != 2) return null;
    final port = int.tryParse(parts[1]);
    if (port == null || parts[0].isEmpty) return null;
    return SavedReader(host: parts[0], port: port);
  }
}

class ConnectScreen extends StatefulWidget {
  const ConnectScreen({super.key, required this.client, required this.dark, required this.onThemeChanged});
  final ReaderClient client;
  final bool dark;
  final ValueChanged<bool> onThemeChanged;
  @override
  State<ConnectScreen> createState() => _ConnectScreenState();
}

class _ConnectScreenState extends State<ConnectScreen> {
  final _ip = TextEditingController(text: '192.168.4.1');
  final _port = TextEditingController(text: '5000');
  List<SavedReader> _saved = <SavedReader>[];
  List<SavedReader> _found = <SavedReader>[];
  bool _connecting = false;
  bool _scanning = false;
  String _scanMessage = '';
  String _connectionMessage = '';

  @override
  void initState() {
    super.initState();
    unawaited(_loadSaved());
    widget.client.connectionMessage.addListener(_onMessageChanged);
  }

  void _onMessageChanged() {
    if (mounted) setState(() => _connectionMessage = widget.client.connectionMessage.value);
  }

  Future<void> _loadSaved() async {
    final prefs = await SharedPreferences.getInstance();
    final entries = prefs.getStringList('saved_readers') ?? <String>[];
    if (!mounted) return;
    setState(() => _saved = entries.map(SavedReader.decode).whereType<SavedReader>().toList());
  }

  Future<void> _remember(SavedReader reader) async {
    final prefs = await SharedPreferences.getInstance();
    final next = <SavedReader>[reader, ..._saved.where((r) => r.host != reader.host || r.port != reader.port)];
    final clipped = next.take(5).toList();
    await prefs.setStringList('saved_readers', clipped.map((r) => r.encode()).toList());
    if (mounted) setState(() => _saved = clipped);
  }

  Future<void> _connect({String? host, int? port}) async {
    final target = host ?? _ip.text.trim();
    final targetPort = port ?? int.tryParse(_port.text.trim()) ?? 5000;
    if (target.isEmpty) { showMessage(context, 'Enter a reader IP address.'); return; }
    setState(() { _connecting = true; _connectionMessage = 'Connecting to $target:$targetPort…'; });
    try {
      await widget.client.connect(target, port: targetPort);
      await _remember(SavedReader(host: target, port: targetPort));
    } catch (error) {
      if (mounted) showMessage(context, humanError(error));
    } finally {
      if (mounted) setState(() { _connecting = false; _connectionMessage = widget.client.connectionMessage.value; });
    }
  }

  Future<void> _scanNetwork() async {
    setState(() { _scanning = true; _found = <SavedReader>[]; _scanMessage = 'Looking for the phone’s Wi-Fi network…'; });
    try {
      final interfaces = await NetworkInterface.list(type: InternetAddressType.IPv4, includeLoopback: false);
      final localIps = <String>{};
      final candidates = <MapEntry<int, String>>[];
      for (final interface in interfaces) {
        final interfaceName = interface.name.toLowerCase();
        for (final address in interface.addresses) {
          final ip = address.address;
          localIps.add(ip);
          if (ip.startsWith('127.') || ip.startsWith('169.254.')) continue;
          final octets = ip.split('.');
          if (octets.length != 4 || !octets.every((o) => int.tryParse(o) != null)) continue;
          final preferredInterface = interfaceName.contains('wlan') || interfaceName.contains('wifi') || interfaceName == 'en0';
          final privateIpv4 = ip.startsWith('10.') || ip.startsWith('192.168.') || (octets[0] == '172' && (int.tryParse(octets[1]) ?? 0) >= 16 && (int.tryParse(octets[1]) ?? 0) <= 31);
          final rank = preferredInterface ? 0 : (privateIpv4 ? 1 : 2);
          candidates.add(MapEntry<int, String>(rank, ip));
        }
      }
      candidates.sort((a, b) => a.key.compareTo(b.key));
      if (candidates.isEmpty) throw StateError('Could not determine the phone IPv4 subnet. Connect to the reader Wi-Fi and try again.');
      final chosenIp = candidates.first.value;
      final chosenOctets = chosenIp.split('.');
      final prefix = '${chosenOctets[0]}.${chosenOctets[1]}.${chosenOctets[2]}';
      final targets = <String>[
        for (var last = 1; last <= 254; last++) if (!localIps.contains('$prefix.$last')) '$prefix.$last',
      ];
      int cursor = 0;
      int checked = 0;
      final results = <SavedReader>[];
      if (mounted) setState(() => _scanMessage = 'Scanning $prefix.x for readers…');
      Future<void> worker() async {
        while (true) {
          final i = cursor++;
          if (i >= targets.length) return;
          final host = targets[i];
          if (await _probe(host)) results.add(SavedReader(host: host, port: 5000));
          checked++;
          if (mounted && checked % 16 == 0) setState(() => _scanMessage = 'Scanning $prefix.x… ${(checked * 100 / targets.length).round()}% · ${results.length} found');
        }
      }
      await Future.wait(List<Future<void>>.generate(64, (_) => worker()));
      results.sort((a, b) => _ipNumber(a.host).compareTo(_ipNumber(b.host)));
      if (!mounted) return;
      setState(() { _found = results; _scanMessage = results.isEmpty ? 'No reader found on $prefix.x.' : ''; });
    } catch (error) {
      if (mounted) setState(() => _scanMessage = humanError(error));
    } finally {
      if (mounted) setState(() => _scanning = false);
    }
  }

  int _ipNumber(String ip) => ip.split('.').fold<int>(0, (sum, octet) => sum * 256 + (int.tryParse(octet) ?? 0));

  Future<bool> _probe(String host) async {
    Socket? socket;
    final firstLine = Completer<String?>();
    final framer = LineFramer();
    StreamSubscription<List<int>>? sub;
    var sentVersionCommand = false;
    try {
      socket = await Socket.connect(host, 5000, timeout: const Duration(milliseconds: 800));
      socket.setOption(SocketOption.tcpNoDelay, true);
      sub = socket.listen((bytes) {
        for (final line in framer.add(utf8.decode(bytes, allowMalformed: true))) {
          if (line.startsWith('DEBUG,')) continue;
          if (!firstLine.isCompleted) firstLine.complete(line);
        }
      }, onError: (Object _) { if (!firstLine.isCompleted) firstLine.complete(null); }, onDone: () { if (!firstLine.isCompleted) firstLine.complete(null); });
      socket.add(utf8.encode('GET_VERSION\r\n'));
      sentVersionCommand = true;
      await socket.flush();
      // GET_VERSION queries the RFID module and crosses the WiFi coprocessor
      // link: usually ~100 ms, occasionally over a second.
      final line = await firstLine.future.timeout(const Duration(seconds: 2), onTimeout: () => null);
      return line != null && line.startsWith('VERSION,');
    } catch (_) {
      return false;
    } finally {
      // A scan probe is a real TCP client too: release its command lock before
      // closing, even when the peer replies with an unexpected line.
      if (socket != null && sentVersionCommand) {
        try { socket.add(utf8.encode('RELEASE\r\n')); await socket.flush(); } catch (_) {}
      }
      await sub?.cancel();
      socket?.destroy();
    }
  }

  @override
  void dispose() {
    widget.client.connectionMessage.removeListener(_onMessageChanged);
    _ip.dispose();
    _port.dispose();
    super.dispose();
  }

  bool get _messageIsProblem {
    final m = _connectionMessage.toLowerCase();
    return m.contains('lock') || m.contains('busy') || m.contains('fail') || m.contains('error') || m.contains('refused') || m.contains('timed out') || m.contains('retrying');
  }

  Widget _readerTile(SavedReader reader, IconData icon, {String? subtitle}) => ListTile(
    contentPadding: const EdgeInsets.symmetric(horizontal: 4),
    dense: true,
    leading: Icon(icon),
    title: Text(reader.host, style: const TextStyle(fontWeight: FontWeight.w600, fontSize: 15)),
    subtitle: subtitle == null ? null : Text(subtitle),
    trailing: const Icon(Icons.chevron_right),
    onTap: _connecting ? null : () => _connect(host: reader.host, port: reader.port),
  );

  Widget _label(BuildContext context, String text) => Padding(
    padding: const EdgeInsets.only(top: 16, bottom: 2),
    child: Text(text.toUpperCase(), style: Theme.of(context).textTheme.labelSmall?.copyWith(color: Theme.of(context).colorScheme.onSurfaceVariant, letterSpacing: 1.1, fontWeight: FontWeight.w700)),
  );

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    final busy = _connecting || _scanning;
    final status = _connecting ? 'Connecting…' : _connectionMessage;
    return AnnotatedRegion<SystemUiOverlayStyle>(value: (widget.dark ? SystemUiOverlayStyle.light : SystemUiOverlayStyle.dark).copyWith(statusBarColor: Colors.transparent), child: Scaffold(body: SafeArea(child: Center(child: ConstrainedBox(constraints: const BoxConstraints(maxWidth: 520), child: ListView(padding: const EdgeInsets.fromLTRB(20, 4, 20, 24), children: [
      Align(alignment: Alignment.centerRight, child: IconButton(tooltip: widget.dark ? 'Use light theme' : 'Use dark theme', icon: Icon(widget.dark ? Icons.light_mode_outlined : Icons.dark_mode_outlined), onPressed: () => widget.onThemeChanged(!widget.dark))),
      const SizedBox(height: 8),
      Center(child: Container(
        width: 64, height: 64,
        decoration: BoxDecoration(color: theme.colorScheme.primary, borderRadius: BorderRadius.circular(18)),
        child: Icon(Icons.attach_file, size: 34, color: theme.colorScheme.onPrimary),
      )),
      const SizedBox(height: 14),
      Text('TAGlink', textAlign: TextAlign.center, style: theme.textTheme.headlineMedium?.copyWith(fontWeight: FontWeight.w800, letterSpacing: 0.3)),
      Text('RFID reader control', textAlign: TextAlign.center, style: theme.textTheme.bodyMedium?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
      const SizedBox(height: 24),
      Card(child: Padding(padding: const EdgeInsets.fromLTRB(18, 18, 18, 12), child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Text('Connect to a reader', style: theme.textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w700)),
        const SizedBox(height: 14),
        Row(children: [
          Expanded(child: TextField(controller: _ip, keyboardType: TextInputType.url, autocorrect: false, textInputAction: TextInputAction.go, onSubmitted: (_) { if (!busy) _connect(); }, decoration: const InputDecoration(labelText: 'Reader IP address', hintText: '192.168.1.80', prefixIcon: Icon(Icons.router_outlined)))),
          const SizedBox(width: 10),
          SizedBox(width: 88, child: TextField(controller: _port, keyboardType: TextInputType.number, decoration: const InputDecoration(labelText: 'Port'))),
        ]),
        const SizedBox(height: 12),
        FilledButton.icon(
          style: FilledButton.styleFrom(minimumSize: const Size.fromHeight(50)),
          onPressed: busy ? null : () => _connect(),
          icon: _connecting ? SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2, color: theme.colorScheme.onPrimary)) : const Icon(Icons.link),
          label: Text(_connecting ? 'Connecting…' : 'Connect'),
        ),
        if (_connecting || _messageIsProblem) Padding(padding: const EdgeInsets.only(top: 10), child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Icon(_messageIsProblem ? Icons.error_outline : Icons.hourglass_top, size: 18, color: _messageIsProblem ? theme.colorScheme.error : theme.colorScheme.onSurfaceVariant),
          const SizedBox(width: 8),
          Expanded(child: Text(status, style: theme.textTheme.bodySmall?.copyWith(color: _messageIsProblem ? theme.colorScheme.error : theme.colorScheme.onSurfaceVariant))),
        ])),
        const SizedBox(height: 12),
        Row(children: [
          Expanded(child: OutlinedButton.icon(onPressed: busy ? null : () => _connect(host: '192.168.4.1', port: 5000), icon: const Icon(Icons.wifi_tethering, size: 18), label: const Text('Hotspot'))),
          const SizedBox(width: 10),
          Expanded(child: OutlinedButton.icon(onPressed: busy ? null : _scanNetwork, icon: _scanning ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2)) : const Icon(Icons.travel_explore, size: 18), label: Text(_scanning ? 'Scanning…' : 'Scan'))),
        ]),
        if (_scanMessage.isNotEmpty) Padding(padding: const EdgeInsets.only(top: 8), child: Text(_scanMessage, style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onSurfaceVariant))),
        if (_found.isNotEmpty) ...[
          _label(context, 'Found on this network'),
          for (final reader in _found) _readerTile(reader, Icons.sensors, subtitle: 'Port ${reader.port}'),
        ],
        if (_saved.isNotEmpty) ...[
          _label(context, 'Recent'),
          for (final reader in _saved) _readerTile(reader, Icons.history, subtitle: 'Port ${reader.port}'),
        ],
      ]))),
      const SizedBox(height: 16),
      Text('Join the reader’s hotspot, or put the phone on the same Wi-Fi as the reader.', textAlign: TextAlign.center, style: theme.textTheme.bodySmall?.copyWith(color: theme.colorScheme.onSurfaceVariant)),
    ]))))));
  }
}
