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
  Desktop speakers play TV device 0, downmixed to stereo. DRC audio is rendered
  for guest callbacks but is not streamed to the phone or mirrored to speakers.
- HLE MVPlayer audio has its own FFmpeg demux/codec cursor. AAC soundtracks are
  resampled to stereo at 48 kHz and mixed with AX audio against the guest movie
  clock. Movie sound respects TV device volume and final-mix callbacks.
  Seeking flushes codec/resampler state and decodes preroll packets;
  video-only movies remain valid. Audio can run before the guest initializes AX.
- SDL uses a 100 ms maximum queue. Underruns produce silence; overflow discards
  oldest complete stereo frames. The audio callback never accesses guest memory.
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

`WEMU_AX_TRACE=1` reports frame ticks, active voices, output peak and queue depth.
A zero peak can mean the title has not started a voice, its routing/volume is
zero, or its source is silent. The desktop intentionally clears inherited
emulation diagnostic variables; run the core directly for this trace.

Build dependencies now include `libswresample` alongside `libavcodec`,
`libavformat`, `libavutil` and `libswscale`. `setup.sh`, the manual dependency
list and CI package lists include the required development packages.

## Verification and limits

The complete Release build and all seven CTest targets passed. The core suite
reported 1,004 passing tests and 67 skips (optional GPU/backend and local-media
checks). The desktop also loaded successfully with the offscreen Qt backend.

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

Remaining work includes hardware-accurate polyphase SRC, low-pass/biquad filters,
compressor/depop behavior, voice stealing callbacks, full AX protection semantics,
the standalone `AXDecodeAdpcmData` helper, Wiimote speakers, physical surround
output, phone audio transport, and end-to-end gameplay/listening verification.
Slow emulation can still cause audible underruns; this change does not make
commercial games fully playable.
