import 'dart:async';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:wakelock_plus/wakelock_plus.dart';

import '../gamepad/controller.dart';
import '../gamepad/layout.dart';
import '../gamepad/painter.dart';
import '../main.dart';
import '../net/wemu_client.dart';
import '../storage.dart';
import '../strings.dart';
import '../theme.dart';
import 'library_screen.dart' show confirmStop;
import 'settings_screen.dart';

/// The phone as the Wii U GamePad: GamePad screen (touch) plus physical-style controls.
class GamepadScreen extends StatefulWidget {
  const GamepadScreen({super.key});

  @override
  State<GamepadScreen> createState() => _GamepadScreenState();
}

class _GamepadScreenState extends State<GamepadScreen> with WidgetsBindingObserver {
  late final WemuClient _client;
  late final AppStorage _storage;
  late final GamepadController _controller;
  bool _menuOpen = false;
  bool _initialized = false;
  Timer? _leaveTimer;
  ControlsLayout? _appliedLayout;
  bool? _appliedFlip, _appliedMotion;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    if (_initialized) return;
    _initialized = true;
    final scope = AppScope.of(context);
    _client = scope.client;
    _storage = scope.storage;
    _controller = GamepadController(send: _client.sendInput, onHome: _openMenu);
    WidgetsBinding.instance.addObserver(this);
    _client.addListener(_onClient);
    _storage.addListener(_onSettings);
    _onSettings();
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
    WakelockPlus.enable();
    _client.setStreaming(true, source: _client.streamSource);
  }

  @override
  void dispose() {
    _leaveTimer?.cancel();
    WidgetsBinding.instance.removeObserver(this);
    _client.removeListener(_onClient);
    _storage.removeListener(_onSettings);
    _controller.releaseAll();
    _client.sendInput(_controller.sample);
    _controller.dispose();
    _client.setStreaming(false);
    WakelockPlus.disable();
    SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
    SystemChrome.setPreferredOrientations([]);
    super.dispose();
  }

  void _onSettings() {
    _controller.haptics = _storage.haptics;
    if (_appliedFlip != _storage.flipLandscape) {
      _appliedFlip = _storage.flipLandscape;
      SystemChrome.setPreferredOrientations([_storage.flipLandscape ? DeviceOrientation.landscapeRight : DeviceOrientation.landscapeLeft]);
    }
    if (_appliedMotion != _storage.motion || _appliedFlip != _storage.flipLandscape) {
      _appliedMotion = _storage.motion;
      _controller.setMotion(_storage.motion, flipped: _storage.flipLandscape);
    }
    if (_appliedLayout != _storage.layout) {
      _appliedLayout = _storage.layout;
      _controller.releaseAll();
    }
    if (mounted) setState(() {});
  }

  // The game ended (on the PC or from here) or the PC is gone: back to the library.
  void _onClient() {
    final gone = _client.status == ConnectionStatus.disconnected || _client.status == ConnectionStatus.needPin;
    final ended = _client.session == SessionState.idle && _client.isConnected;
    if (gone || ended) {
      _leaveTimer ??= Timer(Duration(milliseconds: gone ? 0 : 700), () {
        if (mounted) Navigator.of(context).popUntil((route) => route.isFirst);
      });
    } else {
      _leaveTimer?.cancel();
      _leaveTimer = null;
    }
    if (mounted) setState(() {});
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.paused || state == AppLifecycleState.inactive) {
      _controller.releaseAll();
      _client.sendInput(_controller.sample);
      if (state == AppLifecycleState.paused) _client.setStreaming(false);
    } else if (state == AppLifecycleState.resumed) {
      SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
      _client.setStreaming(true);
    }
  }

  void _openMenu() {
    _controller.releaseAll();
    setState(() => _menuOpen = true);
  }

  void _closeMenu() => setState(() => _menuOpen = false);

  @override
  Widget build(BuildContext context) {
    return PopScope(
      canPop: false,
      onPopInvokedWithResult: (didPop, _) {
        if (didPop) return;
        // System back opens/closes the menu instead of leaving mid-game.
        if (_menuOpen) {
          _closeMenu();
        } else if (_client.inGame) {
          _openMenu();
        } else {
          Navigator.of(context).pop();
        }
      },
      child: Scaffold(
        backgroundColor: Colors.black,
        body: LayoutBuilder(builder: (context, constraints) {
          final padding = MediaQuery.of(context).padding;
          final layout = GamepadLayout.compute(constraints.biggest, padding, _storage.layout, _storage.controlsScale);
          _controller.layout = layout;
          final overlay = _storage.layout == ControlsLayout.overlay;
          return Stack(
            children: [
              if (_storage.layout == ControlsLayout.sides) const Positioned.fill(child: _SidesBackground()),
              Positioned.fromRect(
                rect: layout.screen,
                child: _ScreenView(client: _client, rounded: layout.roundedScreen),
              ),
              Positioned.fill(
                child: Listener(
                  behavior: HitTestBehavior.opaque,
                  onPointerDown: (e) => _controller.pointerDown(e.pointer, e.localPosition),
                  onPointerMove: (e) => _controller.pointerMove(e.pointer, e.localPosition),
                  onPointerUp: (e) => _controller.pointerUp(e.pointer),
                  onPointerCancel: (e) => _controller.pointerUp(e.pointer),
                  child: RepaintBoundary(
                    child: CustomPaint(
                      painter: ControlsPainter(
                        _controller,
                        opacity: overlay ? _storage.overlayOpacity : _storage.layout == ControlsLayout.screenOnly ? 0.55 : 1.0,
                      ),
                    ),
                  ),
                ),
              ),
              if (_storage.showStats) Positioned(top: padding.top + 6, left: 0, right: 0, child: Center(child: _Stats(client: _client))),
              if (_client.status == ConnectionStatus.reconnecting)
                Positioned(top: padding.top + 8, left: 0, right: 0, child: Center(child: _Pill(icon: Icons.wifi_off_rounded, text: S.reconnecting, color: WemuColors.warning))),
              if (_menuOpen) Positioned.fill(child: _Menu(client: _client, storage: _storage, onClose: _closeMenu)),
            ],
          );
        }),
      ),
    );
  }
}

class _SidesBackground extends StatelessWidget {
  const _SidesBackground();

  @override
  Widget build(BuildContext context) => const DecoratedBox(
        decoration: BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topCenter,
            end: Alignment.bottomCenter,
            colors: [Color(0xFF1A2130), Color(0xFF0E131C)],
          ),
        ),
      );
}

/// The streamed GamePad (or TV) image, with friendly states while there is none.
class _ScreenView extends StatelessWidget {
  const _ScreenView({required this.client, required this.rounded});

  final WemuClient client;
  final bool rounded;

  @override
  Widget build(BuildContext context) {
    final radius = BorderRadius.circular(rounded ? 10 : 0);
    return DecoratedBox(
      decoration: BoxDecoration(
        borderRadius: radius,
        color: Colors.black,
        boxShadow: rounded ? [const BoxShadow(color: Colors.black54, blurRadius: 18, spreadRadius: 2)] : null,
        border: rounded ? Border.all(color: const Color(0xFF2A3446), width: 2) : null,
      ),
      child: ClipRRect(
        borderRadius: radius,
        child: ListenableBuilder(
          listenable: client.frames,
          builder: (context, _) {
            final image = client.frames.image;
            if (image != null) {
              return Stack(
                fit: StackFit.expand,
                children: [
                  RawImage(image: image, fit: BoxFit.fill, filterQuality: FilterQuality.medium),
                  if (client.session == SessionState.paused) _Veil(icon: Icons.pause_rounded, text: S.gamePaused),
                ],
              );
            }
            final starting = client.session == SessionState.starting;
            return _Veil(
              icon: starting ? null : Icons.tv_off_rounded,
              text: starting ? '${S.starting}\n${client.sessionTitle}' : S.noSignal,
              hint: starting ? null : S.noSignalHint,
            );
          },
        ),
      ),
    );
  }
}

class _Veil extends StatelessWidget {
  const _Veil({required this.icon, required this.text, this.hint});

  final IconData? icon;
  final String text;
  final String? hint;

  @override
  Widget build(BuildContext context) {
    return Container(
      color: Colors.black.withValues(alpha: 0.6),
      padding: const EdgeInsets.all(16),
      child: FittedBox(
        fit: BoxFit.scaleDown,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            if (icon == null)
              const SizedBox(width: 36, height: 36, child: CircularProgressIndicator(strokeWidth: 3))
            else
              Icon(icon, size: 44, color: WemuColors.muted),
            const SizedBox(height: 12),
            Text(text, textAlign: TextAlign.center, style: const TextStyle(fontSize: 17, fontWeight: FontWeight.w800, color: WemuColors.text)),
            if (hint != null) ...[
              const SizedBox(height: 6),
              SizedBox(width: 320, child: Text(hint!, textAlign: TextAlign.center, style: const TextStyle(color: WemuColors.muted, fontSize: 13))),
            ],
          ],
        ),
      ),
    );
  }
}

class _Stats extends StatelessWidget {
  const _Stats({required this.client});

  final WemuClient client;

  @override
  Widget build(BuildContext context) => _Pill(
        icon: Icons.speed_rounded,
        text: '${client.latencyMs ?? '–'} ms · ${client.frameRate.toStringAsFixed(0)} fps',
        color: WemuColors.text,
      );
}

class _Pill extends StatelessWidget {
  const _Pill({required this.icon, required this.text, required this.color});

  final IconData icon;
  final String text;
  final Color color;

  @override
  Widget build(BuildContext context) => IgnorePointer(
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
          decoration: BoxDecoration(color: Colors.black.withValues(alpha: 0.6), borderRadius: BorderRadius.circular(20)),
          child: Row(mainAxisSize: MainAxisSize.min, children: [
            Icon(icon, size: 15, color: color),
            const SizedBox(width: 6),
            Text(text, style: TextStyle(color: color, fontSize: 12, fontWeight: FontWeight.w700)),
          ]),
        ),
      );
}

/// HOME menu: session control, display source and layout, without leaving the game.
class _Menu extends StatelessWidget {
  const _Menu({required this.client, required this.storage, required this.onClose});

  final WemuClient client;
  final AppStorage storage;
  final VoidCallback onClose;

  @override
  Widget build(BuildContext context) {
    final paused = client.session == SessionState.paused;
    final tv = client.streamSource == 'tv';
    return GestureDetector(
      onTap: onClose,
      child: BackdropFilter(
        filter: ui.ImageFilter.blur(sigmaX: 14, sigmaY: 14),
        child: Container(
          color: Colors.black.withValues(alpha: 0.55),
          alignment: Alignment.center,
          child: GestureDetector(
            onTap: () {},
            child: SafeArea(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(client.sessionTitle.isEmpty ? S.appName : client.sessionTitle,
                      style: const TextStyle(fontSize: 22, fontWeight: FontWeight.w900), overflow: TextOverflow.ellipsis),
                  const SizedBox(height: 4),
                  Text(client.server?.name ?? '', style: const TextStyle(color: WemuColors.muted)),
                  const SizedBox(height: 20),
                  Wrap(
                    spacing: 12,
                    runSpacing: 12,
                    alignment: WrapAlignment.center,
                    children: [
                      _MenuTile(icon: Icons.play_arrow_rounded, label: S.resume, accent: true, onTap: () {
                        if (paused) client.resumeGame();
                        onClose();
                      }),
                      _MenuTile(
                        icon: paused ? Icons.play_circle_outline_rounded : Icons.pause_rounded,
                        label: paused ? S.resume : S.pause,
                        onTap: () => paused ? client.resumeGame() : client.pauseGame(),
                      ),
                      _MenuTile(
                        icon: tv ? Icons.tablet_android_rounded : Icons.tv_rounded,
                        label: tv ? S.sourceGamepad : S.sourceTv,
                        onTap: () => client.setStreaming(true, source: tv ? 'gamepad' : 'tv'),
                      ),
                      _MenuTile(
                        icon: switch (storage.layout) {
                          ControlsLayout.sides => Icons.layers_rounded,
                          ControlsLayout.overlay => Icons.touch_app_rounded,
                          ControlsLayout.screenOnly => Icons.view_column_rounded,
                        },
                        label: switch (storage.layout) {
                          ControlsLayout.sides => S.layoutOverlay,
                          ControlsLayout.overlay => S.layoutScreenOnly,
                          ControlsLayout.screenOnly => S.layoutSides,
                        },
                        onTap: () => storage.layout = ControlsLayout.values[(storage.layout.index + 1) % ControlsLayout.values.length],
                      ),
                      _MenuTile(icon: Icons.tune_rounded, label: S.settings, onTap: () {
                        onClose();
                        Navigator.push(context, MaterialPageRoute(builder: (_) => const SettingsScreen()));
                      }),
                      _MenuTile(icon: Icons.grid_view_rounded, label: S.backToLibrary, onTap: () => Navigator.of(context).pop()),
                      _MenuTile(icon: Icons.power_settings_new_rounded, label: S.stop, danger: true, onTap: () async {
                        if (await confirmStop(context)) client.stopGame();
                      }),
                    ],
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

class _MenuTile extends StatelessWidget {
  const _MenuTile({required this.icon, required this.label, required this.onTap, this.accent = false, this.danger = false});

  final IconData icon;
  final String label;
  final VoidCallback onTap;
  final bool accent, danger;

  @override
  Widget build(BuildContext context) {
    final color = danger ? WemuColors.danger : accent ? WemuColors.accent : WemuColors.text;
    return Material(
      color: accent ? WemuColors.accent.withValues(alpha: 0.18) : WemuColors.surface.withValues(alpha: 0.9),
      borderRadius: BorderRadius.circular(20),
      child: InkWell(
        borderRadius: BorderRadius.circular(20),
        onTap: () {
          HapticFeedback.selectionClick();
          onTap();
        },
        child: Container(
          width: 112,
          height: 96,
          padding: const EdgeInsets.all(10),
          decoration: BoxDecoration(
            borderRadius: BorderRadius.circular(20),
            border: Border.all(color: accent ? WemuColors.accent : WemuColors.outline),
          ),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(icon, color: color, size: 30),
              const SizedBox(height: 8),
              Text(label, textAlign: TextAlign.center, maxLines: 2, style: TextStyle(color: color, fontWeight: FontWeight.w700, fontSize: 12.5)),
            ],
          ),
        ),
      ),
    );
  }
}
