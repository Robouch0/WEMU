# Complete-frame CPU timing

`WEMU_FRAME_PROFILE=1` reports one `[FRAME_PROFILE]` record per guest present.
Exclusive nested timings cover guest execution, HLE, replay, native draw CPU
work, CPU readback, GPU fence waits, video decode, presentation, pacing, pause
and native compilation. Their total accounts for the full measured wall interval.
Stream-output passes additionally emit `[STREAMOUT_PROFILE]` with the frame,
vertex count, wall time in microseconds and completion status. These passes run
before ordinary raster draw timing begins; their time is already included in
replay and must not be added again to frame totals. Their wall time includes
waiting for vertex workers, rather than summing each worker's CPU time.
The `native` field distinguishes Vulkan compute from reference execution; see
[native stream output](gpu-stream-output.md) for comparison and verification flags.
A nested stage charges its parent only while the nested work is inactive, so
readback/fence waits are not counted again as draw/HLE time. Scopes can span a
present boundary. Pause is reported separately from active emulation work.

With profiling disabled, scopes do not read the clock or allocate a timeline.
Static GPU cleanup bypasses timing scopes because it can run after the thread-local
timeline is destroyed.
The timeline is local to the emulation thread; worker/asynchronous GPU time is
represented only through work/waits observed on that thread. Existing
`WEMU_NATIVE_GPU_TIMING=1` timestamps GPU render work separately and should not
be added to CPU wall-time totals. It does not cover every upload/copy in a frame.

Compare equal scene windows or movie frame counts across runs, using the same
profiling flags, host conditions and presentation path. Core IPC-sink timings
exclude Qt texture upload and desktop vsync; displayed-game performance requires
separate GUI verification. Present FPS measures delivered guest presents; movie
FPS measures decoded/delivered movie frames. Real-time movie progress must also
match the media timestamps, even if guest presents repeat each movie image.

`WEMU_CLOCK_PROFILE=1` separately samples guest ticks versus host elapsed time;
see [emulation timing](emulation-timing.md) for speed and swap-interval diagnosis.
`WEMU_NATIVE_DIRECT_PROFILE=1` breaks down expensive direct Vulkan draws on the
CPU, including texture preparation and command recording; see
[texture layer reuse](texture-layer-reuse.md) for fields and a Balloon Battle
investigation. Neither diagnostic is a GPU timestamp measurement.
