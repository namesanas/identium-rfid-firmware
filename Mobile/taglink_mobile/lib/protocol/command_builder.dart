/// Validated builders for all TAGlink reader commands.
class CommandBuilder {
  static const int minPowerCentiDbm = 500;
  static const int maxPowerCentiDbm = 3000;

  static String simple(String command) {
    const allowed = <String>{
      'GET_STATUS', 'GET_VERSION', 'GET_SERIAL', 'GET_TEMPERATURE', 'START', 'STOP',
      'GET_POWER', 'GET_REGION', 'GET_ANTENNA', 'GET_SESSION', 'GET_GPI',
      'GET_CAPABILITIES', 'GET_WIFI', 'GET_DIAG', 'RELEASE',
    };
    if (!allowed.contains(command)) throw ArgumentError.value(command, 'command', 'Unsupported command');
    return command;
  }

  static String singlePoll(int milliseconds) {
    if (milliseconds < 1 || milliseconds > 120000) throw ArgumentError.value(milliseconds, 'milliseconds', 'Must be 1..120000');
    return 'SINGLE_POLL,$milliseconds';
  }

  static String setPower(int centiDbm) {
    if (centiDbm < minPowerCentiDbm || centiDbm > maxPowerCentiDbm || centiDbm % 50 != 0) {
      throw ArgumentError.value(centiDbm, 'centiDbm', 'Must be 500..3000 in 50-centi-dBm steps');
    }
    return 'SET_POWER,$centiDbm';
  }

  static String setSession(int value) {
    if (value < 0 || value > 3) throw ArgumentError.value(value, 'value', 'Session must be 0..3');
    return 'SET_SESSION,$value';
  }

  static String readTag({required String epc, required int bank, required int wordAddress, required int wordCount}) {
    _validateEpc(epc);
    if (bank < 0 || bank > 3) throw ArgumentError.value(bank, 'bank', 'Bank must be 0..3');
    if (wordAddress < 0 || wordAddress > 65535) throw ArgumentError.value(wordAddress, 'wordAddress', 'Must be 0..65535');
    if (wordCount < 1 || wordCount > 128) throw ArgumentError.value(wordCount, 'wordCount', 'Must be 1..128');
    return 'READ_TAG,${epc.toUpperCase()},$bank,$wordAddress,$wordCount';
  }

  static String writeTag({required String epc, required int bank, required int wordAddress, required String accessPassword, required String dataHex}) {
    _validateEpc(epc);
    if (bank < 0 || bank > 3) throw ArgumentError.value(bank, 'bank', 'Bank must be 0..3');
    if (wordAddress < 0 || wordAddress > 65535) throw ArgumentError.value(wordAddress, 'wordAddress', 'Must be 0..65535');
    if (!RegExp(r'^[0-9a-fA-F]{8}$').hasMatch(accessPassword)) throw ArgumentError.value(accessPassword, 'accessPassword', 'Use exactly 8 hex characters');
    if (dataHex.isEmpty || dataHex.length % 4 != 0 || !RegExp(r'^[0-9a-fA-F]+$').hasMatch(dataHex)) {
      throw ArgumentError.value(dataHex, 'dataHex', 'Data must be hex and a multiple of 4 characters');
    }
    return 'WRITE_TAG,${epc.toUpperCase()},$bank,$wordAddress,${accessPassword.toUpperCase()},${dataHex.toUpperCase()}';
  }

  static String writeEpc({required String oldEpc, required String newEpc}) {
    _validateEpc(oldEpc);
    _validateEpc(newEpc);
    return 'WRITE_EPC,${oldEpc.toUpperCase()},${newEpc.toUpperCase()}';
  }

  static String setGpo({required int channel, required bool value, int durationMs = 0}) {
    if (channel < 1 || channel > 4) throw ArgumentError.value(channel, 'channel', 'GPO must be 1..4');
    if (durationMs < 0 || durationMs > 2147483647) throw ArgumentError.value(durationMs, 'durationMs', 'Invalid duration');
    return 'SET_GPO,$channel,${value ? 1 : 0},$durationMs';
  }

  static String setWifi({required bool enabled, required String mode, required String ssid, required String password}) {
    if (mode != 'STA' && mode != 'AP') throw ArgumentError.value(mode, 'mode', 'Must be STA or AP');
    if (ssid.contains(',') || ssid.contains('\r') || ssid.contains('\n')) throw ArgumentError.value(ssid, 'ssid', 'SSID cannot contain commas or newlines');
    if (ssid.runes.any((rune) => rune > 0x7f)) throw ArgumentError.value(ssid, 'ssid', 'Only ASCII is supported by the reader protocol');
    if (password.runes.any((rune) => rune > 0x7f)) throw ArgumentError.value(password, 'password', 'Only ASCII is supported by the reader protocol');
    if (password != '****' && password.isNotEmpty && (password.length < 8 || password.length > 64)) {
      throw ArgumentError.value(password, 'password', 'Password must be empty or 8..64 characters');
    }
    if (password.contains('\r') || password.contains('\n')) throw ArgumentError.value(password, 'password', 'Newlines are not allowed');
    return 'SET_WIFI,${enabled ? 1 : 0},$mode,$ssid,$password';
  }

  static void _validateEpc(String epc) {
    if (epc.isEmpty || epc.length % 4 != 0 || !RegExp(r'^[0-9a-fA-F]+$').hasMatch(epc)) {
      throw ArgumentError.value(epc, 'epc', 'EPC must be hexadecimal and a multiple of 4 characters');
    }
  }
}
