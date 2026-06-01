const protocol = window.location.protocol === "https:"
    ? "wss:"
    : "ws:";
const host = window.location.host;
const ws = new WebSocket(`${protocol}://${host}/ws`)

ws.addEventListener("open", () => {
    log("Connected to websocket.");
    // TODO get metadata or something
});

ws.addEventListener("error", () => {
    log("ERROR");
});

function send_command(command_obj) {

}

function get_playing_state() {

}

function play_pause() {
    const cmd = {};

    if (true) { // if playing
        cmd.command = "pause";    
    }
    else {
        cmd.command = "play";
    }

    ws.send(JSON.stringify(cmd));
}

function seek(timestamp) {
    // probably have the function that calls this pass in the timestamp
}