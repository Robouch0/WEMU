import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../net/protocol.dart';
import '../theme.dart';
import 'controller.dart';
import 'layout.dart';

/// Draws every control from the controller's live state.
class ControlsPainter extends CustomPainter {
  ControlsPainter(this.controller, {required this.opacity}) : super(repaint: controller);

  final GamepadController controller;
  final double opacity;

  static final Map<String, TextPainter> _labels = {};

  @override
  void paint(Canvas canvas, Size size) {
    final layout = controller.layout;
    if (layout == null) return;
    canvas.saveLayer(Offset.zero & size, Paint()..color = Colors.white.withValues(alpha: opacity));
    for (final c in layout.controls) {
      switch (c.kind) {
        case ControlKind.stick:
          _stick(canvas, c, c.id == 'stickL' ? controller.leftStick : controller.rightStick, controller.pressedIds.contains(c.id));
        case ControlKind.dpad:
          _dpad(canvas, c, controller.dpadBits);
        case ControlKind.button:
          _button(canvas, c, controller.pressedIds.contains(c.id));
      }
    }
    canvas.restore();
  }

  void _stick(Canvas canvas, ControlSpec c, Offset value, bool clicked) {
    final r = c.radius;
    final center = c.center;
    // Well
    canvas.drawCircle(
      center,
      r,
      Paint()
        ..shader = RadialGradient(colors: const [Color(0xFF0A0E15), Color(0xFF1A2232)], stops: const [0.55, 1])
            .createShader(Rect.fromCircle(center: center, radius: r)),
    );
    canvas.drawCircle(center, r, Paint()..style = PaintingStyle.stroke..strokeWidth = 1.5..color = WemuColors.outline);
    // Direction hint ring
    if (value != Offset.zero) {
      final sweep = math.pi / 2.4;
      final angle = math.atan2(value.dy, value.dx);
      canvas.drawArc(
        Rect.fromCircle(center: center, radius: r - 3),
        angle - sweep / 2,
        sweep,
        false,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 3
          ..strokeCap = StrokeCap.round
          ..color = WemuColors.accent.withValues(alpha: 0.4 + 0.6 * value.distance.clamp(0.0, 1.0)),
      );
    }
    // Cap
    final capCenter = center + value * (r * 0.42);
    final capR = r * 0.52;
    canvas.drawCircle(capCenter + const Offset(0, 4), capR, Paint()..color = Colors.black.withValues(alpha: 0.45)..maskFilter = const MaskFilter.blur(BlurStyle.normal, 6));
    canvas.drawCircle(
      capCenter,
      capR,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: clicked ? const [Color(0xFF5CD6FF), Color(0xFF0E7FB5)] : const [Color(0xFF4A5871), Color(0xFF283245)],
        ).createShader(Rect.fromCircle(center: capCenter, radius: capR)),
    );
    canvas.drawCircle(capCenter, capR * 0.72, Paint()..style = PaintingStyle.stroke..strokeWidth = 1.2..color = Colors.white.withValues(alpha: 0.08));
  }

  void _dpad(Canvas canvas, ControlSpec c, int bits) {
    final s = c.rect.width;
    final arm = s * 0.34;
    final center = c.center;
    final cross = Path()
      ..addRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: center, width: arm, height: s), Radius.circular(arm * 0.22)))
      ..addRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: center, width: s, height: arm), Radius.circular(arm * 0.22)));
    canvas.drawPath(cross.shift(const Offset(0, 3)), Paint()..color = Colors.black.withValues(alpha: 0.4)..maskFilter = const MaskFilter.blur(BlurStyle.normal, 5));
    canvas.drawPath(cross, Paint()..color = WemuColors.buttonFace);
    final glow = Paint()..color = WemuColors.buttonPressed;
    final h = s / 2;
    void armRect(int bit, Rect r) {
      if (bits & bit != 0) canvas.drawRRect(RRect.fromRectAndRadius(r, Radius.circular(arm * 0.22)), glow);
    }

    armRect(VpadButton.up, Rect.fromLTWH(center.dx - arm / 2, center.dy - h, arm, h - arm / 2));
    armRect(VpadButton.down, Rect.fromLTWH(center.dx - arm / 2, center.dy + arm / 2, arm, h - arm / 2));
    armRect(VpadButton.left, Rect.fromLTWH(center.dx - h, center.dy - arm / 2, h - arm / 2, arm));
    armRect(VpadButton.right, Rect.fromLTWH(center.dx + arm / 2, center.dy - arm / 2, h - arm / 2, arm));
    // Arrows
    final arrow = Paint()..color = Colors.white.withValues(alpha: 0.55);
    for (var i = 0; i < 4; i++) {
      final a = i * math.pi / 2 - math.pi / 2;
      final dir = Offset(math.cos(a), math.sin(a));
      final tip = center + dir * (h - arm * 0.3);
      final base = center + dir * (h - arm * 0.62);
      final side = Offset(-dir.dy, dir.dx) * arm * 0.16;
      canvas.drawPath(Path()..moveTo(tip.dx, tip.dy)..lineTo(base.dx + side.dx, base.dy + side.dy)..lineTo(base.dx - side.dx, base.dy - side.dy)..close(), arrow);
    }
    canvas.drawCircle(center, arm * 0.2, Paint()..color = Colors.black.withValues(alpha: 0.25));
  }

  void _button(Canvas canvas, ControlSpec c, bool pressed) {
    final rect = c.rect;
    final isHome = c.id == 'home';
    final rrect = c.shape == ControlShape.pill
        ? RRect.fromRectAndRadius(rect, Radius.circular(rect.height / 2))
        : RRect.fromRectAndRadius(rect, Radius.circular(rect.width / 2));
    final offset = pressed ? 1.0 : 3.0;
    canvas.drawRRect(rrect.shift(Offset(0, offset)), Paint()..color = Colors.black.withValues(alpha: 0.45)..maskFilter = const MaskFilter.blur(BlurStyle.normal, 4));
    final face = pressed
        ? WemuColors.buttonPressed
        : isHome
            ? const Color(0xFF1B2433)
            : WemuColors.buttonFace;
    canvas.drawRRect(
      rrect.shift(Offset(0, pressed ? 1 : 0)),
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [Color.lerp(face, Colors.white, pressed ? 0.15 : 0.07)!, face],
        ).createShader(rect),
    );
    canvas.drawRRect(rrect, Paint()..style = PaintingStyle.stroke..strokeWidth = 1..color = Colors.white.withValues(alpha: pressed ? 0.25 : 0.08));
    final center = rect.center + Offset(0, pressed ? 1 : 0);
    if (isHome) {
      _homeIcon(canvas, center, rect.width * 0.42, pressed);
      return;
    }
    final fontSize = c.shape == ControlShape.pill ? rect.height * 0.42 : rect.width * (c.label.length > 1 ? 0.36 : 0.46);
    final key = '${c.label}|${fontSize.toStringAsFixed(1)}|$pressed';
    final label = _labels.putIfAbsent(
      key,
      () => TextPainter(
        text: TextSpan(
          text: c.label,
          style: TextStyle(fontSize: fontSize, fontWeight: FontWeight.w900, color: pressed ? const Color(0xFF04131C) : const Color(0xFFE4EAF4)),
        ),
        textDirection: TextDirection.ltr,
      )..layout(),
    );
    label.paint(canvas, center - Offset(label.width / 2, label.height / 2));
  }

  void _homeIcon(Canvas canvas, Offset c, double s, bool pressed) {
    final paint = Paint()
      ..color = pressed ? const Color(0xFF04131C) : WemuColors.accent
      ..style = PaintingStyle.stroke
      ..strokeWidth = s * 0.14
      ..strokeJoin = StrokeJoin.round
      ..strokeCap = StrokeCap.round;
    final path = Path()
      ..moveTo(c.dx - s / 2, c.dy - s * 0.02)
      ..lineTo(c.dx, c.dy - s / 2)
      ..lineTo(c.dx + s / 2, c.dy - s * 0.02)
      ..moveTo(c.dx - s * 0.34, c.dy - s * 0.15)
      ..lineTo(c.dx - s * 0.34, c.dy + s * 0.42)
      ..lineTo(c.dx + s * 0.34, c.dy + s * 0.42)
      ..lineTo(c.dx + s * 0.34, c.dy - s * 0.15);
    canvas.drawPath(path, paint);
  }

  @override
  bool shouldRepaint(ControlsPainter oldDelegate) => oldDelegate.opacity != opacity || oldDelegate.controller != controller;
}
