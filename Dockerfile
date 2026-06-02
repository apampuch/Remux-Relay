# # build stage
# FROM rust:1.94 AS builder
# WORKDIR /app

# # clone and build
# RUN git clone https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs.git

# RUN apt update
# RUN apt install -y \
#     libgstreamer1.0-dev \
#     libgstreamer-plugins-base1.0-dev \
#     libgstreamer-plugins-bad1.0-dev \
#     build-essential \
#     pkg-config \
#     gstreamer1.0-tools \
#     gstreamer1.0-plugins-base \
#     gstreamer1.0-plugins-good \
#     gstreamer1.0-plugins-bad \
#     gstreamer1.0-libav \
#     gstreamer1.0-nice

# RUN cd gst-plugins-rs
# RUN cargo install cargo-c
# RUN cargo cbuild -p gst-plugin-webrtchttp

FROM ubuntu:24.04
WORKDIR /app

RUN mkdir /sockets

RUN apt-get update
RUN apt-get install -y \
gstreamer1.0-tools \
gstreamer1.0-plugins-base \
gstreamer1.0-plugins-good \
gstreamer1.0-plugins-bad \
gstreamer1.0-libav \
gstreamer1.0-nice \
gstreamer1.0-plugins-ugly \
libjansson-dev

COPY ./libs /app/webrtchttp/
COPY ./bin/ /app/bin/

ENV GST_PLUGIN_PATH="/app/webrtchttp/"

# RUN gst-inspect-1.0 whipclientsink

CMD ["./bin/remuxrelay"]
