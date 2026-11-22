import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:wemu_gamepad/net/protocol.dart';

void main() {
  test('input sample matches the 52-byte layout WEMU parses', () {
    final sample = InputSample()
      ..buttons = VpadButton.a | VpadButton.zr
      ..lx = -0.5
      ..ry = 1
      ..touched = true
      ..touchX = 853
      ..touchY = 479
      ..gz = 0.25;
    final bytes = ByteData.sublistView(sample.encode());
    expect(bytes.lengthInBytes, 52);
    expect(bytes.getUint32(0, Endian.little), 0x8040);
    expect(bytes.getFloat32(4, Endian.little), -0.5);
    expect(bytes.getFloat32(16, Endian.little), 1.0);
    expect(bytes.getUint8(20), 1);
    expect(bytes.getUint16(22, Endian.little), 853);
    expect(bytes.getUint16(24, Endian.little), 479);
    expect(bytes.getFloat32(32, Endian.little), -1.0); // neutral gravity on y
    expect(bytes.getFloat32(48, Endian.little), 0.25);
  });

  test('messages are framed as length, type, payload', () {
    final message = Protocol.json(Protocol.command, {'cmd': 'stop'});
    final view = ByteData.sublistView(message);
    expect(view.getUint32(0, Endian.little), message.length - 4);
    expect(message[4], Protocol.command);
    expect(jsonDecode(utf8.decode(message.sublist(5))), {'cmd': 'stop'});
  });

  test('reader reassembles split and coalesced messages', () {
    final a = Protocol.message(Protocol.pong, List.filled(8, 7));
    final b = Protocol.json(Protocol.session, {'state': 'running'});
    final stream = Uint8List.fromList([...a, ...b]);
    final reader = MessageReader();
    final received = <(int, Uint8List)>[];
    for (var i = 0; i < stream.length; i += 3) {
      received.addAll(reader.add(Uint8List.sublistView(stream, i, (i + 3).clamp(0, stream.length))));
    }
    expect(received.map((m) => m.$1), [Protocol.pong, Protocol.session]);
    expect(received[0].$2, List.filled(8, 7));
    expect(jsonDecode(utf8.decode(received[1].$2)), {'state': 'running'});
    expect(reader.add(Uint8List(0)), isEmpty);
  });

  test('reader rejects impossible lengths', () {
    final bad = Uint8List(5)..buffer.asByteData().setUint32(0, 0, Endian.little);
    expect(() => MessageReader().add(bad), throwsFormatException);
  });
}
