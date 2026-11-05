#pragma once

class QCoreApplication;
class EmulatorLauncher;
class GameView;

// Optional full-desktop measurement driven by QT_WEMU_SESSION_PROBE=<JSON
// file>.
void installSessionProbe(QCoreApplication &application, EmulatorLauncher &launcher, GameView &view);
