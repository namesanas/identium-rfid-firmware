import 'dart:async';
import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'features/connect/connect_screen.dart';
import 'features/info/info_screen.dart';
import 'features/inventory/inventory_screen.dart';
import 'features/logs/logs_screen.dart';
import 'features/settings/settings_screen.dart';
import 'features/wifi/wifi_screen.dart';
import 'transport/reader_client.dart';

class TaglinkApp extends StatefulWidget {
  const TaglinkApp({super.key});
  @override
  State<TaglinkApp> createState() => _TaglinkAppState();
}

class _TaglinkAppState extends State<TaglinkApp> with WidgetsBindingObserver {
  final ReaderClient _client = ReaderClient();
  bool _dark = false;
  bool _ready = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    unawaited(_loadTheme());
  }

  Future<void> _loadTheme() async {
    final prefs = await SharedPreferences.getInstance();
    if (!mounted) return;
    setState(() { _dark = prefs.getBool('dark_theme') ?? false; _ready = true; });
  }

  Future<void> _toggleTheme(bool value) async {
    setState(() => _dark = value);
    final prefs = await SharedPreferences.getInstance();
    await prefs.setBool('dark_theme', value);
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.paused || state == AppLifecycleState.detached) {
      unawaited(_client.disconnect(background: true));
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    unawaited(_client.dispose());
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!_ready) return const MaterialApp(home: Scaffold(body: Center(child: CircularProgressIndicator())));
    final scheme = ColorScheme.fromSeed(seedColor: const Color(0xFF2458A6), brightness: _dark ? Brightness.dark : Brightness.light);
    return MaterialApp(
      title: 'TAGlink Mobile',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(colorScheme: scheme, useMaterial3: true, scaffoldBackgroundColor: _dark ? const Color(0xFF11151B) : const Color(0xFFF6F8FB)),
      darkTheme: ThemeData(colorScheme: scheme, useMaterial3: true, brightness: Brightness.dark),
      themeMode: _dark ? ThemeMode.dark : ThemeMode.light,
      home: ValueListenableBuilder<bool>(
        valueListenable: _client.connected,
        builder: (context, isConnected, _) => isConnected
          ? ReaderShell(client: _client, dark: _dark, onThemeChanged: _toggleTheme)
          : ConnectScreen(client: _client, dark: _dark, onThemeChanged: _toggleTheme),
      ),
    );
  }
}

class ReaderShell extends StatefulWidget {
  const ReaderShell({super.key, required this.client, required this.dark, required this.onThemeChanged});
  final ReaderClient client;
  final bool dark;
  final ValueChanged<bool> onThemeChanged;
  @override
  State<ReaderShell> createState() => _ReaderShellState();
}

class _ReaderShellState extends State<ReaderShell> {
  int _index = 0;
  late final List<Widget?> _pages = <Widget?>[
    InventoryScreen(client: widget.client),
    null, null, null, null,
  ];

  Widget _createPage(int index) => switch (index) {
    0 => InventoryScreen(client: widget.client),
    1 => SettingsScreen(client: widget.client),
    2 => InfoScreen(client: widget.client),
    3 => WifiScreen(client: widget.client),
    _ => LogsScreen(client: widget.client),
  };

  void _selectPage(int index) {
    setState(() {
      _index = index;
      _pages[index] ??= _createPage(index);
    });
  }
  static const _titles = <String>['Inventory', 'Settings', 'Reader info', 'Wi-Fi setup', 'Protocol log'];

  @override
  Widget build(BuildContext context) => Scaffold(
    appBar: AppBar(
      title: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(_titles[_index], style: const TextStyle(fontWeight: FontWeight.w700)),
        Text('${widget.client.host ?? ''}:${widget.client.port}', style: Theme.of(context).textTheme.labelSmall),
      ]),
      actions: [
        IconButton(tooltip: widget.dark ? 'Use light theme' : 'Use dark theme', icon: Icon(widget.dark ? Icons.light_mode : Icons.dark_mode), onPressed: () => widget.onThemeChanged(!widget.dark)),
        IconButton(tooltip: 'Disconnect', icon: const Icon(Icons.link_off), onPressed: () async {
          final yes = await showDialog<bool>(context: context, builder: (ctx) => AlertDialog(title: const Text('Disconnect reader?'), content: const Text('The app will stop inventory if it started it, send RELEASE, and close TCP.'), actions: [TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('Cancel')), FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('Disconnect'))]));
          if (yes == true) await widget.client.disconnect();
        }),
      ],
    ),
    body: Column(children: [
      ValueListenableBuilder<String>(valueListenable: widget.client.connectionMessage, builder: (context, message, _) {
        if (message.isEmpty || message.startsWith('Connected to ')) return const SizedBox.shrink();
        return Container(
          width: double.infinity,
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 9),
          color: Theme.of(context).colorScheme.errorContainer,
          child: Row(children: [
            Icon(Icons.warning_amber_rounded, color: Theme.of(context).colorScheme.onErrorContainer),
            const SizedBox(width: 8),
            Expanded(child: Text(message, style: TextStyle(color: Theme.of(context).colorScheme.onErrorContainer))),
            TextButton(onPressed: () async { try { await widget.client.send('GET_STATUS'); } catch (_) {} }, child: const Text('Retry')),
          ]),
        );
      }),
      Expanded(child: IndexedStack(index: _index, children: List<Widget>.generate(_pages.length, (i) => _pages[i] ?? const SizedBox.shrink()))),
    ]),
    bottomNavigationBar: NavigationBar(selectedIndex: _index, onDestinationSelected: _selectPage, destinations: const [
      NavigationDestination(icon: Icon(Icons.radar), label: 'Inventory'),
      NavigationDestination(icon: Icon(Icons.tune), label: 'Settings'),
      NavigationDestination(icon: Icon(Icons.info_outline), label: 'Info'),
      NavigationDestination(icon: Icon(Icons.wifi), label: 'Wi-Fi'),
      NavigationDestination(icon: Icon(Icons.terminal), label: 'Logs'),
    ]),
  );
}
