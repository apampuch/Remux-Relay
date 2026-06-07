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

ws.onmessage

function send_command(command_obj) {

}

function get_playing_state() {
    const id = crypto.randomUUID();

    const cmd = {
        "command": "get_play_state",
        "id": crypto.randomUUID()
    };

    return new Promise((resolve) => {
        pendingRequests.set(id, resolve);
        ws.send(JSON.stringify(cmd));
        // ws.send(JSON.stringify(
        //     id,
        //     type,
        //     cmd
        // ))
    });
}

function play_pause() {
    const cmd = {"command": "toggle"};

    console.log("Toggling");

    // const playing_state = await get_playing_state();

    // if (true) { // if playing
    //     cmd.command = "pause";    
    // }
    // else {
    //     cmd.command = "play";
    // }

    ws.send(JSON.stringify(cmd));
}

function seek(timestamp) {
    // probably have the function that calls this pass in the timestamp
}

document.getElementById("playPauseButton").addEventListener("click", play_pause);
