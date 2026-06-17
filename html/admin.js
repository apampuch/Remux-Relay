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

function send_to_server(cmd) {
    ws.send(JSON.stringify(cmd));
}

function get_playing_state() {
    const id = Date.now() + Math.floor(Math.random() * 1_000_000_000);

    const cmd = {
        "command": "get_play_state",
        "id": id
    };

    return new Promise((resolve) => {
        pendingRequests.set(id, resolve);
        send_to_server(cmd);
    });
}

function play_pause() {
    const id = Date.now() + Math.floor(Math.random() * 1_000_000_000);

    const cmd = {
        "command": "toggle",
        "id": id
    };

    send_to_server(cmd);
}

function seek(timestamp) {
    const id = Date.now() + Math.floor(Math.random() * 1_000_000_000);

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

    send_to_server(cmd);
}

document.getElementById("playPauseButton").addEventListener("click", play_pause);
document.getElementById("skipBackButton").addEventListener("click", () => seek("-5000"));
document.getElementById("skipForwardButton").addEventListener("click", () => seek("+5000"));
