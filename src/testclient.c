#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

int main(int argc, char *argv[]) {
    int server_socket;
    struct sockaddr_un server_addr;
    int connection_result;

    memset(&server_addr, 0, sizeof(server_addr));

    // char ch='C';
    char command[256];
    char response[256];
    memset(command, 0, sizeof(command));
    memset(response, 0, sizeof(response));

    server_socket = socket(AF_UNIX, SOCK_STREAM,0);

    server_addr.sun_family = AF_UNIX;
    strcpy(server_addr.sun_path, "unix_socket");

    connection_result = connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));

    if (connection_result == -1) {
        perror("Error:");
        exit(1);
    }

    // hacky way of writing json but whatever, dont care
    if (argc == 2) {
        snprintf(command, sizeof(command), "{\"command\":\"%s\"}", argv[1]);
    } else if (argc == 3) {
        snprintf(command, sizeof(command), "{\"command\":\"%s\",\"%s\":%s}", argv[1], argv[1], argv[2]);
    }

    write(server_socket, command, strlen(command));
    read(server_socket, &response, sizeof(response));
    printf("%s\n", response);
    close(server_socket);
    exit(0);
}
