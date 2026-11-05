# Guest speed and display cadence

FPS alone does not establish emulation speed. A game updating animation once per
rendered frame can run too fast even when its OS clock advances at normal speed.
Conversely, changing that clock can slow timers and movies without correcting
the animation cadence.

## Investigation

The October 2026 investigation found that `GX2SetSwapInterval` was a no-op and
`GX2GetSwapInterval` had no implementation. Presentation always capped at 60 Hz,
and `WEMU_FRAME_CLOCK=1` always scheduled one display refresh per swap.

Pikmin 3 requests interval 2 during startup. Ignoring that request allowed about
59 presents/s for a 30 Hz mode. This can accelerate frame-driven animation,
including blinking lights. It does not establish that every lighting artifact
has the same cause. In particular, Charlie's helmet beacon still needs visual
verification in gameplay after applying this change.

Mario Kart 8 requested interval 1 throughout a run from boot through the first
Grand Prix course. Its sampled guest clock did not run faster than real time.
A subsequent comparison reproduced disappearing background projector beams
specifically with asynchronous replay; see the detailed comparison below.

These are local Release/x64 core measurements through the desktop protocol sink,
with normal throttling, frame clock and all-frame replay enabled. The sink
acknowledges images immediately, excluding Qt uploads and desktop vsync. No
builds or tests ran during the measured windows. The baseline is commit
`4818b47` plus the same opt-in clock logger.

| Sample | Presents | Present FPS | Guest elapsed | Host elapsed | Guest/host speed |
| --- | ---: | ---: | ---: | ---: | ---: |
| Pikmin 3, before | 360–1200 | 58.64 | 14.000 s | 14.324 s | 0.977× |
| Pikmin 3, corrected interval | 360–1200 | 29.83 | 28.000 s | 28.161 s | 0.994× |
| MK8 opening, before | 360–840 | 59.43 | 8.000 s | 8.077 s | 0.990× |
| MK8 opening, corrected interval | 360–840 | 59.42 | 8.000 s | 8.078 s | 0.990× |
| MK8 first course, before | 4200–4800 | 36.30 | 10.000 s | 16.530 s | 0.605× |
| MK8 first course, corrected interval | 4200–4800 | 39.52 | 10.000 s | 15.180 s | 0.659× |

All 41 sampled MK8 guest tick values match exactly between runs at the same swap
count. Host throughput varied; this single comparison does not establish an FPS
gain. The requested interval and guest simulation schedule stayed unchanged.
An additional 101 consecutive frames from first-course gameplay were captured
for inspection. That window does not show whole-image alternating bright/dark
frames; it does not rule out localized flicker or other sections of the course.

Pikmin's movie delivery over presents 120–1200 remained near 30 decoded images/s:
30.00 before and 29.32 afterward, including the transition into the movie.
The movie advances approximately once per corrected guest frame instead of once
per two incorrectly paced frames. Different movie positions at the same present
count are expected after correcting guest time per swap.

## Implementation

Swap interval belongs to the scheduler session and defaults to 1. Both the
renderer and scheduler use shared timing conversions:

- Interval 1: one 60 Hz refresh, about 16.67 ms per swap.
- Interval 2: two refreshes, about 33.33 ms per swap.
- Interval 0: no swap pacing or synthetic guest frame wait. CPU execution and
  explicit guest waits continue to advance time.

The setter/getter preserve the requested interval; values 20 or larger leave the
previous value intact. Reenabling swap waits after interval 0 anchors a new
display deadline. `GX2WaitForVsync` retains its independent 60 Hz boundary, even
when swaps use interval 2. Ready guest workers still execute during a frame wait.
This uses the same paths for all titles and for Qt and standalone presentation.

The timer frequency remains 62,156,250 ticks/s. WUT defines the timer frequency
as [bus clock / 4](https://github.com/devkitPro/wut/blob/master/include/coreinit/time.h).
The refresh-interval interpretation agrees with WUT's
[60 FPS setup](https://github.com/devkitPro/wut/blob/master/libraries/libwhb/src/gfx.c)
and Cemu's [GX2 interval state](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/OS/libs/gx2/GX2_Misc.cpp).

## MK8 menu and projector follow-up

Per-swap logging in the actual cup-selection/menu window, presents 1800–3240,
measured 24.000 guest seconds over 24.242 host seconds (0.990×). Guest tick
deltas had a median of 16.67 ms and ranged from 11.09 to 21.96 ms. There was no
sustained fast clock in that menu sample.

In the first second of the first-course intro, presents 3650–3710 measured
1.000 guest seconds over 1.037 host seconds (0.965×). Per-frame guest deltas
ranged from 15.87 to 17.38 ms. The clock was not accelerating the projector
animation in that sample either.

The same sequence was then run with `WEMU_ASYNC_REPLAY=0` as a diagnostic.
Normal asynchronous presentation shows the preceding submitted frame, so
comparisons align async frame N with synchronous frame N−1. In a 60-frame
sample, the left background beam had low blue contrast in 26 asynchronous
images and none of the synchronous images. Contrast here is the maximum column
mean of `B - (R + G) / 2` over the upper 70 rows, columns 300–519; the separating
threshold was 35/255. The synchronous minimum was 41.39, versus 14.61 for async.
The paired images at async 3662 / sync 3661 clearly show the projector shafts
missing from the asynchronous image. This is a rendering difference, not a
Wii U reference measurement of the intended projector animation period.

Captures, `clock.csv` and `summary.json` are under the local ignored directories
`build/timing-review/mk8-intro-async` and `build/timing-review/mk8-intro-sync`.
The paired PNG is `build/timing-review/mk8-projector-dropout.png`; extracted
contrast samples are in `build/timing-review/mk8-projector-data.json`.
Both runs used the same input script, x64 backend and normal throttling, with
`--frames 3900 --measure-from 1800 --clock-profile --clock-profile-every 1
--capture-from 3650 --capture-every 1`.

Asynchronous replay remains enabled. Disabling it globally sacrifices CPU/GPU
overlap and has not been established as a performance-neutral fix. The source
previously read attribute/index and texture data from live guest memory during
replay, while the guest prepared its following frame. The subsequent
[submitted-buffer fix](submitted-draw-buffers.md) versions CPU vertex/index
inputs and eliminates beam dropouts in the same sample with asynchronous replay
enabled. Texture inputs remain managed by their existing paths.

## Measuring speed

`WEMU_CLOCK_PROFILE=1` emits `[CLOCK]` samples at the first swap and every 120
swaps, containing cumulative swap count, guest ticks, host seconds since the
first sample, and the requested interval. `WEMU_CLOCK_PROFILE_EVERY` changes the
default 120-swap sample interval; use 1 to inspect individual frame steps.
Calculate speed from differences
between samples, excluding startup and pauses:

```text
guest_seconds = (last_ticks - first_ticks) / 62156250
speed = guest_seconds / (last_wall - first_wall)
```

The probe's `--clock-profile` flag enables this logger, writes `clock.csv`, and
adds a `guest_clock` object to `summary.json`. Clock samples bound the measurement
window independently of the per-present FPS window; their endpoints are reported
explicitly. `--capture-from` can limit image capture to a late gameplay window.
`--clock-profile-every 1` enables per-swap sampling when used with `--clock-profile`.

```sh
python3 scratchpad/session_probe.py \
  'games/PIKMIN 3 [Game] [000500001012be00]/code/carrot.rpx' \
  --cpu x64 --frames 1200 --seconds 85 --measure-from 360 \
  --clock-profile --trace-hle-name GX2SetSwapInterval --trace-hle-every 1 \
  --output /tmp/wemu-timing-pikmin
```

Do not use `--unthrottled` to assess normal game speed: it deliberately disables
host pacing. It remains useful for measuring processing capacity. The GUI strips
inherited `WEMU_` experiments and enables the frame clock explicitly.

## Limits and performance

This is a cadence correction, not a CPU/GPU throughput optimization. A lower FPS
counter in a requested 30 Hz mode is expected. The interval-1 budget stays the
same; a host that cannot finish a 60 Hz frame still produces slowdown.

Guest timing remains approximate: interpreter service ticks charge retired
instructions, HLE yields have a fixed scheduling cost, and an idle scheduler
fast-forwards to guest deadlines. These measurements do not establish cycle
accuracy or rule out clock excursions in other scenes. Reducing the advertised
timer frequency would change game timers, physics and movie timing; it is not a
general FPS fix. Keeping simulation at normal speed while drawing fewer frames
requires a separate policy for frame skipping and CPU/GPU synchronization.
