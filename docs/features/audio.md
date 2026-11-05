# Desktop audio

WEMU now renders AX voices and HLE movie soundtracks into a bounded SDL2
playback queue. Commercial game compatibility is still experimental: sound
requires the title to reach its audio playback code and supply valid buffers.

## Implemented

- 96 AX voices with big-endian signed PCM16, signed PCM8, and Nintendo DSP ADPCM.
- Inclusive end offsets, loops, loop counters, ADPCM predictor history and loop
  contexts, including streaming loops that retain decoder history.
- Fractional playback-rate conversion with linear interpolation, voice volume
  envelopes, per-channel/per-bus gains and ramps, and device master volume.
- AX's 3 ms guest-clock frame scheduling. `AXInit` renders 96 samples at 32 kHz;
  `AXInitWithParams` can render 144 at 48 kHz. Desktop output is always 48 kHz.
- Frame/application callbacks run before mixing. Aux callbacks receive their
  planar buses, user data and sample counts; their results return to the main
  bus. Final-mix callbacks can edit samples before host submission. Pre/post
  upsampling selection determines whether final callbacks receive 96 or 144
  samples in 32 kHz mode.
- TV and both DRC devices have independent gains, aux buses and final buffers.
  Desktop speakers play TV device 0. Stereo uses the title's L/R planes;
  mono duplicates its main channel. Six-channel output uses a normalized
  stereo downmix without adding LFE to the full-range speakers. DRC audio is rendered
  for guest callbacks but is not streamed to the phone or mirrored to speakers.
- HLE MVPlayer audio has its own FFmpeg demux/codec cursor. AAC soundtracks are
  resampled to stereo at 48 kHz and mixed with AX audio against the guest movie
  clock. Movie sound respects TV device volume and final-mix callbacks.
  Seeking flushes codec/resampler state and decodes preroll packets;
  video-only movies remain valid. Audio can run before the guest initializes AX.
- SDL keeps 60 ms of startup/recovery headroom in a 200 ms maximum queue.
  Short production bursts no longer become repeated sound/silence fragments.
  A 5 ms fade handles starvation and recovery, and overflow crossfades into the
  retained frames. The sample rate stays at 48 kHz.
- Adaptive, pitch-preserving time stretching uses SoundTouch 2.4.0 on the
  producer thread. It measures raw sample delivery against host audio demand
  every 100 ms, smooths that estimate, and uses slow queue feedback to avoid
  repeatedly draining and refilling at moderately reduced emulation speed.
  The controller does not infer emulation speed from the displayed FPS.
  Pitch stays normal while tempo follows sample production (supported tempo
  range 0.25–1.25). Neither guest clocks nor guest callback timing are changed.
  SoundTouch's input, output and overlap processing history is cleared on pause;
  the learned tempo is retained for a smooth resume. Full resets relearn it.
- The queue retains floating-point mixing headroom until host volume is applied,
  then saturates once to PCM16. Volume changes ramp over 10 ms. Aux and final
  callback buffers also retain signed 32-bit headroom instead of clipping early.
- Unregistered aux buses have no wet return; their raw sends are not added back
  as duplicate dry voices. Registered effects still process their own buses.
- The 32→48 kHz converter carries history between 3 ms frames for both pre- and
  post-final-mix upsampling. It no longer holds/repeats a boundary sample each frame.
  The audio callback never accesses guest memory.
- Saved 0–100% volume controls in Settings and the desktop pause menu. Zero
  mutes playback while guest voices and movie time continue advancing. Host
  pause/resume clears queued samples to prevent old audio playing on resume.

Public AX voice layouts follow the
[WUT headers](https://github.com/devkitPro/wut/blob/master/include/sndcore2/voice.h).
Aux/final callback parameter layouts were also checked against
[Decaf's declarations](https://github.com/decaf-emu/decaf-emu/blob/master/src/libdecaf/src/cafe/libraries/sndcore2/sndcore2_device.h).

## Controls and diagnostics

Launch the desktop normally; audio uses SDL's default playback device. The
pause menu also accepts Left/Right on the keyboard or controller to change
volume in 5% steps. In the standalone core, `WEMU_AUDIO_VOLUME=0..100` sets host volume. SDL's own
`SDL_AUDIODRIVER` environment variable selects a backend when needed. An audio
initialization failure is logged and does not prevent emulation.

Time stretching is enabled by default for live playback. For a direct-core A/B
comparison, set `WEMU_AUDIO_STRETCH=0` to use the buffered path without stretching.
As with other `WEMU_` diagnostics, desktop launches clear this inherited override.

`WEMU_AX_TRACE=1` reports frame ticks, active voices, output peak before PCM16
saturation, overrange samples in the current mix, queue depth, buffering state,
underrun/recovery events, missing frames, dropped frames and clipped host samples.
`tempo` is the source-time consumption ratio (1 is real time; approximately 0.67
stretches 40-FPS delivery from a nominal 60-FPS source without lowering pitch).
`production_rate` is the smoothed raw-source/host-demand ratio.
Counters are cumulative for the output instance; startup buffering is not an underrun.
If peaks are in range but underruns rise, investigate audio production timing or
emulation speed. If clipping rises while playback stays buffered, investigate
levels, routing or the title's effect callbacks.
A zero peak can mean the title has not started a voice, its routing/volume is
zero, or its source is silent. The desktop intentionally clears inherited
emulation diagnostic variables; run the core directly for this trace.

Build dependencies now include `libswresample` alongside `libavcodec`,
`libavformat`, `libavutil` and `libswscale`. `setup.sh`, the manual dependency
list and CI package lists include the required development packages.

The first CMake configure also downloads the pinned SoundTouch 2.4.0 source
archive from its upstream host. SHA-256 verification protects the cached source;
no system SoundTouch package is needed. See
[the dependency notice](../../core/third_party/SoundTouch.md) for its source and license.

## Verification and limits

The adaptive playback update passed all seven CTest targets in the default
Release build: 1,044 core tests passed and 67 optional tests were skipped.
Formatting and targeted clang-tidy checks passed. The desktop loaded with the
offscreen Qt backend. Earlier audio quality checks also passed in the non-LLVM build. Audio quality regressions additionally
cover burst delivery, buffer recovery, fades, overflow crossfades, volume before
clipping, invalid sample rejection, stereo/mono/surround routing, inactive effect
buses, signed 32-bit callback headroom and interpolation continuity across frames.

The adaptive playback tests reproduce sustained 35-, 40- and 60-FPS source
delivery, including up to 20 ms of packet jitter, against a 48 kHz host clock. The old buffered path repeatedly goes silent; the
stretching path has no silent callback or new underrun after its settling period.
Independent 440 Hz and 660 Hz stereo signals retain their pitches. Additional
tests check duration scaling, processing-history reset and a genuinely stalled
producer falling silent rather than repeating stale audio. Exact callback-sized
packets leave the queue empty without falsely triggering an underrun or refill.
The larger bounded queue accommodates SoundTouch's batch output; it does not
accumulate unbounded latency.

The audio tests cover signed samples, ADPCM coefficients/history/header changes,
loop boundaries, interpolation, envelopes, callback ordering, aux return mixing,
callback edits reaching the host queue, 32/48 kHz operation, DRC isolation,
volume/mute, underrun/overflow, dummy-device reopen, AAC seeking and EOF.
`menu-audio.bin` is generated test material; no commercial assets are included.
An optional local soundtrack check runs with:

```sh
WEMU_TEST_MOVIE='/path/to/a/movie-with-audio.mp4' \
  ./build/core/tests/wemu_tests \
  --gtest_filter=MovieAudioTest.OptionalLocalMovieProducesFiniteNonSilentSound
```

Local soundtrack checks passed for Pikmin 3's opening, Smash's English opening,
and Mario Kart 8's staff credits. MK8's menu movie has only video; its soundtrack
must come through AX. These checks establish decoded, finite, non-silent samples,
not complete game compatibility or listening quality. A release MK8 boot
produced active AX voices and nonzero PCM output through the host buffer
(peaks observed above 8,000 in signed 16-bit units).

A subsequent 40-second MK8 smoke run with the quality fixes produced nonzero
output with no observed clipped samples or dropped queue frames. It still
reported underruns while emulation stalled (the run also shared CPU time with
a build). This verifies routing and sample delivery, not uninterrupted playback
or subjective listening quality. The burst-delivery regression independently
checks that the playback cushion absorbs bounded jitter without underruns.

Remaining work includes hardware-accurate polyphase SRC, low-pass/biquad filters,
compressor/depop behavior, voice stealing callbacks, full AX protection semantics,
the standalone `AXDecodeAdpcmData` helper, Wiimote speakers, physical surround
output, phone audio transport, and end-to-end gameplay/listening verification.
A 60 ms cushion absorbs short stalls, and adaptive stretching handles a
sustained moderate sample shortage without periodic silence. It adds processing
latency and slows musical tempo to match emulation speed. It cannot recover
samples during a complete producer stall, or guarantee artifact-free playback
at extreme slowdown. Such cases can still fade and refill. Per-voice SRC still
uses linear interpolation, so hardware polyphase filtering and listening checks
remain future work; these changes do not make commercial games fully playable.

SoundTouch's tempo processing and streaming interface are described in its
[upstream documentation](https://www.surina.net/soundtouch/README.html).

The quality fixes were cross-checked against Cemu's public
[aux routing](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/OS/libs/snd_core/ax_aux.cpp),
[channel output](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/OS/libs/snd_core/ax_out.cpp)
and [continuous upsampling](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/OS/libs/snd_core/ax_ist.cpp)
implementations. WEMU's tests exercise its own code with generated signals.
