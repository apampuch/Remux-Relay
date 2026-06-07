const net = require('net');
const WebSocket = require('ws');

const wss = new WebSocket.Server({ port: 3000 });

wss.on('connection', (ws) => {
    console.log("Connection established.");

    const relaySocket = net.createConnection('');

    ws.on('message', (data) => {
        console.log("Received message.");
        ws.send("Message receieved.");

        // get id

        // extract inner json

        // pass to unix socket

        // send response
    })

    ws.on('close', (data) => {
        console.log('Closing.');
    })

});
