#include "myshell.h"
#include "network_utils.h"
#include "parser.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_PORT 5050
#define BACKLOG 5

static void restore_fd(int original_fd, int target_fd) {
    if (original_fd != -1) {
        dup2(original_fd, target_fd);
        close(original_fd);
    }
}

static int run_shell_command(const char *command, char **output, int *status) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return -1;
    }
    
    int stdout_backup = dup(STDOUT_FILENO);
    int stderr_backup = dup(STDERR_FILENO);
    if (stdout_backup == -1 || stderr_backup == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    
    fflush(stdout);
    fflush(stderr);
    
    if (dup2(pipefd[1], STDOUT_FILENO) == -1 ||
        dup2(pipefd[1], STDERR_FILENO) == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        restore_fd(stdout_backup, STDOUT_FILENO);
        restore_fd(stderr_backup, STDERR_FILENO);
        return -1;
    }
    close(pipefd[1]);
    
    pipeline_t pipeline;
    memset(&pipeline, 0, sizeof(pipeline));
    
    int local_status = 0;
    char *line_copy = strdup(command);
    if (!line_copy) {
        restore_fd(stdout_backup, STDOUT_FILENO);
        restore_fd(stderr_backup, STDERR_FILENO);
        close(pipefd[0]);
        return -1;
    }
    
    if (parse_command_line(line_copy, &pipeline) == 0) {
        if (validate_pipeline(&pipeline) == 0) {
            local_status = (execute_pipeline(&pipeline) == 0) ? 0 : 1;
        } else {
            local_status = 1;
        }
    } else {
        local_status = 1;
    }
    
    free(line_copy);
    free_pipeline(&pipeline);
    
    fflush(stdout);
    fflush(stderr);
    restore_fd(stdout_backup, STDOUT_FILENO);
    restore_fd(stderr_backup, STDERR_FILENO);
    
    char buffer[512];
    size_t capacity = 1024;
    size_t total = 0;
    char *result = malloc(capacity);
    if (!result) {
        close(pipefd[0]);
        return -1;
    }
    
    ssize_t bytes;
    while ((bytes = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        if (total + (size_t)bytes + 1 > capacity) {
            capacity = (capacity + bytes) * 2;
            char *resized = realloc(result, capacity);
            if (!resized) {
                free(result);
                close(pipefd[0]);
                return -1;
            }
            result = resized;
        }
        memcpy(result + total, buffer, (size_t)bytes);
        total += (size_t)bytes;
    }
    close(pipefd[0]);
    
    if (bytes == -1) {
        free(result);
        return -1;
    }
    
    result[total] = '\0';
    *output = result;
    if (status) {
        *status = local_status;
    }
    
    return 0;
}

static void log_multiline_output(const char *prefix, const char *output) {
    if (!output || output[0] == '\0') {
        printf("%s (no output)\n", prefix);
        return;
    }
    
    printf("%s\n", prefix);
    fputs(output, stdout);
    if (output[strlen(output) - 1] != '\n') {
        putchar('\n');
    }
}

static int extract_missing_command(const char *output, char **missing) {
    /* Be robust to both messages:
       - "myshell: <cmd>: command not found"
       - "myshell: <cmd>: No such file or directory"
       Scan line-by-line for a prefix and accepted suffixes. */
    const char *prefix = "myshell: ";
    const char *suffix1 = "command not found";
    const char *suffix2 = "No such file or directory";
    size_t prefix_len = strlen(prefix);

    if (!output) {
        return 0;
    }

    const char *p = output;
    while (*p != '\0') {
        /* Move to start of next non-empty line */
        while (*p == '\n') p++;
        if (*p == '\0') break;

        if (strncmp(p, prefix, prefix_len) == 0) {
            const char *cmd_start = p + prefix_len;
            const char *colon = strchr(cmd_start, ':');
            if (colon) {
                const char *msg = colon + 1;
                while (*msg == ' ') msg++;
                if ((strstr(msg, suffix1) != NULL) || (strstr(msg, suffix2) != NULL)) {
                    size_t cmd_len = (size_t)(colon - cmd_start);
                    char *command = malloc(cmd_len + 1);
                    if (!command) {
                        return 0;
                    }
                    memcpy(command, cmd_start, cmd_len);
                    command[cmd_len] = '\0';
                    *missing = command;
                    return 1;
                }
            }
        }

        /* Advance to next line */
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }

    return 0;
}

/* Verify that a candidate token is actually one of the command names
   (argv[0]) present in the parsed pipeline of the provided command line. */
static int is_actual_command_name(const char *command_line, const char *candidate) {
    if (!command_line || !candidate || *candidate == '\0') {
        return 0;
    }
    pipeline_t pipeline;
    memset(&pipeline, 0, sizeof(pipeline));
    char *copy = strdup(command_line);
    if (!copy) {
        return 0;
    }
    int result = 0;
    if (parse_command_line(copy, &pipeline) == 0 && pipeline.num_commands > 0) {
        for (int i = 0; i < pipeline.num_commands; i++) {
            command_t *cmd = &pipeline.commands[i];
            if (cmd->argc > 0 && cmd->args[0] && strcmp(cmd->args[0], candidate) == 0) {
                result = 1;
                break;
            }
        }
    }
    free(copy);
    free_pipeline(&pipeline);
    return result;
}

static void handle_client(int client_fd) {
    while (1) {
        char *command = NULL;
        size_t command_length = 0;
        
        if (recv_message(client_fd, &command, &command_length) == -1) {
            printf("[INFO] Client disconnected unexpectedly.\n");
            free(command);
            break;
        }
        
        char *trimmed = trim_whitespace(command);
        if (trimmed[0] == '\0') {
            free(command);
            if (send_message(client_fd, "", 0) == -1) {
                printf("[ERROR] Failed to acknowledge empty command.\n");
                break;
            }
            continue;
        }
        
        printf("[RECEIVED] Received command: \"%s\" from client.\n", trimmed);
        
        if (strcmp(trimmed, "exit") == 0) {
            free(command);
            printf("[INFO] Client requested exit.\n");
            break;
        }
        
        printf("[EXECUTING] Executing command: \"%s\"\n", trimmed);
        
        char *output = NULL;
        int status = 0;
        if (run_shell_command(trimmed, &output, &status) == -1) {
            const char *error_message = "Internal server error.\n";
            printf("[ERROR] Failed to execute command.\n");
            send_message(client_fd, error_message, strlen(error_message));
            free(command);
            free(output);
            continue;
        }
        
        char *send_buffer = output;
        int send_buffer_allocated = 0;
        
        if (status == 0) {
            log_multiline_output("[OUTPUT] Sending output to client:", output);
        } else {
            char *missing_cmd = NULL;
            if (extract_missing_command(output, &missing_cmd) &&
                is_actual_command_name(trimmed, missing_cmd)) {
                /* Log rubric-formatted line, but send the actual captured output
                   so client sees both the error and any pipeline output (e.g., '0'). */
                printf("[ERROR] Command not found: \"%s\"\n", missing_cmd);
                log_multiline_output("[OUTPUT] Sending error message to client:", output);
                /* send_buffer remains 'output' */
                free(missing_cmd);
            } else {
                printf("[ERROR] Command completed with errors.\n");
                log_multiline_output("[OUTPUT] Sending error message to client:", output);
            }
        }
        
        size_t send_length = strlen(send_buffer);
        if (send_message(client_fd, send_buffer, send_length) == -1) {
            printf("[ERROR] Failed to send response to client.\n");
            free(command);
            if (send_buffer_allocated) {
                free(send_buffer);
            }
            free(output);
            break;
        }
        
        free(command);
        if (send_buffer_allocated) {
            free(send_buffer);
        }
        free(output);
    }
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0) {
            fprintf(stderr, "Invalid port provided. Using default %d.\n", DEFAULT_PORT);
            port = DEFAULT_PORT;
        }
    }
    
    signal(SIGPIPE, SIG_IGN);
    
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons((uint16_t)port);
    
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }
    
    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }
    
    printf("[INFO] Server started, waiting for client connections on port %d...\n", port);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
            perror("accept");
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("[INFO] Client connected from %s:%d.\n", client_ip, ntohs(client_addr.sin_port));
        
        handle_client(client_fd);
        
        close(client_fd);
        printf("[INFO] Client connection closed.\n");
    }
    
    close(server_fd);
    return EXIT_SUCCESS;
}
