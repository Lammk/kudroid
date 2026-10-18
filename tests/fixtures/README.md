# Media fixture

`vp8-vorbis.webm` contains ten 16×16 red VP8 frames at 25 fps and 0.4 seconds of stereo 440 Hz Vorbis audio at 48 kHz. Generated entirely from synthetic signals.

Regenerate with FFmpeg (libvpx and libvorbis encoders required):

```sh
ffmpeg -f lavfi -i 'color=c=red:s=16x16:r=25:d=0.4' \
  -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=0.4' \
  -ac 2 -c:v libvpx -deadline best -g 1 -c:a libvorbis -q:a 3 \
  -fflags +bitexact -flags:v +bitexact -flags:a +bitexact -shortest \
  vp8-vorbis.webm
```

Tests consume the checked-in file and do not require FFmpeg.
