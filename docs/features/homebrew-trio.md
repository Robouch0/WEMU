# Homebrew trio: October 26, 2026

**Updated results:** see [desktop compatibility validation](homebrew-visible-compatibility.md).
The results below describe an earlier pass and are retained as history.

This records the initial installation pass. The subsequent
[shared RPX API exploration](homebrew-shared-api.md) uses original game binaries
and implements general emulator APIs, with commercial-title comparisons.

Downloaded Flappy Bird GX2, Space Game and Blockamok Remix into their named
directories under `games/`. The store ZIPs matched the MD5 checksums published
on their store pages. Original archives and upstream sources are retained under
`scratchpad/downloads/homebrew-trio/` and `scratchpad/homebrew-trio-src/`.

## Space Game: gameplay renders and accepts input

The [store's version 1.5](https://hb-app.store/wiiu/spacegame) contains an older
ELF executable that this WEMU loader rejects. The developer also publishes a
native [RPX release](https://github.com/vgmoose/wiiu-space/releases/tag/v1.1rpx).
That executable is installed as `games/Space Game/code/space.rpx` and appears in
the regular desktop library. This upstream beta has no sound; it is not the
same build as the store's 1.5 ELF.

Start the existing desktop with `./build/gui/appgui`, select **Space Game**,
and press A in the game menu. Use a stick to move and the GamePad touchscreen to
aim/fire. The TV's game area occupies only part of the output in this build.

The external desktop-protocol probe completed 300 frames with button, stick,
and touch input. Captures show changing ship orientation/position and enemy
positions. This is a short functional check, not complete game compatibility.
The measured window included menu/pause time, so its average presentation rate
should not be treated as a gameplay benchmark.

Reproduce from the repository root:

```sh
python3 scratchpad/homebrew-trio/probe.py \
  'games/Space Game/code/space.rpx' \
  --output reports/homebrew-trio/space-recheck --cpu x64 \
  --frames 300 --seconds 20 --capture-every 20 \
  --timed-input reports/homebrew-trio/space-timed-input.json
```

The separate probe wrapper supplies input by wall time because this game can
sit on a menu without presenting another frame. It leaves the existing session
probe and emulator protocol unchanged.

## Flappy Bird GX2: downloaded, startup blocked

The [store package](https://hb-app.store/wiiu/flappy_bird) contains an ELF that
fails loading with `Could not find section with specified name.` The
[author's RPX-channel release post](https://gbatemp.net/threads/release-flappy-bird-gx2-rpx-channel.455971/)
links a channel ZIP, also available from the store CDN's
`chan_zips/flappy_bird.zip` path. Its original RPX has been extracted into
`games/Flappy Bird GX2/code/flappy_bird.rpx`, with the upstream ELF retained.
The homebrew package uses NUSPacker's published default package encryption key;
no console key was requested or used.

The RPX reaches application initialization but stalls in its optional network
debug logger: WEMU's unimplemented `send` import returns zero, so the logger
does not advance. A local experiment disabled the logger and avoided the
optional suspended-thread query before resuming a thread. This got further,
but still produced no frames and exposed unsupported thread-suspension queries
and heap-size queries.

**The installed RPX is restored to the original upstream bytes.** The unsuccessful
experimental RPX and patch record are kept in `reports/homebrew-trio/`.
The abandoned patch script has been archived as
`reports/homebrew-shared-api/abandoned-game-patch.py.txt`; it is no longer an
active workspace utility. Game-specific patching is not the current approach.

## Blockamok Remix: downloaded, no rendered frames

The [store's 1.2 package](https://hb-app.store/wiiu/BlockamokRemix) supplies both
RPX and WUHB files. Both are retained under `games/Blockamok Remix/`; WEMU's
library selects the RPX. It loads but produces no frames during the bounded
startup checks. Logs report missing GX2 display/allocator, atomic and thread
APIs, followed by an unsupported opcode at guest PC `0x020DB368`.

Host-side SDL software-renderer and dummy-audio environment hints did not
change the result. Their propagation into guest SDL was not established, so
this does not prove the guest actually selected those drivers. The original
binary is unchanged. Upstream source was inspected, but no replacement SDL
implementation or game rewrite was installed.

## Commercial-title preservation

No core or GUI source, executable, global setting, existing session-probe code,
or commercial-title content was edited in this pass. A SHA-256 snapshot of 418
existing tracked files and executables matches after the work. This baseline
includes the earlier Doom changes; those were neither extended nor reverted.

MK8, Smash and Pikmin 3 each completed a separate 60-frame startup probe with
exit code zero and no probe error. These are boot checks, not sustained gameplay
or before/after performance comparisons. The unchanged emulator binaries are
the stronger evidence that this installation did not alter their execution.

Evidence is local in `reports/homebrew-trio/`:

- `baseline-verification.json` and `baseline-sha256.json`: unchanged-file check.
- `space-timed-play/`: successful 300-frame input run, captures and summary.
- `flappy-rpx/`, `flappy-game-thread/`: upstream and experimental failure logs.
- `blockamok-store/`, `blockamok-software/`: bounded startup failures.
- `commercial-mk8/`, `commercial-smash/`, `commercial-pikmin3/`: boot checks.
- `download-sha256.json`: hashes of downloaded ZIP files.

No retail content, emulator builds or downloaded third-party packages have
been staged or committed.
