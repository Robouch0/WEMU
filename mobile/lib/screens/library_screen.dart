import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../main.dart';
import '../net/wemu_client.dart';
import '../strings.dart';
import '../theme.dart';
import 'settings_screen.dart';

/// The PC's game library; launching a game opens the GamePad view automatically.
class LibraryScreen extends StatelessWidget {
  const LibraryScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final client = AppScope.of(context).client;
    final reconnecting = client.status == ConnectionStatus.reconnecting;
    Game? current;
    for (final game in client.games) {
      if (game.id == client.sessionGameId) current = game;
    }

    return Scaffold(
      body: GlowBackground(
        child: SafeArea(
          bottom: false,
          child: CustomScrollView(
            slivers: [
              SliverAppBar(
                pinned: true,
                backgroundColor: WemuColors.background.withValues(alpha: 0.92),
                toolbarHeight: 72,
                title: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(client.server?.name ?? 'WEMU'),
                    const SizedBox(height: 2),
                    Row(
                      children: [
                        _StatusDot(color: reconnecting ? WemuColors.warning : WemuColors.success),
                        const SizedBox(width: 6),
                        Flexible(
                          child: Text(
                            reconnecting ? S.reconnecting : '${client.server?.host ?? ''}${client.latencyMs != null ? ' · ${client.latencyMs} ms' : ''}',
                            overflow: TextOverflow.ellipsis,
                            style: const TextStyle(fontSize: 13, color: WemuColors.muted, fontWeight: FontWeight.w500),
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
                actions: [
                  IconButton(
                    tooltip: S.settings,
                    icon: const Icon(Icons.tune_rounded),
                    onPressed: () => Navigator.push(context, MaterialPageRoute(builder: (_) => const SettingsScreen())),
                  ),
                  IconButton(tooltip: S.disconnect, icon: const Icon(Icons.logout_rounded), onPressed: client.disconnect),
                  const SizedBox(width: 4),
                ],
              ),
              if (client.inGame)
                SliverToBoxAdapter(
                  child: Padding(
                    padding: const EdgeInsets.fromLTRB(20, 8, 20, 8),
                    child: _NowPlaying(client: client, game: current),
                  ),
                ),
              SliverToBoxAdapter(
                child: Padding(
                  padding: const EdgeInsets.fromLTRB(24, 20, 24, 12),
                  child: Row(
                    children: [
                      Text(S.library.toUpperCase(), style: const TextStyle(color: WemuColors.muted, fontWeight: FontWeight.w800, fontSize: 12, letterSpacing: 1.4)),
                      const SizedBox(width: 8),
                      Container(
                        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
                        decoration: BoxDecoration(color: WemuColors.surfaceHigh, borderRadius: BorderRadius.circular(8)),
                        child: Text('${client.games.length}', style: const TextStyle(fontWeight: FontWeight.w800, fontSize: 12)),
                      ),
                    ],
                  ),
                ),
              ),
              if (client.games.isEmpty)
                SliverToBoxAdapter(
                  child: Padding(
                    padding: const EdgeInsets.all(20),
                    child: Card(
                      child: Padding(
                        padding: const EdgeInsets.all(24),
                        child: Column(
                          children: [
                            const Icon(Icons.videogame_asset_off_outlined, size: 40, color: WemuColors.muted),
                            const SizedBox(height: 12),
                            Text(S.emptyLibrary, textAlign: TextAlign.center, style: const TextStyle(color: WemuColors.muted, height: 1.4)),
                          ],
                        ),
                      ),
                    ),
                  ),
                ),
              SliverPadding(
                padding: const EdgeInsets.fromLTRB(20, 0, 20, 32),
                sliver: SliverGrid.builder(
                  gridDelegate: const SliverGridDelegateWithMaxCrossAxisExtent(
                    maxCrossAxisExtent: 200,
                    mainAxisSpacing: 16,
                    crossAxisSpacing: 16,
                    childAspectRatio: 0.78,
                  ),
                  itemCount: client.games.length,
                  itemBuilder: (context, i) => _GameCard(
                    game: client.games[i],
                    playing: client.games[i].id == client.sessionGameId && client.inGame,
                    onTap: () => _showGame(context, client, client.games[i]),
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  void _showGame(BuildContext context, WemuClient client, Game game) {
    HapticFeedback.selectionClick();
    showModalBottomSheet(
      context: context,
      backgroundColor: WemuColors.surface,
      shape: const RoundedRectangleBorder(borderRadius: BorderRadius.vertical(top: Radius.circular(28))),
      builder: (sheetContext) => SafeArea(
        child: Padding(
          padding: const EdgeInsets.fromLTRB(24, 12, 24, 24),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Center(child: Container(width: 40, height: 4, decoration: BoxDecoration(color: WemuColors.outline, borderRadius: BorderRadius.circular(2)))),
              const SizedBox(height: 20),
              Row(
                children: [
                  ClipRRect(borderRadius: BorderRadius.circular(18), child: SizedBox.square(dimension: 96, child: _Cover(game: game))),
                  const SizedBox(width: 18),
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(game.name, style: const TextStyle(fontSize: 22, fontWeight: FontWeight.w900, height: 1.15)),
                        if (game.publisher.isNotEmpty) ...[
                          const SizedBox(height: 4),
                          Text(game.publisher, style: const TextStyle(color: WemuColors.muted, fontWeight: FontWeight.w600)),
                        ],
                        const SizedBox(height: 8),
                        Wrap(spacing: 6, runSpacing: 6, children: [
                          if (game.version.isNotEmpty) _Chip('v${game.version}'),
                          if (game.titleId.isNotEmpty) _Chip(game.titleId),
                        ]),
                      ],
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 24),
              FilledButton.icon(
                onPressed: client.inGame
                    ? null
                    : () {
                        HapticFeedback.mediumImpact();
                        client.launch(game);
                        Navigator.pop(sheetContext);
                      },
                icon: const Icon(Icons.play_arrow_rounded, size: 28),
                label: Text(client.inGame ? S.alreadyRunning : S.play),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _NowPlaying extends StatelessWidget {
  const _NowPlaying({required this.client, required this.game});

  final WemuClient client;
  final Game? game;

  @override
  Widget build(BuildContext context) {
    final starting = client.session == SessionState.starting;
    return Container(
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(24),
        gradient: const LinearGradient(colors: [Color(0xFF0F5C86), Color(0xFF173A5E)], begin: Alignment.topLeft, end: Alignment.bottomRight),
        boxShadow: [BoxShadow(color: WemuColors.accent.withValues(alpha: 0.25), blurRadius: 30, offset: const Offset(0, 10))],
      ),
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              ClipRRect(
                borderRadius: BorderRadius.circular(14),
                child: SizedBox.square(dimension: 64, child: game != null ? _Cover(game: game!) : const ColoredBox(color: Colors.black26)),
              ),
              const SizedBox(width: 14),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      (starting ? S.starting : client.session == SessionState.paused ? S.gamePaused : S.nowPlaying).toUpperCase(),
                      style: const TextStyle(fontSize: 11, fontWeight: FontWeight.w900, letterSpacing: 1.3, color: Color(0xFFB9E8FF)),
                    ),
                    const SizedBox(height: 4),
                    Text(client.sessionTitle, maxLines: 2, overflow: TextOverflow.ellipsis, style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w900)),
                  ],
                ),
              ),
              if (starting) const SizedBox(width: 22, height: 22, child: CircularProgressIndicator(strokeWidth: 2.5, color: Colors.white)),
            ],
          ),
          const SizedBox(height: 14),
          Row(
            children: [
              Expanded(
                child: FilledButton.icon(
                  style: FilledButton.styleFrom(backgroundColor: Colors.white, foregroundColor: const Color(0xFF0B2A40)),
                  onPressed: () => openGamepad(context),
                  icon: const Icon(Icons.gamepad_rounded),
                  label: Text(S.openGamepad),
                ),
              ),
              const SizedBox(width: 10),
              IconButton.filledTonal(
                style: IconButton.styleFrom(minimumSize: const Size(52, 52), backgroundColor: Colors.white24),
                tooltip: S.stop,
                onPressed: () async {
                  final ok = await confirmStop(context);
                  if (ok) client.stopGame();
                },
                icon: const Icon(Icons.stop_rounded, color: Colors.white),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

Future<bool> confirmStop(BuildContext context) async {
  return await showDialog<bool>(
        context: context,
        builder: (context) => AlertDialog(
          backgroundColor: WemuColors.surface,
          title: Text(S.stopConfirm),
          actions: [
            TextButton(onPressed: () => Navigator.pop(context, false), child: Text(S.cancel)),
            FilledButton(
              style: FilledButton.styleFrom(backgroundColor: WemuColors.danger, foregroundColor: Colors.white, minimumSize: const Size(0, 44)),
              onPressed: () => Navigator.pop(context, true),
              child: Text(S.stop),
            ),
          ],
        ),
      ) ??
      false;
}

class _GameCard extends StatelessWidget {
  const _GameCard({required this.game, required this.playing, required this.onTap});

  final Game game;
  final bool playing;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Material(
      color: WemuColors.surface,
      borderRadius: BorderRadius.circular(22),
      clipBehavior: Clip.antiAlias,
      child: InkWell(
        onTap: onTap,
        child: Container(
          decoration: BoxDecoration(
            borderRadius: BorderRadius.circular(22),
            border: Border.all(color: playing ? WemuColors.accent : WemuColors.outline, width: playing ? 2 : 1),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Expanded(
                child: Stack(
                  fit: StackFit.expand,
                  children: [
                    _Cover(game: game),
                    if (playing)
                      Positioned(
                        top: 10,
                        left: 10,
                        child: Container(
                          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                          decoration: BoxDecoration(color: WemuColors.accent, borderRadius: BorderRadius.circular(8)),
                          child: const Icon(Icons.play_arrow_rounded, size: 16, color: Color(0xFF04131C)),
                        ),
                      ),
                  ],
                ),
              ),
              Padding(
                padding: const EdgeInsets.fromLTRB(12, 10, 12, 12),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(game.name, maxLines: 1, overflow: TextOverflow.ellipsis, style: const TextStyle(fontWeight: FontWeight.w800, fontSize: 15)),
                    const SizedBox(height: 2),
                    Text(
                      game.publisher.isNotEmpty ? game.publisher : ' ',
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(color: WemuColors.muted, fontSize: 12, fontWeight: FontWeight.w600),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// Title icon from WEMU, or a generated monogram while it loads / when there is none.
class _Cover extends StatelessWidget {
  const _Cover({required this.game});

  final Game game;

  @override
  Widget build(BuildContext context) {
    final icon = game.icon;
    if (icon != null) return Image.memory(icon, fit: BoxFit.cover, gaplessPlayback: true, filterQuality: FilterQuality.medium);
    final hue = (game.name.hashCode % 360).abs().toDouble();
    return DecoratedBox(
      decoration: BoxDecoration(
        gradient: LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [HSLColor.fromAHSL(1, hue, 0.55, 0.42).toColor(), HSLColor.fromAHSL(1, (hue + 40) % 360, 0.6, 0.22).toColor()],
        ),
      ),
      child: Center(
        child: Text(
          game.name.isNotEmpty ? game.name.characters.first.toUpperCase() : '?',
          style: const TextStyle(fontSize: 44, fontWeight: FontWeight.w900, color: Colors.white70),
        ),
      ),
    );
  }
}

class _Chip extends StatelessWidget {
  const _Chip(this.text);

  final String text;

  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
        decoration: BoxDecoration(color: WemuColors.surfaceHigh, borderRadius: BorderRadius.circular(8)),
        child: Text(text, style: const TextStyle(fontSize: 12, fontFamily: 'monospace', color: WemuColors.muted)),
      );
}

class _StatusDot extends StatelessWidget {
  const _StatusDot({required this.color});

  final Color color;

  @override
  Widget build(BuildContext context) => Container(
        width: 8,
        height: 8,
        decoration: BoxDecoration(color: color, shape: BoxShape.circle, boxShadow: [BoxShadow(color: color.withValues(alpha: 0.6), blurRadius: 6)]),
      );
}
