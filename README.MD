# Requirements
docker

The following gst-plugins-rs built:
- `rtp`
- `webrtc`
- `webrtchttp`

Open these ports:
  - 8080/tcp (web server)
  - 8189/udp (mediamtx)
  - 8889/tcp (mediamtx)

(last tested commit is `153ccd0624e820958eff474f1da67b051fc42e37`)

(last tested gstreamer commit is `8f181e21d540065930a70b4c355b4df4dc81c575`)

The files `libgstrsrtp.so` `libgstrswebrtc.so` `libgstwebrtchttp.so` should all be placed in `libs/`

## Docker Compose File
```yaml
name: remux-relay
services:
  mediamtx:
    container_name: rr-mediamtx
    image: bluenviron/mediamtx:1.18.2  # 1.19 has issues with ICE for some reason
    ports:
      - "8554:8554"
      - "1935:1935"
      - "8888:8888"
      - "8889:8889"
      - "8890:8890/udp"
      - "8189:8189/udp"
    environment:
        - MTX_RTSPTRANSPORTS=tcp
        - MTX_WEBRTCADDITIONALHOSTS=127.0.0.1  # SET THIS TO YOUR SERVER'S EXTERNAL IP

  web:
    container_name: rr-web
    image: apampuch/remux-relay-web:latest
    ports:
      - "8080:80"  # or whatever port you want to expose

  backend:
    container_name: rr-bridge
    image: apampuch/remux-relay-bridge:latest
    expose:
      - "3000"
    volumes:
      - socket-volume:/sockets

  remux-relay:
    container_name: rr-player
    image: apampuch/remux-relay:latest
    depends_on:
      - mediamtx
    stdin_open: true   # -i
    tty: true          # -t
    volumes:
      - ./videos:/videos:ro
      - socket-volume:/sockets

volumes:
  socket-volume:

networks:
  default:
    name: rr-network
```

# Suggested Encode Options
```bash
ffmpeg \
  -i test.mkv \
  -map 0:v:0 \
  -map 0:a:0 \
  -c:v libx264 \
  -preset slow \
  -b:v 10M \
  -bufsize 2M \
  -pix_fmt yuv420p \
  -level 4.1 \
  -x264-params "bframes=0:keyint=24" \
  -c:a libopus \
  -b:a 96k \
  -ac 2 \
  -map_metadata -1 \
  -map_chapters -1 \
  output.mkv
```
You MUST leave out b-frames.
No min-keyint

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
    signaller::whip-endpoint="http://mediamtx:8889/stream/whip"
```

You may want to set these in `/etc/sysctl.conf` if you get stuttering issues:
```bash
net.core.rmem_max = 16777216
net.core.rmem_default = 4194304
```
