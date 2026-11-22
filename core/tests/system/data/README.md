# H.264 Test Fixture

`reordered.h264` is generated test-pattern data, not a game asset. Seven 64x48
frames include two B-frames between reference frames and Annex-B access-unit
delimiters. Regenerate with:

```sh
ffmpeg -f lavfi -i 'testsrc2=size=64x48:rate=6' -frames:v 7 -c:v libx264 -pix_fmt yuv420p -x264-params 'bframes=2:b-adapt=0:scenecut=0:keyint=30:aud=1' -f h264 reordered.h264
ffmpeg -f lavfi -i 'testsrc2=size=64x50:rate=1' -frames:v 1 -c:v libx264 -pix_fmt yuv420p -x264-params 'bframes=0:aud=1' -f h264 cropped.h264
```

`cropped.h264` distinguishes a 64-pixel coded height from its 50 visible rows.

## Movie audio fixture

`menu-audio.bin` is an MP4 containing a generated 64x48 test pattern and
stereo 440 Hz AAC at 32 kHz. It tests resampling to 48 kHz, packet priming,
seeking and draining, without using a game asset. Regenerate with:

```sh
ffmpeg -f lavfi -i 'testsrc2=size=64x48:rate=10:duration=1' \
       -f lavfi -i 'sine=frequency=440:sample_rate=32000:duration=1' \
       -c:v libx264 -pix_fmt yuv420p -c:a aac -ac 2 -shortest \
       -f mp4 menu-audio.bin
```
