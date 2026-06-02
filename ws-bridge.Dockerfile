FROM node:22-alpine

WORKDIR /app

COPY ws-bridge/package*.json .

RUN npm install

CMD ["node", "bridge.js"]
