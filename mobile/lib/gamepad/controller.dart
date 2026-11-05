import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:sensors_plus/sensors_plus.dart';

import '../net/protocol.dart';
import 'layout.dart';

enum _CaptureKind { buttons, stick, dpad, screen }

class _Capture {
  _Capture(this.kind, {this.control});

  final _CaptureKind kind;
  final ControlSpec? control; // stick or d-pad being held
  Set<String> pressed = {}; // button ids under this finger
  Offset position = Offset.zero;
  bool stickClick = false;
}

/// Turns raw pointers into GamePad state: every finger is captured by the control it lands on
/// (stick, D-pad, buttons or the touch screen) and keeps it until lifted; buttons can be slid
/// across. Sends samples to WEMU at up to ~120 Hz, only when something changed.
class GamepadController extends ChangeNotifier {
  GamepadController({required this.send, required this.onHome});

  final void Function(InputSample sample) send;
  final VoidCallback onHome;

  GamepadLayout? layout;
  bool haptics = true;

  final InputSample sample = InputSample();
  final Map<int, _Capture> _captures = {};
  final Map<String, DateTime> _lastStickRelease = {};
  Offset leftStick = Offset.zero, rightStick = Offset.zero; // visual, y down
  int pressedBits = 0;
  Set<String> pressedIds = {};
  int dpadBits = 0;

  Timer? _sendTimer;
  DateTime _lastSend = DateTime.fromMillisecondsSinceEpoch(0);
  static const _minInterval = Duration(milliseconds: 8);

  StreamSubscription<GyroscopeEvent>? _gyro;
  StreamSubscription<AccelerometerEvent>? _accel;
  bool _flipped = false;

  // ── pointers ─────────────────────────────────────────────────────────────

  void pointerDown(int pointer, Offset p) {
    final l = layout;
    if (l == null) return;
    // Sticks and D-pad first: they have the largest grab areas.
    for (final c in l.controls) {
      if (c.kind == ControlKind.button || !c.hit(p)) continue;
      if (_captures.values.any((cap) => cap.control?.id == c.id)) continue;
      final capture = _Capture(c.kind == ControlKind.stick ? _CaptureKind.stick : _CaptureKind.dpad, control: c)..position = p;
      if (c.kind == ControlKind.stick) {
        final last = _lastStickRelease[c.id];
        capture.stickClick = last != null && DateTime.now().difference(last) < const Duration(milliseconds: 280);
      }
      _captures[pointer] = capture;
      _update(haptic: c.kind == ControlKind.dpad || capture.stickClick);
      return;
    }
    final buttons = _buttonsAt(p, slideOnly: false);
    if (buttons.isNotEmpty) {
      if (buttons.contains('home')) {
        if (haptics) HapticFeedback.mediumImpact();
        onHome();
        return;
      }
      _captures[pointer] = _Capture(_CaptureKind.buttons)
        ..pressed = buttons
        ..position = p;
      _update(haptic: true);
      return;
    }
    if (l.screen.contains(p) && !_captures.values.any((c) => c.kind == _CaptureKind.screen)) {
      _captures[pointer] = _Capture(_CaptureKind.screen)..position = p;
      _update();
    }
  }

  void pointerMove(int pointer, Offset p) {
    final capture = _captures[pointer];
    if (capture == null) return;
    capture.position = p;
    if (capture.kind == _CaptureKind.buttons) {
      final now = _buttonsAt(p, slideOnly: true);
      final gained = now.difference(capture.pressed).isNotEmpty;
      capture.pressed = now;
      _update(haptic: gained);
    } else {
      _update(haptic: capture.kind == _CaptureKind.dpad);
    }
  }

  void pointerUp(int pointer) {
    final capture = _captures.remove(pointer);
    if (capture == null) return;
    if (capture.kind == _CaptureKind.stick) _lastStickRelease[capture.control!.id] = DateTime.now();
    _update();
  }

  /// Drops every finger (app paused, menu opened, screen left).
  void releaseAll() {
    _captures.clear();
    _update();
  }

  Set<String> _buttonsAt(Offset p, {required bool slideOnly}) {
    final hits = <String>{};
    for (final c in layout!.controls) {
      if (c.kind != ControlKind.button || (slideOnly && !c.slideable)) continue;
      if (c.hit(p)) hits.add(c.id);
    }
    // Overlapping grab areas let a thumb between A and B press both, like on real hardware;
    // never more than two.
    if (hits.length > 2) {
      final sorted = hits.toList()
        ..sort((a, b) => (_spec(a).center - p).distance.compareTo((_spec(b).center - p).distance));
      return sorted.take(2).toSet();
    }
    return hits;
  }

  ControlSpec _spec(String id) => layout!.controls.firstWhere((c) => c.id == id);

  // ── state ────────────────────────────────────────────────────────────────

  void _update({bool haptic = false}) {
    final l = layout;
    if (l == null) return;
    var bits = 0;
    final ids = <String>{};
    var left = Offset.zero, right = Offset.zero;
    var dpad = 0;
    var touched = false;
    var tx = 0, ty = 0;
    for (final capture in _captures.values) {
      switch (capture.kind) {
        case _CaptureKind.buttons:
          for (final id in capture.pressed) {
            ids.add(id);
            bits |= _spec(id).bit;
          }
        case _CaptureKind.stick:
          final c = capture.control!;
          var v = (capture.position - c.center) / (c.radius * 0.72);
          if (v.distance > 1) v = v / v.distance;
          if (v.distance < 0.08) v = Offset.zero; // dead zone: a resting thumb never drifts
          if (c.id == 'stickL') {
            left = v;
          } else {
            right = v;
          }
          if (capture.stickClick) {
            bits |= c.bit;
            ids.add(c.id);
          }
        case _CaptureKind.dpad:
          dpad |= _dpadBits(capture.control!, capture.position);
        case _CaptureKind.screen:
          final s = l.screen;
          final x = ((capture.position.dx - s.left) / s.width).clamp(0.0, 1.0);
          final y = ((capture.position.dy - s.top) / s.height).clamp(0.0, 1.0);
          touched = true;
          tx = (x * 853).round();
          ty = (y * 479).round();
      }
    }
    bits |= dpad;
    final newly = bits & ~pressedBits;
    if (haptic && haptics && newly != 0) HapticFeedback.lightImpact();
    final changed = bits != pressedBits || left != leftStick || right != rightStick || touched != sample.touched || tx != sample.touchX || ty != sample.touchY;
    pressedBits = bits;
    pressedIds = ids;
    dpadBits = dpad;
    leftStick = left;
    rightStick = right;
    sample
      ..buttons = bits
      ..lx = left.dx
      ..ly = 0.0 - left.dy // never -0.0 at rest
      ..rx = right.dx
      ..ry = 0.0 - right.dy
      ..touched = touched
      ..touchX = tx
      ..touchY = ty;
    if (changed) {
      notifyListeners();
      _scheduleSend();
    }
  }

  int _dpadBits(ControlSpec c, Offset p) {
    final v = p - c.center;
    if (v.distance < c.radius * 0.2) return 0;
    // 8 sectors of 45°: diagonals press two directions.
    final angle = math.atan2(v.dy, v.dx); // y down
    final sector = ((angle / (math.pi / 4)).round() + 8) % 8;
    const map = [
      VpadButton.right,
      VpadButton.right | VpadButton.down,
      VpadButton.down,
      VpadButton.down | VpadButton.left,
      VpadButton.left,
      VpadButton.left | VpadButton.up,
      VpadButton.up,
      VpadButton.up | VpadButton.right,
    ];
    return map[sector];
  }

  void _scheduleSend() {
    if (_sendTimer != null) return;
    final wait = _minInterval - DateTime.now().difference(_lastSend);
    if (wait <= Duration.zero) {
      _flush();
    } else {
      _sendTimer = Timer(wait, _flush);
    }
  }

  void _flush() {
    _sendTimer = null;
    _lastSend = DateTime.now();
    send(sample);
  }

  // ── motion ───────────────────────────────────────────────────────────────

  /// Phone sensors are in the portrait frame; the GamePad view is locked to one landscape side.
  void setMotion(bool enabled, {required bool flipped}) {
    _flipped = flipped;
    _gyro?.cancel();
    _accel?.cancel();
    _gyro = _accel = null;
    if (!enabled) {
      sample
        ..ax = 0
        ..ay = -1
        ..az = 0
        ..gx = 0
        ..gy = 0
        ..gz = 0;
      _scheduleSend();
      return;
    }
    try {
      _accel = accelerometerEventStream(samplingPeriod: SensorInterval.gameInterval).listen((e) {
        // Reaction force (m/s²) -> gravity direction in g.
        final (x, y) = _toLandscape(e.x, e.y);
        sample
          ..ax = -x / 9.81
          ..ay = -y / 9.81
          ..az = -e.z / 9.81;
      }, onError: (_) {});
      _gyro = gyroscopeEventStream(samplingPeriod: SensorInterval.gameInterval).listen((e) {
        // rad/s -> revolutions/s
        final (x, y) = _toLandscape(e.x, e.y);
        sample
          ..gx = x / (2 * math.pi)
          ..gy = y / (2 * math.pi)
          ..gz = e.z / (2 * math.pi);
        _scheduleSend();
      }, onError: (_) {});
    } catch (_) {
      // No sensors on this device.
    }
  }

  // Landscape-left (device top on the left): screen right = -device y, screen up = +device x.
  (double, double) _toLandscape(double x, double y) => _flipped ? (y, -x) : (-y, x);

  @override
  void dispose() {
    _sendTimer?.cancel();
    _gyro?.cancel();
    _accel?.cancel();
    super.dispose();
  }
}
