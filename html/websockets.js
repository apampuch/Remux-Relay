const protocol = window.location.protocol === "https:"
    ? "wss:"
    : "ws:";
const host = window.location.host;
export const ws = new WebSocket(`${protocol}//${host}/ws`);