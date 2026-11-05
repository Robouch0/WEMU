import 'dart:convert';
import 'dart:math';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

class SavedServer {
  SavedServer({required this.host, required this.port, required this.name, this.token});

  final String host;
  final int port;
  String name;
  String? token;

  String get key => '$host:$port';

  Map<String, Object?> toJson() => {'host': host, 'port': port, 'name': name, 'token': token};

  static SavedServer fromJson(Map<String, dynamic> json) => SavedServer(
        host: json['host'] as String,
        port: (json['port'] as num).toInt(),
        name: json['name'] as String? ?? 'WEMU',
        token: json['token'] as String?,
      );
}

enum ControlsLayout { sides, overlay, screenOnly }

enum VideoQuality { low, medium, high }

/// Persistent app state: identity, known PCs and preferences.
class AppStorage extends ChangeNotifier {
  AppStorage._(this._prefs);

  final SharedPreferences _prefs;

  static Future<AppStorage> load() async => AppStorage._(await SharedPreferences.getInstance());

  String get deviceId {
    var id = _prefs.getString('deviceId');
    if (id == null) {
      final random = Random.secure();
      id = List.generate(16, (_) => random.nextInt(256).toRadixString(16).padLeft(2, '0')).join();
      _prefs.setString('deviceId', id);
    }
    return id;
  }

  List<SavedServer> get servers {
    final raw = _prefs.getString('servers');
    if (raw == null) return [];
    try {
      return (jsonDecode(raw) as List).map((e) => SavedServer.fromJson(e as Map<String, dynamic>)).toList();
    } catch (_) {
      return [];
    }
  }

  SavedServer? server(String key) {
    for (final s in servers) {
      if (s.key == key) return s;
    }
    return null;
  }

  String? get lastServerKey => _prefs.getString('lastServer');

  void rememberServer(SavedServer server) {
    final list = servers.where((s) => s.key != server.key).toList()..insert(0, server);
    _prefs.setString('servers', jsonEncode(list.map((s) => s.toJson()).toList()));
    _prefs.setString('lastServer', server.key);
    notifyListeners();
  }

  void forgetServer(String key) {
    final list = servers.where((s) => s.key != key).toList();
    _prefs.setString('servers', jsonEncode(list.map((s) => s.toJson()).toList()));
    if (lastServerKey == key) _prefs.remove('lastServer');
    notifyListeners();
  }

  void clearLastServer() {
    _prefs.remove('lastServer');
  }

  T _enum<T extends Enum>(String key, List<T> values, T fallback) {
    final name = _prefs.getString(key);
    return values.firstWhere((v) => v.name == name, orElse: () => fallback);
  }

  ControlsLayout get layout => _enum('layout', ControlsLayout.values, ControlsLayout.sides);
  set layout(ControlsLayout value) => _set(() => _prefs.setString('layout', value.name));

  VideoQuality get quality => _enum('quality', VideoQuality.values, VideoQuality.medium);
  set quality(VideoQuality value) => _set(() => _prefs.setString('quality', value.name));

  bool get haptics => _prefs.getBool('haptics') ?? true;
  set haptics(bool value) => _set(() => _prefs.setBool('haptics', value));

  bool get motion => _prefs.getBool('motion') ?? false;
  set motion(bool value) => _set(() => _prefs.setBool('motion', value));

  bool get showStats => _prefs.getBool('showStats') ?? false;
  set showStats(bool value) => _set(() => _prefs.setBool('showStats', value));

  bool get flipLandscape => _prefs.getBool('flipLandscape') ?? false;
  set flipLandscape(bool value) => _set(() => _prefs.setBool('flipLandscape', value));

  double get controlsScale => _prefs.getDouble('controlsScale') ?? 1.0;
  set controlsScale(double value) => _set(() => _prefs.setDouble('controlsScale', value));

  double get overlayOpacity => _prefs.getDouble('overlayOpacity') ?? 0.45;
  set overlayOpacity(double value) => _set(() => _prefs.setDouble('overlayOpacity', value));

  void _set(void Function() write) {
    write();
    notifyListeners();
  }
}
