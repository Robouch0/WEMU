import 'package:flutter/painting.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:wemu_gamepad/gamepad/controller.dart';
import 'package:wemu_gamepad/gamepad/layout.dart';
import 'package:wemu_gamepad/net/protocol.dart';
import 'package:wemu_gamepad/storage.dart';

// A 20:9 phone in landscape, in logical pixels, with a camera cutout on the left.
const phone = Size(873, 393);
const safe = EdgeInsets.only(left: 32);

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('layout', () {
    for (final mode in ControlsLayout.values) {
      for (final scale in [0.75, 1.0, 1.3]) {
        test('$mode at ${scale}x keeps controls on screen and in the safe area', () {
          final layout = GamepadLayout.compute(phone, safe, mode, scale);
          final area = safe.deflateRect(Offset.zero & phone);
          expect(layout.screen.width / layout.screen.height, closeTo(16 / 9, 0.01));
          for (final c in layout.controls) {
            expect(area.inflate(0.5).contains(c.rect.topLeft) && area.inflate(0.5).contains(c.rect.bottomRight), isTrue, reason: c.id);
          }
        });
      }
    }

    test('beside the screen, no control covers the GamePad screen', () {
      final layout = GamepadLayout.compute(phone, safe, ControlsLayout.sides, 1.0);
      for (final c in layout.controls) {
        expect(c.rect.overlaps(layout.screen.deflate(1)), isFalse, reason: c.id);
      }
      final ids = layout.controls.map((c) => c.id).toSet();
      expect(ids, containsAll(['stickL', 'stickR', 'dpad', 'a', 'b', 'x', 'y', 'l', 'r', 'zl', 'zr', 'minus', 'plus', 'home']));
    });
  });

  group('controller', () {
    late GamepadController controller;
    late List<InputSample> sent;
    late int homes;
    late GamepadLayout layout;

    setUp(() {
      sent = [];
      homes = 0;
      controller = GamepadController(send: (s) => sent.add(s), onHome: () => homes++)..haptics = false;
      layout = GamepadLayout.compute(phone, safe, ControlsLayout.sides, 1.0);
      controller.layout = layout;
    });

    ControlSpec spec(String id) => layout.controls.firstWhere((c) => c.id == id);

    test('buttons press and release; two fingers combine', () {
      controller.pointerDown(1, spec('a').center);
      controller.pointerDown(2, spec('zr').center);
      expect(controller.sample.buttons, VpadButton.a | VpadButton.zr);
      controller.pointerUp(1);
      expect(controller.sample.buttons, VpadButton.zr);
      controller.pointerUp(2);
      expect(controller.sample.buttons, 0);
    });

    test('a finger slides from one face button to another', () {
      controller.pointerDown(1, spec('b').center);
      controller.pointerMove(1, spec('a').center);
      expect(controller.sample.buttons, VpadButton.a);
    });

    test('stick reports direction with y up and a dead zone', () {
      final stick = spec('stickL');
      controller.pointerDown(1, stick.center);
      controller.pointerMove(1, stick.center + Offset(0, -stick.radius)); // finger up
      expect(controller.sample.ly, closeTo(1, 1e-6));
      expect(controller.sample.lx, closeTo(0, 1e-6));
      controller.pointerMove(1, stick.center + const Offset(1, 1));
      expect(controller.sample.lx, 0);
      expect(controller.sample.ly, 0);
      controller.pointerUp(1);
    });

    test('d-pad diagonals press two directions', () {
      final pad = spec('dpad');
      controller.pointerDown(1, pad.center + Offset(pad.radius * 0.6, -pad.radius * 0.6));
      expect(controller.sample.buttons, VpadButton.up | VpadButton.right);
    });

    test('touches map to GamePad pixels', () {
      final screen = layout.screen;
      controller.pointerDown(1, screen.topLeft);
      expect((controller.sample.touchX, controller.sample.touchY, controller.sample.touched), (0, 0, true));
      controller.pointerMove(1, screen.bottomRight);
      expect((controller.sample.touchX, controller.sample.touchY), (853, 479));
      controller.pointerUp(1);
      expect(controller.sample.touched, isFalse);
    });

    test('HOME opens the menu instead of reaching the game', () {
      controller.pointerDown(1, spec('home').center);
      expect(homes, 1);
      expect(controller.sample.buttons, 0);
    });

    test('changes are sent at most every 8 ms', () async {
      controller.pointerDown(1, spec('a').center);
      controller.pointerUp(1);
      controller.pointerDown(1, spec('b').center);
      await Future<void>.delayed(const Duration(milliseconds: 20));
      expect(sent, isNotEmpty);
      expect(sent.last.buttons, VpadButton.b);
    });
  });
}
