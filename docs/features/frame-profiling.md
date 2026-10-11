# Complete-frame CPU timing

`WEMU_FRAME_PROFILE=1` reports one `[FRAME_PROFILE]` record per guest present.
Exclusive nested timings cover guest execution, HLE, replay, native draw CPU
work, CPU readback, GPU fence waits, video decode, presentation, pacing, pause
and native compilation. Their total accounts for the full measured wall interval.
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
