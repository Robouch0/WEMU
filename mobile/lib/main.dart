import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'net/wemu_client.dart';
import 'screens/connect_screen.dart';
import 'screens/gamepad_screen.dart';
import 'screens/library_screen.dart';
import 'screens/pin_screen.dart';
import 'storage.dart';
import 'strings.dart';
import 'theme.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  SystemChrome.setSystemUIOverlayStyle(const SystemUiOverlayStyle(
    statusBarColor: Colors.transparent,
    statusBarIconBrightness: Brightness.light,
    systemNavigationBarColor: WemuColors.background,
  ));
  final storage = await AppStorage.load();
  final client = WemuClient(storage);
  final last = storage.lastServerKey;
  final saved = last == null ? null : storage.server(last);
  if (saved != null && saved.token != null) client.connect(saved);
  runApp(WemuApp(storage: storage, client: client));
}

/// Gives every screen the storage and the connection.
class AppScope extends InheritedWidget {
  const AppScope({super.key, required this.storage, required this.client, required super.child});

  final AppStorage storage;
  final WemuClient client;

  static AppScope of(BuildContext context) => context.dependOnInheritedWidgetOfExactType<AppScope>()!;

  @override
  bool updateShouldNotify(AppScope oldWidget) => storage != oldWidget.storage || client != oldWidget.client;
}

class WemuApp extends StatefulWidget {
  const WemuApp({super.key, required this.storage, required this.client});

  final AppStorage storage;
  final WemuClient client;

  @override
  State<WemuApp> createState() => _WemuAppState();
}

class _WemuAppState extends State<WemuApp> {
  final _navigator = GlobalKey<NavigatorState>();
  final _messenger = GlobalKey<ScaffoldMessengerState>();
  bool _gamepadOpen = false;
  SessionState _lastSession = SessionState.idle;

  @override
  void initState() {
    super.initState();
    widget.client.addListener(_onClient);
    widget.client.notices.listen((notice) {
      if (notice == 'already_running' || notice == 'unknown_game') {
        _messenger.currentState?.showSnackBar(SnackBar(content: Text(S.error(notice))));
      }
    });
  }

  @override
  void dispose() {
    widget.client.removeListener(_onClient);
    super.dispose();
  }

  // A game started on the PC or from this phone: show the GamePad, wherever the user is.
  void _onClient() {
    final client = widget.client;
    final session = client.session;
    if (_lastSession == SessionState.idle && session != SessionState.idle && client.isConnected && !_gamepadOpen) {
      openGamepad();
    }
    _lastSession = session;
  }

  void openGamepad() {
    final navigator = _navigator.currentState;
    if (navigator == null || _gamepadOpen) return;
    _gamepadOpen = true;
    navigator.popUntil((route) => route.isFirst);
    navigator.push(PageRouteBuilder(
      transitionDuration: const Duration(milliseconds: 350),
      reverseTransitionDuration: const Duration(milliseconds: 250),
      pageBuilder: (_, _, _) => const GamepadScreen(),
      transitionsBuilder: (_, animation, _, child) => FadeTransition(
        opacity: animation,
        child: ScaleTransition(scale: Tween(begin: 1.04, end: 1.0).animate(CurvedAnimation(parent: animation, curve: Curves.easeOutCubic)), child: child),
      ),
    )).then((_) => _gamepadOpen = false);
  }

  @override
  Widget build(BuildContext context) {
    return AppScope(
      storage: widget.storage,
      client: widget.client,
      child: _GamepadOpener(
        open: openGamepad,
        child: MaterialApp(
          title: S.appName,
          debugShowCheckedModeBanner: false,
          theme: buildTheme(),
          navigatorKey: _navigator,
          scaffoldMessengerKey: _messenger,
          home: ListenableBuilder(
            listenable: Listenable.merge([widget.client, widget.storage]),
            builder: (context, _) {
              final client = widget.client;
              final Widget page = switch (client.status) {
                // Fresh (non-const) instances so each screen rebuilds with the connection state.
                ConnectionStatus.connected || ConnectionStatus.reconnecting => LibraryScreen(),
                ConnectionStatus.needPin => PinScreen(),
                ConnectionStatus.connecting when client.pairing => PinScreen(),
                _ => ConnectScreen(),
              };
              return AnimatedSwitcher(duration: const Duration(milliseconds: 300), child: KeyedSubtree(key: ValueKey(page.runtimeType), child: page));
            },
          ),
        ),
      ),
    );
  }
}

/// Lets screens below MaterialApp open the GamePad view through the app state.
class _GamepadOpener extends InheritedWidget {
  const _GamepadOpener({required this.open, required super.child});

  final VoidCallback open;

  @override
  bool updateShouldNotify(_GamepadOpener oldWidget) => false;
}

void openGamepad(BuildContext context) => context.getInheritedWidgetOfExactType<_GamepadOpener>()?.open();
