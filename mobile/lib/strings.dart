import 'dart:ui';

/// UI text in French or English, following the phone's language.
class Strings {
  Strings._(this.fr);

  static final Strings current = Strings._(PlatformDispatcher.instance.locale.languageCode == 'fr');

  final bool fr;

  String _t(String en, String french) => fr ? french : en;

  String get appName => 'WEMU GamePad';
  String get tagline => _t('Your phone becomes the Wii U GamePad', 'Ton téléphone devient la GamePad Wii U');
  String get searching => _t('Looking for WEMU on your Wi-Fi…', 'Recherche de WEMU sur ton Wi-Fi…');
  String get searchAgain => _t('Search again', 'Relancer la recherche');
  String get nearby => _t('On this network', 'Sur ce réseau');
  String get known => _t('Recent PCs', 'PC récents');
  String get noneFound =>
      _t('No WEMU found. Make sure WEMU is open on your PC and that both devices use the same Wi-Fi.',
          'Aucun WEMU trouvé. Vérifie que WEMU est ouvert sur ton PC et que les deux appareils sont sur le même Wi-Fi.');
  String get manual => _t('Enter an address', 'Saisir une adresse');
  String get manualHint => _t('PC address, e.g. 192.168.1.20', 'Adresse du PC, ex. 192.168.1.20');
  String get connect => _t('Connect', 'Se connecter');
  String get cancel => _t('Cancel', 'Annuler');
  String get forget => _t('Forget', 'Oublier');
  String get connecting => _t('Connecting…', 'Connexion…');
  String get reconnecting => _t('Connection lost — reconnecting…', 'Connexion perdue — reconnexion…');
  String get paired => _t('Paired', 'Appairé');

  String get pairTitle => _t('Pair with', 'Appairer avec');
  String get pairHelp =>
      _t('Enter the 4-digit PIN shown in WEMU (Library header or Settings → Mobile GamePad).',
          'Saisis le code à 4 chiffres affiché dans WEMU (en-tête de la bibliothèque ou Paramètres → Mobile GamePad).');

  String get library => _t('Library', 'Bibliothèque');
  String get emptyLibrary =>
      _t('No games in the WEMU library yet. Choose a games folder in WEMU on your PC.',
          'Aucun jeu dans la bibliothèque WEMU. Choisis un dossier de jeux dans WEMU sur ton PC.');
  String get play => _t('Play', 'Jouer');
  String get nowPlaying => _t('Now playing', 'En cours');
  String get openGamepad => _t('Open GamePad', 'Ouvrir la GamePad');
  String get starting => _t('Starting…', 'Démarrage…');
  String get alreadyRunning => _t('A game is already running on the PC.', 'Un jeu est déjà lancé sur le PC.');
  String get disconnect => _t('Disconnect', 'Se déconnecter');

  String get resume => _t('Resume', 'Reprendre');
  String get pause => _t('Pause game', 'Mettre en pause');
  String get stop => _t('Quit game', 'Quitter le jeu');
  String get stopConfirm => _t('Quit the game on the PC?', 'Quitter le jeu sur le PC ?');
  String get gamePaused => _t('Game paused', 'Jeu en pause');
  String get backToLibrary => _t('Library', 'Bibliothèque');
  String get display => _t('Display', 'Affichage');
  String get sourceGamepad => _t('GamePad screen', 'Écran GamePad');
  String get sourceTv => _t('TV screen', 'Écran TV');
  String get noSignal => _t('Waiting for the GamePad screen…', 'En attente de l\'écran GamePad…');
  String get noSignalHint =>
      _t('Some titles draw nothing on the GamePad. Switch to the TV screen from the menu.',
          'Certains jeux n\'affichent rien sur la GamePad. Passe sur l\'écran TV depuis le menu.');

  String get settings => _t('Settings', 'Réglages');
  String get controls => _t('Controls', 'Commandes');
  String get layoutSides => _t('Beside the screen', 'Autour de l\'écran');
  String get layoutOverlay => _t('Over the screen', 'Sur l\'écran');
  String get layoutScreenOnly => _t('Touch screen only', 'Écran tactile seul');
  String get layoutHelp =>
      _t('“Touch screen only” is for playing with a controller plugged into the PC.',
          '« Écran tactile seul » sert à jouer avec une manette branchée sur le PC.');
  String get controlsSize => _t('Button size', 'Taille des boutons');
  String get overlayOpacity => _t('Overlay opacity', 'Opacité des boutons');
  String get haptics => _t('Vibrate on press', 'Vibration à l\'appui');
  String get motion => _t('Motion controls (gyroscope)', 'Gyroscope (contrôles par mouvement)');
  String get flip => _t('Flip landscape orientation', 'Inverser le sens paysage');
  String get video => _t('Video', 'Vidéo');
  String get quality => _t('Image quality', 'Qualité d\'image');
  String get qualityLow => _t('Smooth', 'Fluide');
  String get qualityMedium => _t('Balanced', 'Équilibrée');
  String get qualityHigh => _t('Sharp', 'Nette');
  String get stats => _t('Show latency and FPS', 'Afficher latence et FPS');
  String get about => _t('About', 'À propos');

  String error(String? key) {
    switch (key) {
      case 'unreachable':
        return _t('Cannot reach this PC. Is WEMU open and on the same Wi-Fi?', 'PC injoignable. WEMU est-il ouvert et sur le même Wi-Fi ?');
      case 'bad_pin':
        return _t('Wrong PIN, try again.', 'Code incorrect, réessaie.');
      case 'locked':
        return _t('Too many attempts. Wait 30 seconds, then use the new PIN.',
            'Trop d\'essais. Attends 30 secondes puis utilise le nouveau code.');
      case 'protocol':
        return _t('This WEMU version is not compatible with the app.', 'Cette version de WEMU n\'est pas compatible avec l\'app.');
      case 'replaced':
        return _t('Another phone took over the GamePad.', 'Un autre téléphone a pris la GamePad.');
      case 'disconnected':
        return _t('Disconnected from the PC.', 'Déconnecté depuis le PC.');
      case 'forgotten':
        return _t('The PC forgot this phone. Pair it again.', 'Le PC a oublié ce téléphone. Appaire-le à nouveau.');
      case 'disabled':
        return _t('Mobile GamePad was turned off in WEMU.', 'La GamePad mobile a été désactivée dans WEMU.');
      case 'already_running':
        return alreadyRunning;
      case 'unknown_game':
        return _t('This game is no longer in the library.', 'Ce jeu n\'est plus dans la bibliothèque.');
      default:
        return _t('Connection closed.', 'Connexion fermée.');
    }
  }
}

final S = Strings.current;
