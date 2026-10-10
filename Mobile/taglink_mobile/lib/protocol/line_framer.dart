/// Reassembles newline-delimited protocol replies from arbitrary TCP chunks.
class LineFramer {
  String _buffer = '';

  List<String> add(String chunk) {
    _buffer += chunk;
    final lines = <String>[];
    int newline;
    while ((newline = _buffer.indexOf('\n')) >= 0) {
      var line = _buffer.substring(0, newline);
      _buffer = _buffer.substring(newline + 1);
      if (line.endsWith('\r')) line = line.substring(0, line.length - 1);
      if (line.isNotEmpty) lines.add(line);
    }
    // Prevent an untrusted peer from growing an unterminated line forever.
    if (_buffer.length > 65536) _buffer = _buffer.substring(_buffer.length - 8192);
    return lines;
  }

  void reset() => _buffer = '';
  String get pendingText => _buffer;
}
