import 'package:flutter/material.dart';

import '../main.dart';
import '../storage.dart';
import '../strings.dart';
import '../theme.dart';
import '../widgets/logo.dart';

class SettingsScreen extends StatelessWidget {
  const SettingsScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final scope = AppScope.of(context);
    final storage = scope.storage;
    return Scaffold(
      appBar: AppBar(title: Text(S.settings)),
      body: ListenableBuilder(
        listenable: storage,
        builder: (context, _) => ListView(
          padding: const EdgeInsets.fromLTRB(20, 8, 20, 32),
          children: [
            _Section(S.controls),
            _Group(children: [
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 16, 16, 8),
                child: SegmentedButton<ControlsLayout>(
                  showSelectedIcon: false,
                  segments: [
                    ButtonSegment(value: ControlsLayout.sides, icon: const Icon(Icons.view_column_rounded), label: Text(S.layoutSides, textAlign: TextAlign.center)),
                    ButtonSegment(value: ControlsLayout.overlay, icon: const Icon(Icons.layers_rounded), label: Text(S.layoutOverlay, textAlign: TextAlign.center)),
                    ButtonSegment(value: ControlsLayout.screenOnly, icon: const Icon(Icons.touch_app_rounded), label: Text(S.layoutScreenOnly, textAlign: TextAlign.center)),
                  ],
                  selected: {storage.layout},
                  onSelectionChanged: (value) => storage.layout = value.first,
                ),
              ),
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 0, 16, 8),
                child: Text(S.layoutHelp, style: const TextStyle(color: WemuColors.muted, fontSize: 13)),
              ),
              _SliderTile(
                label: S.controlsSize,
                value: storage.controlsScale,
                min: 0.75,
                max: 1.3,
                format: (v) => '${(v * 100).round()} %',
                onChanged: (v) => storage.controlsScale = v,
              ),
              if (storage.layout == ControlsLayout.overlay)
                _SliderTile(
                  label: S.overlayOpacity,
                  value: storage.overlayOpacity,
                  min: 0.15,
                  max: 0.9,
                  format: (v) => '${(v * 100).round()} %',
                  onChanged: (v) => storage.overlayOpacity = v,
                ),
              SwitchListTile(title: Text(S.haptics), value: storage.haptics, onChanged: (v) => storage.haptics = v),
              SwitchListTile(title: Text(S.motion), value: storage.motion, onChanged: (v) => storage.motion = v),
              SwitchListTile(title: Text(S.flip), value: storage.flipLandscape, onChanged: (v) => storage.flipLandscape = v),
            ]),
            _Section(S.video),
            _Group(children: [
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 16, 16, 4),
                child: Align(alignment: Alignment.centerLeft, child: Text(S.quality, style: const TextStyle(fontWeight: FontWeight.w600))),
              ),
              Padding(
                padding: const EdgeInsets.fromLTRB(16, 8, 16, 12),
                child: SegmentedButton<VideoQuality>(
                  showSelectedIcon: false,
                  segments: [
                    ButtonSegment(value: VideoQuality.low, label: Text(S.qualityLow)),
                    ButtonSegment(value: VideoQuality.medium, label: Text(S.qualityMedium)),
                    ButtonSegment(value: VideoQuality.high, label: Text(S.qualityHigh)),
                  ],
                  selected: {storage.quality},
                  onSelectionChanged: (value) {
                    storage.quality = value.first;
                    scope.client.refreshQuality();
                  },
                ),
              ),
              SwitchListTile(title: Text(S.stats), value: storage.showStats, onChanged: (v) => storage.showStats = v),
            ]),
            _Section(S.about),
            _Group(children: [
              Padding(
                padding: const EdgeInsets.all(20),
                child: Row(
                  children: [
                    const GamepadLogo(size: 72, glow: false),
                    const SizedBox(width: 16),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(S.appName, style: const TextStyle(fontWeight: FontWeight.w900, fontSize: 17)),
                          const SizedBox(height: 4),
                          Text(S.tagline, style: const TextStyle(color: WemuColors.muted)),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            ]),
          ],
        ),
      ),
    );
  }
}

class _Section extends StatelessWidget {
  const _Section(this.title);

  final String title;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.fromLTRB(4, 24, 4, 10),
        child: Text(title.toUpperCase(), style: const TextStyle(color: WemuColors.muted, fontWeight: FontWeight.w800, fontSize: 12, letterSpacing: 1.4)),
      );
}

class _Group extends StatelessWidget {
  const _Group({required this.children});

  final List<Widget> children;

  @override
  Widget build(BuildContext context) => Card(clipBehavior: Clip.antiAlias, child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: children));
}

class _SliderTile extends StatelessWidget {
  const _SliderTile({required this.label, required this.value, required this.min, required this.max, required this.format, required this.onChanged});

  final String label;
  final double value, min, max;
  final String Function(double) format;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.fromLTRB(16, 4, 8, 0),
        child: Row(
          children: [
            Expanded(flex: 4, child: Text(label, style: const TextStyle(fontWeight: FontWeight.w600))),
            Expanded(flex: 6, child: Slider(value: value.clamp(min, max), min: min, max: max, onChanged: onChanged)),
            SizedBox(width: 52, child: Text(format(value), style: const TextStyle(color: WemuColors.muted))),
          ],
        ),
      );
}
