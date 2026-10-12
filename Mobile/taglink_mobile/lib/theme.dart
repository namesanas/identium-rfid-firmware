import 'package:flutter/material.dart';

/// The brown palettes of the TAGlink desktop app (TAGlink/theme.py, 'brown').
class _Palette {
  const _Palette({
    required this.window, required this.surface, required this.surfaceAlt, required this.field,
    required this.border, required this.borderStrong, required this.text, required this.textStrong,
    required this.muted, required this.accent, required this.accentBorder, required this.accentSoft,
    required this.onAccent, required this.danger, required this.success, required this.selection,
    required this.log,
  });
  final Color window, surface, surfaceAlt, field, border, borderStrong, text, textStrong, muted;
  final Color accent, accentBorder, accentSoft, onAccent, danger, success, selection, log;
}

const _dark = _Palette(
  window: Color(0xFF14110F), surface: Color(0xFF1D1815), surfaceAlt: Color(0xFF28211C), field: Color(0xFF15120F),
  border: Color(0xFF3A2D25), borderStrong: Color(0xFF554136), text: Color(0xFFEDE5DE), textStrong: Color(0xFFFFF8F1),
  muted: Color(0xFFB9A79A), accent: Color(0xFF9A643A), accentBorder: Color(0xFFC18452), accentSoft: Color(0xFF4A2E1E),
  onAccent: Color(0xFFFFF8F1), danger: Color(0xFFB84F45), success: Color(0xFF4EAB7C), selection: Color(0xFF6B452C),
  log: Color(0xFF100D0B),
);

const _light = _Palette(
  window: Color(0xFFF5F1ED), surface: Color(0xFFFFFCF9), surfaceAlt: Color(0xFFFAF6F1), field: Color(0xFFFFFDFC),
  border: Color(0xFFDED2C7), borderStrong: Color(0xFFC6B6A8), text: Color(0xFF352A22), textStrong: Color(0xFF241A14),
  muted: Color(0xFF75675D), accent: Color(0xFF8A5734), accentBorder: Color(0xFF8A5734), accentSoft: Color(0xFFF0E0D2),
  onAccent: Color(0xFFFFFFFF), danger: Color(0xFFB54D45), success: Color(0xFF287B59), selection: Color(0xFFE4CDBB),
  log: Color(0xFFFFFCF9),
);

/// Colours the Material scheme has no slot for.
class TaglinkColors extends ThemeExtension<TaglinkColors> {
  const TaglinkColors({required this.success, required this.muted, required this.log});
  final Color success, muted, log;
  static TaglinkColors of(BuildContext context) => Theme.of(context).extension<TaglinkColors>()!;
  @override
  TaglinkColors copyWith({Color? success, Color? muted, Color? log}) =>
      TaglinkColors(success: success ?? this.success, muted: muted ?? this.muted, log: log ?? this.log);
  @override
  TaglinkColors lerp(TaglinkColors? other, double t) => other == null ? this : TaglinkColors(
      success: Color.lerp(success, other.success, t)!, muted: Color.lerp(muted, other.muted, t)!, log: Color.lerp(log, other.log, t)!);
}

ThemeData taglinkTheme(Brightness brightness) {
  final p = brightness == Brightness.dark ? _dark : _light;
  final dark = brightness == Brightness.dark;
  final scheme = ColorScheme(
    brightness: brightness,
    primary: dark ? p.accentBorder : p.accent, onPrimary: dark ? const Color(0xFF241A14) : p.onAccent,
    primaryContainer: p.accentSoft, onPrimaryContainer: p.textStrong,
    secondary: dark ? p.accentBorder : p.accent, onSecondary: p.onAccent,
    secondaryContainer: p.accentSoft, onSecondaryContainer: p.textStrong,
    tertiary: p.success, onTertiary: p.onAccent,
    error: p.danger, onError: const Color(0xFFFFFFFF),
    errorContainer: dark ? const Color(0xFF4A231F) : const Color(0xFFF6DEDA), onErrorContainer: dark ? const Color(0xFFEF9A8B) : const Color(0xFF7A2A24),
    surface: p.window, onSurface: p.text, onSurfaceVariant: p.muted,
    surfaceContainerLowest: p.field, surfaceContainerLow: p.surface, surfaceContainer: p.surface,
    surfaceContainerHigh: p.surfaceAlt, surfaceContainerHighest: p.surfaceAlt,
    outline: p.borderStrong, outlineVariant: p.border,
    inverseSurface: p.text, onInverseSurface: p.window, inversePrimary: p.accentSoft,
    shadow: Colors.black, scrim: Colors.black, surfaceTint: Colors.transparent,
  );
  final shape = RoundedRectangleBorder(borderRadius: BorderRadius.circular(10));
  return ThemeData(
    useMaterial3: true,
    colorScheme: scheme,
    scaffoldBackgroundColor: p.window,
    extensions: [TaglinkColors(success: p.success, muted: p.muted, log: p.log)],
    appBarTheme: AppBarTheme(backgroundColor: p.surface, foregroundColor: p.textStrong, surfaceTintColor: Colors.transparent, elevation: 0, scrolledUnderElevation: 0,
        shape: Border(bottom: BorderSide(color: p.border))),
    cardTheme: CardThemeData(color: p.surface, surfaceTintColor: Colors.transparent, elevation: 0, margin: const EdgeInsets.symmetric(vertical: 4),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12), side: BorderSide(color: p.border))),
    navigationBarTheme: NavigationBarThemeData(backgroundColor: p.surface, surfaceTintColor: Colors.transparent, indicatorColor: p.accentSoft, elevation: 0,
        iconTheme: WidgetStateProperty.resolveWith((s) => IconThemeData(color: s.contains(WidgetState.selected) ? p.textStrong : p.muted)),
        labelTextStyle: WidgetStateProperty.resolveWith((s) => TextStyle(fontSize: 12, color: s.contains(WidgetState.selected) ? p.textStrong : p.muted, fontWeight: s.contains(WidgetState.selected) ? FontWeight.w700 : FontWeight.w500))),
    filledButtonTheme: FilledButtonThemeData(style: FilledButton.styleFrom(backgroundColor: p.accent, foregroundColor: p.onAccent, shape: shape)),
    outlinedButtonTheme: OutlinedButtonThemeData(style: OutlinedButton.styleFrom(foregroundColor: p.text, side: BorderSide(color: p.borderStrong), shape: shape)),
    textButtonTheme: TextButtonThemeData(style: TextButton.styleFrom(foregroundColor: dark ? p.accentBorder : p.accent)),
    inputDecorationTheme: InputDecorationTheme(filled: true, fillColor: p.field,
        border: OutlineInputBorder(borderRadius: BorderRadius.circular(10), borderSide: BorderSide(color: p.border)),
        enabledBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(10), borderSide: BorderSide(color: p.border)),
        focusedBorder: OutlineInputBorder(borderRadius: BorderRadius.circular(10), borderSide: BorderSide(color: p.accentBorder, width: 1.6))),
    chipTheme: ChipThemeData(backgroundColor: p.surfaceAlt, side: BorderSide(color: p.border), labelStyle: TextStyle(color: p.text, fontWeight: FontWeight.w600)),
    dividerTheme: DividerThemeData(color: p.border, space: 1),
    sliderTheme: SliderThemeData(activeTrackColor: p.accentBorder, thumbColor: p.accentBorder, inactiveTrackColor: p.border),
    switchTheme: SwitchThemeData(
        thumbColor: WidgetStateProperty.resolveWith((s) => s.contains(WidgetState.selected) ? p.onAccent : p.muted),
        trackColor: WidgetStateProperty.resolveWith((s) => s.contains(WidgetState.selected) ? p.accent : p.surfaceAlt)),
    dialogTheme: DialogThemeData(backgroundColor: p.surface, surfaceTintColor: Colors.transparent),
    snackBarTheme: SnackBarThemeData(backgroundColor: p.surfaceAlt, contentTextStyle: TextStyle(color: p.textStrong), actionTextColor: p.accentBorder),
    progressIndicatorTheme: ProgressIndicatorThemeData(color: p.accentBorder),
    popupMenuTheme: PopupMenuThemeData(color: p.surface, surfaceTintColor: Colors.transparent),
    listTileTheme: ListTileThemeData(iconColor: p.muted),
  );
}
