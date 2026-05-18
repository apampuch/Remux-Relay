# Requirements
docker

# Suggested Encode Options
```bash
ffmpeg \
  -i test.mkv \
  -ss 120 \
  -t 60 \
  -map 0:v:0 \
  -map 0:a:0 \
  -c:v libx264 \
  -preset slow \
  -b:v 10M \
  -bufsize 2M \
  -pix_fmt yuv420p \
  -tune zerolatency \
  -level 4.1 \
  -x264-params "bframes=0:keyint=24" \
  -c:a libopus \
  -b:a 96k \
  -ac 2 \
  -map_metadata -1 \
  -map_chapters -1 \
  output.mkv
```

# Test Command
```bash
gst-launch-1.0 \
  videotestsrc is-live=true pattern=ball ! \
    videoconvert ! \
    x264enc tune=zerolatency speed-preset=ultrafast bitrate=1000 key-int-max=30 ! \
    h264parse ! queue ! whip. \
  audiotestsrc is-live=true ! \
    audioconvert ! audioresample ! \
    opusenc ! queue ! whip. \
  whipclientsink name=whip \
    signaller::whip-endpoint="http://mediamtx:8889/stream/whip"```
