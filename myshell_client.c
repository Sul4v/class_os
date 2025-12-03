/* Remote myshell client: translates user input into framed messages for the server. */
#include "network_utils.h"

#include <arpa/inet.h>
#include <errno.h>
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
#define CLIENT_HOST_ENV "MYSHELL_HOST"
#define CLIENT_PORT_ENV "MYSHELL_PORT"

/* Parse ports supplied via CLI/env while rejecting invalid values. */
static int parse_port(const char *value, int fallback, const char *source) {
    if (!value || *value == '\0') {
        return fallback;
    }

    char *endptr = NULL;
    errno = 0;
    long candidate = strtol(value, &endptr, 10);
    if (errno != 0 || endptr == value || *endptr != '\0' ||
        candidate <= 0 || candidate > 65535) {
        if (source) {
            fprintf(stderr,
                    "[WARN] Ignoring invalid %s \"%s\". Using port %d.\n",
                    source, value, fallback);
        }
        return fallback;
    }
    return (int)candidate;
}

/* Leave fgets-friendly newline stripping in one place for clarity. */
static void trim_newline(char *line) {
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }
}

/* Main loop: connect to the server, forward user commands, print responses. */
int main(int argc, char *argv[]) {
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;

    const char *env_host = getenv(CLIENT_HOST_ENV);
    if (env_host && *env_host) {
        host = env_host;
    }

    port = parse_port(getenv(CLIENT_PORT_ENV), port, CLIENT_PORT_ENV);
    
    /* CLI arguments, when present, override both the defaults and env vars. */
    if (argc > 1 && argv[1][0] != '\0') {
        host = argv[1];
    }
    if (argc > 2) {
        port = parse_port(argv[2], port, "command-line port");
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
            /* Let the server break its loop before we tear down the socket. */
            break;
        }
        
        while (1) {
            char *response = NULL;
            size_t response_length = 0;
            if (recv_message(client_fd, &response, &response_length) == -1) {
                fprintf(stderr, "Connection closed by server.\n");
                free(response);
                goto cleanup;
            }
            if (response_length == 0) {
                free(response);
                break;
            }
            fputs(response, stdout);
            if (response[response_length - 1] != '\n') {
                putchar('\n');
            }
            free(response);
        }
    }
    
cleanup:
    close(client_fd);
    printf("[INFO] Disconnected from server.\n");
    return EXIT_SUCCESS;
}
