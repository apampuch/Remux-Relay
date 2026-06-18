const net = require('net');
const WebSocket = require('ws');

const wss = new WebSocket.Server({ port: 3000 });

wss.on('connection', (ws) => {
    console.log("Connection established.");

    const relaySocket = net.createConnection('/sockets/relay.sock');

    ws.on('message', async (dataJSONString) => {
        if (dataJSONString.at(-1) !== '\n')
            dataJSONString += '\n';

        const data = JSON.parse(dataJSONString);
        const reply = {};
        const errorObj = {};

        /* check if we have id
           if we do, add it to the reply */
        if (Object.hasOwn(data, "id"))
            reply.id = data.id;
        else {
            errorObj.error = "Object has no ID.";
            ws.send(JSON.stringify(errorObj));
            return;
        }

        /* check if we have a command */
        if (Object.hasOwn(data, "command"))
            var command = data.command;
        else {
            errorObj.error = "Object has no command.";
            ws.send(JSON.stringify(errorObj));
            return;
        }

        // console.log("Sending to server.");

        // just send it
        relaySocket.write(dataJSONString);
    });

    // just forward any data from the unix socket to the websocket
    relaySocket.on('data', async (dataBytesBuffer) => {
        // this is a buffer of bytes
        // for now assume anything we get is a json and send it away
        // console.log("Unix event trigger:" + dataBytesBuffer.toString());
        ws.send(dataBytesBuffer.toString());
    });

    ws.on('close', (data) => {
        console.log('Closing connection.');
    });

});
