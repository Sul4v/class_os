#include "network_utils.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 5050
#define INPUT_BUFFER 1024

static void trim_newline(char *line) {
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }
}

int main(int argc, char *argv[]) {
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;
    
    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = atoi(argv[2]);
        if (port <= 0) {
            fprintf(stderr, "Invalid port provided. Using default %d.\n", DEFAULT_PORT);
            port = DEFAULT_PORT;
        }
    }
    
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);
    
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(client_fd);
        return EXIT_FAILURE;
    }
    
    if (connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("connect");
        close(client_fd);
        return EXIT_FAILURE;
    }
    
    printf("[INFO] Connected to server %s:%d\n", host, port);
    
    char line[INPUT_BUFFER];
    while (1) {
        printf("$ ");
        fflush(stdout);
        
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        
        trim_newline(line);
        
        if (send_message(client_fd, line, strlen(line)) == -1) {
            fprintf(stderr, "Failed to send command to server.\n");
            break;
        }
        
        if (strcmp(line, "exit") == 0) {
            break;
        }
        
        char *response = NULL;
        size_t response_length = 0;
        if (recv_message(client_fd, &response, &response_length) == -1) {
            fprintf(stderr, "Connection closed by server.\n");
            free(response);
            break;
        }
        
        if (response_length > 0) {
            fputs(response, stdout);
            if (response[response_length - 1] != '\n') {
                putchar('\n');
            }
        }
        
        free(response);
    }
    
    close(client_fd);
    printf("[INFO] Disconnected from server.\n");
    return EXIT_SUCCESS;
}
