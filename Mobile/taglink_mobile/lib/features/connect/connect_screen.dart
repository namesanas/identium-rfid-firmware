import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
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
  String _connectionMessage = 'Enter the reader IP or use its access-point shortcut.';

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
    setState(() { _scanning = true; _found = <SavedReader>[]; _scanMessage = 'Finding the phone Wi-Fi subnet…'; });
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
      if (mounted) setState(() => _scanMessage = 'Scanning $prefix.0/24 on TCP port 5000 (64 parallel probes)…');
      Future<void> worker() async {
        while (true) {
          final i = cursor++;
          if (i >= targets.length) return;
          final host = targets[i];
          if (await _probe(host)) results.add(SavedReader(host: host, port: 5000));
          checked++;
          if (mounted && checked % 16 == 0) setState(() => _scanMessage = 'Scanned $checked/${targets.length} addresses… found ${results.length}.');
        }
      }
      await Future.wait(List<Future<void>>.generate(64, (_) => worker()));
      results.sort((a, b) => _ipNumber(a.host).compareTo(_ipNumber(b.host)));
      if (!mounted) return;
      setState(() { _found = results; _scanMessage = results.isEmpty ? 'No compatible reader replied to GET_VERSION on $prefix.0/24.' : 'Found ${results.length} reader(s).'; });
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
      final line = await firstLine.future.timeout(const Duration(milliseconds: 800), onTimeout: () => null);
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

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(title: const Text('TAGlink Mobile'), actions: [IconButton(tooltip: widget.dark ? 'Use light theme' : 'Use dark theme', icon: Icon(widget.dark ? Icons.light_mode : Icons.dark_mode), onPressed: () => widget.onThemeChanged(!widget.dark))]),
    body: SafeArea(child: ListView(padding: const EdgeInsets.fromLTRB(16, 8, 16, 24), children: [
      Card(child: Padding(padding: const EdgeInsets.all(18), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        const Icon(Icons.radar, size: 42),
        const SizedBox(height: 8),
        Text('Connect to your reader', style: Theme.of(context).textTheme.headlineSmall?.copyWith(fontWeight: FontWeight.bold)),
        const SizedBox(height: 8),
        const Text('Connect your phone to the reader Wi-Fi network RFID-Reader (AP mode) or put both devices on the same LAN. TAGlink uses TCP port 5000.'),
        const SizedBox(height: 14),
        Row(children: [Expanded(child: TextField(controller: _ip, keyboardType: TextInputType.url, autocorrect: false, decoration: const InputDecoration(labelText: 'Reader IP / hostname', hintText: '192.168.1.80', prefixIcon: Icon(Icons.router), border: OutlineInputBorder()))), const SizedBox(width: 10), SizedBox(width: 92, child: TextField(controller: _port, keyboardType: TextInputType.number, decoration: const InputDecoration(labelText: 'Port', border: OutlineInputBorder()))) ]),
        const SizedBox(height: 12),
        SizedBox(width: double.infinity, child: FilledButton.icon(onPressed: _connecting ? null : () => _connect(), icon: _connecting ? const SizedBox(width: 18, height: 18, child: CircularProgressIndicator(strokeWidth: 2)) : const Icon(Icons.link), label: Text(_connecting ? 'Connecting…' : 'Connect'))),
        const SizedBox(height: 8),
        SizedBox(width: double.infinity, child: OutlinedButton.icon(onPressed: _connecting ? null : () => _connect(host: '192.168.4.1', port: 5000), icon: const Icon(Icons.wifi), label: const Text('Reader access point · 192.168.4.1'))),
        const SizedBox(height: 6),
        Text(_connectionMessage, style: Theme.of(context).textTheme.bodySmall?.copyWith(color: _connectionMessage.contains('busy') || _connectionMessage.contains('lock') ? Theme.of(context).colorScheme.error : null)),
      ]))),
      Card(child: Padding(padding: const EdgeInsets.all(16), child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [Expanded(child: Text('Scan network', style: Theme.of(context).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.bold))), if (_scanning) const SizedBox(height: 20, width: 20, child: CircularProgressIndicator(strokeWidth: 2))]),
        const SizedBox(height: 6),
        const Text('Checks the phone’s IPv4 /24 subnet for port 5000 and validates each result with GET_VERSION.'),
        const SizedBox(height: 10),
        SizedBox(width: double.infinity, child: OutlinedButton.icon(onPressed: _scanning || _connecting ? null : _scanNetwork, icon: const Icon(Icons.radar), label: Text(_scanning ? 'Scanning…' : 'Scan network'))),
        if (_scanMessage.isNotEmpty) Padding(padding: const EdgeInsets.only(top: 6), child: Text(_scanMessage)),
        for (final reader in _found) ListTile(leading: const Icon(Icons.router), title: Text(reader.label), trailing: const Icon(Icons.chevron_right), onTap: _connecting ? null : () => _connect(host: reader.host, port: reader.port)),
      ]))),
      if (_saved.isNotEmpty) ...[
        sectionTitle('Recent readers', subtitle: 'Up to five addresses are saved on this device.'),
        for (final reader in _saved) Card(child: ListTile(leading: const Icon(Icons.history), title: Text(reader.host), subtitle: Text('TCP port ${reader.port}'), trailing: const Icon(Icons.chevron_right), onTap: _connecting ? null : () => _connect(host: reader.host, port: reader.port))),
      ],
      const SizedBox(height: 8),
      const Text('If the reader reports no status response, another interface may hold the command lock. Send RELEASE from USB/serial or reset the reader.', style: TextStyle(color: Colors.grey)),
    ])),
  );
}
