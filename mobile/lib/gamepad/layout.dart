import 'dart:math' as math;
import 'package:flutter/painting.dart';

import '../net/protocol.dart';
import '../storage.dart';

enum ControlKind { button, stick, dpad }

enum ControlShape { circle, pill }

class ControlSpec {
  const ControlSpec({
    required this.id,
    required this.kind,
    required this.rect,
    this.bit = 0,
    this.label = '',
    this.shape = ControlShape.circle,
    this.slideable = true,
  });

  final String id;
  final ControlKind kind;
  final Rect rect;
  final int bit; // VPAD bit for buttons (0 = app action, e.g. HOME opens the menu)
  final String label;
  final ControlShape shape;
  final bool slideable; // a finger sliding onto it presses it (face and shoulder buttons)

  Offset get center => rect.center;
  double get radius => rect.shortestSide / 2;

  /// Generous hit areas: thumbs land approximately.
  bool hit(Offset p) {
    switch (shape) {
      case ControlShape.circle:
        final grow = kind == ControlKind.stick ? 1.35 : kind == ControlKind.dpad ? 1.15 : 1.22;
        return (p - center).distance <= radius * grow;
      case ControlShape.pill:
        return rect.inflate(rect.height * 0.2).contains(p);
    }
  }
}

/// Where the GamePad screen and every control sit for a given surface and preferences.
class GamepadLayout {
  GamepadLayout._(this.screen, this.controls, this.roundedScreen);

  final Rect screen;
  final List<ControlSpec> controls;
  final bool roundedScreen;

  static GamepadLayout compute(Size size, EdgeInsets safe, ControlsLayout mode, double scale) {
    final area = safe.deflateRect(Offset.zero & size);
    switch (mode) {
      case ControlsLayout.sides:
        return _sides(area, scale);
      case ControlsLayout.overlay:
        return _overlay(area, scale);
      case ControlsLayout.screenOnly:
        final screen = _fit(area);
        final home = 40.0 * scale;
        return GamepadLayout._(screen, [
          ControlSpec(id: 'home', kind: ControlKind.button, rect: Rect.fromLTWH(area.left + 10, area.top + 10, home, home), label: 'HOME', slideable: false),
        ], false);
    }
  }

  static Rect _fit(Rect area) {
    var w = area.width, h = w * 9 / 16;
    if (h > area.height) {
      h = area.height;
      w = h * 16 / 9;
    }
    return Rect.fromCenter(center: area.center, width: w, height: h);
  }

  static GamepadLayout _sides(Rect area, double k) {
    const pad = 10.0;
    final bottomRow = 46.0 * k;
    final minSide = 158.0 * k;
    var screenW = math.min(area.width - 2 * minSide, (area.height - 2 * pad - bottomRow) * 16 / 9);
    screenW = math.max(screenW, area.width * 0.4);
    final screenH = screenW * 9 / 16;
    final groupH = screenH + bottomRow;
    final top = area.top + math.max(pad, (area.height - groupH) / 2);
    final screen = Rect.fromLTWH(area.center.dx - screenW / 2, top, screenW, screenH);
    final sideW = (area.width - screenW) / 2;
    final left = Rect.fromLTWH(area.left, area.top, sideW, area.height).deflate(pad);
    final right = Rect.fromLTWH(screen.right, area.top, sideW, area.height).deflate(pad);
    final controls = <ControlSpec>[
      ..._column(left, k, isLeft: true),
      ..._column(right, k, isLeft: false),
      ..._metaRow(Offset(screen.center.dx, screen.bottom + bottomRow / 2 + 2), k),
    ];
    return GamepadLayout._(screen, controls, true);
  }

  /// One side of the GamePad: shoulder buttons on top, then stick, then D-pad / face buttons.
  static List<ControlSpec> _column(Rect panel, double k, {required bool isLeft}) {
    final pillH = 38.0 * k;
    final pillW = math.min((panel.width - 8) / 2, 76.0 * k);
    final outer = isLeft ? panel.left : panel.right - pillW;
    final inner = isLeft ? panel.left + pillW + 8 : panel.right - 2 * pillW - 8;
    final shoulders = [
      ControlSpec(
        id: isLeft ? 'zl' : 'zr',
        kind: ControlKind.button,
        rect: Rect.fromLTWH(outer, panel.top, pillW, pillH),
        bit: isLeft ? VpadButton.zl : VpadButton.zr,
        label: isLeft ? 'ZL' : 'ZR',
        shape: ControlShape.pill,
      ),
      ControlSpec(
        id: isLeft ? 'l' : 'r',
        kind: ControlKind.button,
        rect: Rect.fromLTWH(inner, panel.top, pillW, pillH),
        bit: isLeft ? VpadButton.l : VpadButton.r,
        label: isLeft ? 'L' : 'R',
        shape: ControlShape.pill,
      ),
    ];
    final y0 = panel.top + pillH + 10;
    final avail = panel.bottom - y0;
    final d = math.min(math.min(panel.width * 0.92, avail / 2 - 6), 136.0 * k);
    final cx = panel.center.dx;
    final stick = Rect.fromCenter(center: Offset(cx, y0 + avail * 0.27), width: d, height: d);
    final lower = Rect.fromCenter(center: Offset(cx, y0 + avail * 0.76), width: d, height: d);
    return [
      ...shoulders,
      ControlSpec(id: isLeft ? 'stickL' : 'stickR', kind: ControlKind.stick, rect: stick, bit: isLeft ? VpadButton.stickL : VpadButton.stickR),
      if (isLeft) ControlSpec(id: 'dpad', kind: ControlKind.dpad, rect: lower.deflate(d * 0.04)) else ..._faceButtons(lower),
    ];
  }

  static List<ControlSpec> _faceButtons(Rect cluster) {
    final b = cluster.width * 0.37;
    final c = cluster.center, o = cluster.width / 2 - b / 2;
    ControlSpec face(String id, String label, int bit, Offset at) =>
        ControlSpec(id: id, kind: ControlKind.button, rect: Rect.fromCenter(center: at, width: b, height: b), bit: bit, label: label);
    return [
      face('x', 'X', VpadButton.x, c + Offset(0, -o)),
      face('a', 'A', VpadButton.a, c + Offset(o, 0)),
      face('b', 'B', VpadButton.b, c + Offset(0, o)),
      face('y', 'Y', VpadButton.y, c + Offset(-o, 0)),
    ];
  }

  static List<ControlSpec> _metaRow(Offset center, double k) {
    final s = 38.0 * k, gap = 70.0 * k;
    Rect at(double dx) => Rect.fromCenter(center: center + Offset(dx, 0), width: s, height: s);
    return [
      ControlSpec(id: 'minus', kind: ControlKind.button, rect: at(-gap), bit: VpadButton.minus, label: '−', slideable: false),
      ControlSpec(id: 'home', kind: ControlKind.button, rect: at(0), label: 'HOME', slideable: false),
      ControlSpec(id: 'plus', kind: ControlKind.button, rect: at(gap), bit: VpadButton.plus, label: '+', slideable: false),
    ];
  }

  static GamepadLayout _overlay(Rect area, double k) {
    const pad = 14.0;
    final screen = _fit(area);
    final inner = area.deflate(pad);
    final d = math.min(128.0 * k, inner.height * 0.36);
    final pillH = 38.0 * k, pillW = 72.0 * k;
    final controls = <ControlSpec>[
      ControlSpec(id: 'zl', kind: ControlKind.button, rect: Rect.fromLTWH(inner.left, inner.top, pillW, pillH), bit: VpadButton.zl, label: 'ZL', shape: ControlShape.pill),
      ControlSpec(id: 'l', kind: ControlKind.button, rect: Rect.fromLTWH(inner.left + pillW + 10, inner.top, pillW, pillH), bit: VpadButton.l, label: 'L', shape: ControlShape.pill),
      ControlSpec(id: 'zr', kind: ControlKind.button, rect: Rect.fromLTWH(inner.right - pillW, inner.top, pillW, pillH), bit: VpadButton.zr, label: 'ZR', shape: ControlShape.pill),
      ControlSpec(id: 'r', kind: ControlKind.button, rect: Rect.fromLTWH(inner.right - 2 * pillW - 10, inner.top, pillW, pillH), bit: VpadButton.r, label: 'R', shape: ControlShape.pill),
      ControlSpec(
        id: 'stickL',
        kind: ControlKind.stick,
        rect: Rect.fromCenter(center: Offset(inner.left + d / 2 + 12, inner.top + pillH + 18 + d / 2), width: d, height: d),
        bit: VpadButton.stickL,
      ),
      ControlSpec(id: 'dpad', kind: ControlKind.dpad, rect: Rect.fromCenter(center: Offset(inner.left + d / 2 + 48 * k, inner.bottom - d / 2), width: d * 0.9, height: d * 0.9)),
      ControlSpec(
        id: 'stickR',
        kind: ControlKind.stick,
        rect: Rect.fromCenter(center: Offset(inner.right - d / 2 - 12, inner.top + pillH + 18 + d / 2), width: d, height: d),
        bit: VpadButton.stickR,
      ),
      ..._faceButtons(Rect.fromCenter(center: Offset(inner.right - d / 2 - 48 * k, inner.bottom - d / 2), width: d, height: d)),
      ..._metaRow(Offset(area.center.dx, inner.bottom - 19 * k), k),
    ];
    return GamepadLayout._(screen, controls, false);
  }
}
