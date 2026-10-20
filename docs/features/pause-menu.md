# Pause menu and FPS display

Build and launch the desktop GUI with `./build/gui/appgui`, then start a game.

The **Games folder** selection is saved for future launches. A command-line
`--library /path/to/folder` override applies to that launch only, including
refreshes; it does not replace the saved folder.

- The library, game image and QML pause menu share one Qt window. The emulation
  core remains a separate process; a core crash returns to the selected library
  entry with a session error and log link.
- Press **O** or **Escape** in the game view, or a controller's **Guide** button,
  to pause and open the translucent menu.
- Use **Up/Down** and **Enter** (a controller's D-pad or left stick and **A**), or
  click a menu row, to select an action.
- **Resume**, **O**, **Escape**, **Guide** or a controller's **B** resumes the paused
  game. Each first cancels an open quit confirmation.
- **Show FPS** enables or disables the display for the current game session.
- **Return to library** opens a confirmation. Cancel keeps the game paused;
  confirming stops the session and focuses the library carousel.

During a running game, **Enter** sends Wii U A, **Backspace** sends B,
**P** sends Plus/Start and **M** sends Minus. Arrows and WASD send the digital
directional pad.

Controllers connected to the PC (SDL game controllers) drive the GamePad through
the active profile of **Settings**: A, B, X, Y, the D-pad, L, R, ZL and ZR follow
its bindings (triggers count as pressed past 40 %). Start sends Plus, Back sends
Minus, the stick clicks send the GamePad's stick clicks, and both sticks are
analog (15 % radial dead zone). Keyboard, controller and phone GamePad buttons
are merged; for each stick, the source pushed furthest wins. Controllers keep
working while the window is in the background. Buttons held when a game starts,
pauses or resumes wait for a release before reaching the game.

The isolated core buffers up to 32 button-state transitions until guest controller
reads consume them. A quick press/release therefore remains visible at low FPS.
Held buttons retain their state after the queue empties. Pause, stop and
disconnection clear pending transitions; pause-menu keys remain suppressed.

For input diagnosis, launch with `QT_WEMU_INPUT_TRACE=1 ./build/gui/appgui`.
The GUI log reports outgoing `[SESSION_BUTTONS]` masks, `[SESSION_GAMEPAD]`
sticks and periodic frame/movie counts. The session log reports `[VPAD_INPUT]`
samples actually delivered to the guest, including press and release edges. This
opt-in tracing records diagnostics; it does not generate input or change guest
clocks.

Pause preserves the loaded session and its caches. Guest instructions, timers,
callbacks and movie delivery stop while the host menu remains responsive.
An existing synchronous HLE call or GPU wait finishes before pause takes effect.
Menu keys remain captured until released, so they do not trigger game actions
when resuming. Quitting discards unsaved progress.

The display separates **PRESENT FPS** (guest presents, including repeated images)
from **MOVIE FPS** (H.264 frames delivered to the guest). Menu redraws and paused
time are excluded. A value of `--` means the counter is warming up or has no
recent frames. These counters measure delivery cadence, not GPU execution time.

The core sends its existing RGBA readback through a local socket to a Qt Quick
texture item. The visible view acknowledges each frame, bounding the queue to
two outstanding frames, displayed in order. A pause acknowledgement waits for
submitted frames to settle. UI redraws do not create extra guest frames. QML draws
the menu and FPS display without changing guest render targets. Shared theme
values are in `gui/qml/components/Theme.js`.

The core's Vulkan shader/raster backend is retained. Direct GPU texture sharing
across processes is a future optimization; this version includes CPU copies and
a Qt texture upload. The desktop core transport is built when Qt Core/Network
are available. A standalone `core/wemu --gui-session /path/game.rpx` retains its
SDL/Vulkan window and bitmap pause menu; in that mode O opens the menu and Escape
outside it closes the game window.
