const protocol = window.location.protocol === "https:"
    ? "wss:"
    : "ws:";
const host = window.location.host;
export const ws = new WebSocket(`${protocol}//${host}/ws`);

export const pendingRequests = new Map();  // only the admin page deals with pending requests

export function random_id() {
    let new_id = Date.now() + Math.floor(Math.random() * 1_000_000_000);
    return new_id;
}

export function send_to_server(cmd) {
    let send_str = JSON.stringify(cmd)

    ws.send(send_str);
}
