# Crispy Doom U compatibility probe

**Newer results:** [runtime and presentation fixes](homebrew-runtime-fixes.md).
The limitations below describe the earlier implementation.

As of October 26, 2026, the `wiiu-0.3.3` RPX reaches its launcher, enumerates
WADs on the emulated SD card, accepts GamePad selection, and reads the selected
IWAD. Gameplay is not working yet.

The local test installation is `games/crispy-doom/`. It contains the RPX under
`code/` and an SD layout under `sdcard/`. Launch the desktop with:

```sh
WEMU_SD_ROOT="$PWD/games/crispy-doom/sdcard" ./build/gui/appgui
```

Select Crispy Doom in the game library. In its launcher, A selects an IWAD and
Plus starts it (desktop defaults: P is Plus). `WEMU_SD_ROOT` must point at the
directory containing `wiiu/`, rather than the app directory. SD emulation is
disabled when that variable is absent. Only the Cafe FS SD mount is implemented;
the newer Aroma port's FSA access remains unsupported.

## Packages and data

- [Crispy Doom U store package](https://hb-app.store/wiiu/Crispy-Doom),
  [source](https://github.com/thearst3rd/crispy-doom-u).
  Package MD5: `51a3cfbb687ce645bcbf71e88c26fc72`.
- The package includes Freedoom Phase 1 and 2. Their WADs are installed under
  `sdcard/wiiu/apps/crispy-doom-u/wads/`; the Freedoom license is retained beside
  the application. Keep license text outside `wads/`: this launcher lists every
  regular file there, regardless of extension.
- The additional local `doom1.wad` is Doom 1.9 shareware from
  [this shareware archive](https://www.jbserver.com/downloads/games/doom/misc/shareware/doom1.wad.zip).
  Size: 4,196,020 bytes; MD5: `f0cefca49926d00903cf57551d901abe`.
  No retail Doom WADs were downloaded. Local binaries and WADs are not source
  dependencies and should not be committed with the implementation.
- The newer [1.1 graphics-fix package](https://hb-app.store/wiiu/CrispyDoomU)
  was also checked (MD5 `1d45fa66576d669881164f1f714ad5ad`); its launcher reports
  no WADs with the current FSA stubs.

## Emulator changes and validation

The RPX loader now skips file reads for NOBITS sections and includes their guest
memory size when placing imports. Optional SD mounting provides
`/vol/external01`. `FSOpenFileEx` supports ordinary opens with the handle output
in r10, and `FSMakeDir` creates host-backed directories. HLE realloc preserves
the old allocation's contents. Startup supplies argc=1 and a valid argv[0].

The core builds successfully. All 34 focused loader, filesystem, memory and
memory ownership tests passed:

```sh
build/core/tests/wemu_tests \
  --gtest_filter='LoaderTest.*:FsTest.*:MemoryTest.*:MemoryAccessTest*.*:MemoryOwnershipTest.*'
```

## Remaining failures

With normal sound settings, startup stalls after creating SDL's audio thread
and registering the AX callback. A scheduler snapshot shows main waiting in
`SDL_CondWait`, another thread waiting in `SDL_SemWaitTimeout`, the audio device
thread sleeping, and AXFrame running. This is a startup stall, not measured
gameplay performance.

To investigate initialization independently of audio, a temporary
`config/default.cfg` set `snd_sfxdevice 0` and `snd_musicdevice 0`. That file was
removed after testing. With those settings:

- Freedoom Phase 1 fails in `R_InitTextures` with
  `Z_Free: freed a pointer without ZONEID`. Both interpreter and x64 runs fail.
- Doom shareware fails in `R_GenerateLookup` with
  `W_CacheLumpNum: %i >= numlumps`.
- Unhandled RPX relocation types 252 and 253 are still reported. Whether these
  contribute to either failure has not been established.

Local evidence is in `reports/crispy-doom/`: `launcher.png`, per-run core logs,
frame captures and JSON summaries. `file-api-fixed/` records the audio stall,
`video-error/` and `interpreter/` record Freedoom failures, and `shareware/`
records the smaller IWAD's failure. The working launcher is not evidence of
gameplay or audio compatibility.
