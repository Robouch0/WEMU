# Smash gameplay rendering and keyboard input

Smash's white gameplay screen had several independent causes. User-mode
quantization registers UGQR0–UGQR7 now share state with GQR0–GQR7 in both the
interpreter and x86-64 JIT. This preserves the unsigned quantized animation
loads used to build camera and model transforms.

`GX2CalcDepthBufferHiZInfo` now uses AddrLib to report HTILE size and alignment.
A zero allocation previously made the guest omit the world-depth clear. The
renderer still uses its ordinary depth attachment; reporting metadata does not
implement compressed HTILE rendering.

Captured stream-output bindings, enable state, write offsets and saved contexts
now reach command replay. Non-indexed, single-instance point passes execute the
reference vertex program even with rasterization disabled. Direct STREAM0–3
exports preserve component masks and write little-endian words into bounded
guest buffers. Output commits only after the complete pass succeeds. Later
uniform-block bindings in the same submission see these writes rather than
stale call-time snapshots. Context save/restore retains stream-output state.
Eligible uniform-only passes with at least 64 vertices now use
[native compute stream output](gpu-stream-output.md), with the existing vertex
worker pool as a fallback. Passes sampling textures remain serial. Other primitive modes, indexed
passes, instancing, relative exports and burst exports remain unsupported and
report a diagnostic instead of partially writing output.

Rendered color views with compatible element layouts and host storage share
image contents across numeric-format flags. Decoded guest texture caches still
include the complete format. Independently sized uniform-bank windows keep
unequal skinning palettes within the native shader's total constant budget.

Keyboard WASD now drives the analog left stick, which Smash uses for movement.
Arrows remain the digital directional pad. J/Enter sends A, K/Backspace sends B,
Space sends X, I sends Y, Q/E sends L/R, and Shift/Ctrl sends ZL/ZR. Default Smash
controls map these to attacks, specials, jumping, grabs and shields. Focus loss
and pause clear keyboard movement. See [pause and input](pause-menu.md).

## Verification

The desktop session protocol probe reached Battlefield Training with Mario and
a CPU Bowser Jr., rendered the stage and complete animated fighters, moved and
jumped Mario, and landed a three-hit combo that raised CPU damage to 8%.
A normal timed Smash 1v1 also reached a fully rendered match with Mario and a
CPU Luigi on a space stage. The CPU attacked Mario, and player attacks
raised Luigi's damage to 4%. The checked-in
`scratchpad/scenarios/smash-1v1.json` reproduces the menu inputs for the English
UI; character and random-stage outcomes can depend on the dump and save state.
These runs verify the gameplay paths exercised by those scenes; they do not establish
compatibility with every stage or mode, or a sustained 60 FPS target.

For a local dump, run the standard match scenario with:

```bash
python3 scratchpad/session_probe.py /path/to/code/cross_f.rpx \
  --cpu x64 --output /tmp/wemu-smash-1v1 --frames 3500 --seconds 180 \
  --input-script scratchpad/scenarios/smash-1v1.json --capture-every 100
```

`scratchpad/session_probe.py --cpu x64` accepts `--input-script PATH`, a JSON
list of states with a `frame` number. Each state replaces the previous scripted
state. For example:

```json
[
  {"frame": 2700, "left_stick": [1, 0]},
  {"frame": 2710, "left_stick": [1, 0], "buttons": 8192},
  {"frame": 2716, "buttons": 0}
]
```

`--control-file PATH` reads a live JSON state on each present, overriding supplied
script keys. Supported fields are `buttons`, `left_stick`, `right_stick`,
`touched`, `touch_x`, `touch_y`, `accel` and `gyro`. A `stop` field set to true
ends the session normally. Replace the file atomically when changing controls.
`--env WEMU_NAME=VALUE` adds an explicit diagnostic setting. Game files and
framebuffer captures are local inputs and artifacts, not repository assets.

Core delivery measurements exclude Qt uploads and display synchronization.
Use `--measure-from` after gameplay starts; boot/menu averages and repeated
presents do not measure gameplay speed. `--unthrottled` also removes host frame
pacing and must be identified when reporting results.
