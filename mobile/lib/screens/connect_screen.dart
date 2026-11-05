import 'dart:async';
import 'package:flutter/material.dart';

import '../main.dart';
import '../net/discovery.dart';
import '../net/protocol.dart';
import '../net/wemu_client.dart';
import '../storage.dart';
import '../strings.dart';
import '../theme.dart';
import '../widgets/logo.dart';

/// First screen: find WEMU on the Wi-Fi (or type its address) and connect.
class ConnectScreen extends StatefulWidget {
  const ConnectScreen({super.key});

  @override
  State<ConnectScreen> createState() => _ConnectScreenState();
}

class _ConnectScreenState extends State<ConnectScreen> with SingleTickerProviderStateMixin {
  final List<DiscoveredServer> _found = [];
  StreamSubscription<DiscoveredServer>? _scan;
  bool _scanning = false;
  late final AnimationController _pulse = AnimationController(vsync: this, duration: const Duration(milliseconds: 2200));

  @override
  void initState() {
    super.initState();
    _startScan();
  }

  @override
  void dispose() {
    _scan?.cancel();
    _pulse.dispose();
    super.dispose();
  }

  void _startScan() {
    _scan?.cancel();
    setState(() {
      _found.clear();
      _scanning = true;
    });
    _pulse.repeat();
    _scan = Discovery.scan().listen(
      (server) => setState(() => _found.add(server)),
      onDone: () {
        if (!mounted) return;
        setState(() => _scanning = false);
        _pulse.stop();
      },
    );
  }

  void _connect(String host, int port, String name) {
    final scope = AppScope.of(context);
    final saved = scope.storage.server('$host:$port');
    scope.client.connect(saved ?? SavedServer(host: host, port: port, name: name));
  }

  Future<void> _manualEntry() async {
    final controller = TextEditingController();
    final result = await showModalBottomSheet<String>(
      context: context,
      isScrollControlled: true,
      backgroundColor: WemuColors.surface,
      shape: const RoundedRectangleBorder(borderRadius: BorderRadius.vertical(top: Radius.circular(28))),
      builder: (context) => Padding(
        padding: EdgeInsets.fromLTRB(24, 12, 24, MediaQuery.of(context).viewInsets.bottom + 24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Center(child: Container(width: 40, height: 4, decoration: BoxDecoration(color: WemuColors.outline, borderRadius: BorderRadius.circular(2)))),
            const SizedBox(height: 20),
            Text(S.manual, style: const TextStyle(fontSize: 20, fontWeight: FontWeight.w800)),
            const SizedBox(height: 16),
            TextField(
              controller: controller,
              autofocus: true,
              keyboardType: const TextInputType.numberWithOptions(decimal: true),
              decoration: InputDecoration(hintText: S.manualHint, prefixIcon: const Icon(Icons.lan_outlined)),
              onSubmitted: (value) => Navigator.pop(context, value),
            ),
            const SizedBox(height: 16),
            FilledButton(onPressed: () => Navigator.pop(context, controller.text), child: Text(S.connect)),
          ],
        ),
      ),
    );
    final text = result?.trim() ?? '';
    if (text.isEmpty) return;
    final parts = text.split(':');
    final port = parts.length > 1 ? int.tryParse(parts[1]) ?? Protocol.defaultPort : Protocol.defaultPort;
    _connect(parts[0], port, parts[0]);
  }

  @override
  Widget build(BuildContext context) {
    final scope = AppScope.of(context);
    final client = scope.client;
    final connecting = client.status == ConnectionStatus.connecting;
    final foundKeys = _found.map((s) => s.key).toSet();
    final recent = scope.storage.servers.where((s) => !foundKeys.contains(s.key)).toList();

    return Scaffold(
      body: GlowBackground(
        child: SafeArea(
          child: CustomScrollView(
            slivers: [
              SliverToBoxAdapter(
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(24, 36, 24, 8),
                  child: Column(
                    children: [
                      SizedBox(
                        height: 150,
                        child: AnimatedBuilder(
                          animation: _pulse,
                          builder: (context, child) => CustomPaint(painter: _RadarPainter(_scanning ? _pulse.value : null), child: child),
                          child: const Center(child: GamepadLogo(size: 128)),
                        ),
                      ),
                      const SizedBox(height: 20),
                      Text(S.appName, style: const TextStyle(fontSize: 30, fontWeight: FontWeight.w900, letterSpacing: -0.8)),
                      const SizedBox(height: 6),
                      Text(S.tagline, textAlign: TextAlign.center, style: const TextStyle(color: WemuColors.muted, fontSize: 15)),
                    ],
                  ),
                ),
              ),
              if (client.error != null && !connecting)
                SliverToBoxAdapter(
                  child: Padding(
                    padding: const EdgeInsets.fromLTRB(20, 16, 20, 0),
                    child: _Banner(icon: Icons.error_outline, color: WemuColors.danger, text: S.error(client.error)),
                  ),
                ),
              _sectionHeader(S.nearby, trailing: _scanning
                  ? const SizedBox(width: 16, height: 16, child: CircularProgressIndicator(strokeWidth: 2))
                  : TextButton.icon(onPressed: _startScan, icon: const Icon(Icons.refresh, size: 18), label: Text(S.searchAgain))),
              if (_found.isEmpty)
                SliverToBoxAdapter(
                  child: Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 20),
                    child: Card(
                      child: Padding(
                        padding: const EdgeInsets.all(20),
                        child: Row(
                          children: [
                            Icon(_scanning ? Icons.wifi_find : Icons.wifi_off, color: WemuColors.muted),
                            const SizedBox(width: 16),
                            Expanded(child: Text(_scanning ? S.searching : S.noneFound, style: const TextStyle(color: WemuColors.muted, height: 1.35))),
                          ],
                        ),
                      ),
                    ),
                  ),
                ),
              SliverList.separated(
                itemCount: _found.length,
                separatorBuilder: (_, _) => const SizedBox(height: 10),
                itemBuilder: (context, i) {
                  final server = _found[i];
                  final saved = scope.storage.server(server.key);
                  return _ServerTile(
                    name: server.name,
                    address: server.host,
                    paired: saved?.token != null,
                    busy: connecting && client.server?.key == server.key,
                    onTap: connecting ? null : () => _connect(server.host, server.port, server.name),
                  );
                },
              ),
              if (recent.isNotEmpty) _sectionHeader(S.known),
              SliverList.separated(
                itemCount: recent.length,
                separatorBuilder: (_, _) => const SizedBox(height: 10),
                itemBuilder: (context, i) {
                  final server = recent[i];
                  return _ServerTile(
                    name: server.name,
                    address: server.host,
                    paired: server.token != null,
                    offline: true,
                    busy: connecting && client.server?.key == server.key,
                    onTap: connecting ? null : () => scope.client.connect(server),
                    onForget: () => scope.storage.forgetServer(server.key),
                  );
                },
              ),
              SliverToBoxAdapter(
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(20, 24, 20, 32),
                  child: OutlinedButton.icon(onPressed: connecting ? null : _manualEntry, icon: const Icon(Icons.keyboard_alt_outlined), label: Text(S.manual)),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _sectionHeader(String title, {Widget? trailing}) => SliverToBoxAdapter(
        child: Padding(
          padding: const EdgeInsets.fromLTRB(24, 28, 16, 12),
          child: Row(
            children: [
              Text(title.toUpperCase(), style: const TextStyle(color: WemuColors.muted, fontWeight: FontWeight.w800, fontSize: 12, letterSpacing: 1.4)),
              const Spacer(),
              ?trailing,
            ],
          ),
        ),
      );
}

class _ServerTile extends StatelessWidget {
  const _ServerTile({
    required this.name,
    required this.address,
    required this.paired,
    required this.onTap,
    this.busy = false,
    this.offline = false,
    this.onForget,
  });

  final String name, address;
  final bool paired, busy, offline;
  final VoidCallback? onTap;
  final VoidCallback? onForget;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 20),
      child: Card(
        clipBehavior: Clip.antiAlias,
        child: InkWell(
          onTap: onTap,
          onLongPress: onForget,
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Row(
              children: [
                Container(
                  width: 52,
                  height: 52,
                  decoration: BoxDecoration(
                    color: offline ? WemuColors.surfaceHigh : WemuColors.accentSoft,
                    borderRadius: BorderRadius.circular(16),
                  ),
                  child: Icon(Icons.desktop_windows_rounded, color: offline ? WemuColors.muted : WemuColors.accent),
                ),
                const SizedBox(width: 16),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(name, maxLines: 1, overflow: TextOverflow.ellipsis, style: const TextStyle(fontSize: 17, fontWeight: FontWeight.w800)),
                      const SizedBox(height: 3),
                      Row(
                        children: [
                          Text(address, style: const TextStyle(color: WemuColors.muted, fontFamily: 'monospace', fontSize: 13)),
                          if (paired) ...[
                            const SizedBox(width: 8),
                            Container(
                              padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 2),
                              decoration: BoxDecoration(color: WemuColors.success.withValues(alpha: 0.15), borderRadius: BorderRadius.circular(6)),
                              child: Text(S.paired, style: const TextStyle(color: WemuColors.success, fontSize: 11, fontWeight: FontWeight.w800)),
                            ),
                          ],
                        ],
                      ),
                    ],
                  ),
                ),
                if (busy)
                  const SizedBox(width: 22, height: 22, child: CircularProgressIndicator(strokeWidth: 2.5))
                else if (onForget != null)
                  IconButton(onPressed: onForget, tooltip: S.forget, icon: const Icon(Icons.close_rounded, color: WemuColors.muted))
                else
                  const Icon(Icons.chevron_right_rounded, color: WemuColors.muted),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _Banner extends StatelessWidget {
  const _Banner({required this.icon, required this.color, required this.text});

  final IconData icon;
  final Color color;
  final String text;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: color.withValues(alpha: 0.4)),
      ),
      child: Row(
        children: [
          Icon(icon, color: color),
          const SizedBox(width: 12),
          Expanded(child: Text(text, style: TextStyle(color: color, fontWeight: FontWeight.w600))),
        ],
      ),
    );
  }
}

/// Expanding rings while the network is being scanned.
class _RadarPainter extends CustomPainter {
  _RadarPainter(this.progress);

  final double? progress;

  @override
  void paint(Canvas canvas, Size size) {
    final p = progress;
    if (p == null) return;
    final center = size.center(Offset.zero);
    final maxRadius = size.height * 0.7;
    for (var i = 0; i < 3; i++) {
      final t = (p + i / 3) % 1.0;
      canvas.drawCircle(
        center,
        40 + t * maxRadius,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 2
          ..color = WemuColors.accent.withValues(alpha: (1 - t) * 0.35),
      );
    }
  }

  @override
  bool shouldRepaint(_RadarPainter oldDelegate) => oldDelegate.progress != progress;
}
