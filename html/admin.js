const protocol = window.location.protocol === "https:"
    ? "wss:"
    : "ws:";
const host = window.location.host;
const ws = new WebSocket(`${protocol}//${host}/ws`);

const pendingRequests = new Map();

ws.addEventListener("open", () => {
    console.log("Connected to websocket.");
    // TODO get metadata or something
});

ws.addEventListener("message", (event) => {
    const msg = JSON.parse(event.data);

    if (Object.hasOwn(msg, "id")) {
        const resolver = pendingRequests.get(msg.id);
        if (resolver) {
            pendingRequests.delete(msg.id);
            resolver(msg);
        }
    } else {
        console.log(msg);
    }
});

ws.addEventListener("error", () => {
    console.log("ERROR");
});

function get_playing_state() {
    const id = crypto.randomUUID();

    const cmd = {
        "command": "get_play_state",
        "id": id
    };

    return new Promise((resolve) => {
        pendingRequests.set(id, resolve);
        ws.send(JSON.stringify(cmd));
    });
}

function play_pause() {
    const id = crypto.randomUUID();

    const cmd = {
        "command": "toggle",
        "id": id
    };

    ws.send(JSON.stringify(cmd));
}

function seek(timestamp) {
    const id = crypto.randomUUID();

    const cmd = {
        "command": "seek",
        "id": id
    };

    var first_char = timestamp.charAt(0);
    if (first_char == '+') {
        timestamp = timestamp.substr(1);
        cmd.seek_type = "forward";
    } else if (first_char == '-') {
        timestamp = timestamp.substr(1);
        cmd.seek_type = "backward";
    } else {
        cmd.seek_type = "absolute";
    }

    cmd.seek_time = parseInt(timestamp);

    ws.send(JSON.stringify(cmd));
}

document.getElementById("playPauseButton").addEventListener("click", play_pause);
document.getElementById("skipBackButton").addEventListener("click", () => seek("-5000"));
document.getElementById("skipForwardButton").addEventListener("click", () => seek("+5000"));
