FROM nginx:alpine

COPY ./html /usr/share/nginx/html
COPY ./ws-bridge.conf /etc/nginx/conf.d/default.conf
