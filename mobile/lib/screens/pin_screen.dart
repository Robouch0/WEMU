import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../main.dart';
import '../net/wemu_client.dart';
import '../strings.dart';
import '../theme.dart';

/// Pairing: the 4-digit PIN WEMU shows, typed on a large keypad.
class PinScreen extends StatefulWidget {
  const PinScreen({super.key});

  @override
  State<PinScreen> createState() => _PinScreenState();
}

class _PinScreenState extends State<PinScreen> with SingleTickerProviderStateMixin {
  String _pin = '';
  bool _submitted = false;
  late final AnimationController _shake = AnimationController(vsync: this, duration: const Duration(milliseconds: 450));

  WemuClient? _client;

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    final client = AppScope.of(context).client;
    if (client != _client) {
      _client?.removeListener(_onClient);
      _client = client..addListener(_onClient);
    }
  }

  @override
  void dispose() {
    _client?.removeListener(_onClient);
    _shake.dispose();
    super.dispose();
  }

  void _onClient() {
    final client = _client!;
    if (client.status == ConnectionStatus.needPin && _submitted) {
      // The PC answered: wrong PIN (or locked). Clear and let the user retry.
      _pin = '';
      _submitted = false;
      if (client.pinError != null) {
        _shake.forward(from: 0);
        HapticFeedback.heavyImpact();
      }
    }
    if (mounted) setState(() {});
  }

  void _press(String digit) {
    if (_submitted || _pin.length >= 4) return;
    HapticFeedback.selectionClick();
    setState(() => _pin += digit);
    if (_pin.length == 4) {
      _submitted = true;
      Future.delayed(const Duration(milliseconds: 150), () {
        if (mounted) AppScope.of(context).client.submitPin(_pin);
      });
    }
  }

  void _erase() {
    if (_submitted || _pin.isEmpty) return;
    HapticFeedback.selectionClick();
    setState(() => _pin = _pin.substring(0, _pin.length - 1));
  }

  @override
  Widget build(BuildContext context) {
    final client = AppScope.of(context).client;
    final error = _submitted ? null : client.pinError;
    return Scaffold(
      appBar: AppBar(
        leading: IconButton(icon: const Icon(Icons.arrow_back_rounded), onPressed: client.disconnect),
      ),
      body: SafeArea(
        child: LayoutBuilder(builder: (context, constraints) {
          final keySize = math.min(84.0, math.min(constraints.maxWidth / 4.2, constraints.maxHeight / 8));
          return Column(
            children: [
              const SizedBox(height: 8),
              Text(S.pairTitle, style: const TextStyle(color: WemuColors.muted, fontSize: 15, fontWeight: FontWeight.w600)),
              const SizedBox(height: 4),
              Text(client.server?.name ?? 'WEMU', style: const TextStyle(fontSize: 26, fontWeight: FontWeight.w900)),
              Padding(
                padding: const EdgeInsets.fromLTRB(32, 12, 32, 0),
                child: Text(S.pairHelp, textAlign: TextAlign.center, style: const TextStyle(color: WemuColors.muted, height: 1.4)),
              ),
              const Spacer(),
              AnimatedBuilder(
                animation: _shake,
                builder: (context, child) => Transform.translate(
                  offset: Offset(math.sin(_shake.value * math.pi * 6) * 12 * (1 - _shake.value), 0),
                  child: child,
                ),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    for (var i = 0; i < 4; i++)
                      AnimatedContainer(
                        duration: const Duration(milliseconds: 160),
                        margin: const EdgeInsets.symmetric(horizontal: 8),
                        width: 58,
                        height: 70,
                        alignment: Alignment.center,
                        decoration: BoxDecoration(
                          color: WemuColors.surface,
                          borderRadius: BorderRadius.circular(16),
                          border: Border.all(
                            width: 2,
                            color: error != null && _pin.isEmpty
                                ? WemuColors.danger
                                : i == _pin.length
                                    ? WemuColors.accent
                                    : WemuColors.outline,
                          ),
                        ),
                        child: i < _pin.length
                            ? Text(_pin[i], style: const TextStyle(fontSize: 32, fontWeight: FontWeight.w900))
                            : null,
                      ),
                  ],
                ),
              ),
              SizedBox(
                height: 44,
                child: Center(
                  child: _submitted
                      ? const SizedBox(width: 20, height: 20, child: CircularProgressIndicator(strokeWidth: 2.5))
                      : error != null
                          ? Text(S.error(error), style: const TextStyle(color: WemuColors.danger, fontWeight: FontWeight.w700))
                          : null,
                ),
              ),
              const Spacer(),
              for (final row in const [
                ['1', '2', '3'],
                ['4', '5', '6'],
                ['7', '8', '9'],
                ['', '0', '<'],
              ])
                Padding(
                  padding: const EdgeInsets.symmetric(vertical: 6),
                  child: Row(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      for (final key in row)
                        Padding(
                          padding: const EdgeInsets.symmetric(horizontal: 12),
                          child: SizedBox.square(
                            dimension: keySize,
                            child: key.isEmpty
                                ? null
                                : Material(
                                    color: key == '<' ? Colors.transparent : WemuColors.surface,
                                    shape: const CircleBorder(),
                                    clipBehavior: Clip.antiAlias,
                                    child: InkWell(
                                      onTap: () => key == '<' ? _erase() : _press(key),
                                      child: Center(
                                        child: key == '<'
                                            ? const Icon(Icons.backspace_outlined, color: WemuColors.muted)
                                            : Text(key, style: TextStyle(fontSize: keySize * 0.36, fontWeight: FontWeight.w700)),
                                      ),
                                    ),
                                  ),
                          ),
                        ),
                    ],
                  ),
                ),
              const SizedBox(height: 24),
            ],
          );
        }),
      ),
    );
  }
}
