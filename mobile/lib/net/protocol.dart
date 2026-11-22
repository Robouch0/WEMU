import 'dart:convert';
import 'dart:typed_data';

/// Wire protocol shared with WEMU (gui/src/remote/RemoteProtocol.hpp).
/// TCP, little-endian. Every message: u32 length (type byte + payload), u8 type, payload.
class Protocol {
  static const int version = 1;
  static const int defaultPort = 47470;
  static const int discoveryPort = 47471;
  static const String discoveryProbe = 'WEMU_DISCOVER';
  static const int maximumServerMessage = 8 * 1024 * 1024;

  // phone -> WEMU
  static const int hello = 0x01;
  static const int input = 0x02;
  static const int frameAck = 0x03;
  static const int command = 0x04;
  static const int ping = 0x05;

  // WEMU -> phone
  static const int info = 0x80;
  static const int welcome = 0x81;
  static const int reject = 0x82;
  static const int library = 0x83;
  static const int icon = 0x84;
  static const int session = 0x85;
  static const int frame = 0x86;
  static const int pong = 0x87;
  static const int notice = 0x88;

  static Uint8List message(int type, [List<int> payload = const []]) {
    final out = Uint8List(5 + payload.length);
    final view = ByteData.sublistView(out);
    view.setUint32(0, payload.length + 1, Endian.little);
    out[4] = type;
    out.setRange(5, out.length, payload);
    return out;
  }

  static Uint8List json(int type, Map<String, Object?> body) => message(type, utf8.encode(jsonEncode(body)));
}

/// Wii U GamePad (VPAD) button bits.
class VpadButton {
  static const int a = 0x8000;
  static const int b = 0x4000;
  static const int x = 0x2000;
  static const int y = 0x1000;
  static const int left = 0x0800;
  static const int right = 0x0400;
  static const int up = 0x0200;
  static const int down = 0x0100;
  static const int zl = 0x0080;
  static const int zr = 0x0040;
  static const int l = 0x0020;
  static const int r = 0x0010;
  static const int plus = 0x0008;
  static const int minus = 0x0004;
  static const int home = 0x0002;
  static const int stickR = 0x00020000;
  static const int stickL = 0x00040000;
}

/// One GamePad sample: 52 bytes on the wire (see RemoteProtocol::parseInput).
class InputSample {
  int buttons = 0;
  double lx = 0, ly = 0, rx = 0, ry = 0;
  bool touched = false;
  int touchX = 0, touchY = 0;
  // Gravity in g and angular rate in revolutions/second, GamePad axes (x right, y up, z out of the screen).
  double ax = 0, ay = -1, az = 0;
  double gx = 0, gy = 0, gz = 0;

  Uint8List encode() {
    final data = ByteData(52);
    data.setUint32(0, buttons, Endian.little);
    data.setFloat32(4, lx, Endian.little);
    data.setFloat32(8, ly, Endian.little);
    data.setFloat32(12, rx, Endian.little);
    data.setFloat32(16, ry, Endian.little);
    data.setUint8(20, touched ? 1 : 0);
    data.setUint16(22, touchX, Endian.little);
    data.setUint16(24, touchY, Endian.little);
    data.setFloat32(28, ax, Endian.little);
    data.setFloat32(32, ay, Endian.little);
    data.setFloat32(36, az, Endian.little);
    data.setFloat32(40, gx, Endian.little);
    data.setFloat32(44, gy, Endian.little);
    data.setFloat32(48, gz, Endian.little);
    return data.buffer.asUint8List();
  }
}

/// Incremental parser for the server's message stream.
class MessageReader {
  final BytesBuilder _buffer = BytesBuilder(copy: false);
  Uint8List _pending = Uint8List(0);

  /// Feeds received bytes, returns every complete (type, payload) message.
  List<(int, Uint8List)> add(Uint8List chunk) {
    if (_pending.isNotEmpty) {
      _buffer.add(_pending);
      _buffer.add(chunk);
      _pending = _buffer.takeBytes();
    } else {
      _pending = chunk;
    }
    final messages = <(int, Uint8List)>[];
    var offset = 0;
    while (_pending.length - offset >= 5) {
      final view = ByteData.sublistView(_pending, offset);
      final size = view.getUint32(0, Endian.little);
      if (size < 1 || size > Protocol.maximumServerMessage) {
        throw const FormatException('Invalid message length');
      }
      if (_pending.length - offset < size + 4) break;
      final type = _pending[offset + 4];
      messages.add((type, Uint8List.sublistView(_pending, offset + 5, offset + 4 + size)));
      offset += size + 4;
    }
    // Keep the views valid: the remainder is copied, consumed bytes are released with the old buffer.
    _pending = offset == 0 ? _pending : Uint8List.fromList(Uint8List.sublistView(_pending, offset));
    return messages;
  }
}
