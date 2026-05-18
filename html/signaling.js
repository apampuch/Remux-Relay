// signaling.js
const { WebSocketServer } = require('ws');
const wss = new WebSocketServer({ port: 8765 });

let gstreamer = null, browser = null;

wss.on('connection', ws => {
  ws.on('message', raw => {
    const msg = JSON.parse(raw);

    if (msg.role === 'producer') { gstreamer = ws; return; }
    if (msg.role === 'consumer') { browser = ws; return; }

    // Relay everything else between the two
    const target = ws === gstreamer ? browser : gstreamer;
    if (target?.readyState === 1) target.send(raw);
  });
});
