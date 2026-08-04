import { ws, pendingRequests, random_id, send_to_server } from "./websockets.js";

function makeConnectionObject() {
    const pc = new RTCPeerConnection({
        iceServers: []  // no STUN, don't wait for srflx candidates
    });

    pc.addTransceiver('video', {
        direction: 'recvonly'
    });
    pc.addTransceiver('audio', {
        direction: 'recvonly'
    });
    pc.ontrack = ({
        streams, receiver, track
    }) => {
        // console.log('ontrack fired', streams);
        document.getElementById('videoPlayer').srcObject = streams[0];

        if ('jitterBufferTarget' in receiver) {
            receiver.jitterBufferTarget = 300;
            console.log(`jitterBufferTarget set for ${track.kind}:`, receiver.jitterBufferTarget);
        } else {
            console.log(`jitterBufferTarget NOT supported for ${track.kind}`);
        }
    };

    // pc.oniceconnectionstatechange = () => console.log('ICE state:', pc.iceConnectionState);
    // pc.onconnectionstatechange = () => console.log('connection state:', pc.connectionState);

    // TODO make this auto-reconnect
    pc.onconnectionstatechange = () => {
        // console.log(pc.connectionState);

        switch (pc.connectionState) {
            case "connected":
                // Fully connected
                break;

            case "disconnected":
                // Lost connectivity (may recover)
                break;

            case "failed":
                // ICE has failed; generally won't recover without an ICE restart
                break;

            case "closed":
                // Someone called pc.close()
                break;
        }
    };

    return pc;
}

// it can freeze if you don't do this
function waitForIceGathering(pc) {
    return new Promise(resolve => {
        if (pc.iceGatheringState === "complete") {
            resolve();
            return;
        }

        pc.addEventListener("icegatheringstatechange", () => {
            if (pc.iceGatheringState === "complete") {
                resolve();
            }
        });
    });
}

ws.addEventListener("message", (event) => {
    try {
        const msg = JSON.parse(event.data);

        if (Object.hasOwn(msg, "id"))  {
            const resolver = pendingRequests.get(msg.id);
            if (resolver) {
                pendingRequests.delete(msg.id);
                resolver(msg);
            } else {
                console.warn("id not found:" + msg.id);
            }
        } else if (Object.hasOwn(msg, "connection_cmd")) {
            // handle commands related to reconnecting
            switch (msg.connection_cmd) {
                case "connect":
                    startStream();
                    break;
                case "close":
                    pc.close();
                    break;
            }
        }
    } catch (error) {
        throw(error);
    }
});

ws.addEventListener("open", async () => {
    console.log("Connected to websocket.");

    // basically ask it if we can connect, and then connect if we
    const id = random_id();

    const cmd = {
        "command": "connect",
        "id": id
    };

    const promise = new Promise((resolve) => {
        pendingRequests.set(id, resolve);
        send_to_server(cmd);
    });

    const response = await promise;
    
    if (response.hasOwn("connection_cmd") && response.connection_cmd === "connect")
        startStream();

});

// starts the stream and retries if no connection
async function startStream() {
    const host = window.location.hostname;

    while (true) {
        try {
            const pc = makeConnectionObject();
            await pc.setLocalDescription(await pc.createOffer());
            await waitForIceGathering(pc);

            const res = await fetch(`http://${host}:8889/stream/whep`, {
                method: 'POST',
                headers: {
                    "Content-Type": "application/sdp"
                },
                body: pc.localDescription.sdp
            });

            if (res.status === 404) {
                // console.log("Stream not ready, retrying...");
                pc.close();
                await new Promise(r => setTimeout(r, 1000));
                continue;
            }

            if (!res.ok)
                throw new Error(`${res.status} ${await res.text()}`);

            const answer = await res.text();
            await pc.setRemoteDescription({
                type: "answer",
                sdp: answer
            });

            // console.log("Connected!");
            return pc;

        } catch (err) {
            console.error(err);
            await new Promise(r => setTimeout(r, 1000));
        }
    }
}

// this is so we can call it in the console, worst case
window.startStream = startStream;
