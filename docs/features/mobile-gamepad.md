# Mobile GamePad

A phone running the **WEMU GamePad** app (`mobile/`, Flutter, Android and iOS)
replaces the Wii U GamePad over Wi-Fi. The PC keeps the TV image; the phone shows
the GamePad (DRC) screen and sends buttons, sticks, touch and motion back. A game
started from either side opens the GamePad view on the phone automatically.

## Using it

1. Start WEMU. The library header shows **Pair phone · PIN**; the same panel is
   in Settings → Mobile GamePad (enable switch, PIN, addresses, paired phones).
2. Open the app on a phone on the same network. It lists WEMU instances found by
   UDP broadcast and by a sweep of the phone's /24 subnet (iOS blocks broadcast
   without a special entitlement). An address can also be typed.
3. Enter the 4-digit PIN once. The PC issues a token, so the phone reconnects by
   itself afterwards, including after Wi-Fi drops or a WEMU restart.
4. Pick a game in the phone's library, or launch one on the PC.

The HOME button opens the in-app menu: resume, pause, quit, switch the phone to
the **TV image** (for titles that leave the GamePad blank), change the controls
layout, settings. Layouts: controls *beside the screen* (default), *over the
screen* (adjustable opacity), or *touch screen only* for playing with a
controller plugged into the PC. Settings also cover button size, vibration,
gyroscope, landscape side, image quality and a latency/FPS overlay.

Only one phone drives the session; a paired phone connecting takes over. Its
buttons are OR-ed with the keyboard's.

## Architecture

```
phone app ──TCP 47470──► appgui RemoteGamepadServer ──QLocalSocket──► wemu core
          ◄─ JPEG frames ─          (pairing, library,     ◄─ DrcFrame ── Gx2Replayer / OSScreen
          ── input ──────►           JPEG encoding)        ── Gamepad ──► VPADRead
          UDP 47471 discovery
```

* **Core.** `Gx2Replayer` captures `GX2CopyColorBufferToScanBuffer` copies whose
  scan target includes the DRC (bit 4) into an 854×480 buffer, resampling other
  sizes like the scan-out engine; `OSScreenFlipBuffersEx(DRC)` forwards the
  OSScreen buffer. Frames are only built and sent while the window asked for
  them (`Action::DrcStream`), so sessions without a phone pay nothing.
  `VPADRead` fills sticks (plus their stick-emulation button bits), the three
  touch samples, accelerometer and gyroscope from the latest `GamepadState`.
  `VPADGetTPCalibratedPoint[Ex]` invert the raw touch mapping.
* **Session protocol v3** (`core/src/frontend/QtSessionProtocol.hpp`) adds
  `DrcFrame` (core → window, unacknowledged, newest wins) and `Gamepad`
  (window → core, values clamped). The core's control parser drains messages
  incrementally so a stalled guest cannot overflow it with queued input.
* **GUI.** `gui/src/remote/RemoteGamepadServer` handles discovery, PIN pairing
  (5 failures lock pairing for 30 s and rotate the PIN; tokens are stored hashed
  in QSettings), library and icons, launch/stop/pause, and video: the newest
  frame is JPEG-encoded on a worker thread with at most two frames awaiting
  acknowledgement and a bounded socket queue, which keeps latency flat on a
  slow network.
* **Wire protocol** (`gui/src/remote/RemoteProtocol.hpp`, mirrored by
  `mobile/lib/net/protocol.dart`): little-endian `u32 length, u8 type, payload`.
  Control messages are JSON; frames are `u32 seq, u16 w, u16 h, u8 source, JPEG`;
  input is a fixed 52-byte sample. WEMU greets every connection with `Info`, so
  a subnet scan is just "connect and read one message".

Motion follows the GamePad axes (x right, y up, z out of the screen): gravity in g
(flat, face up: z = −1) and angular rate in revolutions per second. The
orientation matrix stays identity; titles that integrate gyro aiming themselves
work, titles that read `direction` do not yet. GamePad audio stays on the PC.

## Building the app

```bash
cd mobile
flutter pub get
flutter build apk --release          # build/app/outputs/flutter-apk/app-release.apk
```

The IPA needs macOS: `.github/workflows/mobile.yml` builds an ad-hoc signed IPA
on a GitHub macOS runner next to the APK. Both are uploaded unzipped (artifacts
`wemu-gamepad.ipa` and `wemu-gamepad.apk`), so a phone downloads the file itself.
Re-sign the IPA on install with Feather, Sideloadly or AltStore.

## Testing

* `wemu_tests` (`SchedulerTest.*`): neutral and remote VPAD samples, calibration
  round trip, DRC forwarding gated by `DrcStream`.
* `wemu_session_protocol_tests`, `wemu_remote_protocol_tests`: framing and value
  clamping on both protocols.
* `cd mobile && flutter test`: wire format, message reassembly, layouts at every
  size and mode, multitouch behaviour (combos, sliding, dead zone, diagonals,
  touch mapping, send throttling).
* `games/gamepadtest` is a homebrew that draws every GamePad input on the DRC
  screen; source and build line in `games/gamepadtest/source`.
* `WEMU_REMOTE_PIN=1234` fixes the PIN, for headless runs
  (`QT_QPA_PLATFORM=offscreen`) and scripted tests.
