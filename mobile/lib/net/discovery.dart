import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'protocol.dart';

class DiscoveredServer {
  DiscoveredServer({required this.host, required this.port, required this.name});

  final String host;
  final int port;
  final String name;

  String get key => '$host:$port';
}

/// Finds WEMU instances on the local network.
///
/// Two strategies run together: a UDP broadcast probe (fast; Android) and a TCP sweep of the
/// phone's /24 subnet (iOS blocks broadcast without a special entitlement, unicast is allowed).
class Discovery {
  static Future<List<String>> localAddresses() async {
    final result = <String>[];
    try {
      for (final interface in await NetworkInterface.list(type: InternetAddressType.IPv4)) {
        final name = interface.name.toLowerCase();
        // Cellular and VPN interfaces never reach a PC on the Wi-Fi.
        if (name.startsWith('rmnet') || name.startsWith('pdp_ip') || name.startsWith('tun') || name.startsWith('ipsec')) {
          continue;
        }
        for (final address in interface.addresses) {
          if (!address.isLoopback && _isPrivate(address.address)) result.add(address.address);
        }
      }
    } catch (_) {}
    return result;
  }

  static bool _isPrivate(String ip) {
    final parts = ip.split('.').map(int.tryParse).toList();
    if (parts.length != 4 || parts.contains(null)) return false;
    final a = parts[0]!, b = parts[1]!;
    return a == 10 || (a == 172 && b >= 16 && b < 32) || (a == 192 && b == 168);
  }

  /// Emits servers as they answer, completes after [timeout].
  static Stream<DiscoveredServer> scan({Duration timeout = const Duration(seconds: 4)}) {
    final controller = StreamController<DiscoveredServer>();
    final seen = <String>{};
    void found(DiscoveredServer server) {
      if (!controller.isClosed && seen.add(server.key)) controller.add(server);
    }

    Future<void> run() async {
      final addresses = await localAddresses();
      await Future.wait([
        _broadcast(addresses, found, timeout),
        for (final address in addresses) _sweep(address, found, timeout),
      ]);
      await controller.close();
    }

    run();
    return controller.stream;
  }

  static Future<void> _broadcast(List<String> addresses, void Function(DiscoveredServer) found, Duration timeout) async {
    RawDatagramSocket? socket;
    try {
      socket = await RawDatagramSocket.bind(InternetAddress.anyIPv4, 0);
      socket.broadcastEnabled = true;
      socket.listen((event) {
        if (event != RawSocketEvent.read) return;
        final datagram = socket?.receive();
        if (datagram == null) return;
        try {
          final info = jsonDecode(utf8.decode(datagram.data)) as Map<String, dynamic>;
          if (info['service'] != 'wemu') return;
          found(DiscoveredServer(
            host: datagram.address.address,
            port: (info['port'] as num?)?.toInt() ?? Protocol.defaultPort,
            name: info['name'] as String? ?? 'WEMU',
          ));
        } catch (_) {}
      });
      final probe = utf8.encode(Protocol.discoveryProbe);
      final targets = {'255.255.255.255', for (final a in addresses) '${a.substring(0, a.lastIndexOf('.'))}.255'};
      for (var attempt = 0; attempt < 3; attempt++) {
        for (final target in targets) {
          try {
            socket.send(probe, InternetAddress(target), Protocol.discoveryPort);
          } catch (_) {}
        }
        await Future.delayed(const Duration(milliseconds: 400));
      }
      await Future.delayed(timeout - const Duration(milliseconds: 1200));
    } catch (_) {
      // Broadcast unavailable (iOS without the multicast entitlement): the sweep covers it.
    } finally {
      socket?.close();
    }
  }

  static Future<void> _sweep(String local, void Function(DiscoveredServer) found, Duration timeout) async {
    final prefix = local.substring(0, local.lastIndexOf('.'));
    final hosts = [for (var i = 1; i < 255; i++) '$prefix.$i']..remove(local);
    final deadline = DateTime.now().add(timeout);
    const batch = 64;
    for (var start = 0; start < hosts.length && DateTime.now().isBefore(deadline); start += batch) {
      final slice = hosts.sublist(start, (start + batch).clamp(0, hosts.length));
      await Future.wait(slice.map((host) async {
        final server = await probe(host, Protocol.defaultPort, connectTimeout: const Duration(milliseconds: 350));
        if (server != null) found(server);
      }));
    }
  }

  /// Connects to [host] and reads the Info greeting WEMU sends to every new connection.
  static Future<DiscoveredServer?> probe(String host, int port, {Duration connectTimeout = const Duration(seconds: 2)}) async {
    Socket? socket;
    try {
      socket = await Socket.connect(host, port, timeout: connectTimeout);
      final reader = MessageReader();
      final completer = Completer<DiscoveredServer?>();
      final subscription = socket.listen((chunk) {
        try {
          for (final (type, payload) in reader.add(Uint8List.fromList(chunk))) {
            if (type != Protocol.info || completer.isCompleted) continue;
            final info = jsonDecode(utf8.decode(payload)) as Map<String, dynamic>;
            completer.complete(info['service'] == 'wemu'
                ? DiscoveredServer(host: host, port: port, name: info['name'] as String? ?? 'WEMU')
                : null);
          }
        } catch (_) {
          if (!completer.isCompleted) completer.complete(null);
        }
      }, onError: (_) {
        if (!completer.isCompleted) completer.complete(null);
      }, onDone: () {
        if (!completer.isCompleted) completer.complete(null);
      });
      final result = await completer.future.timeout(const Duration(milliseconds: 1200), onTimeout: () => null);
      await subscription.cancel();
      return result;
    } catch (_) {
      return null;
    } finally {
      socket?.destroy();
    }
  }
}
