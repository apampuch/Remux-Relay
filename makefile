CFLAGS := $(shell pkg-config --cflags gstreamer-1.0)
LIBS := $(shell pkg-config --libs gstreamer-1.0 jansson)

all: client server

client: src/testclient.c
	gcc src/testclient.c -o ./bin/testclient $(CFLAGS) $(LIBS)

server: src/main.c
	gcc src/main.c src/stream_components.c src/debug_probes.c -o ./bin/remuxrelay $(CFLAGS) $(LIBS)
