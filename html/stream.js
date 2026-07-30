// starts the stream, should be called h
function startStream() {
    pc.createOffer()
        .then(offer => pc.setLocalDescription(offer))
        .then(() => new Promise(resolve => {
            if (pc.iceGatheringState === 'complete') return resolve();
            // trickle isn't used for WHEP here since we send the offer immediately anyway,
            // this promise is now basically instant since setLocalDescription resolves fast
            resolve();
        }))
        .then(() => {
            const host = window.location.hostname;

            return fetch(`http://${host}:8889/stream/whep`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/sdp'
                },
                body: pc.localDescription.sdp
            });
        })
        .then(async res => {
            if (!res.ok) {
                console.error('WHEP error:', res.status, await res.text());
                return;
            }
            const answer = await res.text();
            // console.log('answer SDP:', answer);
            return pc.setRemoteDescription({
                type: 'answer',
                sdp: answer
            });
        })
        .catch(err => console.error('setup failed:', err));
}

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

startStream();
