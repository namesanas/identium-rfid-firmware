import 'dart:async';
import 'dart:collection';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';

import '../protocol/command_builder.dart';
import '../protocol/line_framer.dart';
import '../protocol/models.dart';
import '../protocol/parser.dart';

class _PendingCommand {
  _PendingCommand(this.command, this.timeout)
      : reply = Completer<String>(), result = Completer<String>();
  final String command;
  final Duration timeout;
  final Completer<String> reply;
  final Completer<String> result;
}

/// Owns the TCP socket, newline framing, command FIFO, timeouts, and diagnostics.
/// All socket I/O is asynchronous; UI code never waits on the socket synchronously.
class ReaderClient {
  ReaderClient() {
    _batchTimer = Timer.periodic(const Duration(milliseconds: 200), (_) => _flushTags());
  }

  final ValueNotifier<bool> connected = ValueNotifier<bool>(false);
  final ValueNotifier<String> connectionMessage = ValueNotifier<String>('Not connected');
  final ValueNotifier<String> readerStatus = ValueNotifier<String>('UNKNOWN');
  final ValueNotifier<bool> appStartedInventory = ValueNotifier<bool>(false);
  final StreamController<List<ReaderTag>> _tagBatches = StreamController<List<ReaderTag>>.broadcast();
  final StreamController<ProtocolLogEntry> _logStream = StreamController<ProtocolLogEntry>.broadcast();
  final Queue<_PendingCommand> _queue = Queue<_PendingCommand>();
  final List<ProtocolLogEntry> _logs = <ProtocolLogEntry>[];
  final List<ReaderTag> _unbatchedTags = <ReaderTag>[];
  final LineFramer _framer = LineFramer();

  Stream<List<ReaderTag>> get tagBatches => _tagBatches.stream;
  Stream<ProtocolLogEntry> get logStream => _logStream.stream;
  List<ProtocolLogEntry> get logs => List<ProtocolLogEntry>.unmodifiable(_logs);
  String? get host => _host;
  int get port => _port;

  Socket? _socket;
  StreamSubscription<List<int>>? _socketSubscription;
  Timer? _reconnectTimer;
  Timer? _lockRetryTimer;
  Timer? _batchTimer;
  String? _host;
  int _port = 5000;
  bool _draining = false;
  bool _intentionalDisconnect = false;
  bool _reconnecting = false;
  bool _hasConnectedBefore = false;
  bool _lockRetryRunning = false;
  int _reconnectAttempt = 0;
  Future<void>? _disconnectFuture;

  static const List<Duration> _retryDelays = <Duration>[
    Duration(seconds: 1), Duration(seconds: 2), Duration(seconds: 5), Duration(seconds: 10),
  ];

  Future<void> connect(String host, {int port = 5000}) async {
    final target = host.trim();
    if (target.isEmpty) throw const FormatException('Enter a reader IP address or hostname.');
    if (port < 1 || port > 65535) throw const FormatException('Port must be between 1 and 65535.');
    final sameReader = target == _host && port == _port;
    _intentionalDisconnect = true;
    _cancelReconnect();
    _lockRetryTimer?.cancel();
    if (connected.value) {
      await disconnect(); // Preserve STOP -> RELEASE -> close when changing connections.
    } else {
      await _closeSocketOnly();
    }
    if (!sameReader) appStartedInventory.value = false;
    _host = target;
    _port = port;
    _framer.reset();
    connectionMessage.value = 'Connecting to $target:$port…';
    try {
      await _openSocket();
      _intentionalDisconnect = false;
      _reconnectAttempt = 0;
      _hasConnectedBefore = true;
      connectionMessage.value = 'Connected to $target:$port';
      unawaited(_probeStatus());
    } on SocketException catch (e) {
      connected.value = false;
      final detail = e.message.toLowerCase();
      final busyLikely = detail.contains('refused') || detail.contains('reset') || detail.contains('already in use');
      connectionMessage.value = busyLikely
          ? 'Reader is busy with another device (connection refused/reset).'
          : 'Could not connect to $target:$port. Check the reader IP, Wi-Fi and port 5000.';
      _intentionalDisconnect = true;
      rethrow;
    } on TimeoutException {
      connected.value = false;
      connectionMessage.value = 'Connection timed out. Check the reader IP and Wi-Fi.';
      _intentionalDisconnect = true;
      rethrow;
    } catch (_) {
      connected.value = false;
      _intentionalDisconnect = true;
      rethrow;
    }
  }

  Future<void> _openSocket() async {
    final targetHost = _host;
    if (targetHost == null) throw StateError('No reader host configured');
    final socket = await Socket.connect(targetHost, _port, timeout: const Duration(seconds: 3));
    socket.setOption(SocketOption.tcpNoDelay, true);
    _socket = socket;
    connected.value = true;
    _framer.reset();
    _socketSubscription = socket.listen(
      _onSocketData,
      onError: (Object error, StackTrace stack) => _onSocketLost(error),
      onDone: () => _onSocketLost(null),
      cancelOnError: true,
    );
  }

  void _onSocketData(List<int> bytes) {
    final chunk = utf8.decode(bytes, allowMalformed: true);
    for (final line in _framer.add(chunk)) {
      if (line.startsWith('DEBUG,')) continue;
      _addLog('RX', line);
      if (line.startsWith('TAG,')) {
        final tag = parseTag(line);
        if (tag != null) _unbatchedTags.add(tag);
        continue; // Never consume a pending command with an unsolicited tag line.
      }
      // Only the command currently being transmitted can receive a reply.
      final active = _activeCommand;
      if (active != null && _matches(active.command, line) && !active.reply.isCompleted) {
        active.reply.complete(line);
      }
      // Reader status can change independently of command return values.
      final status = parseStatus(line);
      if (status != null) {
        readerStatus.value = status.state;
        if (status.state == 'IDLE') appStartedInventory.value = false;
        _lockRetryTimer?.cancel();
        _lockRetryRunning = false;
        connectionMessage.value = 'Connected to $_host:$_port';
      }
    }
  }

  _PendingCommand? _activeCommand;

  bool _matches(String command, String line) {
    final commandName = command.split(',').first;
    // Only consume a command-specific error (or INVALID_COMMAND) for the
    // active FIFO item. A stale/delayed error must not satisfy another command.
    if (line == 'ERROR,INVALID_COMMAND' || line == 'ERROR,$commandName') return true;
    final replyKey = switch (commandName) {
      'GET_STATUS' => 'STATUS,',
      'GET_VERSION' => 'VERSION,',
      'GET_SERIAL' => 'SERIAL,',
      'GET_TEMPERATURE' => 'TEMP,',
      'START' => 'OK,START',
      'STOP' => 'OK,STOP',
      'SINGLE_POLL' => 'OK,SINGLE_POLL',
      'GET_POWER' => 'POWER,',
      'SET_POWER' => 'OK,SET_POWER',
      'GET_REGION' => 'REGION,',
      'GET_ANTENNA' => 'ANTENNA,',
      'GET_SESSION' => 'SESSION,',
      'SET_SESSION' => 'OK,SET_SESSION',
      'READ_TAG' => 'READ_DATA,',
      'WRITE_TAG' => 'OK,WRITE_TAG',
      'WRITE_EPC' => 'OK,WRITE_EPC',
      'GET_GPI' => 'GPI,',
      'SET_GPO' => 'OK,SET_GPO',
      'GET_CAPABILITIES' => 'CAPABILITIES,',
      'GET_WIFI' => 'WIFI,',
      'SET_WIFI' => 'OK,SET_WIFI',
      'GET_DIAG' => 'DIAG,',
      'RELEASE' => 'OK,RELEASE',
      _ => '',
    };
    if (replyKey.isEmpty || !line.startsWith(replyKey)) return false;
    // Prefix matching still distinguishes OK replies from different commands.
    return true;
  }

  Future<String> send(String command) {
    if (!connected.value || _socket == null) return Future<String>.error(StateError('Not connected to a reader.'));
    if (command.contains('\r') || command.contains('\n')) return Future<String>.error(ArgumentError('Commands cannot contain newlines.'));
    var timeout = const Duration(seconds: 3);
    if (command.startsWith('SINGLE_POLL,')) {
      final parts = command.split(',');
      final ms = int.tryParse(parts.length > 1 ? parts[1] : '') ?? 0;
      timeout = Duration(milliseconds: ms + 3000);
    }
    final item = _PendingCommand(command, timeout);
    _queue.add(item);
    unawaited(_drainQueue());
    return item.result.future.then((response) {
      if (command == 'START' && response == 'OK,START') {
        appStartedInventory.value = true;
        readerStatus.value = 'INVENTORY';
      } else if (command == 'STOP' && response == 'OK,STOP') {
        appStartedInventory.value = false;
        readerStatus.value = 'IDLE';
      }
      return response;
    });
  }

  Future<void> _drainQueue() async {
    if (_draining) return;
    _draining = true;
    try {
      while (_queue.isNotEmpty) {
        final item = _queue.first;
        _activeCommand = item;
        final socket = _socket;
        if (!connected.value || socket == null) {
          _finishError(item, StateError('Reader disconnected before command could be sent.'));
          _queue.removeFirst();
          _activeCommand = null;
          continue;
        }
        try {
          _addLog('TX', item.command);
          socket.add(utf8.encode('${item.command}\r\n'));
          await socket.flush();
          final response = await item.reply.future.timeout(item.timeout);
          if (!item.result.isCompleted) item.result.complete(response);
        } on TimeoutException {
          final error = TimeoutException('No reply to ${item.command} within ${item.timeout.inSeconds == 0 ? item.timeout.inMilliseconds : item.timeout.inSeconds} seconds.');
          _finishError(item, error);
          if (item.command == 'GET_STATUS' && connected.value) _showLockMessageAndRetry();
        } catch (error) {
          _finishError(item, error);
        } finally {
          if (_queue.isNotEmpty && identical(_queue.first, item)) _queue.removeFirst();
          _activeCommand = null;
        }
      }
    } finally {
      _draining = false;
      if (_queue.isNotEmpty && connected.value) unawaited(_drainQueue());
    }
  }

  void _finishError(_PendingCommand item, Object error) {
    if (!item.result.isCompleted) item.result.completeError(error);
    // Wake the queue worker without creating an unobserved Completer error.
    if (!item.reply.isCompleted) item.reply.complete('');
  }

  void _showLockMessageAndRetry() {
    connectionMessage.value = 'Connected, but another interface holds the command lock. Send RELEASE from USB/serial, or reset the reader. Retrying status every 3 seconds…';
    if (_lockRetryRunning) return;
    _lockRetryRunning = true;
    _lockRetryTimer?.cancel();
    _lockRetryTimer = Timer.periodic(const Duration(seconds: 3), (_) {
      if (!connected.value || _activeCommand != null || _queue.isNotEmpty) return;
      unawaited(_probeStatus());
    });
  }

  Future<void> _probeStatus() async {
    if (!connected.value || _activeCommand != null || _queue.isNotEmpty) return;
    try {
      final reply = await send(CommandBuilder.simple('GET_STATUS'));
      if (reply.startsWith('ERROR,')) {
        connectionMessage.value = 'Reader rejected GET_STATUS: $reply';
      }
    } on TimeoutException {
      _showLockMessageAndRetry();
    } catch (_) {
      // Disconnect and queue errors are reported by the connection state notifier.
    }
  }

  void _flushTags() {
    if (_unbatchedTags.isEmpty || _tagBatches.isClosed) return;
    final batch = List<ReaderTag>.of(_unbatchedTags);
    _unbatchedTags.clear();
    _tagBatches.add(batch);
  }

  void _addLog(String direction, String line) {
    final entry = ProtocolLogEntry(time: DateTime.now(), direction: direction, line: line);
    _logs.add(entry);
    if (_logs.length > 500) _logs.removeRange(0, _logs.length - 500);
    if (!_logStream.isClosed) _logStream.add(entry);
  }

  void clearLogs() => _logs.clear();

  void _onSocketLost(Object? error) {
    if (_socket == null && !connected.value) return;
    _socket = null;
    connected.value = false;
    readerStatus.value = 'UNKNOWN';
    // Keep inventory ownership through reconnect; a successful IDLE status or explicit
    // disconnect clears it. This allows background cleanup to STOP inventory after a drop.
    final active = _activeCommand;
    if (active != null) _finishError(active, SocketException('Reader connection lost'));
    for (final item in _queue.toList()) {
      if (!identical(item, active)) _finishError(item, SocketException('Reader connection lost'));
    }
    connectionMessage.value = 'Disconnected';
    if (!_intentionalDisconnect && _hasConnectedBefore && !_reconnecting) _scheduleReconnect();
  }

  void _scheduleReconnect() {
    _cancelReconnect();
    if (_reconnectAttempt >= _retryDelays.length) {
      connectionMessage.value = 'Disconnected. Automatic reconnect limit reached; tap Connect to try again.';
      return;
    }
    final delay = _retryDelays[_reconnectAttempt++];
    connectionMessage.value = 'Disconnected. Retrying in ${delay.inSeconds}s (attempt $_reconnectAttempt/${_retryDelays.length})…';
    _reconnectTimer = Timer(delay, () => unawaited(_attemptReconnect()));
  }

  Future<void> _attemptReconnect() async {
    if (_intentionalDisconnect || _host == null || _reconnecting) return;
    _reconnecting = true;
    try {
      await _openSocket();
      // Socket.connect cannot be cancelled. If the app disconnected while the
      // reconnect attempt was in flight (e.g. entering the background), close
      // the late socket rather than reviving an intentionally closed session.
      if (_intentionalDisconnect) {
        await _closeSocketOnly();
        connected.value = false;
        return;
      }
      connectionMessage.value = 'Reconnected. Synchronizing reader status…';
      try {
        final reply = await send(CommandBuilder.simple('GET_STATUS'));
        if (reply.startsWith('ERROR,')) {
          connectionMessage.value = 'Reader rejected GET_STATUS: $reply';
        } else {
          _reconnectAttempt = 0;
          connectionMessage.value = 'Connected to $_host:$_port';
        }
      } on TimeoutException {
        // A live TCP connection with no status reply is the documented
        // USB/serial command-lock case, not a network reconnect failure.
        _showLockMessageAndRetry();
      } catch (error) {
        connectionMessage.value = 'Reconnected, but status synchronization failed: $error';
        if (!connected.value) _scheduleReconnect();
      }
    } catch (_) {
      connected.value = false;
      _scheduleReconnect();
    } finally {
      _reconnecting = false;
    }
  }

  void _cancelReconnect() {
    _reconnectTimer?.cancel();
    _reconnectTimer = null;
  }

  /// Sends STOP (only if this app started inventory), RELEASE, then closes the socket.
  /// Coalesces lifecycle/dispose/user requests so they cannot close the socket
  /// underneath another cleanup sequence.
  Future<void> disconnect({bool background = false}) {
    final existing = _disconnectFuture;
    if (existing != null) return existing;
    final operation = _disconnectImpl(background: background);
    _disconnectFuture = operation;
    return operation.whenComplete(() {
      if (identical(_disconnectFuture, operation)) _disconnectFuture = null;
    });
  }

  Future<void> _disconnectImpl({required bool background}) async {
    _intentionalDisconnect = true;
    _cancelReconnect();
    _lockRetryTimer?.cancel();
    if (connected.value) {
      if (appStartedInventory.value) {
        // Let the FIFO drain in order. Each command has its own 3 s protocol
        // timeout; an outer 3 s timeout could close the socket before STOP
        // or RELEASE reached the front of the queue.
        try { await send(CommandBuilder.simple('STOP')); } catch (_) {}
      }
      try { await send(CommandBuilder.simple('RELEASE')); } catch (_) {}
    }
    await _closeSocketOnly();
    connected.value = false;
    appStartedInventory.value = false;
    readerStatus.value = 'UNKNOWN';
    connectionMessage.value = background ? 'Disconnected while app was in the background' : 'Disconnected';
  }

  Future<void> _closeSocketOnly() async {
    final sub = _socketSubscription;
    _socketSubscription = null;
    if (sub != null) {
      try { await sub.cancel(); } catch (_) {}
    }
    final socket = _socket;
    _socket = null;
    connected.value = false;
    if (socket != null) {
      try { socket.destroy(); } catch (_) {}
    }
    for (final item in _queue.toList()) {
      _finishError(item, StateError('Connection closed'));
    }
    _queue.clear();
    _activeCommand = null;
    _framer.reset();
  }

  Future<void> dispose() async {
    await disconnect();
    _batchTimer?.cancel();
    await _tagBatches.close();
    await _logStream.close();
    connected.dispose();
    connectionMessage.dispose();
    readerStatus.dispose();
    appStartedInventory.dispose();
  }
}
