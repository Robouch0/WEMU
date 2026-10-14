# WEMU GamePad

Use your phone as the Wii U GamePad for [WEMU](../README.md): the GamePad screen
streams to the phone, and its buttons, sticks, touch screen and gyroscope drive
the game running on your PC.

Design and protocol: [docs/features/mobile-gamepad.md](../docs/features/mobile-gamepad.md).

## Build

```bash
flutter pub get
flutter test
flutter build apk --release   # Android: build/app/outputs/flutter-apk/app-release.apk
```

iOS builds need macOS; CI (`.github/workflows/mobile.yml`) produces an ad-hoc
signed IPA to re-sign and install with Feather, Sideloadly or AltStore.

## Layout

| Path | Role |
|---|---|
| `lib/net/` | Wire protocol, discovery (broadcast + subnet sweep), connection client |
| `lib/gamepad/` | Control layout, multitouch input, painting |
| `lib/screens/` | Connect, PIN pairing, library, GamePad view, settings |
| `lib/storage.dart` | Known PCs, pairing tokens, preferences |
| `lib/strings.dart` | French / English UI text |
