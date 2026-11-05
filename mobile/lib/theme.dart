import 'package:flutter/material.dart';

/// Visual identity: deep night-blue surfaces with the Wii U's cyan as the accent.
class WemuColors {
  static const background = Color(0xFF0B0F17);
  static const surface = Color(0xFF141B27);
  static const surfaceHigh = Color(0xFF1C2535);
  static const outline = Color(0xFF2A3548);
  static const accent = Color(0xFF19B8F0);
  static const accentSoft = Color(0x3319B8F0);
  static const text = Color(0xFFF1F5FB);
  static const muted = Color(0xFF8D9AB0);
  static const success = Color(0xFF2FD082);
  static const warning = Color(0xFFF5B33D);
  static const danger = Color(0xFFFF5D6C);

  // Face buttons keep the GamePad's neutral look; the letters carry the colour.
  static const buttonFace = Color(0xFF232D3F);
  static const buttonPressed = Color(0xFF19B8F0);
}

ThemeData buildTheme() {
  final scheme = ColorScheme.fromSeed(
    seedColor: WemuColors.accent,
    brightness: Brightness.dark,
    surface: WemuColors.surface,
    primary: WemuColors.accent,
  );
  return ThemeData(
    useMaterial3: true,
    colorScheme: scheme,
    scaffoldBackgroundColor: WemuColors.background,
    splashFactory: InkSparkle.splashFactory,
    appBarTheme: const AppBarTheme(
      backgroundColor: Colors.transparent,
      elevation: 0,
      scrolledUnderElevation: 0,
      centerTitle: false,
      titleTextStyle: TextStyle(fontSize: 22, fontWeight: FontWeight.w800, color: WemuColors.text, letterSpacing: -0.3),
    ),
    cardTheme: CardThemeData(
      color: WemuColors.surface,
      elevation: 0,
      margin: EdgeInsets.zero,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(20), side: const BorderSide(color: WemuColors.outline)),
    ),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        backgroundColor: WemuColors.accent,
        foregroundColor: const Color(0xFF04131C),
        minimumSize: const Size(0, 52),
        textStyle: const TextStyle(fontWeight: FontWeight.w800, fontSize: 16),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        foregroundColor: WemuColors.text,
        minimumSize: const Size(0, 52),
        side: const BorderSide(color: WemuColors.outline),
        textStyle: const TextStyle(fontWeight: FontWeight.w700, fontSize: 15),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
      ),
    ),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: WemuColors.surfaceHigh,
      border: OutlineInputBorder(borderRadius: BorderRadius.circular(14), borderSide: BorderSide.none),
      focusedBorder: OutlineInputBorder(
        borderRadius: BorderRadius.circular(14),
        borderSide: const BorderSide(color: WemuColors.accent, width: 2),
      ),
    ),
    snackBarTheme: SnackBarThemeData(
      behavior: SnackBarBehavior.floating,
      backgroundColor: WemuColors.surfaceHigh,
      contentTextStyle: const TextStyle(color: WemuColors.text, fontWeight: FontWeight.w600),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(14)),
    ),
    switchTheme: SwitchThemeData(
      thumbColor: WidgetStateProperty.resolveWith((s) => s.contains(WidgetState.selected) ? Colors.white : WemuColors.muted),
      trackColor: WidgetStateProperty.resolveWith((s) => s.contains(WidgetState.selected) ? WemuColors.accent : WemuColors.surfaceHigh),
    ),
  );
}

/// Soft radial glow used behind hero content.
class GlowBackground extends StatelessWidget {
  const GlowBackground({super.key, required this.child});

  final Widget child;

  @override
  Widget build(BuildContext context) {
    return DecoratedBox(
      decoration: const BoxDecoration(
        gradient: RadialGradient(
          center: Alignment(0, -1.1),
          radius: 1.3,
          colors: [Color(0xFF123349), WemuColors.background],
          stops: [0, 0.75],
        ),
      ),
      child: child,
    );
  }
}
