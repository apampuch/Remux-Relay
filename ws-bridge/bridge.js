const net = require('net');
const WebSocket = require('ws');

const wss = new WebSocket.Server({ port: 3000 });

wss.on('connection', (ws) => {
    console.log("Connection established.");

    const relaySocket = net.createConnection('/sockets/relay.sock');

    function sendCommand(command) {
        console.log("Sending command.");

        return new Promise((resolve, reject) => {            
            relaySocket.once("data", (data) => {
                resolve(data.toString());
                // relaySocket.end();  // graceful close
            });

            relaySocket.once("error", (err) => {
                reject(err);
                // relaySocket.destroy();  // force cleanup
            });

            relaySocket.write(command);
        });
    }

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

        // TODO extract other data

        /* just pass the JSON string to the unix socket */
        reply.message = await sendCommand(dataJSONString); 

        /* send response */
        const replyStr = JSON.stringify(reply);
        ws.send(replyStr);
    })

    ws.on('close', (data) => {
        console.log('Closing.');
    })

});
