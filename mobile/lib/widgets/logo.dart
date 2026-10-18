import 'package:flutter/material.dart';

import '../theme.dart';

/// A stylised Wii U GamePad: wide body, screen, two sticks.
class GamepadLogo extends StatelessWidget {
  const GamepadLogo({super.key, this.size = 120, this.glow = true});

  final double size;
  final bool glow;

  @override
  Widget build(BuildContext context) {
    return SizedBox(width: size, height: size * 0.62, child: CustomPaint(painter: _LogoPainter(glow)));
  }
}

class _LogoPainter extends CustomPainter {
  _LogoPainter(this.glow);

  final bool glow;

  @override
  void paint(Canvas canvas, Size size) {
    final w = size.width, h = size.height;
    final body = RRect.fromRectAndRadius(Rect.fromLTWH(0, h * 0.08, w, h * 0.84), Radius.circular(h * 0.3));
    if (glow) {
      canvas.drawRRect(body.inflate(4), Paint()..color = WemuColors.accent.withValues(alpha: 0.35)..maskFilter = const MaskFilter.blur(BlurStyle.normal, 18));
    }
    canvas.drawRRect(
      body,
      Paint()
        ..shader = const LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [Color(0xFF2A3446), Color(0xFF161D2A)],
        ).createShader(body.outerRect),
    );
    canvas.drawRRect(body, Paint()..style = PaintingStyle.stroke..strokeWidth = w * 0.012..color = const Color(0xFF3A475E));
    final screen = RRect.fromRectAndRadius(Rect.fromLTWH(w * 0.27, h * 0.2, w * 0.46, h * 0.6), Radius.circular(h * 0.05));
    canvas.drawRRect(
      screen,
      Paint()
        ..shader = const LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [Color(0xFF29C6FF), Color(0xFF0E6FA8)],
        ).createShader(screen.outerRect),
    );
    // Screen sheen
    final sheen = Path()
      ..moveTo(screen.left, screen.top)
      ..lineTo(screen.left + screen.width * 0.55, screen.top)
      ..lineTo(screen.left + screen.width * 0.25, screen.bottom)
      ..lineTo(screen.left, screen.bottom)
      ..close();
    canvas.save();
    canvas.clipRRect(screen);
    canvas.drawPath(sheen, Paint()..color = Colors.white.withValues(alpha: 0.12));
    canvas.restore();
    final stick = Paint()..color = const Color(0xFF0D121B);
    final cap = Paint()..color = const Color(0xFF465570);
    for (final x in [w * 0.135, w * 0.865]) {
      final c = Offset(x, h * 0.36);
      canvas.drawCircle(c, h * 0.11, stick);
      canvas.drawCircle(c, h * 0.075, cap);
    }
    final dot = Paint()..color = const Color(0xFF8D9AB0);
    // D-pad and face buttons hint
    canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: Offset(w * 0.135, h * 0.66), width: h * 0.17, height: h * 0.055), const Radius.circular(2)), dot);
    canvas.drawRRect(RRect.fromRectAndRadius(Rect.fromCenter(center: Offset(w * 0.135, h * 0.66), width: h * 0.055, height: h * 0.17), const Radius.circular(2)), dot);
    for (final o in [const Offset(0, -1), const Offset(1, 0), const Offset(0, 1), const Offset(-1, 0)]) {
      canvas.drawCircle(Offset(w * 0.865, h * 0.66) + o * h * 0.07, h * 0.03, dot);
    }
  }

  @override
  bool shouldRepaint(_LogoPainter oldDelegate) => false;
}
