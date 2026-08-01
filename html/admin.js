import { ws } from "./websockets.js";

const pendingRequests = new Map();  // only the admin page deals with pending requests

// the stream duration is stored in seek_bar.max in milliseconds
const seek_bar = document.getElementById("seekBar");

// initialize both values to 0 for now, to start with an invalid value that we can maybe use to debug
seek_bar.min = 0;
seek_bar.max = 0;

// only update the seekbar from playing time while true
var update_seekbar = true;

function random_id() {
    let new_id = Date.now() + Math.floor(Math.random() * 1_000_000_000);
    // console.log(new_id);
    return new_id;
}

function convertMsToTime(ms) {
    // Calculate total units
    let seconds = Math.floor(ms / 1000);
    let minutes = Math.floor(seconds / 60);
    let hours = Math.floor(minutes / 60);

    // Use modulo to get remaining parts
    seconds = seconds % 60;
    minutes = minutes % 60;

    // Format numbers to always show 2 digits using padStart()
    const formattedH = String(hours).padStart(2, '0');
    const formattedM = String(minutes).padStart(2, '0');
    const formattedS = String(seconds).padStart(2, '0');

    return `${formattedH}:${formattedM}:${formattedS}`;
}

seek_bar.addEventListener("input", (event) => {
    // just turn off seekbar updating while dragging so we don't get weirdness
    update_seekbar = false;
})

seek_bar.addEventListener("change", (event) => {
    seek(event.target.value);

    update_seekbar = true;
})

// TODO maybe just get rid of the whole promise architecture and just take things as they come

ws.addEventListener("open", async () => {
    console.log("Connected to websocket.");

    // get the list of files
    list_files();

    // update stream duration
    var duration_obj = await get_duration();

    seek_bar.max = duration_obj.stream_duration;
    document.getElementById("totalDuration").textContent = convertMsToTime(duration_obj.stream_duration);
});

ws.addEventListener("message", (event) => {
    try {
        const msg = JSON.parse(event.data);
        // console.log("Receiving: " + event.data);

        if (Object.hasOwn(msg, "id")) {
            const resolver = pendingRequests.get(msg.id);
            if (resolver) {
                // console.log("Removing id: " + msg.id);
                pendingRequests.delete(msg.id);
                resolver(msg);
            } else {
                console.warn("id not found:" + msg.id);
            }
        } else if (Object.hasOwn(msg, "update_type")) {
            if (msg.update_type == "position") {
                if (!Object.hasOwn(msg, "new_position")) {
                    console.error("Position update does not have new_position property");
                    return;
                }

                // ignore if duration is 0 since we might get a position update before the initial duration update
                if (seek_bar == 0) return;

                // do nothing if we aren't updating the seekbar
                if (!update_seekbar) return;

                // the stream duration is always stored in seek_bar.max in milliseconds
                seek_bar.value = msg.new_position;
                document.getElementById("currentPosition").textContent = convertMsToTime(msg.new_position);
            }

        }
    } catch (error) {
        console.log(event.data);
        throw error;
    }
    
});

ws.addEventListener("error", () => {
    console.log("ERROR");
});

function send_to_server(cmd) {
    let send_str = JSON.stringify(cmd)

    // console.log("Sending:" + send_str);

    ws.send(send_str);
}

// function get_playing_state() {
//     const id = random_id();

//     const cmd = {
//         "command": "get_play_state",
//         "id": id
//     };

//     return new Promise((resolve) => {
//         pendingRequests.set(id, resolve);
//         send_to_server(cmd);
//     });
// }

function get_duration() {
    const id = random_id();

    const cmd = {
        "command": "get_duration",
        "id": id
    };

    return new Promise((resolve) => {
        pendingRequests.set(id, resolve);
        send_to_server(cmd);
    });
}

function play_pause() {
    const id = random_id();

    const cmd = {
        "command": "toggle",
        "id": id
    };

    return new Promise((resolve) => {
        pendingRequests.set(id, resolve);
        send_to_server(cmd);
    });
}

function seek(timestamp) {
    const id = random_id();

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

    return new Promise((resolve) => {
        pendingRequests.set(id, resolve);
        send_to_server(cmd);
    });
}

async function list_files() {
    const id = random_id();

    const cmd = {
        "command": "list_files",
        "id": id
    };

    const promise = new Promise((resolve) => {
        pendingRequests.set(id, resolve);
        send_to_server(cmd);
    });

    const response = await promise;

    const dropdown = document.getElementById("fileSelector");

    response.paths.forEach((item) => {
        const option = new Option(item, item);
        dropdown.add(option);
    });
}

document.getElementById("playPauseButton").addEventListener("click", play_pause);
document.getElementById("skipBackButton").addEventListener("click", () => seek("-5000"));
document.getElementById("skipForwardButton").addEventListener("click", () => seek("+5000"));
