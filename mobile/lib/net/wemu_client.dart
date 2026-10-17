import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';

import '../storage.dart';
import 'protocol.dart';

enum ConnectionStatus { disconnected, connecting, needPin, connected, reconnecting }

enum SessionState { idle, starting, running, paused, stopping }

class Game {
  Game({required this.id, required this.name, this.publisher = '', this.version = '', this.titleId = '', this.hasIcon = false});

  final String id, name, publisher, version, titleId;
  final bool hasIcon;
  Uint8List? icon;
}

/// Latest decoded GamePad-screen image. Repaints listeners without rebuilding the widget tree.
class FrameController extends ChangeNotifier {
  ui.Image? _image;
  int source = 0; // 0 GamePad, 1 TV
  ui.Image? get image => _image;

  void _set(ui.Image image, int frameSource) {
    _image?.dispose();
    _image = image;
    source = frameSource;
    notifyListeners();
  }

  void clear() {
    _image?.dispose();
    _image = null;
    notifyListeners();
  }
}

/// Connection to one WEMU instance: pairing, library, session state, video and input.
class WemuClient extends ChangeNotifier {
  WemuClient(this.storage);

  final AppStorage storage;
  final FrameController frames = FrameController();

  ConnectionStatus status = ConnectionStatus.disconnected;
  SavedServer? server;
  String? error; // last user-facing problem, as a key understood by Strings.error()
  String? pinError;
  List<Game> games = [];
  SessionState session = SessionState.idle;
  String sessionTitle = '';
  String sessionGameId = '';
  String sessionError = '';
  int? latencyMs;
  double frameRate = 0;

  final _notices = StreamController<String>.broadcast();
  Stream<String> get notices => _notices.stream;

  Socket? _socket;
  MessageReader _reader = MessageReader();
  String? _pin;
  bool _userClosed = false;
  bool _welcomed = false;
  Timer? _pingTimer, _reconnectTimer;
  int _connectAttempt = 0;

  // Streaming
  bool _streamWanted = false;
  String _streamSource = 'gamepad';
  bool _decoding = false;
  (int, int, Uint8List)? _pendingFrame; // sequence, source, jpeg
  int _framesThisSecond = 0;
  DateTime _frameWindow = DateTime.now();

  bool get isConnected => status == ConnectionStatus.connected;
  /// True while a PIN is being checked by the PC.
  bool get pairing => _pin != null;
  bool get inGame => session != SessionState.idle;

  Future<void> connect(SavedServer target, {String? pin}) async {
    _userClosed = false;
    _reconnectTimer?.cancel();
    server = target;
    _pin = pin;
    error = null;
    if (status != ConnectionStatus.reconnecting) status = ConnectionStatus.connecting;
    notifyListeners();
    final attempt = ++_connectAttempt;
    _closeSocket();
    try {
      final socket = await Socket.connect(target.host, target.port, timeout: const Duration(seconds: 4));
      if (attempt != _connectAttempt || _userClosed) {
        socket.destroy();
        return;
      }
      socket.setOption(SocketOption.tcpNoDelay, true);
      _socket = socket;
      _reader = MessageReader();
      _welcomed = false;
      socket.listen(
        (data) => _onData(socket, data),
        onError: (_) => _onClosed(socket),
        onDone: () => _onClosed(socket),
        cancelOnError: true,
      );
    } catch (_) {
      if (attempt != _connectAttempt) return;
      if (status == ConnectionStatus.reconnecting) {
        _scheduleReconnect();
      } else {
        status = ConnectionStatus.disconnected;
        error = 'unreachable';
        notifyListeners();
      }
    }
  }

  void submitPin(String pin) {
    final target = server;
    if (target != null) connect(target, pin: pin);
  }

  /// Leaves the PC for good (until the user picks it again).
  void disconnect() {
    _userClosed = true;
    _reconnectTimer?.cancel();
    _closeSocket();
    status = ConnectionStatus.disconnected;
    _resetSession();
    storage.clearLastServer();
    notifyListeners();
  }

  void _closeSocket() {
    _pingTimer?.cancel();
    _socket?.destroy();
    _socket = null;
  }

  void _resetSession() {
    session = SessionState.idle;
    sessionTitle = sessionGameId = sessionError = '';
    latencyMs = null;
    frameRate = 0;
    _pendingFrame = null;
    frames.clear();
  }

  void _onClosed(Socket socket) {
    if (socket != _socket) return;
    _closeSocket();
    if (_userClosed || status == ConnectionStatus.needPin) return;
    if (_welcomed) {
      // Wi-Fi hiccup or WEMU restarted: keep the screen and try again quietly.
      status = ConnectionStatus.reconnecting;
      notifyListeners();
      _scheduleReconnect();
    } else if (status != ConnectionStatus.disconnected) {
      status = ConnectionStatus.disconnected;
      error ??= 'closed';
      notifyListeners();
    }
  }

  void _scheduleReconnect() {
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(const Duration(seconds: 2), () {
      final target = server;
      if (target != null && !_userClosed) connect(target);
    });
  }

  void _send(Uint8List bytes) {
    try {
      _socket?.add(bytes);
    } catch (_) {}
  }

  void sendCommand(String cmd, [Map<String, Object?> args = const {}]) {
    if (!_welcomed) return;
    _send(Protocol.json(Protocol.command, {'cmd': cmd, ...args}));
  }

  void sendInput(InputSample sample) {
    if (_welcomed) _send(Protocol.message(Protocol.input, sample.encode()));
  }

  void launch(Game game) => sendCommand('launch', {'id': game.id});
  void stopGame() => sendCommand('stop');
  void pauseGame() => sendCommand('pause');
  void resumeGame() => sendCommand('resume');

  /// Starts or stops the video feed. [source] is 'gamepad' or 'tv'.
  void setStreaming(bool on, {String? source}) {
    _streamWanted = on;
    if (source != null) _streamSource = source;
    if (!on) frames.clear();
    _sendStreamState();
  }

  String get streamSource => _streamSource;

  void _sendStreamState() {
    const qualities = {VideoQuality.low: 55, VideoQuality.medium: 72, VideoQuality.high: 88};
    sendCommand('stream', {'on': _streamWanted, 'source': _streamSource, 'quality': qualities[storage.quality]});
  }

  void refreshQuality() {
    if (_streamWanted) _sendStreamState();
  }

  void _onData(Socket socket, Uint8List data) {
    if (socket != _socket) return;
    try {
      for (final (type, payload) in _reader.add(data)) {
        _handle(type, payload);
      }
    } catch (_) {
      _closeSocket();
      _onClosed(socket);
    }
  }

  Map<String, dynamic> _json(Uint8List payload) => jsonDecode(utf8.decode(payload)) as Map<String, dynamic>;

  void _handle(int type, Uint8List payload) {
    switch (type) {
      case Protocol.info:
        final target = server!;
        final info = _json(payload);
        target.name = info['name'] as String? ?? target.name;
        final hello = <String, Object?>{
          'protocol': Protocol.version,
          'deviceId': storage.deviceId,
          'deviceName': _deviceName(),
          'platform': Platform.isIOS ? 'iOS' : 'Android',
        };
        if (_pin != null) {
          hello['pin'] = _pin;
        } else if (target.token != null) {
          hello['token'] = target.token;
        }
        _send(Protocol.json(Protocol.hello, hello));
      case Protocol.welcome:
        final welcome = _json(payload);
        final target = server!;
        target.name = welcome['name'] as String? ?? target.name;
        if (welcome['token'] is String) target.token = welcome['token'] as String;
        storage.rememberServer(target);
        _welcomed = true;
        _pin = null;
        pinError = null;
        status = ConnectionStatus.connected;
        _startPings();
        if (_streamWanted) _sendStreamState();
        notifyListeners();
      case Protocol.reject:
        final reason = _json(payload)['reason'] as String? ?? 'protocol';
        _closeSocket();
        if (reason == 'pin_required' || reason == 'bad_pin') {
          // A stored token that no longer works (PC forgot this phone) falls back to pairing.
          server?.token = null;
          status = ConnectionStatus.needPin;
          pinError = reason == 'bad_pin' ? 'bad_pin' : null;
        } else {
          status = ConnectionStatus.disconnected;
          error = reason;
          if (reason == 'locked') {
            status = ConnectionStatus.needPin;
            pinError = 'locked';
          }
        }
        notifyListeners();
      case Protocol.library:
        final list = (_json(payload)['games'] as List?) ?? [];
        final previous = {for (final g in games) g.id: g.icon};
        games = [
          for (final g in list.cast<Map<String, dynamic>>())
            Game(
              id: g['id'] as String,
              name: (g['name'] as String?)?.isNotEmpty == true ? g['name'] as String : '?',
              publisher: g['publisher'] as String? ?? '',
              version: g['version'] as String? ?? '',
              titleId: g['titleId'] as String? ?? '',
              hasIcon: g['icon'] == true,
            )..icon = previous[g['id']],
        ];
        for (final game in games) {
          if (game.hasIcon && game.icon == null) sendCommand('icon', {'id': game.id});
        }
        notifyListeners();
      case Protocol.icon:
        if (payload.length < 2) return;
        final idLength = ByteData.sublistView(payload).getUint16(0, Endian.little);
        if (payload.length < 2 + idLength) return;
        final id = utf8.decode(payload.sublist(2, 2 + idLength));
        for (final game in games) {
          if (game.id == id) game.icon = Uint8List.fromList(payload.sublist(2 + idLength));
        }
        notifyListeners();
      case Protocol.session:
        final s = _json(payload);
        session = SessionState.values.firstWhere((v) => v.name == s['state'], orElse: () => SessionState.idle);
        sessionTitle = s['title'] as String? ?? '';
        sessionGameId = s['id'] as String? ?? '';
        sessionError = s['error'] as String? ?? '';
        if (session == SessionState.idle) {
          _pendingFrame = null;
          frames.clear();
        }
        notifyListeners();
      case Protocol.frame:
        if (payload.length < 9) return;
        final view = ByteData.sublistView(payload);
        _pendingFrame = (view.getUint32(0, Endian.little), payload[8], Uint8List.sublistView(payload, 9));
        _decodeNext();
      case Protocol.pong:
        if (payload.length == 8) {
          final sent = ByteData.sublistView(payload).getInt64(0, Endian.little);
          latencyMs = DateTime.now().microsecondsSinceEpoch ~/ 1000 - sent;
          notifyListeners();
        }
      case Protocol.notice:
        final message = _json(payload)['message'] as String? ?? '';
        if (message.isNotEmpty) _notices.add(message);
        if (message == 'forgotten') server?.token = null;
        if (message == 'replaced' || message == 'disconnected' || message == 'forgotten' || message == 'disabled') {
          // The PC ended this connection on purpose: do not fight it with reconnects.
          _userClosed = true;
          status = ConnectionStatus.disconnected;
          error = message;
          _closeSocket();
          _resetSession();
          notifyListeners();
        }
    }
  }

  Future<void> _decodeNext() async {
    if (_decoding) return;
    final next = _pendingFrame;
    if (next == null) return;
    _pendingFrame = null;
    _decoding = true;
    final (sequence, source, jpeg) = next;
    try {
      final codec = await ui.instantiateImageCodec(jpeg);
      final frame = await codec.getNextFrame();
      codec.dispose();
      if (_streamWanted) {
        frames._set(frame.image, source);
      } else {
        frame.image.dispose();
      }
      _countFrame();
    } catch (_) {
      // A corrupt frame is skipped; the next one replaces it.
    } finally {
      // Acknowledging the newest decoded frame also releases any older one skipped above.
      final ack = ByteData(4)..setUint32(0, sequence, Endian.little);
      _send(Protocol.message(Protocol.frameAck, ack.buffer.asUint8List()));
      _decoding = false;
      if (_pendingFrame != null) _decodeNext();
    }
  }

  void _countFrame() {
    _framesThisSecond++;
    final now = DateTime.now();
    final elapsed = now.difference(_frameWindow).inMilliseconds;
    if (elapsed >= 1000) {
      frameRate = _framesThisSecond * 1000 / elapsed;
      _framesThisSecond = 0;
      _frameWindow = now;
      notifyListeners();
    }
  }

  void _startPings() {
    _pingTimer?.cancel();
    void ping() {
      final data = ByteData(8)..setInt64(0, DateTime.now().microsecondsSinceEpoch ~/ 1000, Endian.little);
      _send(Protocol.message(Protocol.ping, data.buffer.asUint8List()));
    }

    ping();
    _pingTimer = Timer.periodic(const Duration(seconds: 1), (_) => ping());
  }

  static String _deviceName() {
    try {
      final host = Platform.localHostname;
      if (host.isNotEmpty && host != 'localhost') return host.replaceAll('.local', '');
    } catch (_) {}
    return Platform.isIOS ? 'iPhone' : 'Android';
  }

  @override
  void dispose() {
    _userClosed = true;
    _reconnectTimer?.cancel();
    _closeSocket();
    frames.dispose();
    _notices.close();
    super.dispose();
  }
}
