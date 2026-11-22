/// UI text.
class Strings {
  Strings._();

  static final Strings current = Strings._();

  String get appName => 'WEMU GamePad';
  String get tagline => 'Your phone becomes the Wii U GamePad';
  String get searching => 'Looking for WEMU on your Wi-Fi…';
  String get searchAgain => 'Search again';
  String get nearby => 'On this network';
  String get known => 'Recent PCs';
  String get noneFound => 'No WEMU found. Make sure WEMU is open on your PC and that both devices use the same Wi-Fi.';
  String get manual => 'Enter an address';
  String get manualHint => 'PC address, e.g. 192.168.1.20';
  String get connect => 'Connect';
  String get cancel => 'Cancel';
  String get forget => 'Forget';
  String get connecting => 'Connecting…';
  String get reconnecting => 'Connection lost — reconnecting…';
  String get paired => 'Paired';

  String get pairTitle => 'Pair with';
  String get pairHelp => 'Enter the 4-digit PIN shown in WEMU (Library header or Settings → Mobile GamePad).';

  String get library => 'Library';
  String get emptyLibrary => 'No games in the WEMU library yet. Choose a games folder in WEMU on your PC.';
  String get play => 'Play';
  String get nowPlaying => 'Now playing';
  String get openGamepad => 'Open GamePad';
  String get starting => 'Starting…';
  String get alreadyRunning => 'A game is already running on the PC.';
  String get disconnect => 'Disconnect';

  String get resume => 'Resume';
  String get pause => 'Pause game';
  String get stop => 'Quit game';
  String get stopConfirm => 'Quit the game on the PC?';
  String get gamePaused => 'Game paused';
  String get backToLibrary => 'Library';
  String get display => 'Display';
  String get sourceGamepad => 'GamePad screen';
  String get sourceTv => 'TV screen';
  String get noSignal => 'Waiting for the GamePad screen…';
  String get noSignalHint => 'Some titles draw nothing on the GamePad. Switch to the TV screen from the menu.';

  String get settings => 'Settings';
  String get controls => 'Controls';
  String get layoutSides => 'Beside the screen';
  String get layoutOverlay => 'Over the screen';
  String get layoutScreenOnly => 'Touch screen only';
  String get layoutHelp => '“Touch screen only” is for playing with a controller plugged into the PC.';
  String get controlsSize => 'Button size';
  String get overlayOpacity => 'Overlay opacity';
  String get haptics => 'Vibrate on press';
  String get motion => 'Motion controls (gyroscope)';
  String get flip => 'Flip landscape orientation';
  String get video => 'Video';
  String get quality => 'Image quality';
  String get qualityLow => 'Smooth';
  String get qualityMedium => 'Balanced';
  String get qualityHigh => 'Sharp';
  String get stats => 'Show latency and FPS';
  String get about => 'About';

  String error(String? key) {
    switch (key) {
      case 'unreachable':
        return 'Cannot reach this PC. Is WEMU open and on the same Wi-Fi?';
      case 'bad_pin':
        return 'Wrong PIN, try again.';
      case 'locked':
        return 'Too many attempts. Wait 30 seconds, then use the new PIN.';
      case 'protocol':
        return 'This WEMU version is not compatible with the app.';
      case 'replaced':
        return 'Another phone took over the GamePad.';
      case 'disconnected':
        return 'Disconnected from the PC.';
      case 'forgotten':
        return 'The PC forgot this phone. Pair it again.';
      case 'disabled':
        return 'Mobile GamePad was turned off in WEMU.';
      case 'already_running':
        return alreadyRunning;
      case 'unknown_game':
        return 'This game is no longer in the library.';
      default:
        return 'Connection closed.';
    }
  }
}

final S = Strings.current;
