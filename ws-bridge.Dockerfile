FROM node:22-alpine

WORKDIR /app

COPY ws-bridge/ .

RUN npm ci

EXPOSE 3000

CMD ["node", "bridge.js"]
